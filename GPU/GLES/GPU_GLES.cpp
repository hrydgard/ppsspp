// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "base/logging.h"
#include "profiler/profiler.h"
#include "i18n/i18n.h"

#include "Common/ChunkFile.h"
#include "Common/GraphicsContext.h"

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/FramebufferCommon.h"

#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/GPU_GLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"

#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

#ifdef _WIN32
#include "Windows/GPU/WindowsGLContext.h"
#endif

GPU_GLES::GPU_GLES(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
: GPUCommon(gfxCtx, draw), drawEngine_(draw), fragmentTestCache_(draw), depalShaderCache_(draw) {
	UpdateVsyncInterval(true);
	CheckGPUFeatures();

	GLRenderManager *render = (GLRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	shaderManagerGL_ = new ShaderManagerGLES(draw);
	framebufferManagerGL_ = new FramebufferManagerGLES(draw, render);
	framebufferManager_ = framebufferManagerGL_;
	textureCacheGL_ = new TextureCacheGLES(draw);
	textureCache_ = textureCacheGL_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerGL_;
	drawEngineCommon_ = &drawEngine_;

	drawEngine_.SetShaderManager(shaderManagerGL_);
	drawEngine_.SetTextureCache(textureCacheGL_);
	drawEngine_.SetFramebufferManager(framebufferManagerGL_);
	drawEngine_.SetFragmentTestCache(&fragmentTestCache_);
	framebufferManagerGL_->Init();
	framebufferManagerGL_->SetTextureCache(textureCacheGL_);
	framebufferManagerGL_->SetShaderManager(shaderManagerGL_);
	framebufferManagerGL_->SetDrawEngine(&drawEngine_);
	textureCacheGL_->SetFramebufferManager(framebufferManagerGL_);
	textureCacheGL_->SetDepalShaderCache(&depalShaderCache_);
	textureCacheGL_->SetShaderManager(shaderManagerGL_);
	textureCacheGL_->SetDrawEngine(&drawEngine_);
	fragmentTestCache_.SetTextureCache(textureCacheGL_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.

	UpdateCmdInfo();

	BuildReportingInfo();
	// Update again after init to be sure of any silly driver problems.
	UpdateVsyncInterval(true);

	textureCacheGL_->NotifyConfigChanged();

	// Load shader cache.
	std::string discID = g_paramSFO.GetDiscID();
	if (discID.size()) {
		File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
		shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) + "/" + discID + ".glshadercache";
		// Actually precompiled by IsReady() since we're single-threaded.
		shaderManagerGL_->Load(shaderCachePath_);
	}

	if (g_Config.bHardwareTessellation) {
		// Disable hardware tessellation if device is unsupported.
		bool hasTexelFetch = gl_extensions.GLES3 || (!gl_extensions.IsGLES && gl_extensions.VersionGEThan(3, 3, 0)) || gl_extensions.EXT_gpu_shader4;
		if (!gstate_c.SupportsAll(GPU_SUPPORTS_INSTANCE_RENDERING | GPU_SUPPORTS_VERTEX_TEXTURE_FETCH | GPU_SUPPORTS_TEXTURE_FLOAT) || !hasTexelFetch) {
			// TODO: Check unsupported device name list.(Above gpu features are supported but it has issues with weak gpu, memory, shader compiler etc...)
			g_Config.bHardwareTessellation = false;
			ERROR_LOG(G3D, "Hardware Tessellation is unsupported, falling back to software tessellation");
			I18NCategory *gr = GetI18NCategory("Graphics");
			host->NotifyUserMessage(gr->T("Turn off Hardware Tessellation - unsupported"), 2.5f, 0xFF3030FF);
		}
	}
}

GPU_GLES::~GPU_GLES() {
	if (draw_) {
		GLRenderManager *render = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		render->Wipe();
	}

	// If we're here during app shutdown (exiting the Windows app in-game, for example)
	// everything should already be cleared since DeviceLost has been run.

	if (!shaderCachePath_.empty() && draw_) {
		shaderManagerGL_->Save(shaderCachePath_);
	}

	framebufferManagerGL_->DestroyAllFBOs();
	shaderManagerGL_->ClearCache(true);
	depalShaderCache_.Clear();
	fragmentTestCache_.Clear();
	
	delete shaderManagerGL_;
	shaderManagerGL_ = nullptr;
	delete framebufferManagerGL_;
	delete textureCacheGL_;
}

static constexpr int MakeIntelSimpleVer(int v1, int v2, int v3) {
	return (v1 << 16) | (v2 << 8) | v3;
}

static bool HasIntelDualSrcBug(int versions[4]) {
	// Intel uses a confusing set of at least 3 version numbering schemes.  This is the one given to OpenGL.
	switch (MakeIntelSimpleVer(versions[0], versions[1], versions[2])) {
	case MakeIntelSimpleVer(9, 17, 10):
	case MakeIntelSimpleVer(9, 18, 10):
		return false;
	case MakeIntelSimpleVer(10, 18, 10):
		return versions[3] < 4061;
	case MakeIntelSimpleVer(10, 18, 14):
		return versions[3] < 4080;
	default:
		// Older than above didn't support dual src anyway, newer should have the fix.
		return false;
	}
}

// Take the raw GL extension and versioning data and turn into feature flags.
void GPU_GLES::CheckGPUFeatures() {
	u32 features = 0;

	features |= GPU_SUPPORTS_16BIT_FORMATS;

	if (gl_extensions.ARB_blend_func_extended || gl_extensions.EXT_blend_func_extended) {
		if (!gl_extensions.VersionGEThan(3, 0, 0)) {
			// Don't use this extension on sub 3.0 OpenGL versions as it does not seem reliable
		} else if (gl_extensions.gpuVendor == GPU_VENDOR_INTEL) {
			// Also on Intel, see https://github.com/hrydgard/ppsspp/issues/10117
			// TODO: Remove entirely sometime reasonably far in driver years after 2015.
			const std::string ver = draw_->GetInfoString(Draw::InfoField::APIVERSION);
			int versions[4]{};
			if (sscanf(ver.c_str(), "Build %d.%d.%d.%d", &versions[0], &versions[1], &versions[2], &versions[3]) == 4) {
				if (!HasIntelDualSrcBug(versions)) {
					features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
				}
			} else {
				features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
			}
		} else {
			features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
		}
	}

	if (gl_extensions.IsGLES) {
		if (gl_extensions.GLES3)
			features |= GPU_SUPPORTS_GLSL_ES_300;
	} else {
		if (gl_extensions.VersionGEThan(3, 3, 0))
			features |= GPU_SUPPORTS_GLSL_330;
	}

	if (gl_extensions.EXT_shader_framebuffer_fetch || gl_extensions.NV_shader_framebuffer_fetch || gl_extensions.ARM_shader_framebuffer_fetch) {
		// This has caused problems in the past.  Let's only enable on GLES3.
		if (features & GPU_SUPPORTS_GLSL_ES_300) {
			features |= GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH;
		}
	}
	
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.EXT_framebuffer_object || gl_extensions.IsGLES) {
		features |= GPU_SUPPORTS_FBO;
	}
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.GLES3) {
		features |= GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT;
	}
	if (gl_extensions.NV_framebuffer_blit) {
		features |= GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT;
	}
	if (gl_extensions.ARB_vertex_array_object && gl_extensions.IsCoreContext) {
		features |= GPU_SUPPORTS_VAO;
	}

	bool useCPU = false;
	if (!gl_extensions.IsGLES) {
		// Some cards or drivers seem to always dither when downloading a framebuffer to 16-bit.
		// This causes glitches in games that expect the exact values.
		// It has not been experienced on NVIDIA cards, so those are left using the GPU (which is faster.)
		if (gl_extensions.gpuVendor != GPU_VENDOR_NVIDIA || !gl_extensions.VersionGEThan(3, 0)) {
			useCPU = true;
		}
	} else {
		useCPU = true;
	}

	if (useCPU)
		features |= GPU_PREFER_CPU_DOWNLOAD;

	if ((gl_extensions.gpuVendor == GPU_VENDOR_NVIDIA) || (gl_extensions.gpuVendor == GPU_VENDOR_AMD))
		features |= GPU_PREFER_REVERSE_COLOR_ORDER;

	if (gl_extensions.OES_texture_npot)
		features |= GPU_SUPPORTS_OES_TEXTURE_NPOT;

	if (gl_extensions.EXT_blend_minmax)
		features |= GPU_SUPPORTS_BLEND_MINMAX;

	if (gl_extensions.OES_copy_image || gl_extensions.NV_copy_image || gl_extensions.EXT_copy_image || gl_extensions.ARB_copy_image)
		features |= GPU_SUPPORTS_ANY_COPY_IMAGE;

	if (!gl_extensions.IsGLES)
		features |= GPU_SUPPORTS_LOGIC_OP;

	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;

	if (gl_extensions.EXT_texture_filter_anisotropic)
		features |= GPU_SUPPORTS_ANISOTROPY;

	bool canUseInstanceID = gl_extensions.EXT_draw_instanced || gl_extensions.ARB_draw_instanced;
	bool canDefInstanceID = gl_extensions.IsGLES || gl_extensions.EXT_gpu_shader4 || gl_extensions.VersionGEThan(3, 1);
	bool instanceRendering = gl_extensions.GLES3 || (canUseInstanceID && canDefInstanceID);
	if (instanceRendering)
		features |= GPU_SUPPORTS_INSTANCE_RENDERING;

	int maxVertexTextureImageUnits = gl_extensions.maxVertexTextureUnits;
	if (maxVertexTextureImageUnits >= 3) // At least 3 for hardware tessellation
		features |= GPU_SUPPORTS_VERTEX_TEXTURE_FETCH;

	if (gl_extensions.ARB_texture_float || gl_extensions.OES_texture_float)
		features |= GPU_SUPPORTS_TEXTURE_FLOAT;

	// If we already have a 16-bit depth buffer, we don't need to round.
	bool prefer24 = draw_->GetDeviceCaps().preferredDepthBufferFormat == Draw::DataFormat::D24_S8;
	if (prefer24) {
		if (!g_Config.bHighQualityDepth && (features & GPU_SUPPORTS_ACCURATE_DEPTH) != 0) {
			features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
		} else if (PSP_CoreParameter().compat.flags().PixelDepthRounding) {
			if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
				// Use fragment rounding on desktop and GLES3, most accurate.
				features |= GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
			} else if (prefer24 && (features & GPU_SUPPORTS_ACCURATE_DEPTH) != 0) {
				// Here we can simulate a 16 bit depth buffer by scaling.
				// Note that the depth buffer is fixed point, not floating, so dividing by 256 is pretty good.
				features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
			} else {
				// At least do vertex rounding if nothing else.
				features |= GPU_ROUND_DEPTH_TO_16BIT;
			}
		} else if (PSP_CoreParameter().compat.flags().VertexDepthRounding) {
			features |= GPU_ROUND_DEPTH_TO_16BIT;
		}
	}

	// The Phantasy Star hack :(
	if (PSP_CoreParameter().compat.flags().DepthRangeHack && (features & GPU_SUPPORTS_ACCURATE_DEPTH) == 0) {
		features |= GPU_USE_DEPTH_RANGE_HACK;
	}

	if (PSP_CoreParameter().compat.flags().ClearToRAM) {
		features |= GPU_USE_CLEAR_RAM_HACK;
	}

	gstate_c.featureFlags = features;
}

bool GPU_GLES::IsReady() {
	return shaderManagerGL_->ContinuePrecompile();
}

void GPU_GLES::BuildReportingInfo() {
	GLRenderManager *render = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	std::string glVendor = render->GetGLString(GL_VENDOR);
	std::string glRenderer = render->GetGLString(GL_RENDERER);
	std::string glVersion = render->GetGLString(GL_VERSION);
	std::string glSlVersion = render->GetGLString(GL_SHADING_LANGUAGE_VERSION);
	std::string glExtensions;

	if (gl_extensions.VersionGEThan(3, 0)) {
		glExtensions = g_all_gl_extensions;
	} else {
		glExtensions = render->GetGLString(GL_EXTENSIONS);
	}

	char temp[16384];
	snprintf(temp, sizeof(temp), "%s (%s %s), %s (extensions: %s)", glVersion.c_str(), glVendor.c_str(), glRenderer.c_str(), glSlVersion.c_str(), glExtensions.c_str());
	reportingPrimaryInfo_ = glVendor;
	reportingFullInfo_ = temp;

	Reporting::UpdateConfig();
}

void GPU_GLES::DeviceLost() {
	ILOG("GPU_GLES: DeviceLost");

	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	// TransformDraw has registered as a GfxResourceHolder.
	shaderManagerGL_->DeviceLost();
	textureCacheGL_->DeviceLost();
	fragmentTestCache_.DeviceLost();
	depalShaderCache_.DeviceLost();
	drawEngine_.DeviceLost();
	framebufferManagerGL_->DeviceLost();
	// Don't even try to access the lost device.
	draw_ = nullptr;
}

void GPU_GLES::DeviceRestore() {
	draw_ = (Draw::DrawContext *)PSP_CoreParameter().graphicsContext->GetDrawContext();
	ILOG("GPU_GLES: DeviceRestore");

	UpdateCmdInfo();
	UpdateVsyncInterval(true);

	shaderManagerGL_->DeviceRestore(draw_);
	textureCacheGL_->DeviceRestore(draw_);
	framebufferManagerGL_->DeviceRestore(draw_);
	drawEngine_.DeviceRestore(draw_);
	fragmentTestCache_.DeviceRestore(draw_);
	depalShaderCache_.DeviceRestore(draw_);
}

void GPU_GLES::Reinitialize() {
	GPUCommon::Reinitialize();
	textureCacheGL_->Clear(true);
	depalShaderCache_.Clear();
	framebufferManagerGL_->DestroyAllFBOs();
}

void GPU_GLES::InitClear() {
}

void GPU_GLES::BeginHostFrame() {
	GPUCommon::BeginHostFrame();
	UpdateCmdInfo();
	if (resized_) {
		CheckGPUFeatures();
		framebufferManager_->Resized();
		drawEngine_.Resized();
		shaderManagerGL_->DirtyShader();
		textureCacheGL_->NotifyConfigChanged();
	}

	drawEngine_.BeginFrame();
}

void GPU_GLES::EndHostFrame() {
	drawEngine_.EndFrame();
}

inline void GPU_GLES::UpdateVsyncInterval(bool force) {
#ifdef _WIN32
	int desiredVSyncInterval = g_Config.bVSync ? 1 : 0;
	if (PSP_CoreParameter().unthrottle) {
		desiredVSyncInterval = 0;
	}
	if (PSP_CoreParameter().fpsLimit == 1) {
		// For an alternative speed that is a clean factor of 60, the user probably still wants vsync.
		if (g_Config.iFpsLimit == 0 || (g_Config.iFpsLimit != 15 && g_Config.iFpsLimit != 30 && g_Config.iFpsLimit != 60)) {
			desiredVSyncInterval = 0;
		}
	}

	if (desiredVSyncInterval != lastVsync_ || force) {
		// Disabled EXT_swap_control_tear for now, it never seems to settle at the correct timing
		// so it just keeps tearing. Not what I hoped for...
		//if (gl_extensions.EXT_swap_control_tear) {
		//	// See http://developer.download.nvidia.com/opengl/specs/WGL_EXT_swap_control_tear.txt
		//	glstate.SetVSyncInterval(-desiredVSyncInterval);
		//} else {
		gfxCtx_->SwapInterval(desiredVSyncInterval);
		//}
		lastVsync_ = desiredVSyncInterval;
	}
#endif
}

void GPU_GLES::ReapplyGfxState() {
	GPUCommon::ReapplyGfxState();
}

void GPU_GLES::BeginFrame() {
	UpdateVsyncInterval(resized_);
	resized_ = false;

	textureCacheGL_->StartFrame();
	drawEngine_.DecimateTrackedVertexArrays();
	depalShaderCache_.Decimate();
	fragmentTestCache_.Decimate();

	GPUCommon::BeginFrame();

	// Save the cache from time to time. TODO: How often? We save on exit, so shouldn't need to do this all that often.
	if (!shaderCachePath_.empty() && (gpuStats.numFlips & 4095) == 0) {
		shaderManagerGL_->Save(shaderCachePath_);
	}

	shaderManagerGL_->DirtyShader();

	// Not sure if this is really needed.
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);

	framebufferManagerGL_->BeginFrame();
}

void GPU_GLES::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	host->GPUNotifyDisplay(framebuf, stride, format);
	framebufferManagerGL_->SetDisplayFramebuffer(framebuf, stride, format);
}

void GPU_GLES::CopyDisplayToOutput() {
	// Flush anything left over.
	framebufferManagerGL_->RebindFramebuffer();
	drawEngine_.Flush();

	shaderManagerGL_->DirtyLastShader();

	framebufferManagerGL_->CopyDisplayToOutput();
	framebufferManagerGL_->EndFrame();

	// If buffered, discard the depth buffer of the backbuffer. Don't even know if we need one.
#if 0
#ifdef USING_GLES2
	if (gl_extensions.EXT_discard_framebuffer && g_Config.iRenderingMode != 0) {
		GLenum attachments[] = {GL_DEPTH_EXT, GL_STENCIL_EXT};
		glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, attachments);
	}
#endif
#endif
}

void GPU_GLES::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

inline void GPU_GLES::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE)) {
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		drawEngine_.Flush();
	}
}

void GPU_GLES::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPU_GLES::ExecuteOp(u32 op, u32 diff) {
	const u8 cmd = op >> 24;
	const CommandInfo info = cmdInfo_[cmd];
	const u8 cmdFlags = info.flags;
	if ((cmdFlags & FLAG_EXECUTE) || (diff && (cmdFlags & FLAG_EXECUTEONCHANGE))) {
		(this->*info.func)(op, diff);
	} else if (diff) {
		uint64_t dirty = info.flags >> 8;
		if (dirty)
			gstate_c.Dirty(dirty);
	}
}

void GPU_GLES::GetStats(char *buffer, size_t bufsize) {
	float vertexAverageCycles = gpuStats.numVertsSubmitted > 0 ? (float)gpuStats.vertexGPUCycles / (float)gpuStats.numVertsSubmitted : 0.0f;
	snprintf(buffer, bufsize - 1,
		"DL processing time: %0.2f ms\n"
		"Draw calls: %i, flushes %i, clears %i\n"
		"Cached Draw calls: %i\n"
		"Num Tracked Vertex Arrays: %i\n"
		"GPU cycles executed: %d (%f per vertex)\n"
		"Commands per call level: %i %i %i %i\n"
		"Vertices submitted: %i\n"
		"Cached, Uncached Vertices Drawn: %i, %i\n"
		"FBOs active: %i\n"
		"Textures active: %i, decoded: %i  invalidated: %i\n"
		"Readbacks: %d, uploads: %d\n"
		"Vertex, Fragment, Programs loaded: %i, %i, %i\n",
		gpuStats.msProcessingDisplayLists * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numClears,
		gpuStats.numCachedDrawCalls,
		gpuStats.numTrackedVertexArrays,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles,
		gpuStats.gpuCommandsAtCallLevel[0], gpuStats.gpuCommandsAtCallLevel[1], gpuStats.gpuCommandsAtCallLevel[2], gpuStats.gpuCommandsAtCallLevel[3],
		gpuStats.numVertsSubmitted,
		gpuStats.numCachedVertsDrawn,
		gpuStats.numUncachedVertsDrawn,
		(int)framebufferManagerGL_->NumVFBs(),
		(int)textureCacheGL_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		gpuStats.numReadbacks,
		gpuStats.numUploads,
		shaderManagerGL_->GetNumVertexShaders(),
		shaderManagerGL_->GetNumFragmentShaders(),
		shaderManagerGL_->GetNumPrograms());
}

void GPU_GLES::ClearCacheNextFrame() {
	textureCacheGL_->ClearNextFrame();
}

void GPU_GLES::ClearShaderCache() {
	shaderManagerGL_->ClearCache(true);
}

void GPU_GLES::CleanupBeforeUI() {
	// Clear any enabled vertex arrays.
	shaderManagerGL_->DirtyLastShader();
}

void GPU_GLES::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	// In Freeze-Frame mode, we don't want to do any of this.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCacheGL_->Clear(true);
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManagerGL_->DestroyAllFBOs();
	}
}

std::vector<std::string> GPU_GLES::DebugGetShaderIDs(DebugShaderType type) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderIDs();
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_.DebugGetShaderIDs(type);
	default:
		return shaderManagerGL_->DebugGetShaderIDs(type);
	}
}

std::string GPU_GLES::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	case SHADER_TYPE_DEPAL:
		return depalShaderCache_.DebugGetShaderString(id, type, stringType);
	default:
		return shaderManagerGL_->DebugGetShaderString(id, type, stringType);
	}
}
