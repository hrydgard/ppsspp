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

#include <string>

#include "Common/Serialize/Serializer.h"
#include "Common/GraphicsContext.h"
#include "Common/System/OSD.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

#include "Common/GPU/D3D9/D3D9StateCache.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"

#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/FramebufferManagerDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"

GPU_DX9::GPU_DX9(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommonHW(gfxCtx, draw),
	  drawEngine_(draw) {
	device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	deviceEx_ = (LPDIRECT3DDEVICE9EX)draw->GetNativeObject(Draw::NativeObject::DEVICE_EX);

	shaderManagerDX9_ = new ShaderManagerDX9(draw, device_);
	framebufferManagerDX9_ = new FramebufferManagerDX9(draw);
	framebufferManager_ = framebufferManagerDX9_;
	textureCacheDX9_ = new TextureCacheDX9(draw, framebufferManager_->GetDraw2D());
	textureCache_ = textureCacheDX9_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerDX9_;

	drawEngine_.SetShaderManager(shaderManagerDX9_);
	drawEngine_.SetTextureCache(textureCacheDX9_);
	drawEngine_.SetFramebufferManager(framebufferManagerDX9_);
	drawEngine_.Init();
	framebufferManagerDX9_->SetTextureCache(textureCacheDX9_);
	framebufferManagerDX9_->SetShaderManager(shaderManagerDX9_);
	framebufferManagerDX9_->SetDrawEngine(&drawEngine_);
	framebufferManagerDX9_->Init(msaaLevel_);
	textureCacheDX9_->SetFramebufferManager(framebufferManagerDX9_);
	textureCacheDX9_->SetShaderManager(shaderManagerDX9_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(Log::G3D, "gstate has drifted out of sync!");
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.
	UpdateCmdInfo();
	gstate_c.SetUseFlags(CheckGPUFeatures());

	BuildReportingInfo();

	// Some of our defaults are different from hw defaults, let's assert them.
	// We restore each frame anyway, but here is convenient for tests.
	dxstate.Restore();
	textureCache_->NotifyConfigChanged();

	if (g_Config.bHardwareTessellation) {
		// Disable hardware tessellation bacause DX9 is still unsupported.
		ERROR_LOG(Log::G3D, "Hardware Tessellation is unsupported, falling back to software tessellation");
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		// TODO: Badly formulated
		g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("Turn off Hardware Tessellation - unsupported"));
	}
}

u32 GPU_DX9::CheckGPUFeatures() const {
	u32 features = GPUCommonHW::CheckGPUFeatures();
	features |= GPU_USE_16BIT_FORMATS;
	features |= GPU_USE_TEXTURE_LOD_CONTROL;

	// Accurate depth is required because the Direct3D API does not support inverse Z.
	// So we cannot incorrectly use the viewport transform as the depth range on Direct3D.
	features |= GPU_USE_ACCURATE_DEPTH;

	return CheckGPUFeaturesLate(features);
}

void GPU_DX9::DeviceLost() {
	GPUCommonHW::DeviceLost();
}

void GPU_DX9::DeviceRestore(Draw::DrawContext *draw) {
	GPUCommonHW::DeviceRestore(draw);
}

void GPU_DX9::ReapplyGfxState() {
	dxstate.Restore();
	GPUCommonHW::ReapplyGfxState();
}

void GPU_DX9::BeginHostFrame() {
	GPUCommonHW::BeginHostFrame();

	textureCache_->StartFrame();
	drawEngine_.BeginFrame();

	shaderManagerDX9_->DirtyLastShader();

	framebufferManager_->BeginFrame();

	if (gstate_c.useFlagsChanged) {
		// TODO: It'd be better to recompile them in the background, probably?
		// This most likely means that saw equal depth changed.
		WARN_LOG(Log::G3D, "Shader use flags changed, clearing all shaders and depth buffers");
		shaderManager_->ClearShaders();
		framebufferManager_->ClearAllDepthBuffers();
		gstate_c.useFlagsChanged = false;
	}
}

void GPU_DX9::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

void GPU_DX9::GetStats(char *buffer, size_t bufsize) {
	size_t offset = FormatGPUStatsCommon(buffer, bufsize);
	buffer += offset;
	bufsize -= offset;
	if ((int)bufsize < 0)
		return;
	snprintf(buffer, bufsize,
		"Vertex, Fragment shaders loaded: %i, %i\n",
		shaderManagerDX9_->GetNumVertexShaders(),
		shaderManagerDX9_->GetNumFragmentShaders()
	);
}
