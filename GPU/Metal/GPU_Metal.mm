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

#include "ppsspp_config.h"

#include "Common/Log.h"
#include "Common/Data/Text/StringWriter.h"
#include "Common/GPU/thin3d.h"
#include "Core/Reporting.h"
#include "GPU/GPUState.h"
#include "GPU/Metal/GPU_Metal.h"
#include "GPU/Metal/DrawEngineMetal.h"
#include "GPU/Metal/FramebufferManagerMetal.h"
#include "GPU/Metal/ShaderManagerMetal.h"
#include "GPU/Metal/TextureCacheMetal.h"

GPU_Metal::GPU_Metal(GraphicsContext *gfxCtx, Draw::DrawContext *draw) : GPUCommonHW(gfxCtx, draw) {
	gstate_c.SetUseFlags(CheckGPUFeatures());

	drawEngine_ = new DrawEngineMetal(draw);
	shaderManagerMetal_ = new ShaderManagerMetal(draw);
	framebufferManagerMetal_ = new FramebufferManagerMetal(draw);
	framebufferManager_ = framebufferManagerMetal_;
	textureCacheMetal_ = new TextureCacheMetal(draw, framebufferManager_->GetDraw2D());
	textureCache_ = textureCacheMetal_;
	drawEngineCommon_ = drawEngine_;
	shaderManager_ = shaderManagerMetal_;

	drawEngine_->SetGPUCommon(this);
	drawEngine_->SetTextureCache(textureCacheMetal_);
	drawEngine_->SetFramebufferManager(framebufferManagerMetal_);
	drawEngine_->SetShaderManager(shaderManagerMetal_);
	drawEngine_->Init();
	framebufferManagerMetal_->SetTextureCache(textureCacheMetal_);
	framebufferManagerMetal_->SetDrawEngine(drawEngine_);
	framebufferManagerMetal_->SetShaderManager(shaderManagerMetal_);
	framebufferManagerMetal_->Init(msaaLevel_);
	textureCacheMetal_->SetFramebufferManager(framebufferManagerMetal_);
	textureCacheMetal_->SetShaderManager(shaderManagerMetal_);
	textureCacheMetal_->SetDrawEngine(drawEngine_);

	UpdateCmdInfo();
	BuildReportingInfo();
	textureCache_->NotifyConfigChanged();
}

GPU_Metal::~GPU_Metal() {
	if (draw_) {
		MetalRenderManager *rm = (MetalRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->WaitIdle();
	}
	// The other managers are deleted in ~GPUCommonHW.
	delete drawEngine_;
	drawEngine_ = nullptr;
	drawEngineCommon_ = nullptr;
}

u32 GPU_Metal::CheckGPUFeatures() const {
	u32 features = GPUCommonHW::CheckGPUFeatures();
	return CheckGPUFeaturesLate(features);
}

void GPU_Metal::BeginHostFrame(const DisplayLayoutConfig &config) {
	GPUCommonHW::BeginHostFrame(config);

	drawEngine_->BeginFrame();
	textureCache_->StartFrame();
	framebufferManager_->BeginFrame(config);

	gstate_c.Dirty(DIRTY_ALL);

	if (gstate_c.useFlagsChanged) {
		WARN_LOG(Log::G3D, "Shader use flags changed, clearing all shaders and depth buffers");
		drawEngine_->ClearPipelines();
		shaderManager_->ClearShaders();
		framebufferManager_->ClearAllDepthBuffers();
		gstate_c.useFlagsChanged = false;
	}
}

void GPU_Metal::FinishDeferred() {
	drawEngine_->FinishDeferred();
}

void GPU_Metal::DeviceLost() {
	// The managers free their caches (pipelines, samplers, depth stencil states) right away, so nothing
	// recorded can still be waiting to use them.
	if (draw_) {
		MetalRenderManager *rm = (MetalRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->WaitIdle();
	}
	drawEngine_->ClearPipelines();
	GPUCommonHW::DeviceLost();
}

void GPU_Metal::DeviceRestore(Draw::DrawContext *draw) {
	GPUCommonHW::DeviceRestore(draw);
}

void GPU_Metal::GetStats(StringWriter &w) {
	w.F("Vertex, Fragment shaders, Pipelines loaded: %d, %d, %d\n",
		shaderManagerMetal_->GetNumVertexShaders(),
		shaderManagerMetal_->GetNumFragmentShaders(),
		drawEngine_->GetNumPipelines());
	FormatGPUStatsCommon(w);
}
