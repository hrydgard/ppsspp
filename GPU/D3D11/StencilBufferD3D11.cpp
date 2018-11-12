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

#include <d3d11.h>

#include "base/logging.h"

#include "ext/native/thin3d/thin3d.h"
#include "Core/Reporting.h"
#include "GPU/Common/StencilCommon.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/FragmentShaderGeneratorD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

struct StencilValueUB {
	uint32_t u_stencilValue[4];
};

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

// TODO : If SV_StencilRef is available (D3D11.3) then this can be done in a single pass.
bool FramebufferManagerD3D11::NotifyStencilUpload(u32 addr, int size, bool skipZero) {
	addr &= 0x3FFFFFFF;
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
		if (skipZero) {
			// Common when creating buffers, it's already 0.  We're done.
			return false;
		}

		// Clear stencil+alpha but not color. Only way is to draw a quad.
		context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0x8], nullptr, 0xFFFFFFFF);
		context_->RSSetState(stockD3D11.rasterStateNoCull);
		context_->OMSetDepthStencilState(stockD3D11.depthDisabledStencilWrite, 0);
		context_->IASetVertexBuffers(0, 1, &fsQuadBuffer_, &quadStride_, &quadOffset_);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context_->Draw(4, 0);

		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
		return true;
	}

	// TODO: Helper with logging?
	if (!stencilUploadPS_) {
		std::string errorMessage;
		stencilUploadPS_ = CreatePixelShaderD3D11(device_, stencil_ps, strlen(stencil_ps), featureLevel_);
	}
	if (!stencilUploadVS_) {
		std::string errorMessage;
		std::vector<uint8_t> byteCode;
		stencilUploadVS_ = CreateVertexShaderD3D11(device_, stencil_vs, strlen(stencil_vs), &byteCode, featureLevel_);
		ASSERT_SUCCESS(device_->CreateInputLayout(g_QuadVertexElements, 2, byteCode.data(), byteCode.size(), &stencilUploadInputLayout_));
	}
	if (!stencilValueBuffer_) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(StencilValueUB);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		ASSERT_SUCCESS(device_->CreateBuffer(&desc, nullptr, &stencilValueBuffer_));
	}

	shaderManagerD3D11_->DirtyLastShader();

	u16 w = dstBuffer->renderWidth;
	u16 h = dstBuffer->renderHeight;
	float u1 = 1.0f;
	float v1 = 1.0f;
	MakePixelTexture(src, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->bufferWidth, dstBuffer->bufferHeight, u1, v1);
	if (dstBuffer->fbo) {
		draw_->BindFramebufferAsRenderTarget(dstBuffer->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::CLEAR });
	} else {
		// something is wrong...
	}
	D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
	context_->RSSetViewports(1, &vp);
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);

	float coord[20] = {
		-1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
		 1.0f,  1.0f, 0.0f, u1,   0.0f,
		-1.0f, -1.0f, 0.0f, 0.0f, v1,
		 1.0f, -1.0f, 0.0f, u1,   v1,
	};

	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(quadBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	memcpy(map.pData, coord, sizeof(float) * 4 * 5);
	context_->Unmap(quadBuffer_, 0);

	shaderManagerD3D11_->DirtyLastShader();
	textureCacheD3D11_->ForgetLastTexture();

	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0], nullptr, 0xFFFFFFFF);
	context_->IASetInputLayout(stencilUploadInputLayout_);
	context_->PSSetShader(stencilUploadPS_, nullptr, 0);
	context_->VSSetShader(stencilUploadVS_, nullptr, 0);
	context_->PSSetShaderResources(0, 1, &drawPixelsTexView_);
	context_->RSSetState(stockD3D11.rasterStateNoCull);
	context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context_->IASetVertexBuffers(0, 1, &quadBuffer_, &quadStride_, &quadOffset_);
	context_->PSSetSamplers(0, 1, &stockD3D11.samplerPoint2DClamp);
	context_->OMSetDepthStencilState(stockD3D11.depthDisabledStencilWrite, 0xFF);
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
		if (!stencilMaskStates_[mask]) {
			D3D11_DEPTH_STENCIL_DESC desc{};
			desc.DepthEnable = false;
			desc.StencilEnable = true;
			desc.StencilReadMask = 0xFF;
			desc.StencilWriteMask = mask;
			desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
			desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
			desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
			desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			desc.BackFace = desc.FrontFace;
			device_->CreateDepthStencilState(&desc, &stencilMaskStates_[mask]);
		}
		context_->OMSetDepthStencilState(stencilMaskStates_[mask], 0xFF);

		D3D11_MAPPED_SUBRESOURCE map;
		context_->Map(stencilValueBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
		StencilValueUB ub{ (uint32_t)value };
		memcpy(map.pData, &ub, sizeof(ub));
		context_->Unmap(stencilValueBuffer_, 0);
		context_->PSSetConstantBuffers(0, 1, &stencilValueBuffer_);
		context_->Draw(4, 0);
	}
	RebindFramebuffer();
	return true;
}
