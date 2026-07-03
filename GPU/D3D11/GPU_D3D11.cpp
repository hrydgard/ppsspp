// Copyright (c) 2017- PPSSPP Project.

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

#include "Common/Log.h"
#include "Common/GraphicsContext.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Data/Text/StringWriter.h"

#include "GPU/GPUState.h"

#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/GPU_D3D11.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

GPU_D3D11::GPU_D3D11(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommonHW(gfxCtx, draw), drawEngine_(draw,
	(ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE),
	(ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT)) {
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	D3D_FEATURE_LEVEL featureLevel = (D3D_FEATURE_LEVEL)draw->GetNativeObject(Draw::NativeObject::FEATURE_LEVEL);

	shaderManagerD3D11_ = new ShaderManagerD3D11(draw, device_, context_, featureLevel);
	framebufferManagerD3D11_ = new FramebufferManagerD3D11(draw);
	framebufferManager_ = framebufferManagerD3D11_;
	textureCacheD3D11_ = new TextureCacheD3D11(draw, framebufferManager_->GetDraw2D());
	textureCache_ = textureCacheD3D11_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerD3D11_;
	drawEngine_.SetGPUCommon(this);
	drawEngine_.SetShaderManager(shaderManagerD3D11_);
	drawEngine_.SetTextureCache(textureCacheD3D11_);
	drawEngine_.SetFramebufferManager(framebufferManagerD3D11_);
	drawEngine_.Init();
	framebufferManagerD3D11_->SetTextureCache(textureCacheD3D11_);
	framebufferManagerD3D11_->SetShaderManager(shaderManagerD3D11_);
	framebufferManagerD3D11_->SetDrawEngine(&drawEngine_);
	framebufferManagerD3D11_->Init(msaaLevel_);
	textureCacheD3D11_->SetFramebufferManager(framebufferManagerD3D11_);
	textureCacheD3D11_->SetShaderManager(shaderManagerD3D11_);

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
	textureCache_->NotifyConfigChanged();
}

GPU_D3D11::~GPU_D3D11() {}

void GPU_D3D11::DeviceLost() {
	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	shaderManager_->ClearShaders();
	drawEngine_.ClearInputLayoutMap();

	GPUCommonHW::DeviceLost();
}

void GPU_D3D11::DeviceRestore(Draw::DrawContext *draw) {
	GPUCommonHW::DeviceRestore(draw);
}

void GPU_D3D11::BeginHostFrame(const DisplayLayoutConfig &config) {
	GPUCommonHW::BeginHostFrame(config);

	textureCache_->StartFrame();
	drawEngine_.BeginFrame();

	shaderManager_->DirtyLastShader();

	framebufferManager_->BeginFrame(config);

	if (gstate_c.useFlagsChanged) {
		// TODO: It'd be better to recompile them in the background, probably?
		// This most likely means that saw equal depth changed.
		WARN_LOG(Log::G3D, "Shader use flags changed, clearing all shaders and depth buffers");
		shaderManager_->ClearShaders();
		framebufferManager_->ClearAllDepthBuffers();
		drawEngine_.ClearInputLayoutMap();
		gstate_c.useFlagsChanged = false;
	}
}

void GPU_D3D11::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

void GPU_D3D11::GetStats(StringWriter &w) {
	FormatGPUStatsCommon(w);
	w.F("Vertex, Fragment shaders loaded: %d, %d\n",
		shaderManagerD3D11_->GetNumVertexShaders(),
		shaderManagerD3D11_->GetNumFragmentShaders()
	);
}
