// Copyright (c) 2014- PPSSPP Project.

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

#include <wiiu/gx2.h>

#include "Common/GPU/thin3d.h"
#include "Core/Reporting.h"
#include "GPU/Common/StencilCommon.h"
#include "GPU/GX2/FramebufferManagerGX2.h"
#include "GPU/GX2/FragmentShaderGeneratorGX2.h"
#include "GPU/GX2/ShaderManagerGX2.h"
#include "GPU/GX2/TextureCacheGX2.h"
#include "GPU/GX2/GX2Util.h"
#include "GPU/GX2/GX2Shaders.h"

static const char *stencil_ps = R"(
SamplerState samp : register(s0);
Texture2D<float4> tex : register(t0);
cbuffer base : register(b0) {
  int4 u_stencilValue;
};
struct PS_IN {
  float2 v_texcoord0 : TEXCOORD0;
};
float4 main(PS_IN In) : SV_Target {
  float4 index = tex.Sample(samp, In.v_texcoord0);
	int indexBits = int(index.a * 255.99);
	if ((indexBits & u_stencilValue.x) == 0)
		discard;
  return index.aaaa;
}
)";

// static const char *stencil_ps_fast;

static const char *stencil_vs = R"(
struct VS_IN {
  float4 a_position : POSITION;
  float2 a_texcoord0 : TEXCOORD0;
};
struct VS_OUT {
  float2 v_texcoord0 : TEXCOORD0;
  float4 position : SV_Position;
};
VS_OUT main(VS_IN In) {
  VS_OUT Out;
  Out.position = In.a_position;
  Out.v_texcoord0 = In.a_texcoord0;
  return Out;
}
)";

// TODO : If SV_StencilRef is available (?) then this can be done in a single pass.
bool FramebufferManagerGX2::NotifyStencilUpload(u32 addr, int size, StencilUpload flags) {
	if (!MayIntersectFramebuffer(addr)) {
		return false;
	}

	VirtualFramebuffer *dstBuffer = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		if (vfb->fb_address == addr) {
			dstBuffer = vfb;
		}
	}
	if (!dstBuffer) {
		return false;
	}

	int values = 0;
	u8 usedBits = 0;

	const u8 *src = Memory::GetPointer(addr);
	if (!src) {
		return false;
	}

	switch (dstBuffer->format) {
	case GE_FORMAT_565:
		// Well, this doesn't make much sense.
		return false;
	case GE_FORMAT_5551:
		usedBits = StencilBits5551(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 2;
		break;
	case GE_FORMAT_4444:
		usedBits = StencilBits4444(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 16;
		break;
	case GE_FORMAT_8888:
		usedBits = StencilBits8888(src, dstBuffer->fb_stride * dstBuffer->bufferHeight);
		values = 256;
		break;
	case GE_FORMAT_INVALID:
		// Impossible.
		break;
	}

	if (usedBits == 0) {
		if (flags == StencilUpload::STENCIL_IS_ZERO) {
			// Common when creating buffers, it's already 0.  We're done.
			return false;
		}

		// Clear stencil+alpha but not color. Only way is to draw a quad.
		GX2SetColorControlReg(&StockGX2::blendDisabledColorWrite);
		GX2SetTargetChannelMasksReg(&StockGX2::TargetChannelMasks[0x8]);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_DISABLE);
		GX2SetDepthStencilControlReg(&StockGX2::depthDisabledStencilWrite);
		GX2SetAttribBuffer(0, 4 * quadStride_, quadStride_, fsQuadBuffer_);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);

		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
		return true;
	}

	if (!stencilValueBuffer_) {
		static_assert(!(sizeof(StencilValueUB) & 0x3F), "sizeof(StencilValueUB) must to be aligned to 64bytes!");
		stencilValueBuffer_ = (StencilValueUB *)MEM2_alloc(sizeof(StencilValueUB), GX2_UNIFORM_BLOCK_ALIGNMENT);
		memset(stencilValueBuffer_, 0, sizeof(StencilValueUB));
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_UNIFORM_BLOCK, stencilValueBuffer_, sizeof(StencilValueUB));
	}

	shaderManagerGX2_->DirtyLastShader();

	u16 w = dstBuffer->renderWidth;
	u16 h = dstBuffer->renderHeight;
	float u1 = 1.0f;
	float v1 = 1.0f;
	Draw::Texture *tex = MakePixelTexture(src, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->bufferWidth, dstBuffer->bufferHeight, u1, v1);
	if (!tex)
		return false;
	if (dstBuffer->fbo) {
		// Typically, STENCIL_IS_ZERO means it's already bound.
		Draw::RPAction stencilAction = flags == StencilUpload::STENCIL_IS_ZERO ? Draw::RPAction::KEEP : Draw::RPAction::CLEAR;
		draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, stencilAction }, "NotifyStencilUpload");
	} else {
		// something is wrong...
	}
	GX2SetViewport(0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f);
	GX2SetScissor(0, 0, w, h);
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);

	float coord[20] = {
		-1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, u1, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, v1, 1.0f, -1.0f, 0.0f, u1, v1,
	};

	memcpy(quadBuffer_, coord, sizeof(float) * 4 * 5);
	GX2Invalidate(GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER, quadBuffer_, sizeof(float) * 4 * 5);

	shaderManagerGX2_->DirtyLastShader();
	textureCacheGX2_->ForgetLastTexture();

	GX2SetColorControlReg(&StockGX2::blendColorDisabled);
	GX2SetTargetChannelMasksReg(&StockGX2::TargetChannelMasks[0x0]);
	GX2SetFetchShader(&quadFetchShader_);
	GX2SetPixelShader(&stencilUploadPSshaderGX2);
	GX2SetVertexShader(&stencilUploadVSshaderGX2);
	draw_->BindTextures(0, 1, &tex);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_DISABLE);
	GX2SetAttribBuffer(0, 4 * quadStride_, quadStride_, fsQuadBuffer_);
	GX2SetPixelSampler(&StockGX2::samplerPoint2DClamp, 0);
	GX2SetDepthStencilControlReg(&StockGX2::depthDisabledStencilWrite);
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE);

	for (int i = 1; i < values; i += i) {
		if (!(usedBits & i)) {
			// It's already zero, let's skip it.
			continue;
		}
		uint8_t mask = 0;
		uint8_t value = 0;
		if (dstBuffer->format == GE_FORMAT_4444) {
			mask = i | (i << 4);
			value = i * 16;
		} else if (dstBuffer->format == GE_FORMAT_5551) {
			mask = 0xFF;
			value = i * 128;
		} else {
			mask = i;
			value = i;
		}

		GX2SetDepthStencilControlReg(&StockGX2::depthDisabledStencilWrite);
		GX2SetStencilMaskReg(&stencilMaskStates_[mask]);

		stencilValueBuffer_->u_stencilValue[0] = value;
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_UNIFORM_BLOCK, stencilValueBuffer_, sizeof(StencilValueUB));
		GX2SetPixelUniformBlock(1, sizeof(StencilValueUB), stencilValueBuffer_);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
	}
	tex->Release();
	RebindFramebuffer("NotifyStencilUpload");
	return true;
}
