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

#include "Common/Profiler/Profiler.h"
#include "Common/Data/Text/I18n.h"

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/File/FileUtil.h"
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
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Debugger/Debugger.h"
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
	: GPUCommon(gfxCtx, draw), depalShaderCache_(draw), drawEngine_(draw), fragmentTestCache_(draw) {
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
	drawEngine_.Init();
	framebufferManagerGL_->SetTextureCache(textureCacheGL_);
	framebufferManagerGL_->SetShaderManager(shaderManagerGL_);
	framebufferManagerGL_->SetDrawEngine(&drawEngine_);
	framebufferManagerGL_->Init();
	depalShaderCache_.Init();
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
		if (g_Config.bShaderCache) {
			File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
			shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) / (discID + ".glshadercache");
			// Actually precompiled by IsReady() since we're single-threaded.
			shaderManagerGL_->Load(shaderCachePath_);
		} else {
			INFO_LOG(G3D, "Shader cache disabled. Not loading.");
		}
	}

	if (g_Config.bHardwareTessellation) {
		// Disable hardware tessellation if device is unsupported.
		if (!drawEngine_.SupportsHWTessellation()) {
			ERROR_LOG(G3D, "Hardware Tessellation is unsupported, falling back to software tessellation");
			auto gr = GetI18NCategory("Graphics");
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

	if (shaderCachePath_.Valid() && draw_) {
		if (g_Config.bShaderCache) {
			shaderManagerGL_->Save(shaderCachePath_);
		} else {
			INFO_LOG(G3D, "Shader cache disabled. Not saving.");
		}
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

// Take the raw GL extension and versioning data and turn into feature flags.
void GPU_GLES::CheckGPUFeatures() {
	u32 features = 0;

	features |= GPU_SUPPORTS_16BIT_FORMATS;

	if (gl_extensions.ARB_blend_func_extended || gl_extensions.EXT_blend_func_extended) {
		if (!g_Config.bVendorBugChecksEnabled || !draw_->GetBugs().Has(Draw::Bugs::DUAL_SOURCE_BLENDING_BROKEN)) {
			features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
		}
	}

	if (gl_extensions.IsGLES) {
		if (gl_extensions.GLES3) {
			features |= GPU_SUPPORTS_GLSL_ES_300;
			// Mali reports 30 but works fine...
			if (gl_extensions.range[1][5][1] >= 30) {
				features |= GPU_SUPPORTS_32BIT_INT_FSHADER;
			}
		}
	} else {
		if (gl_extensions.VersionGEThan(3, 3, 0)) {
			features |= GPU_SUPPORTS_GLSL_330;
			features |= GPU_SUPPORTS_32BIT_INT_FSHADER;
		}
	}

	if (gl_extensions.EXT_shader_framebuffer_fetch || gl_extensions.ARM_shader_framebuffer_fetch) {
		// This has caused problems in the past.  Let's only enable on GLES3.
		if (features & GPU_SUPPORTS_GLSL_ES_300) {
			features |= GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH;
		}
	}

	if (gl_extensions.ARB_framebuffer_object || gl_extensions.NV_framebuffer_blit || gl_extensions.GLES3) {
		features |= GPU_SUPPORTS_FRAMEBUFFER_BLIT | GPU_SUPPORTS_FRAMEBUFFER_BLIT_TO_DEPTH;
	}

	if ((gl_extensions.gpuVendor == GPU_VENDOR_NVIDIA) || (gl_extensions.gpuVendor == GPU_VENDOR_AMD))
		features |= GPU_PREFER_REVERSE_COLOR_ORDER;

	if (gl_extensions.OES_texture_npot)
		features |= GPU_SUPPORTS_TEXTURE_NPOT;

	if (gl_extensions.EXT_blend_minmax)
		features |= GPU_SUPPORTS_BLEND_MINMAX;

	if (gl_extensions.OES_copy_image || gl_extensions.NV_copy_image || gl_extensions.EXT_copy_image || gl_extensions.ARB_copy_image)
		features |= GPU_SUPPORTS_COPY_IMAGE;

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

	if (draw_->GetDeviceCaps().depthClampSupported) {
		features |= GPU_SUPPORTS_DEPTH_CLAMP | GPU_SUPPORTS_ACCURATE_DEPTH;
		// Our implementation of depth texturing needs simple Z range, so can't
		// use the extension hacks (yet).
		if (gl_extensions.GLES3)
			features |= GPU_SUPPORTS_DEPTH_TEXTURE;
	}
	if (draw_->GetDeviceCaps().clipDistanceSupported)
		features |= GPU_SUPPORTS_CLIP_DISTANCE;
	if (draw_->GetDeviceCaps().cullDistanceSupported)
		features |= GPU_SUPPORTS_CULL_DISTANCE;
	if (!draw_->GetBugs().Has(Draw::Bugs::BROKEN_NAN_IN_CONDITIONAL)) {
		// Ignore the compat setting if clip and cull are both enabled.
		// When supported, we can do the depth side of range culling more correctly.
		const bool supported = draw_->GetDeviceCaps().clipDistanceSupported && draw_->GetDeviceCaps().cullDistanceSupported;
		const bool disabled = PSP_CoreParameter().compat.flags().DisableRangeCulling;
		if (supported || !disabled) {
			features |= GPU_SUPPORTS_VS_RANGE_CULLING;
		}
	}

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

void  GPU_GLES::CancelReady() {
	shaderManagerGL_->CancelPrecompile();
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
	INFO_LOG(G3D, "GPU_GLES: DeviceLost");

	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	// TransformDraw has registered as a GfxResourceHolder.
	CancelReady();
	shaderManagerGL_->DeviceLost();
	textureCacheGL_->DeviceLost();
	fragmentTestCache_.DeviceLost();
	depalShaderCache_.DeviceLost();
	drawEngine_.DeviceLost();

	GPUCommon::DeviceLost();
}

void GPU_GLES::DeviceRestore() {
	GPUCommon::DeviceRestore();

	UpdateCmdInfo();
	UpdateVsyncInterval(true);

	shaderManagerGL_->DeviceRestore(draw_);
	textureCacheGL_->DeviceRestore(draw_);
	drawEngine_.DeviceRestore(draw_);
	fragmentTestCache_.DeviceRestore(draw_);
	depalShaderCache_.DeviceRestore(draw_);
}

void GPU_GLES::Reinitialize() {
	GPUCommon::Reinitialize();
	depalShaderCache_.Clear();
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
		resized_ = false;
	}

	drawEngine_.BeginFrame();
}

void GPU_GLES::EndHostFrame() {
	drawEngine_.EndFrame();
}

void GPU_GLES::ReapplyGfxState() {
	GPUCommon::ReapplyGfxState();
}

void GPU_GLES::BeginFrame() {
	textureCacheGL_->StartFrame();
	depalShaderCache_.Decimate();
	fragmentTestCache_.Decimate();

	GPUCommon::BeginFrame();

	// Save the cache from time to time. TODO: How often? We save on exit, so shouldn't need to do this all that often.
	if (shaderCachePath_.Valid() && (gpuStats.numFlips & 4095) == 0) {
		shaderManagerGL_->Save(shaderCachePath_);
	}

	shaderManagerGL_->DirtyShader();

	// Not sure if this is really needed.
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);

	framebufferManagerGL_->BeginFrame();
}

void GPU_GLES::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	GPUDebug::NotifyDisplay(framebuf, stride, format);
	framebufferManagerGL_->SetDisplayFramebuffer(framebuf, stride, format);
}

void GPU_GLES::CopyDisplayToOutput(bool reallyDirty) {
	// Flush anything left over.
	framebufferManagerGL_->RebindFramebuffer("RebindFramebuffer - CopyDisplayToOutput");
	drawEngine_.Flush();

	shaderManagerGL_->DirtyLastShader();

	framebufferManagerGL_->CopyDisplayToOutput(reallyDirty);
	framebufferManagerGL_->EndFrame();
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
	size_t offset = FormatGPUStatsCommon(buffer, bufsize);
	buffer += offset;
	bufsize -= offset;
	if ((int)bufsize < 0)
		return;
	snprintf(buffer, bufsize,
		"Vertex, Fragment, Programs loaded: %d, %d, %d\n",
		shaderManagerGL_->GetNumVertexShaders(),
		shaderManagerGL_->GetNumFragmentShaders(),
		shaderManagerGL_->GetNumPrograms()
	);
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
		textureCache_->Clear(true);
		depalShaderCache_.Clear();
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManager_->DestroyAllFBOs();
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
