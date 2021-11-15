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

#include <algorithm>
#include <d3d11.h>
#include <D3Dcompiler.h>

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/System/Display.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/thin3d.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

static const char *vscode =
	"struct VS_IN {\n"
	"  float4 ObjPos   : POSITION;\n"
	"  float2 Uv    : TEXCOORD0;\n"
	"};\n"
	"struct VS_OUT {\n"
	"  float2 Uv    : TEXCOORD0;\n"
	"  float4 ProjPos  : SV_Position;\n"
	"};\n"
	"VS_OUT main(VS_IN In) {\n"
	"  VS_OUT Out;\n"
	"  Out.ProjPos = In.ObjPos;\n"
	"  Out.Uv = In.Uv;\n"
	"  return Out;\n"
	"}\n";

static const char *pscode =
	"SamplerState samp : register(s0);\n"
	"Texture2D<float4> tex : register(t0);\n"
	"struct PS_IN {\n"
	"  float2 Uv : TEXCOORD0;\n"
	"};\n"
	"float4 main( PS_IN In ) : SV_Target {\n"
	"  float4 c = tex.Sample(samp, In.Uv);\n"
	"  return c;\n"
	"}\n";

const D3D11_INPUT_ELEMENT_DESC FramebufferManagerD3D11::g_QuadVertexElements[2] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, },
};

FramebufferManagerD3D11::FramebufferManagerD3D11(Draw::DrawContext *draw)
	: FramebufferManagerCommon(draw) {
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	featureLevel_ = (D3D_FEATURE_LEVEL)draw->GetNativeObject(Draw::NativeObject::FEATURE_LEVEL);

	std::vector<uint8_t> bytecode;

	std::string errorMsg;
	quadVertexShader_ = CreateVertexShaderD3D11(device_, vscode, strlen(vscode), &bytecode, featureLevel_);
	quadPixelShader_ = CreatePixelShaderD3D11(device_, pscode, strlen(pscode), featureLevel_);
	ASSERT_SUCCESS(device_->CreateInputLayout(g_QuadVertexElements, ARRAY_SIZE(g_QuadVertexElements), bytecode.data(), bytecode.size(), &quadInputLayout_));

	// STRIP geometry
	static const float fsCoord[20] = {
		-1.0f,-1.0f, 0.0f, 0.0f, 0.0f,
		 1.0f,-1.0f, 0.0f, 1.0f, 0.0f,
		-1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
		 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
	};
	D3D11_BUFFER_DESC vb{};
	vb.ByteWidth = 20 * 4;
	vb.Usage = D3D11_USAGE_IMMUTABLE;
	vb.CPUAccessFlags = 0;
	vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA data{ fsCoord };
	ASSERT_SUCCESS(device_->CreateBuffer(&vb, &data, &fsQuadBuffer_));
	vb.Usage = D3D11_USAGE_DYNAMIC;
	vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	ASSERT_SUCCESS(device_->CreateBuffer(&vb, nullptr, &quadBuffer_));

	D3D11_TEXTURE2D_DESC desc{};
	desc.CPUAccessFlags = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Width = 1;
	desc.Height = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.MipLevels = 1;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	ASSERT_SUCCESS(device_->CreateTexture2D(&desc, nullptr, &nullTexture_));
	ASSERT_SUCCESS(device_->CreateShaderResourceView(nullTexture_, nullptr, &nullTextureView_));
	uint32_t nullData[1]{};
	context_->UpdateSubresource(nullTexture_, 0, nullptr, nullData, 1, 0);

	presentation_->SetLanguage(HLSL_D3D11);
	preferredPixelsFormat_ = Draw::DataFormat::B8G8R8A8_UNORM;
}

FramebufferManagerD3D11::~FramebufferManagerD3D11() {
	// Drawing cleanup
	if (quadVertexShader_)
		quadVertexShader_->Release();
	if (quadPixelShader_)
		quadPixelShader_->Release();
	quadInputLayout_->Release();
	quadBuffer_->Release();
	fsQuadBuffer_->Release();

	// Stencil cleanup
	for (int i = 0; i < 256; i++) {
		if (stencilMaskStates_[i])
			stencilMaskStates_[i]->Release();
	}
	if (stencilUploadPS_)
		stencilUploadPS_->Release();
	if (stencilUploadVS_)
		stencilUploadVS_->Release();
	if (stencilUploadInputLayout_)
		stencilUploadInputLayout_->Release();
	if (stencilValueBuffer_)
		stencilValueBuffer_->Release();

	if (nullTextureView_)
		nullTextureView_->Release();
	if (nullTexture_)
		nullTexture_->Release();
}

void FramebufferManagerD3D11::SetTextureCache(TextureCacheD3D11 *tc) {
	textureCache_ = tc;
}

void FramebufferManagerD3D11::SetShaderManager(ShaderManagerD3D11 *sm) {
	shaderManager_ = sm;
}

void FramebufferManagerD3D11::SetDrawEngine(DrawEngineD3D11 *td) {
	drawEngine_ = td;
}

void FramebufferManagerD3D11::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) {
	struct Coord {
		Lin::Vec3 pos; float u, v;
	};
	Coord coord[4] = {
		{{x, y, 0}, u0, v0},
		{{x + w, y, 0}, u1, v0},
		{{x + w, y + h,0}, u1, v1},
		{{x, y + h, 0}, u0, v1},
	};

	if (uvRotation != ROTATION_LOCKED_HORIZONTAL) {
		float temp[8];
		int rotation = 0;
		switch (uvRotation) {
		case ROTATION_LOCKED_HORIZONTAL180: rotation = 2; break;
		case ROTATION_LOCKED_VERTICAL: rotation = 1; break;
		case ROTATION_LOCKED_VERTICAL180: rotation = 3; break;
		}
		for (int i = 0; i < 4; i++) {
			temp[i * 2] = coord[((i + rotation) & 3)].u;
			temp[i * 2 + 1] = coord[((i + rotation) & 3)].v;
		}

		for (int i = 0; i < 4; i++) {
			coord[i].u = temp[i * 2];
			coord[i].v = temp[i * 2 + 1];
		}
	}

	float invDestW = 1.0f / (destW * 0.5f);
	float invDestH = 1.0f / (destH * 0.5f);
	for (int i = 0; i < 4; i++) {
		coord[i].pos.x = coord[i].pos.x * invDestW - 1.0f;
		coord[i].pos.y = -(coord[i].pos.y * invDestH - 1.0f);
	}

	if (g_display_rotation != DisplayRotation::ROTATE_0) {
		for (int i = 0; i < 4; i++) {
			// backwards notation, should fix that...
			coord[i].pos = coord[i].pos * g_display_rot_matrix;
		}
	}

	// The above code is for FAN geometry but we can only do STRIP. So rearrange it a little.
	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(quadBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	float *dest = (float *)map.pData;
	memcpy(dest, coord, sizeof(Coord));
	memcpy(dest + 5, coord + 1, sizeof(Coord));
	memcpy(dest + 10, coord + 3, sizeof(Coord));
	memcpy(dest + 15, coord + 2, sizeof(Coord));
	context_->Unmap(quadBuffer_, 0);

	context_->RSSetState(stockD3D11.rasterStateNoCull);
	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], nullptr, 0xFFFFFFFF);
	context_->OMSetDepthStencilState(stockD3D11.depthStencilDisabled, 0);
	context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context_->PSSetSamplers(0, 1, (flags & DRAWTEX_LINEAR) ? &stockD3D11.samplerLinear2DClamp : &stockD3D11.samplerPoint2DClamp);
	UINT stride = 20;
	UINT offset = 0;
	context_->IASetVertexBuffers(0, 1, &quadBuffer_, &stride, &offset);
	context_->Draw(4, 0);
	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);
}

void FramebufferManagerD3D11::Bind2DShader() {
	context_->IASetInputLayout(quadInputLayout_);
	context_->PSSetShader(quadPixelShader_, 0, 0);
	context_->VSSetShader(quadVertexShader_, 0, 0);
}

static void CopyPixelDepthOnly(u32 *dstp, const u32 *srcp, size_t c) {
	size_t x = 0;

#ifdef _M_SSE
	size_t sseSize = (c / 4) * 4;
	const __m128i srcMask = _mm_set1_epi32(0x00FFFFFF);
	const __m128i dstMask = _mm_set1_epi32(0xFF000000);
	__m128i *dst = (__m128i *)dstp;
	const __m128i *src = (const __m128i *)srcp;

	for (; x < sseSize; x += 4) {
		const __m128i bits24 = _mm_and_si128(_mm_load_si128(src), srcMask);
		const __m128i bits8 = _mm_and_si128(_mm_load_si128(dst), dstMask);
		_mm_store_si128(dst, _mm_or_si128(bits24, bits8));
		dst++;
		src++;
	}
#endif

	// Copy the remaining pixels that didn't fit in SSE.
	for (; x < c; ++x) {
		memcpy(dstp + x, srcp + x, 3);
	}
}

void FramebufferManagerD3D11::SimpleBlit(
	Draw::Framebuffer *dest, float destX1, float destY1, float destX2, float destY2,
	Draw::Framebuffer *src, float srcX1, float srcY1, float srcX2, float srcY2, bool linearFilter) {

	int destW, destH, srcW, srcH;
	draw_->GetFramebufferDimensions(src, &srcW, &srcH);
	draw_->GetFramebufferDimensions(dest, &destW, &destH);

	if (srcW == destW && srcH == destH && destX2 - destX1 == srcX2 - srcX1 && destY2 - destY1 == srcY2 - srcY1) {
		// Optimize to a copy
		draw_->CopyFramebufferImage(src, 0, (int)srcX1, (int)srcY1, 0, dest, 0, (int)destX1, (int)destY1, 0, (int)(srcX2 - srcX1), (int)(srcY2 - srcY1), 1, Draw::FB_COLOR_BIT, "SimpleBlit");
		return;
	}

	float dX = 1.0f / (float)destW;
	float dY = 1.0f / (float)destH;
	float sX = 1.0f / (float)srcW;
	float sY = 1.0f / (float)srcH;
	struct Vtx {
		float x, y, z, u, v;
	};
	Vtx vtx[4] = {
		{ -1.0f + 2.0f * dX * destX1, 1.0f - 2.0f * dY * destY1, 0.0f, sX * srcX1, sY * srcY1 },
		{ -1.0f + 2.0f * dX * destX2, 1.0f - 2.0f * dY * destY1, 0.0f, sX * srcX2, sY * srcY1 },
		{ -1.0f + 2.0f * dX * destX1, 1.0f - 2.0f * dY * destY2, 0.0f, sX * srcX1, sY * srcY2 },
		{ -1.0f + 2.0f * dX * destX2, 1.0f - 2.0f * dY * destY2, 0.0f, sX * srcX2, sY * srcY2 },
	};

	D3D11_MAPPED_SUBRESOURCE map;
	ASSERT_SUCCESS(context_->Map(quadBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map));
	memcpy(map.pData, vtx, 4 * sizeof(Vtx));
	context_->Unmap(quadBuffer_, 0);

	// Unbind the texture first to avoid the D3D11 hazard check (can't set render target to things bound as textures and vice versa, not even temporarily).
	draw_->BindTexture(0, nullptr);
	draw_->BindFramebufferAsRenderTarget(dest, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "SimpleBlit");
	draw_->BindFramebufferAsTexture(src, 0, Draw::FB_COLOR_BIT, 0);

	Bind2DShader();
	D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)destW, (float)destH, 0.0f, 1.0f };
	context_->RSSetViewports(1, &vp);
	context_->RSSetState(stockD3D11.rasterStateNoCull);
	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], nullptr, 0xFFFFFFFF);
	context_->OMSetDepthStencilState(stockD3D11.depthStencilDisabled, 0);
	context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context_->PSSetSamplers(0, 1, linearFilter ? &stockD3D11.samplerLinear2DClamp : &stockD3D11.samplerPoint2DClamp);
	UINT stride = sizeof(Vtx);
	UINT offset = 0;
	context_->IASetVertexBuffers(0, 1, &quadBuffer_, &stride, &offset);
	context_->Draw(4, 0);

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_VERTEXSHADER_STATE);
}

void FramebufferManagerD3D11::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, const char *tag) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		if (useBufferedRendering_) {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "BlitFramebuffer_Fail");
		}
		return;
	}

	float srcXFactor = src->renderScaleFactor;
	float srcYFactor = src->renderScaleFactor;
	const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
	if (srcBpp != bpp && bpp != 0) {
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY1 = srcY * srcYFactor;
	int srcY2 = (srcY + h) * srcYFactor;

	float dstXFactor = dst->renderScaleFactor;
	float dstYFactor = dst->renderScaleFactor;
	const int dstBpp = dst->format == GE_FORMAT_8888 ? 4 : 2;
	if (dstBpp != bpp && bpp != 0) {
		dstXFactor = (dstXFactor * bpp) / dstBpp;
	}
	int dstX1 = dstX * dstXFactor;
	int dstX2 = (dstX + w) * dstXFactor;
	int dstY1 = dstY * dstYFactor;
	int dstY2 = (dstY + h) * dstYFactor;

	// Direct3D doesn't support rect -> self.
	Draw::Framebuffer *srcFBO = src->fbo;
	if (src == dst) {
		Draw::Framebuffer *tempFBO = GetTempFBO(TempFBO::BLIT, src->renderWidth, src->renderHeight);
		SimpleBlit(tempFBO, dstX1, dstY1, dstX2, dstY2, src->fbo, srcX1, srcY1, srcX2, srcY2, false);
		srcFBO = tempFBO;
	}
	SimpleBlit(
		dst->fbo, dstX1, dstY1, dstX2, dstY2,
		srcFBO, srcX1, srcY1, srcX2, srcY2,
		false);

	draw_->BindTexture(0, nullptr);
}

// Nobody calls this yet.
void FramebufferManagerD3D11::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	const u32 z_address = vfb->z_address;
	// TODO
}

void FramebufferManagerD3D11::EndFrame() {
}
