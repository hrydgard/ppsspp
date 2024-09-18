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
#include "Common/System/OSD.h"
#include "Common/VR/PPSSPPVR.h"

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
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

#ifdef _WIN32
#include "Windows/GPU/WindowsGLContext.h"
#endif

GPU_GLES::GPU_GLES(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommonHW(gfxCtx, draw), drawEngine_(draw), fragmentTestCache_(draw) {
	gstate_c.SetUseFlags(CheckGPUFeatures());

	shaderManagerGL_ = new ShaderManagerGLES(draw);
	framebufferManagerGL_ = new FramebufferManagerGLES(draw);
	framebufferManager_ = framebufferManagerGL_;
	textureCacheGL_ = new TextureCacheGLES(draw, framebufferManager_->GetDraw2D());
	textureCache_ = textureCacheGL_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerGL_;

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
		ERROR_LOG(Log::G3D, "gstate has drifted out of sync!");
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.

	UpdateCmdInfo();

	BuildReportingInfo();

	textureCache_->NotifyConfigChanged();

	// Load shader cache.
	std::string discID = g_paramSFO.GetDiscID();
	if (discID.size()) {
		if (g_Config.bShaderCache) {
			File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
			shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) / (discID + ".glshadercache");
			// Actually precompiled by IsReady() since we're single-threaded.
			File::IOFile f(shaderCachePath_, "rb");
			if (f.IsOpen()) {
				if (shaderManagerGL_->LoadCacheFlags(f, &drawEngine_)) {
					if (drawEngineCommon_->EverUsedExactEqualDepth()) {
						sawExactEqualDepth_ = true;
					}
					gstate_c.SetUseFlags(CheckGPUFeatures());
					// We're compiling now, clear if they changed.
					gstate_c.useFlagsChanged = false;

					if (shaderManagerGL_->LoadCache(f))
						NOTICE_LOG(Log::G3D, "Precompiling the shader cache from '%s'", shaderCachePath_.c_str());
				}
			}
		} else {
			INFO_LOG(Log::G3D, "Shader cache disabled. Not loading.");
		}
	}

	if (g_Config.bHardwareTessellation) {
		// Disable hardware tessellation if device is unsupported.
		if (!drawEngine_.SupportsHWTessellation()) {
			ERROR_LOG(Log::G3D, "Hardware Tessellation is unsupported, falling back to software tessellation");
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("Turn off Hardware Tessellation - unsupported"));
		}
	}
}

GPU_GLES::~GPU_GLES() {
	// If we're here during app shutdown (exiting the Windows app in-game, for example)
	// everything should already be cleared since DeviceLost has been run.

	if (shaderCachePath_.Valid() && draw_) {
		if (g_Config.bShaderCache) {
			shaderManagerGL_->SaveCache(shaderCachePath_, &drawEngine_);
		} else {
			INFO_LOG(Log::G3D, "Shader cache disabled. Not saving.");
		}
	}
	fragmentTestCache_.Clear();
}

// Take the raw GL extension and versioning data and turn into feature flags.
// TODO: This should use DrawContext::GetDeviceCaps() more and more, and eventually
// this can be shared between all the backends.
u32 GPU_GLES::CheckGPUFeatures() const {
	u32 features = GPUCommonHW::CheckGPUFeatures();

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

	// Can't use switch-case in older glsl.
	if ((gl_extensions.IsGLES && !gl_extensions.GLES3) || (!gl_extensions.IsGLES && !gl_extensions.VersionGEThan(1, 3)))
		features &= ~GPU_USE_LIGHT_UBERSHADER;

	if (IsVREnabled() || g_Config.bForceVR) {
		features |= GPU_USE_VIRTUAL_REALITY;
		features &= ~GPU_USE_VS_RANGE_CULLING;
	}

	if (!gl_extensions.GLES3) {
		// Heuristic.
		features &= ~GPU_USE_FRAGMENT_UBERSHADER;
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
	INFO_LOG(Log::G3D, "GPU_GLES: DeviceLost");

	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	// TransformDraw has registered as a GfxResourceHolder.
	fragmentTestCache_.DeviceLost();

	GPUCommonHW::DeviceLost();
}

void GPU_GLES::DeviceRestore(Draw::DrawContext *draw) {
	GPUCommonHW::DeviceRestore(draw);

	fragmentTestCache_.DeviceRestore(draw_);
}

void GPU_GLES::BeginHostFrame() {
	GPUCommonHW::BeginHostFrame();
	drawEngine_.BeginFrame();

	textureCache_->StartFrame();

	// Save the cache from time to time. TODO: How often? We save on exit, so shouldn't need to do this all that often.

	const int saveShaderCacheFrameInterval = 32767;  // power of 2 - 1. About every 10 minutes at 60fps.
	if (shaderCachePath_.Valid() && !(gpuStats.numFlips & saveShaderCacheFrameInterval) && coreState == CORE_RUNNING) {
		shaderManagerGL_->SaveCache(shaderCachePath_, &drawEngine_);
	}
	shaderManagerGL_->DirtyLastShader();

	// Not sure if this is really needed.
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);

	framebufferManager_->BeginFrame();

	fragmentTestCache_.Decimate();
	if (gstate_c.useFlagsChanged) {
		// TODO: It'd be better to recompile them in the background, probably?
		// This most likely means that saw equal depth changed.
		WARN_LOG(Log::G3D, "Shader use flags changed, clearing all shaders and depth buffers");
		shaderManager_->ClearShaders();
		framebufferManager_->ClearAllDepthBuffers();
		gstate_c.useFlagsChanged = false;
	}
}

void GPU_GLES::EndHostFrame() {
	drawEngine_.EndFrame();
}

void GPU_GLES::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
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
