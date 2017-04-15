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

#include <d3d11.h>
#include <D3Dcompiler.h>

#include "base/display.h"
#include "math/lin/matrix4x4.h"
#include "ext/native/thin3d/thin3d.h"
#include "base/basictypes.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "i18n/i18n.h"

#include "Common/ColorConv.h"
#include "Common/MathUtil.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"

#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/ShaderTranslation.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/PostShader.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"

#include "UI/OnScreenDisplay.h"

#include "ext/native/thin3d/thin3d.h"

#include <algorithm>

#ifdef _M_SSE
#include <emmintrin.h>
#endif

static const char *vscode =
	"struct VS_IN {\n"
	"  float4 ObjPos   : POSITION;\n"
	"  float2 Uv    : TEXCOORD0;\n"
	"};"
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

// The current simple shader translator outputs everything as semantic texcoords, so let's just play along
// for simplicity.
const D3D11_INPUT_ELEMENT_DESC FramebufferManagerD3D11::g_PostVertexElements[2] = {
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, },
	{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 12, },
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
	vb.ByteWidth = ROUND_UP(sizeof(PostShaderUniforms), 16);
	vb.Usage = D3D11_USAGE_DYNAMIC;
	vb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ASSERT_SUCCESS(device_->CreateBuffer(&vb, nullptr, &postConstants_));

	ShaderTranslationInit();

	CompilePostShader();

	D3D11_TEXTURE2D_DESC packDesc{};
	packDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	packDesc.BindFlags = 0;
	packDesc.Width = 512;  // 512x512 is the maximum size of a framebuffer on the PSP.
	packDesc.Height = 512;
	packDesc.ArraySize = 1;
	packDesc.MipLevels = 1;
	packDesc.Usage = D3D11_USAGE_STAGING;
	packDesc.SampleDesc.Count = 1;
	packDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ASSERT_SUCCESS(device_->CreateTexture2D(&packDesc, nullptr, &packTexture_));
}

FramebufferManagerD3D11::~FramebufferManagerD3D11() {
	packTexture_->Release();
	ShaderTranslationShutdown();

	// Drawing cleanup
	if (quadVertexShader_)
		quadVertexShader_->Release();
	if (quadPixelShader_)
		quadPixelShader_->Release();
	quadInputLayout_->Release();
	quadBuffer_->Release();
	fsQuadBuffer_->Release();
	postConstants_->Release();

	if (drawPixelsTex_)
		drawPixelsTex_->Release();
	if (drawPixelsTexView_)
		drawPixelsTexView_->Release();

	if (postVertexShader_) {
		postVertexShader_->Release();
	}
	if (postPixelShader_) {
		postPixelShader_->Release();
	}
	if (postInputLayout_) {
		postInputLayout_->Release();
	}

	// FBO cleanup
	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
		it->second.fbo->Release();
	}
	delete[] convBuf;

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
}

void FramebufferManagerD3D11::SetTextureCache(TextureCacheD3D11 *tc) {
	textureCacheD3D11_ = tc;
	textureCache_ = tc;
}

void FramebufferManagerD3D11::SetShaderManager(ShaderManagerD3D11 *sm) {
	shaderManagerD3D11_ = sm;
	shaderManager_ = sm;
}

void FramebufferManagerD3D11::ClearBuffer(bool keepState) {
	draw_->Clear(Draw::FBChannel::FB_COLOR_BIT | Draw::FBChannel::FB_DEPTH_BIT | Draw::FBChannel::FB_STENCIL_BIT, 0, ToScaledDepth(0), 0);
}

void FramebufferManagerD3D11::DisableState() {
	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], nullptr, 0xFFFFFFFF);
	context_->RSSetState(stockD3D11.rasterStateNoCull);
	context_->OMSetDepthStencilState(stockD3D11.depthStencilDisabled, 0xFF);
}

void FramebufferManagerD3D11::CompilePostShader() {
	SetNumExtraFBOs(0);

	std::string vsSource;
	std::string psSource;

	if (postVertexShader_) {
		postVertexShader_->Release();
		postVertexShader_ = nullptr;
	}
	if (postPixelShader_) {
		postPixelShader_->Release();
		postPixelShader_ = nullptr;
	}
	if (postInputLayout_) {
		postInputLayout_->Release();
		postInputLayout_ = nullptr;
	}

	const ShaderInfo *shaderInfo = nullptr;
	if (g_Config.sPostShaderName == "Off") {
		usePostShader_ = false;
		return;
	}

	usePostShader_ = false;

	ReloadAllPostShaderInfo();
	shaderInfo = GetPostShaderInfo(g_Config.sPostShaderName);
	if (shaderInfo) {
		postShaderAtOutputResolution_ = shaderInfo->outputResolution;
		size_t sz;
		char *vs = (char *)VFSReadFile(shaderInfo->vertexShaderFile.c_str(), &sz);
		if (!vs)
			return;
		char *ps = (char *)VFSReadFile(shaderInfo->fragmentShaderFile.c_str(), &sz);
		if (!ps) {
			free(vs);
			return;
		}
		std::string vsSourceGLSL = vs;
		std::string psSourceGLSL = ps;
		free(vs);
		free(ps);
		TranslatedShaderMetadata metaVS, metaFS;
		std::string errorVS, errorFS;
		if (!TranslateShader(&vsSource, HLSL_D3D11, &metaVS, vsSourceGLSL, GLSL_140, Draw::ShaderStage::VERTEX, &errorVS))
			return;
		if (!TranslateShader(&psSource, HLSL_D3D11, &metaFS, psSourceGLSL, GLSL_140, Draw::ShaderStage::FRAGMENT, &errorFS))
			return;
	} else {
		return;
	}
	I18NCategory *gr = GetI18NCategory("Graphics");

	UINT flags = D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
	std::vector<uint8_t> byteCode;
	postVertexShader_ = CreateVertexShaderD3D11(device_, vsSource.data(), vsSource.size(), &byteCode, featureLevel_, flags);
	if (!postVertexShader_) {
		return;
	}
	postPixelShader_ = CreatePixelShaderD3D11(device_, psSource.data(), psSource.size(), featureLevel_, flags);
	if (!postPixelShader_) {
		postVertexShader_->Release();
		return;
	}
	ASSERT_SUCCESS(device_->CreateInputLayout(g_PostVertexElements, 2, byteCode.data(), byteCode.size(), &postInputLayout_));
	usePostShader_ = true;
}

void FramebufferManagerD3D11::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) {
	u8 *convBuf = NULL;

	// TODO: Check / use D3DCAPS2_DYNAMICTEXTURES?
	if (drawPixelsTex_ && (drawPixelsTexW_ != width || drawPixelsTexH_ != height)) {
		drawPixelsTex_->Release();
		drawPixelsTex_ = nullptr;
		drawPixelsTexView_->Release();
		drawPixelsTexView_ = nullptr;
	}

	if (!drawPixelsTex_) {
		int usage = 0;
		D3D11_TEXTURE2D_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ASSERT_SUCCESS(device_->CreateTexture2D(&desc, nullptr, &drawPixelsTex_));
		ASSERT_SUCCESS(device_->CreateShaderResourceView(drawPixelsTex_, nullptr, &drawPixelsTexView_));
		drawPixelsTexW_ = width;
		drawPixelsTexH_ = height;
	}

	if (!drawPixelsTex_) {
		return;
	}

	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(drawPixelsTex_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

	convBuf = (u8*)map.pData;

	if (srcPixelFormat != GE_FORMAT_8888 || srcStride != 512) {
		for (int y = 0; y < height; y++) {
			switch (srcPixelFormat) {
			case GE_FORMAT_565:
			{
				const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
				u32 *dst = (u32 *)(convBuf + map.RowPitch * y);
				ConvertRGB565ToBGRA8888(dst, src, width);
			}
			break;
			// faster
			case GE_FORMAT_5551:
			{
				const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
				u32 *dst = (u32 *)(convBuf + map.RowPitch * y);
				ConvertRGBA5551ToBGRA8888(dst, src, width);
			}
			break;
			case GE_FORMAT_4444:
			{
				const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
				u8 *dst = (u8 *)(convBuf + map.RowPitch * y);
				ConvertRGBA4444ToBGRA8888((u32 *)dst, src, width);
			}
			break;

			case GE_FORMAT_8888:
			{
				const u32_le *src = (const u32_le *)srcPixels + srcStride * y;
				u32 *dst = (u32 *)(convBuf + map.RowPitch * y);
				ConvertRGBA8888ToBGRA8888(dst, src, width);
			}
			break;
			}
		}
	} else {
		for (int y = 0; y < height; y++) {
			const u32_le *src = (const u32_le *)srcPixels + srcStride * y;
			u32 *dst = (u32 *)(convBuf + map.RowPitch * y);
			ConvertRGBA8888ToBGRA8888(dst, src, width);
		}
	}

	context_->Unmap(drawPixelsTex_, 0);
	context_->PSSetShaderResources(0, 1, &drawPixelsTexView_);
	// D3DXSaveTextureToFile("game:\\cc.png", D3DXIFF_PNG, drawPixelsTex_, NULL);
}

void FramebufferManagerD3D11::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, bool linearFilter) {
	struct Coord {
		Vec3 pos; float u, v;
	};
	Coord coord[4] = {
		{{x, y, 0}, u0, v0},
		{{x + w, y, 0}, u1, v0},
		{{x + w, y + h,0}, u1, v1},
		{{x, y + h, 0}, u0, v1},
	};

	static const short indices[4] = { 0, 1, 3, 2 };

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
	context_->PSSetSamplers(0, 1, linearFilter ? &stockD3D11.samplerLinear2DClamp : &stockD3D11.samplerPoint2DClamp);
	UINT stride = 20;
	UINT offset = 0;
	context_->IASetVertexBuffers(0, 1, &quadBuffer_, &stride, &offset);
	context_->Draw(4, 0);
}

void FramebufferManagerD3D11::Bind2DShader() {
	context_->IASetInputLayout(quadInputLayout_);
	context_->PSSetShader(quadPixelShader_, 0, 0);
	context_->VSSetShader(quadVertexShader_, 0, 0);
}

void FramebufferManagerD3D11::BindPostShader(const PostShaderUniforms &uniforms) {
	if (!postPixelShader_) {
		if (usePostShader_) {
			CompilePostShader();
		}
		if (!usePostShader_) {
			context_->IASetInputLayout(quadInputLayout_);
			context_->PSSetShader(quadPixelShader_, 0, 0);
			context_->VSSetShader(quadVertexShader_, 0, 0);
			return;
		}
	}
	context_->IASetInputLayout(postInputLayout_);
	context_->PSSetShader(postPixelShader_, 0, 0);
	context_->VSSetShader(postVertexShader_, 0, 0);

	D3D11_MAPPED_SUBRESOURCE map;
	ASSERT_SUCCESS(context_->Map(postConstants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map));
	memcpy(map.pData, &uniforms, sizeof(uniforms));
	context_->Unmap(postConstants_, 0);
	context_->VSSetConstantBuffers(0, 1, &postConstants_);  // Probably not necessary
	context_->PSSetConstantBuffers(0, 1, &postConstants_);
}

void FramebufferManagerD3D11::RebindFramebuffer() {
	if (currentRenderVfb_ && currentRenderVfb_->fbo) {
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo);
	} else {
		draw_->BindBackbufferAsRenderTarget();
	}
}

void FramebufferManagerD3D11::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
	if (!useBufferedRendering_ || !vfb->fbo) {
		return;
	}

	draw_->BindFramebufferAsRenderTarget(vfb->fbo);

	// Technically, we should at this point re-interpret the bytes of the old format to the new.
	// That might get tricky, and could cause unnecessary slowness in some games.
	// For now, we just clear alpha/stencil from 565, which fixes shadow issues in Kingdom Hearts.
	// (it uses 565 to write zeros to the buffer, than 4444 to actually render the shadow.)
	//
	// The best way to do this may ultimately be to create a new FBO (combine with any resize?)
	// and blit with a shader to that, then replace the FBO on vfb.  Stencil would still be complex
	// to exactly reproduce in 4444 and 8888 formats.

	if (old == GE_FORMAT_565) {
		context_->OMSetDepthStencilState(stockD3D11.depthDisabledStencilWrite, 0xFF);
		context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0], nullptr, 0xFFFFFFFF);
		context_->RSSetState(stockD3D11.rasterStateNoCull);
		context_->IASetInputLayout(quadInputLayout_);
		context_->PSSetShader(quadPixelShader_, nullptr, 0);
		context_->VSSetShader(quadVertexShader_, nullptr, 0);
		context_->IASetVertexBuffers(0, 1, &fsQuadBuffer_, &quadStride_, &quadOffset_);
		shaderManagerD3D11_->DirtyLastShader();
		D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)vfb->renderWidth, (float)vfb->renderHeight, 0.0f, 1.0f };
		context_->RSSetViewports(1, &vp);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context_->Draw(4, 0);
	}

	RebindFramebuffer();
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

void FramebufferManagerD3D11::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
	if (g_Config.bDisableSlowFramebufEffects) {
		return;
	}
	bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
	bool matchingSize = src->width == dst->width && src->height == dst->height;
	bool matchingRenderSize = src->renderWidth == dst->renderWidth && src->renderHeight == dst->renderHeight;
	if (matchingDepthBuffer && matchingSize && matchingRenderSize) {
		draw_->CopyFramebufferImage(src->fbo, 0, 0, 0, 0, dst->fbo, 0, 0, 0, 0, src->renderWidth, src->renderHeight, 1, Draw::FB_DEPTH_BIT);
		RebindFramebuffer();
	}
}

void FramebufferManagerD3D11::BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags) {
	if (!framebuffer->fbo || !useBufferedRendering_) {
		ID3D11ShaderResourceView *view = nullptr;
		context_->PSSetShaderResources(stage, 1, &view);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	bool skipCopy = (flags & BINDFBCOLOR_MAY_COPY) == 0;
	if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
		skipCopy = true;
	}
	// Currently rendering to this framebuffer. Need to make a copy.
	if (!skipCopy && framebuffer == currentRenderVfb_) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		Draw::Framebuffer *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (Draw::FBColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;

			CopyFramebufferForColorTexture(&copyInfo, framebuffer, flags);
			RebindFramebuffer();
			draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::FB_COLOR_BIT, 0);
		} else {
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		}
	} else if (framebuffer != currentRenderVfb_) {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
	} else {
		ERROR_LOG_REPORT_ONCE(d3d11SelfTexture, G3D, "Attempting to texture to target");
		// Badness on D3D11 to bind the currently rendered-to framebuffer as a texture.
		ID3D11ShaderResourceView *view = nullptr;
		context_->PSSetShaderResources(stage, 1, &view);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}
}

void FramebufferManagerD3D11::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {
	if (vfb) {
		// We'll pseudo-blit framebuffers here to get a resized version of vfb.
		OptimizeDownloadRange(vfb, x, y, w, h);
		if (vfb->renderWidth == vfb->width && vfb->renderHeight == vfb->height) {
			// No need to blit
			PackFramebufferD3D11_(vfb, x, y, w, h);
		}
		else {
			VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
			BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);
			PackFramebufferD3D11_(nvfb, x, y, w, h);
		}

		textureCacheD3D11_->ForgetLastTexture();
		RebindFramebuffer();
	}
}

void FramebufferManagerD3D11::DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) {
	VirtualFramebuffer *vfb = GetVFBAt(fb_address);
	if (vfb && vfb->fb_stride != 0) {
		const u32 bpp = vfb->drawnFormat == GE_FORMAT_8888 ? 4 : 2;
		int x = 0;
		int y = 0;
		int pixels = loadBytes / bpp;
		// The height will be 1 for each stride or part thereof.
		int w = std::min(pixels % vfb->fb_stride, (int)vfb->width);
		int h = std::min((pixels + vfb->fb_stride - 1) / vfb->fb_stride, (int)vfb->height);

		// We might still have a pending draw to the fb in question, flush if so.
		FlushBeforeCopy();

		// No need to download if we already have it.
		if (!vfb->memoryUpdated && vfb->clutUpdatedBytes < loadBytes) {
			// We intentionally don't call OptimizeDownloadRange() here - we don't want to over download.
			// CLUT framebuffers are often incorrectly estimated in size.
			if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
				vfb->memoryUpdated = true;
			}
			vfb->clutUpdatedBytes = loadBytes;

			// We'll pseudo-blit framebuffers here to get a resized version of vfb.
			VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
			BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);

			PackFramebufferD3D11_(nvfb, x, y, w, h);

			textureCacheD3D11_->ForgetLastTexture();
			RebindFramebuffer();
		}
	}
}

bool FramebufferManagerD3D11::CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	nvfb->colorDepth = Draw::FBO_8888;

	nvfb->fbo = draw_->CreateFramebuffer({ nvfb->width, nvfb->height, 1, 1, true, (Draw::FBColorDepth)nvfb->colorDepth });
	if (!(nvfb->fbo)) {
		ERROR_LOG(FRAMEBUF, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
		return false;
	}

	draw_->BindFramebufferAsRenderTarget(nvfb->fbo);
	ClearBuffer();
	return true;
}

void FramebufferManagerD3D11::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// Nothing to do here.
}

void FramebufferManagerD3D11::SimpleBlit(
	Draw::Framebuffer *dest, float destX1, float destY1, float destX2, float destY2,
	Draw::Framebuffer *src, float srcX1, float srcY1, float srcX2, float srcY2, bool linearFilter) {

	int destW, destH, srcW, srcH;
	draw_->GetFramebufferDimensions(src, &srcW, &srcH);
	draw_->GetFramebufferDimensions(dest, &destW, &destH);

	if (srcW == destW && srcH == destH && destX2 - destX1 == srcX2 - srcX1 && destY2 - destY1 == srcY2 - srcY1) {
		// Optimize to a copy
		draw_->CopyFramebufferImage(src, 0, (int)srcX1, (int)srcY1, 0, dest, 0, (int)destX1, (int)destY1, 0, (int)(srcX2 - srcX1), (int)(srcY2 - srcY1), 1, Draw::FB_COLOR_BIT);
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
	draw_->BindFramebufferAsRenderTarget(dest);
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
}

void FramebufferManagerD3D11::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		draw_->BindBackbufferAsRenderTarget();
		return;
	}

	float srcXFactor = (float)src->renderWidth / (float)src->bufferWidth;
	float srcYFactor = (float)src->renderHeight / (float)src->bufferHeight;
	const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
	if (srcBpp != bpp && bpp != 0) {
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY1 = srcY * srcYFactor;
	int srcY2 = (srcY + h) * srcYFactor;

	float dstXFactor = (float)dst->renderWidth / (float)dst->bufferWidth;
	float dstYFactor = (float)dst->renderHeight / (float)dst->bufferHeight;
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
		Draw::Framebuffer *tempFBO = GetTempFBO(src->renderWidth, src->renderHeight, (Draw::FBColorDepth)src->colorDepth);
		SimpleBlit(tempFBO, dstX1, dstY1, dstX2, dstY2, src->fbo, srcX1, srcY1, srcX2, srcY2, false);
		srcFBO = tempFBO;
	}
	SimpleBlit(
		dst->fbo, dstX1, dstY1, dstX2, dstY2,
		srcFBO, srcX1, srcY1, srcX2, srcY2,
		false);
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromRGBA8888(u8 *dst, u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const u32 *src32 = (const u32 *)src;

	if (format == GE_FORMAT_8888) {
		u32 *dst32 = (u32 *)dst;
		if (src == dst) {
			return;
		} else {
			for (u32 y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += srcStride;
				dst32 += dstStride;
			}
		}
	} else {
		// But here it shouldn't matter if they do intersect
		u16 *dst16 = (u16 *)dst;
		switch (format) {
		case GE_FORMAT_565: // BGR 565
			for (u32 y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGB565(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_5551: // ABGR 1555
			for (u32 y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGBA5551(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_4444: // ABGR 4444
			for (u32 y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGBA4444(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_8888:
		case GE_FORMAT_INVALID:
			// Not possible.
			break;
		}
	}
}

// This function takes an already correctly-sized framebuffer and packs it into RAM.
// Does not need to account for scaling.
// Color conversion is currently done on CPU but should be done on GPU.
void FramebufferManagerD3D11::PackFramebufferD3D11_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferD3D11_: vfb->fbo == 0");
		draw_->BindBackbufferAsRenderTarget();
		return;
	}

	const u32 fb_address = (0x04000000) | vfb->fb_address;
	const int dstBpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;

	// We always need to convert from the framebuffer native format.
	// Right now that's always 8888.
	DEBUG_LOG(G3D, "Reading framebuffer to mem, fb_address = %08x", fb_address);
	ID3D11Texture2D *colorTex = (ID3D11Texture2D *)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_COLOR_BIT, 0);

	// TODO: Only copy the necessary rectangle.
	D3D11_BOX srcBox{ (UINT)x, (UINT)y, 0, (UINT)(x+w), (UINT)(y+h), 1 };
	context_->CopySubresourceRegion(packTexture_, 0, x, y, 0, colorTex, 0, &srcBox);

	// Ideally, we'd round robin between two packTexture_, and simply use the other one. Though if the game
	// does a once-off copy, that won't work at all.

	// BIG GPU STALL
	D3D11_MAPPED_SUBRESOURCE map;
	HRESULT result = context_->Map(packTexture_, 0, D3D11_MAP_READ, 0, &map);
	if (FAILED(result)) {
		return;
	}

	// TODO: Handle the other formats?  We don't currently create them, I think.
	const int srcByteOffset = y * map.RowPitch + x * 4;
	const int dstByteOffset = (y * vfb->fb_stride + x) * dstBpp;
	// Pixel size always 4 here because we always request BGRA8888.
	ConvertFromRGBA8888(Memory::GetPointer(fb_address + dstByteOffset), (u8 *)map.pData + srcByteOffset, vfb->fb_stride, map.RowPitch/4, w, h, vfb->format);
	context_->Unmap(packTexture_, 0);
}

// Nobody calls this yet.
void FramebufferManagerD3D11::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	const u32 z_address = (0x04000000) | vfb->z_address;
	// TODO
}

void FramebufferManagerD3D11::EndFrame() {
	if (resized_) {
		// Check if postprocessing shader is doing upscaling as it requires native resolution
		const ShaderInfo *shaderInfo = 0;
		if (g_Config.sPostShaderName != "Off") {
			shaderInfo = GetPostShaderInfo(g_Config.sPostShaderName);
		}

		postShaderIsUpscalingFilter_ = shaderInfo ? shaderInfo->isUpscalingFilter : false;

		// Actually, auto mode should be more granular...
		// Round up to a zoom factor for the render size.
		int zoom = g_Config.iInternalResolution;
		if (zoom == 0) {
			// auto mode, use the longest dimension
			if (!g_Config.IsPortrait()) {
				zoom = (PSP_CoreParameter().pixelWidth + 479) / 480;
			} else {
				zoom = (PSP_CoreParameter().pixelHeight + 479) / 480;
			}
		}
		if (zoom <= 1 || postShaderIsUpscalingFilter_)
			zoom = 1;

		if (g_Config.IsPortrait()) {
			PSP_CoreParameter().renderWidth = 272 * zoom;
			PSP_CoreParameter().renderHeight = 480 * zoom;
		} else {
			PSP_CoreParameter().renderWidth = 480 * zoom;
			PSP_CoreParameter().renderHeight = 272 * zoom;
		}

		if (UpdateSize() || g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
			DestroyAllFBOs();
		}

		// Seems related - if you're ok with numbers all the time, show some more :)
		if (g_Config.iShowFPSCounter != 0) {
			ShowScreenResolution();
		}
		resized_ = false;

		// Might have a new post shader - let's compile it.
		CompilePostShader();
	}
}

void FramebufferManagerD3D11::DeviceLost() {
	DestroyAllFBOs();
	resized_ = false;
}

std::vector<FramebufferInfo> FramebufferManagerD3D11::GetFramebufferList() {
	std::vector<FramebufferInfo> list;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];

		FramebufferInfo info;
		info.fb_address = vfb->fb_address;
		info.z_address = vfb->z_address;
		info.format = vfb->format;
		info.width = vfb->width;
		info.height = vfb->height;
		info.fbo = vfb->fbo;
		list.push_back(info);
	}

	return list;
}

void FramebufferManagerD3D11::DestroyAllFBOs() {
	draw_->BindBackbufferAsRenderTarget();
	currentRenderVfb_ = 0;
	displayFramebuf_ = 0;
	prevDisplayFramebuf_ = 0;
	prevPrevDisplayFramebuf_ = 0;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		INFO_LOG(FRAMEBUF, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		DestroyFramebuf(vfb);
	}
	vfbs_.clear();

	for (size_t i = 0; i < bvfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = bvfbs_[i];
		DestroyFramebuf(vfb);
	}
	bvfbs_.clear();

	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
		delete it->second.fbo;
	}
	tempFBOs_.clear();

	DisableState();
}

void FramebufferManagerD3D11::FlushBeforeCopy() {
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.

	// TODO: It's really bad that we are calling SetRenderFramebuffer here with
	// all the irrelevant state checking it'll use to decide what to do. Should
	// do something more focused here.
	SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	drawEngine_->Flush();
}

void FramebufferManagerD3D11::Resized() {
	resized_ = true;
}

// Lots of this code could be shared (like the downsampling).
bool FramebufferManagerD3D11::GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, format);
		return true;
	}

	int w = vfb->renderWidth, h = vfb->renderHeight;
	Draw::Framebuffer *fboForRead = nullptr;
	if (vfb->fbo) {
		if (maxRes > 0 && vfb->renderWidth > vfb->width * maxRes) {
			w = vfb->width * maxRes;
			h = vfb->height * maxRes;

			Draw::Framebuffer *tempFBO = GetTempFBO(w, h);
			VirtualFramebuffer tempVfb = *vfb;
			tempVfb.fbo = tempFBO;
			tempVfb.bufferWidth = vfb->width;
			tempVfb.bufferHeight = vfb->height;
			tempVfb.renderWidth = w;
			tempVfb.renderHeight = h;
			BlitFramebuffer(&tempVfb, 0, 0, vfb, 0, 0, vfb->width, vfb->height, 0);

			fboForRead = tempFBO;
		} else {
			fboForRead = vfb->fbo;
		}
	}
	if (!fboForRead)
		return false;

	buffer.Allocate(w, h, GE_FORMAT_8888, !useBufferedRendering_, true);

	ID3D11Texture2D *packTex;
	D3D11_TEXTURE2D_DESC packDesc{};
	packDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	packDesc.BindFlags = 0;
	packDesc.Width = w;
	packDesc.Height = h;
	packDesc.ArraySize = 1;
	packDesc.MipLevels = 1;
	packDesc.Usage = D3D11_USAGE_STAGING;
	packDesc.SampleDesc.Count = 1;
	packDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ASSERT_SUCCESS(device_->CreateTexture2D(&packDesc, nullptr, &packTex));

	ID3D11Texture2D *nativeTex = (ID3D11Texture2D *)draw_->GetFramebufferAPITexture(fboForRead, Draw::FB_COLOR_BIT, 0);
	context_->CopyResource(packTex, nativeTex);

	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(packTex, 0, D3D11_MAP_READ, 0, &map);

	for (int y = 0; y < h; y++) {
		uint8_t *dest = (uint8_t *)buffer.GetData() + y * w * 4;
		const uint8_t *src = ((const uint8_t *)map.pData) + map.RowPitch * y;
		memcpy(dest, src, 4 * w);
	}

	context_->Unmap(packTex, 0);
	packTex->Release();
	return true;
}

bool FramebufferManagerD3D11::GetDepthStencilBuffer(VirtualFramebuffer *vfb, GPUDebugBuffer &buffer, bool stencil) {
	int w = vfb->renderWidth, h = vfb->renderHeight;
	Draw::Framebuffer *fboForRead = nullptr;
	fboForRead = vfb->fbo;

	if (stencil) {
		buffer.Allocate(w, h, GPU_DBG_FORMAT_8BIT);
	} else if (gstate_c.Supports(GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT)) {
		buffer.Allocate(w, h, GPU_DBG_FORMAT_FLOAT_DIV_256);
	} else {
		buffer.Allocate(w, h, GPU_DBG_FORMAT_FLOAT);
	}

	ID3D11Texture2D *packTex;
	D3D11_TEXTURE2D_DESC packDesc{};
	packDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	packDesc.BindFlags = 0;
	packDesc.Width = w;
	packDesc.Height = h;
	packDesc.ArraySize = 1;
	packDesc.MipLevels = 1;
	packDesc.Usage = D3D11_USAGE_STAGING;
	packDesc.SampleDesc.Count = 1;
	packDesc.Format = (DXGI_FORMAT)draw_->GetFramebufferAPITexture(fboForRead, Draw::FB_DEPTH_BIT | Draw::FB_FORMAT_BIT, 0);
	ASSERT_SUCCESS(device_->CreateTexture2D(&packDesc, nullptr, &packTex));

	ID3D11Texture2D *nativeTex = (ID3D11Texture2D *)draw_->GetFramebufferAPITexture(fboForRead, Draw::FB_DEPTH_BIT, 0);
	context_->CopyResource(packTex, nativeTex);

	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(packTex, 0, D3D11_MAP_READ, 0, &map);

	for (int y = 0; y < h; y++) {
		float *dest = (float *)(buffer.GetData() + y * w * 4);
		u8 *destStencil = buffer.GetData() + y * w;
		const uint32_t *src = (const uint32_t *)((const uint8_t *)map.pData + map.RowPitch * y);
		for (int x = 0; x < w; x++) {
			if (stencil) {
				destStencil[x] = src[x] >> 24;
			} else {
				dest[x] = (src[x] & 0xFFFFFF) / (256.f * 256.f * 256.f);
			}
		}
	}

	context_->Unmap(packTex, 0);
	packTex->Release();
	return true;
}

bool FramebufferManagerD3D11::GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(z_address | 0x04000000), z_stride, 512, GPU_DBG_FORMAT_16BIT);
		return true;
	}

	if (!vfb->fbo) {
		return false;
	}

	return GetDepthStencilBuffer(vfb, buffer, false);
}

bool FramebufferManagerD3D11::GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		return false;
	}

	if (!vfb->fbo) {
		return false;
	}

	return GetDepthStencilBuffer(vfb, buffer, true);
}

bool FramebufferManagerD3D11::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	ID3D11Texture2D *backbuffer = (ID3D11Texture2D *)draw_->GetNativeObject(Draw::NativeObject::BACKBUFFER_COLOR_TEX);
	if (!backbuffer) {
		ERROR_LOG(G3D, "Failed to get backbuffer from draw context");
		return false;
	}
	D3D11_TEXTURE2D_DESC desc;
	backbuffer->GetDesc(&desc);
	int w = desc.Width;
	int h = desc.Height;
	buffer.Allocate(w, h, GE_FORMAT_8888, !useBufferedRendering_, true);

	ID3D11Texture2D *packTex;
	D3D11_TEXTURE2D_DESC packDesc{};
	packDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	packDesc.BindFlags = 0;
	packDesc.Width = w;
	packDesc.Height = h;
	packDesc.ArraySize = 1;
	packDesc.MipLevels = 1;
	packDesc.Usage = D3D11_USAGE_STAGING;
	packDesc.SampleDesc.Count = 1;
	packDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ASSERT_SUCCESS(device_->CreateTexture2D(&packDesc, nullptr, &packTex));

	context_->CopyResource(packTex, backbuffer);

	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(packTex, 0, D3D11_MAP_READ, 0, &map);

	for (int y = 0; y < h; y++) {
		uint8_t *dest = (uint8_t *)buffer.GetData() + y * w * 4;
		const uint8_t *src = ((const uint8_t *)map.pData) + map.RowPitch * y;
		memcpy(dest, src, 4 * w);
	}

	context_->Unmap(packTex, 0);
	packTex->Release();
	return true;
}
