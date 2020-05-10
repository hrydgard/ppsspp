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

#include <cassert>
#include <cmath>
#include "base/display.h"
#include "base/timeutil.h"
#include "thin3d/thin3d.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/HLE/sceDisplay.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Common/PresentationCommon.h"

struct Vertex {
	float x, y, z;
	float u, v;
	uint32_t rgba;
};

void CenterDisplayOutputRect(float *x, float *y, float *w, float *h, float origW, float origH, float frameW, float frameH, int rotation) {
	float outW;
	float outH;

	bool rotated = rotation == ROTATION_LOCKED_VERTICAL || rotation == ROTATION_LOCKED_VERTICAL180;

	if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::STRETCH) {
		outW = frameW;
		outH = frameH;
	} else {
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::MANUAL) {
			float offsetX = (g_Config.fSmallDisplayOffsetX - 0.5f) * 2.0f * frameW;
			float offsetY = (g_Config.fSmallDisplayOffsetY - 0.5f) * 2.0f * frameH;
			// Have to invert Y for GL
			if (GetGPUBackend() == GPUBackend::OPENGL) {
				offsetY = offsetY * -1.0f;
			}
			float customZoom = g_Config.fSmallDisplayZoomLevel;
			float smallDisplayW = origW * customZoom;
			float smallDisplayH = origH * customZoom;
			if (!rotated) {
				*x = floorf(((frameW - smallDisplayW) / 2.0f) + offsetX);
				*y = floorf(((frameH - smallDisplayH) / 2.0f) + offsetY);
				*w = floorf(smallDisplayW);
				*h = floorf(smallDisplayH);
				return;
			} else {
				*x = floorf(((frameW - smallDisplayH) / 2.0f) + offsetX);
				*y = floorf(((frameH - smallDisplayW) / 2.0f) + offsetY);
				*w = floorf(smallDisplayH);
				*h = floorf(smallDisplayW);
				return;
			}
		} else if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
			// Stretch to 1080 for 272*4.  But don't distort if not widescreen (i.e. ultrawide of halfwide.)
			float pixelCrop = frameH / 270.0f;
			float resCommonWidescreen = pixelCrop - floor(pixelCrop);
			if (!rotated && resCommonWidescreen == 0.0f && frameW >= pixelCrop * 480.0f) {
				*x = floorf((frameW - pixelCrop * 480.0f) * 0.5f);
				*y = floorf(-pixelCrop);
				*w = floorf(pixelCrop * 480.0f);
				*h = floorf(pixelCrop * 272.0f);
				return;
			}
		}

		float origRatio = !rotated ? origW / origH : origH / origW;
		float frameRatio = frameW / frameH;

		if (origRatio > frameRatio) {
			// Image is wider than frame. Center vertically.
			outW = frameW;
			outH = frameW / origRatio;
			// Stretch a little bit
			if (!rotated && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH)
				outH = (frameH + outH) / 2.0f; // (408 + 720) / 2 = 564
		} else {
			// Image is taller than frame. Center horizontally.
			outW = frameH * origRatio;
			outH = frameH;
			if (rotated && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH)
				outW = (frameH + outH) / 2.0f; // (408 + 720) / 2 = 564
		}
	}

	*x = floorf((frameW - outW) / 2.0f);
	*y = floorf((frameH - outH) / 2.0f);
	*w = floorf(outW);
	*h = floorf(outH);
}

PresentationCommon::PresentationCommon(Draw::DrawContext *draw) : draw_(draw) {
	CreateDeviceObjects();
}

PresentationCommon::~PresentationCommon() {
	DestroyDeviceObjects();
}

void PresentationCommon::GetCardboardSettings(CardboardSettings *cardboardSettings) {
	// Calculate Cardboard Settings
	float cardboardScreenScale = g_Config.iCardboardScreenSize / 100.0f;
	float cardboardScreenWidth = pixelWidth_ / 2.0f * cardboardScreenScale;
	float cardboardScreenHeight = pixelHeight_ / 2.0f * cardboardScreenScale;
	float cardboardMaxXShift = (pixelWidth_ / 2.0f - cardboardScreenWidth) / 2.0f;
	float cardboardUserXShift = g_Config.iCardboardXShift / 100.0f * cardboardMaxXShift;
	float cardboardLeftEyeX = cardboardMaxXShift + cardboardUserXShift;
	float cardboardRightEyeX = pixelWidth_ / 2.0f + cardboardMaxXShift - cardboardUserXShift;
	float cardboardMaxYShift = pixelHeight_ / 2.0f - cardboardScreenHeight / 2.0f;
	float cardboardUserYShift = g_Config.iCardboardYShift / 100.0f * cardboardMaxYShift;
	float cardboardScreenY = cardboardMaxYShift + cardboardUserYShift;

	cardboardSettings->enabled = g_Config.bEnableCardboardVR;
	cardboardSettings->leftEyeXPosition = cardboardLeftEyeX;
	cardboardSettings->rightEyeXPosition = cardboardRightEyeX;
	cardboardSettings->screenYPosition = cardboardScreenY;
	cardboardSettings->screenWidth = cardboardScreenWidth;
	cardboardSettings->screenHeight = cardboardScreenHeight;
}

void PresentationCommon::CalculatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight, bool hasVideo, PostShaderUniforms *uniforms) {
	float u_delta = 1.0f / bufferWidth;
	float v_delta = 1.0f / bufferHeight;
	float u_pixel_delta = 1.0f / renderWidth;
	float v_pixel_delta = 1.0f / renderHeight;
	if (postShaderAtOutputResolution_) {
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, ROTATION_LOCKED_HORIZONTAL);
		u_pixel_delta = 1.0f / w;
		v_pixel_delta = 1.0f / h;
	}
	int flipCount = __DisplayGetFlipCount();
	int vCount = __DisplayGetVCount();
	float time[4] = { time_now(), (vCount % 60) * 1.0f / 60.0f, (float)vCount, (float)(flipCount % 60) };

	uniforms->texelDelta[0] = u_delta;
	uniforms->texelDelta[1] = v_delta;
	uniforms->pixelDelta[0] = u_pixel_delta;
	uniforms->pixelDelta[1] = v_pixel_delta;
	memcpy(uniforms->time, time, 4 * sizeof(float));
	uniforms->video = hasVideo;
}

void PresentationCommon::UpdateShaderInfo(const ShaderInfo *shaderInfo) {
	postShaderAtOutputResolution_ = shaderInfo->outputResolution;
}

void PresentationCommon::DeviceLost() {
	DestroyDeviceObjects();
}

void PresentationCommon::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	CreateDeviceObjects();
}

void PresentationCommon::CreateDeviceObjects() {
	using namespace Draw;

	// TODO: Maybe get rid of color0.
	InputLayoutDesc inputDesc = {
		{
			{ sizeof(Vertex), false },
		},
		{
			{ 0, SEM_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
			{ 0, SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 12 },
			{ 0, SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, 20 },
		},
	};

	vdata_ = draw_->CreateBuffer(sizeof(Vertex) * 4, BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);
	// TODO: Use 4 and a strip?  shorts?
	idata_ = draw_->CreateBuffer(sizeof(int) * 6, BufferUsageFlag::DYNAMIC | BufferUsageFlag::INDEXDATA);

	InputLayout *inputLayout = draw_->CreateInputLayout(inputDesc);
	DepthStencilState *depth = draw_->CreateDepthStencilState({ false, false, Comparison::LESS });
	BlendState *blendstateOff = draw_->CreateBlendState({ false, 0xF });
	RasterState *rasterNoCull = draw_->CreateRasterState({});

	samplerNearest_ = draw_->CreateSamplerState({ TextureFilter::NEAREST, TextureFilter::NEAREST, TextureFilter::NEAREST });
	samplerLinear_ = draw_->CreateSamplerState({ TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR });

	PipelineDesc pipelineDesc{
		Primitive::TRIANGLE_LIST,
		{ draw_->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw_->GetFshaderPreset(FS_TEXTURE_COLOR_2D) },
		inputLayout, depth, blendstateOff, rasterNoCull, &vsTexColBufDesc
	};
	texColor_ = draw_->CreateGraphicsPipeline(pipelineDesc);

	PipelineDesc pipelineDescRBSwizzle{
		Primitive::TRIANGLE_LIST,
		{ draw_->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw_->GetFshaderPreset(FS_TEXTURE_COLOR_2D_RB_SWIZZLE) },
		inputLayout, depth, blendstateOff, rasterNoCull, &vsTexColBufDesc
	};
	texColorRBSwizzle_ = draw_->CreateGraphicsPipeline(pipelineDescRBSwizzle);

	inputLayout->Release();
	depth->Release();
	blendstateOff->Release();
	rasterNoCull->Release();
}

template <typename T>
static void DoRelease(T *&obj) {
	if (obj)
		obj->Release();
	obj = nullptr;
}

void PresentationCommon::DestroyDeviceObjects() {
	DoRelease(texColor_);
	DoRelease(texColorRBSwizzle_);
	DoRelease(samplerNearest_);
	DoRelease(samplerLinear_);
	DoRelease(vdata_);
	DoRelease(idata_);
}

void PresentationCommon::CopyToOutput(OutputFlags flags, float x, float y, float x2, float y2, float u0, float v0, float u1, float v1) {
	Draw::Pipeline *pipeline = flags & OutputFlags::RB_SWIZZLE ? texColorRBSwizzle_ : texColor_;

	Draw::SamplerState *sampler;
	if (flags & OutputFlags::NEAREST) {
		sampler = samplerNearest_;
	} else {
		sampler = samplerLinear_;
	}
	draw_->BindSamplerStates(0, 1, &sampler);

	const Vertex verts[4] = {
		{ x, y, 0,    u0, v0,  0xFFFFFFFF }, // TL
		{ x, y2, 0,   u0, v1,  0xFFFFFFFF }, // BL
		{ x2, y2, 0,  u1, v1,  0xFFFFFFFF }, // BR
		{ x2, y, 0,   u1, v0,  0xFFFFFFFF }, // TR
	};
	draw_->UpdateBuffer(vdata_, (const uint8_t *)verts, 0, sizeof(verts), Draw::UPDATE_DISCARD);

	int indexes[] = { 0, 1, 2, 0, 2, 3 };
	draw_->UpdateBuffer(idata_, (const uint8_t *)indexes, 0, sizeof(indexes), Draw::UPDATE_DISCARD);

	Draw::VsTexColUB ub{};
	memcpy(ub.WorldViewProj, g_display_rot_matrix.m, sizeof(float) * 16);
	draw_->BindPipeline(pipeline);
	draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
	draw_->BindVertexBuffers(0, 1, &vdata_, nullptr);
	draw_->BindIndexBuffer(idata_, 0);
	draw_->DrawIndexed(6, 0);
	draw_->BindIndexBuffer(nullptr, 0);
}
