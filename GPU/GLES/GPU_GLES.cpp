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
#include "Common/VR/PPSSPPVR.h"

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
	: GPUCommon(gfxCtx, draw), drawEngine_(draw), fragmentTestCache_(draw) {
	UpdateVsyncInterval(true);
	gstate_c.SetUseFlags(CheckGPUFeatures());

	shaderManagerGL_ = new ShaderManagerGLES(draw);
	framebufferManagerGL_ = new FramebufferManagerGLES(draw);
	framebufferManager_ = framebufferManagerGL_;
	textureCacheGL_ = new TextureCacheGLES(draw, framebufferManager_->GetDraw2D());
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
	framebufferManagerGL_->Init(msaaLevel_);
	textureCacheGL_->SetFramebufferManager(framebufferManagerGL_);
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

	textureCache_->NotifyConfigChanged();

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
	fragmentTestCache_.Clear();
	
	delete shaderManagerGL_;
	shaderManagerGL_ = nullptr;
	delete framebufferManagerGL_;
	delete textureCacheGL_;

	// Clear features so they're not visible in system info.
	gstate_c.SetUseFlags(0);
}

// Take the raw GL extension and versioning data and turn into feature flags.
// TODO: This should use DrawContext::GetDeviceCaps() more and more, and eventually
// this can be shared between all the backends.
u32 GPU_GLES::CheckGPUFeatures() const {
	u32 features = GPUCommon::CheckGPUFeatures();

	features |= GPU_USE_16BIT_FORMATS;

	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		features |= GPU_USE_TEXTURE_LOD_CONTROL;

	bool canUseInstanceID = gl_extensions.EXT_draw_instanced || gl_extensions.ARB_draw_instanced;
	bool canDefInstanceID = gl_extensions.IsGLES || gl_extensions.EXT_gpu_shader4 || gl_extensions.VersionGEThan(3, 1);
	bool instanceRendering = gl_extensions.GLES3 || (canUseInstanceID && canDefInstanceID);
	if (instanceRendering)
		features |= GPU_USE_INSTANCE_RENDERING;

	int maxVertexTextureImageUnits = gl_extensions.maxVertexTextureUnits;
	if (maxVertexTextureImageUnits >= 3) // At least 3 for hardware tessellation
		features |= GPU_USE_VERTEX_TEXTURE_FETCH;

	if (gl_extensions.ARB_texture_float || gl_extensions.OES_texture_float)
		features |= GPU_USE_TEXTURE_FLOAT;

	if (!draw_->GetShaderLanguageDesc().bitwiseOps) {
		features |= GPU_USE_FRAGMENT_TEST_CACHE;
	}

	if (IsVREnabled()) {
		features |= GPU_USE_VIRTUAL_REALITY;
	}
	if (IsMultiviewSupported()) {
		features |= GPU_USE_SINGLE_PASS_STEREO;
	}

	features = CheckGPUFeaturesLate(features);

	if (draw_->GetBugs().Has(Draw::Bugs::ADRENO_RESOURCE_DEADLOCK) && g_Config.bVendorBugChecksEnabled) {
		if (PSP_CoreParameter().compat.flags().OldAdrenoPixelDepthRoundingGL) {
			features |= GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
		}
	}

	// This is a bit ugly, but lets us reuse most of the depth logic in GPUCommon.
	if (features & GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT) {
		if (gl_extensions.IsGLES && !gl_extensions.GLES3) {
			// Unsupported, switch to GPU_ROUND_DEPTH_TO_16BIT instead.
			features &= ~GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
			features |= GPU_ROUND_DEPTH_TO_16BIT;
		}
	}

	return features;
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
}

void GPU_GLES::Reinitialize() {
	GPUCommon::Reinitialize();
}

void GPU_GLES::InitClear() {
}

void GPU_GLES::BeginHostFrame() {
	GPUCommon::BeginHostFrame();
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

void GPU_GLES::CopyDisplayToOutput(bool reallyDirty) {
	// Flush anything left over.
	framebufferManagerGL_->RebindFramebuffer("RebindFramebuffer - CopyDisplayToOutput");
	drawEngine_.Flush();

	shaderManagerGL_->DirtyLastShader();

	framebufferManagerGL_->CopyDisplayToOutput(reallyDirty);
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

void GPU_GLES::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	// In Freeze-Frame mode, we don't want to do any of this.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCache_->Clear(true);
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManager_->DestroyAllFBOs();
	}
}

std::vector<std::string> GPU_GLES::DebugGetShaderIDs(DebugShaderType type) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderIDs();
	case SHADER_TYPE_TEXTURE:
		return textureCache_->GetTextureShaderCache()->DebugGetShaderIDs(type);
	default:
		return shaderManagerGL_->DebugGetShaderIDs(type);
	}
}

std::string GPU_GLES::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	switch (type) {
	case SHADER_TYPE_VERTEXLOADER:
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	case SHADER_TYPE_TEXTURE:
		return textureCache_->GetTextureShaderCache()->DebugGetShaderString(id, type, stringType);
	default:
		return shaderManagerGL_->DebugGetShaderString(id, type, stringType);
	}
}
