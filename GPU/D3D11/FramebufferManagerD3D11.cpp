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

#include "math/lin/matrix4x4.h"
#include "ext/native/thin3d/thin3d.h"

#include "Common/ColorConv.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"

#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"

#include "ext/native/thin3d/thin3d.h"

#include <algorithm>

#ifdef _M_SSE
#include <xmmintrin.h>
#endif

static const char * vscode =
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

//--------------------------------------------------------------------------------------
// Pixel shader
//--------------------------------------------------------------------------------------
static const char * pscode =
	"SamplerState samp : register(s0);\n"
	"Texture2D<float4> tex : register(t0);\n"
	"struct PS_IN {\n"
	"  float2 Uv : TEXCOORD0;\n"
	"};\n"
	"float4 main( PS_IN In ) : SV_Target {\n"
	"  float4 c = tex.Sample(samp, In.Uv);\n"
	"  return c;\n"
	"}\n";

static const D3D11_INPUT_ELEMENT_DESC g_FramebufferVertexElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, },
};

void FramebufferManagerD3D11::ClearBuffer(bool keepState) {
	draw_->Clear(Draw::ClearFlag::COLOR | Draw::ClearFlag::DEPTH | Draw::ClearFlag::STENCIL, 0, ToScaledDepth(0), 0);
}

void FramebufferManagerD3D11::DisableState() {
	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], nullptr, 0xFFFFFFFF);
	context_->RSSetState(stockD3D11.rasterStateNoCull);
	context_->OMSetDepthStencilState(stockD3D11.depthStencilDisabled, 0xFF);
}

FramebufferManagerD3D11::FramebufferManagerD3D11(Draw::DrawContext *draw)
	: FramebufferManagerCommon(draw),
	drawPixelsTex_(0),
	convBuf(0),
	stencilUploadPS_(nullptr),
	stencilUploadVS_(nullptr),
	stencilUploadFailed_(false) {

	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);

	std::vector<uint8_t> bytecode;

	std::string errorMsg;
	pFramebufferVertexShader_ = CreateVertexShaderD3D11(device_, vscode, strlen(vscode), &bytecode);
	pFramebufferPixelShader_ = CreatePixelShaderD3D11(device_, pscode, strlen(pscode));
	device_->CreateInputLayout(g_FramebufferVertexElements, ARRAY_SIZE(g_FramebufferVertexElements), bytecode.data(), bytecode.size(), &pFramebufferVertexDecl_);

	float coord[20] = {
		-1.0f,-1.0f, 0, 0,0,
		1.0f,-1.0f, 0, 0,0,
		1.0f,1.0f, 0, 0,0,
		-1.0f,1.0f, 0, 0,0,
	};
	D3D11_BUFFER_DESC vb{};
	vb.ByteWidth = 20 * 4;
	vb.Usage = D3D11_USAGE_IMMUTABLE;
	vb.CPUAccessFlags = 0;
}

FramebufferManagerD3D11::~FramebufferManagerD3D11() {
	// Drawing cleanup
	if (pFramebufferVertexShader_) {
		pFramebufferVertexShader_->Release();
		pFramebufferVertexShader_ = nullptr;
	}
	if (pFramebufferPixelShader_) {
		pFramebufferPixelShader_->Release();
		pFramebufferPixelShader_ = nullptr;
	}
	pFramebufferVertexDecl_->Release();
	if (drawPixelsTex_) {
		drawPixelsTex_->Release();
	}

	// FBO cleanup
	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
		delete it->second.fbo;
	}
	delete[] convBuf;

	// Stencil cleanup
	for (int i = 0; i < 256; i++) {
		if (stencilMaskStates_[i])
			stencilMaskStates_[i]->Release();
	}
	if (stencilUploadPS_) {
		stencilUploadPS_->Release();
	}
	if (stencilUploadVS_) {
		stencilUploadVS_->Release();
	}
}

void FramebufferManagerD3D11::SetTextureCache(TextureCacheD3D11 *tc) {
	textureCacheD3D11_ = tc;
	textureCache_ = tc;
}

void FramebufferManagerD3D11::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	u8 *convBuf = NULL;

	// TODO: Check / use D3DCAPS2_DYNAMICTEXTURES?
	if (drawPixelsTex_ && (drawPixelsTexW_ != width || drawPixelsTexH_ != height)) {
		drawPixelsTex_->Release();
		drawPixelsTex_ = nullptr;
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
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		device_->CreateTexture2D(&desc, nullptr, &drawPixelsTex_);
		device_->CreateShaderResourceView(drawPixelsTex_, nullptr, &drawPixelsTexView_);
		drawPixelsTexW_ = width;
		drawPixelsTexH_ = height;
	}

	if (!drawPixelsTex_) {
		return;
	}

	D3D11_MAPPED_SUBRESOURCE map;
	context_->Map(drawPixelsTex_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

	convBuf = (u8*)map.pData;

	// Final format is BGRA(directx)
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
	// D3DXSaveTextureToFile("game:\\cc.png", D3DXIFF_PNG, drawPixelsTex_, NULL);
}

void FramebufferManagerD3D11::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	if (useBufferedRendering_ && vfb && vfb->fbo) {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo);
		D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)vfb->renderWidth, (float)vfb->renderHeight, 0.0f, 1.0f };
		context_->RSSetViewports(1, &vp);
	} else {
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, ROTATION_LOCKED_HORIZONTAL);
		D3D11_VIEWPORT vp{ x, y, w, h, 0.0f, 1.0f };
		context_->RSSetViewports(1, &vp);
	}
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height);
	DisableState();
	context_->PSSetShaderResources(0, 1, &drawPixelsTexView_);
	DrawActiveTexture(dstX, dstY, width, height, vfb->bufferWidth, vfb->bufferHeight, 0.0f, 0.0f, 1.0f, 1.0f, ROTATION_LOCKED_HORIZONTAL, true);
	textureCacheD3D11_->ForgetLastTexture();
	//	context_->RSSetViewports(1, &vp);
}

void FramebufferManagerD3D11::DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) {
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272);
	DisableState();

	// This might draw directly at the backbuffer (if so, applyPostShader is set) so if there's a post shader, we need to apply it here.
	// Should try to unify this path with the regular path somehow, but this simple solution works for most of the post shaders
	// (it always runs at output resolution so FXAA may look odd).
	float x, y, w, h;
	int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
	CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, uvRotation);
	context_->PSSetShaderResources(0, 1, &drawPixelsTexView_);
	DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, 0.0f, 0.0f, 480.0f / 512.0f, 1.0f, uvRotation, g_Config.iBufFilter == SCALE_LINEAR);
}

void FramebufferManagerD3D11::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, bool linearFilter) {
	// TODO: StretchRect instead?
	float coord[20] = {
		x,y,0, u0,v0,
		x + w,y,0, u1,v0,
		x + w,y + h,0, u1,v1,
		x,y + h,0, u0,v1,
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
			temp[i * 2] = coord[((i + rotation) & 3) * 5 + 3];
			temp[i * 2 + 1] = coord[((i + rotation) & 3) * 5 + 4];
		}

		for (int i = 0; i < 4; i++) {
			coord[i * 5 + 3] = temp[i * 2];
			coord[i * 5 + 4] = temp[i * 2 + 1];
		}
	}

	float invDestW = 1.0f / (destW * 0.5f);
	float invDestH = 1.0f / (destH * 0.5f);
	float halfPixelX = invDestW * 0.5f;
	float halfPixelY = invDestH * 0.5f;
	for (int i = 0; i < 4; i++) {
		coord[i * 5] = coord[i * 5] * invDestW - 1.0f - halfPixelX;
		coord[i * 5 + 1] = -(coord[i * 5 + 1] * invDestH - 1.0f - halfPixelY);
	}

	context_->RSSetState(stockD3D11.rasterStateNoCull);
	context_->OMSetBlendState(stockD3D11.blendStateDisabledWithColorMask[0xF], nullptr, 0xFFFFFFFF);
	context_->IASetInputLayout(pFramebufferVertexDecl_);
	context_->PSSetShader(pFramebufferPixelShader_, 0, 0);
	context_->VSSetShader(pFramebufferVertexShader_, 0, 0);
	context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	context_->PSSetSamplers(0, 1, linearFilter ? &stockD3D11.samplerLinear2DWrap : &stockD3D11.samplerPoint2DWrap);

	// TODO: DrawRectBuffer ? 
	shaderManager_->DirtyLastShader();
	context_->Draw(2, 0);
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
		context_->IASetInputLayout(pFramebufferVertexDecl_);
		context_->PSSetShader(pFramebufferPixelShader_, nullptr, 0);
		context_->VSSetShader(pFramebufferVertexShader_, nullptr, 0);
		context_->IASetVertexBuffers(0, 1, &vbFullScreenRect_, &vbFullScreenStride_, &vbFullScreenOffset_);
		shaderManager_->DirtyLastShader();
		D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)vfb->renderWidth, (float)vfb->renderHeight, 0.0f, 1.0f };
		context_->RSSetViewports(1, &vp);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context_->Draw(2, 0);
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
	/*
	bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
	bool matchingSize = src->width == dst->width && src->height == dst->height;
	if (matchingDepthBuffer && matchingSize) {
		// Doesn't work.  Use a shader maybe?
		draw_->BindBackbufferAsRenderTarget();

		LPDIRECT3DTEXTURE9 srcTex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(src->fbo, Draw::FB_DEPTH_BIT, 0);
		LPDIRECT3DTEXTURE9 dstTex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(dst->fbo, Draw::FB_DEPTH_BIT, 0);

		if (srcTex && dstTex) {
			D3DSURFACE_DESC srcDesc;
			srcTex->GetLevelDesc(0, &srcDesc);
			D3DSURFACE_DESC dstDesc;
			dstTex->GetLevelDesc(0, &dstDesc);

			D3DLOCKED_RECT srcLock;
			D3DLOCKED_RECT dstLock;
			HRESULT srcLockRes = srcTex->LockRect(0, &srcLock, nullptr, D3DLOCK_READONLY);
			HRESULT dstLockRes = dstTex->LockRect(0, &dstLock, nullptr, 0);
			if (SUCCEEDED(srcLockRes) && SUCCEEDED(dstLockRes)) {
				u32 pitch = std::min(srcLock.Pitch, dstLock.Pitch);
				u32 w = std::min(pitch / 4, std::min(srcDesc.Width, dstDesc.Width));
				u32 h = std::min(srcDesc.Height, dstDesc.Height);
				const u8 *srcp = (const u8 *)srcLock.pBits;
				u8 *dstp = (u8 *)dstLock.pBits;

				if (w == pitch / 4 && srcLock.Pitch == dstLock.Pitch) {
					CopyPixelDepthOnly((u32 *)dstp, (const u32 *)srcp, w * h);
				} else {
					for (u32 y = 0; y < h; ++y) {
						CopyPixelDepthOnly((u32 *)dstp, (const u32 *)srcp, w);
						dstp += dstLock.Pitch;
						srcp += srcLock.Pitch;
					}
				}
			}
			if (SUCCEEDED(srcLockRes)) {
				srcTex->UnlockRect(0);
			}
			if (SUCCEEDED(dstLockRes)) {
				dstTex->UnlockRect(0);
			}
		}

		RebindFramebuffer();
	}*/
}

void FramebufferManagerD3D11::BindFramebufferColor(int stage, VirtualFramebuffer *framebuffer, int flags) {
	if (framebuffer == NULL) {
		framebuffer = currentRenderVfb_;
	}

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
	if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == gstate.getFrameBufRawAddress()) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		Draw::Framebuffer *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (Draw::FBColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;

			int x = 0;
			int y = 0;
			int w = framebuffer->drawnWidth;
			int h = framebuffer->drawnHeight;

			// If max is not > min, we probably could not detect it.  Skip.
			// See the vertex decoder, where this is updated.
			if ((flags & BINDFBCOLOR_MAY_COPY_WITH_UV) == BINDFBCOLOR_MAY_COPY_WITH_UV && gstate_c.vertBounds.maxU > gstate_c.vertBounds.minU) {
				x = gstate_c.vertBounds.minU;
				y = gstate_c.vertBounds.minV;
				w = gstate_c.vertBounds.maxU - x;
				h = gstate_c.vertBounds.maxV - y;

				// If we bound a framebuffer, apply the byte offset as pixels to the copy too.
				if (flags & BINDFBCOLOR_APPLY_TEX_OFFSET) {
					x += gstate_c.curTextureXOffset;
					y += gstate_c.curTextureYOffset;
				}
			}

			BlitFramebuffer(&copyInfo, x, y, framebuffer, x, y, w, h, 0);

			RebindFramebuffer();
			draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::FB_COLOR_BIT, 0);
		} else {
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		}
	} else {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
	}
}

void FramebufferManagerD3D11::CopyDisplayToOutput() {
	DownloadFramebufferOnSwitch(currentRenderVfb_);

	draw_->BindBackbufferAsRenderTarget();
	currentRenderVfb_ = 0;

	if (displayFramebufPtr_ == 0) {
		DEBUG_LOG(SCEGE, "Display disabled, displaying only black");
		// No framebuffer to display! Clear to black.
		ClearBuffer();
		return;
	}

	if (useBufferedRendering_) {
		// In buffered, we no longer clear the backbuffer before we start rendering.
		ClearBuffer();
		D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, 0.0f, 1.0f };
		context_->RSSetViewports(1, &vp);
	}

	u32 offsetX = 0;
	u32 offsetY = 0;

	VirtualFramebuffer *vfb = GetVFBAt(displayFramebufPtr_);
	if (!vfb) {
		// Let's search for a framebuf within this range.
		const u32 addr = (displayFramebufPtr_ & 0x03FFFFFF) | 0x04000000;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *v = vfbs_[i];
			const u32 v_addr = (v->fb_address & 0x03FFFFFF) | 0x04000000;
			const u32 v_size = FramebufferByteSize(v);
			if (addr >= v_addr && addr < v_addr + v_size) {
				const u32 dstBpp = v->format == GE_FORMAT_8888 ? 4 : 2;
				const u32 v_offsetX = ((addr - v_addr) / dstBpp) % v->fb_stride;
				const u32 v_offsetY = ((addr - v_addr) / dstBpp) / v->fb_stride;
				// We have enough space there for the display, right?
				if (v_offsetX + 480 >(u32)v->fb_stride || v->bufferHeight < v_offsetY + 272) {
					continue;
				}
				// Check for the closest one.
				if (offsetY == 0 || offsetY > v_offsetY) {
					offsetX = v_offsetX;
					offsetY = v_offsetY;
					vfb = v;
				}
			}
		}

		if (vfb) {
			// Okay, we found one above.
			INFO_LOG_REPORT_ONCE(displayoffset, HLE, "Rendering from framebuf with offset %08x -> %08x+%dx%d", addr, vfb->fb_address, offsetX, offsetY);
		}
	}

	if (vfb && vfb->format != displayFormat_) {
		if (vfb->last_frame_render + FBO_OLD_AGE < gpuStats.numFlips) {
			// The game probably switched formats on us.
			vfb->format = displayFormat_;
		} else {
			vfb = 0;
		}
	}

	if (!vfb) {
		if (Memory::IsValidAddress(displayFramebufPtr_)) {
			// The game is displaying something directly from RAM. In GTA, it's decoded video.

			// First check that it's not a known RAM copy of a VRAM framebuffer though, as in MotoGP
			for (auto iter = knownFramebufferRAMCopies_.begin(); iter != knownFramebufferRAMCopies_.end(); ++iter) {
				if (iter->second == displayFramebufPtr_) {
					vfb = GetVFBAt(iter->first);
				}
			}

			if (!vfb) {
				// Just a pointer to plain memory to draw. Draw it.
				DrawFramebufferToOutput(Memory::GetPointer(displayFramebufPtr_), displayFormat_, displayStride_, true);
				return;
			}
		} else {
			DEBUG_LOG(SCEGE, "Found no FBO to display! displayFBPtr = %08x", displayFramebufPtr_);
			// No framebuffer to display! Clear to black. If buffered, we already did that.
			if (!useBufferedRendering_)
				ClearBuffer();
			return;
		}
	}

	vfb->usageFlags |= FB_USAGE_DISPLAYED_FRAMEBUFFER;
	vfb->last_frame_displayed = gpuStats.numFlips;
	vfb->dirtyAfterDisplay = false;
	vfb->reallyDirtyAfterDisplay = false;

	if (prevDisplayFramebuf_ != displayFramebuf_) {
		prevPrevDisplayFramebuf_ = prevDisplayFramebuf_;
	}
	if (displayFramebuf_ != vfb) {
		prevDisplayFramebuf_ = displayFramebuf_;
	}
	displayFramebuf_ = vfb;

	if (vfb->fbo) {
		DEBUG_LOG(SCEGE, "Displaying FBO %08x", vfb->fb_address);
		DisableState();
		draw_->BindFramebufferAsTexture(vfb->fbo, 0, Draw::FB_COLOR_BIT, 0);

		// Output coordinates
		float x, y, w, h;
		int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, uvRotation);

		const float u0 = offsetX / (float)vfb->bufferWidth;
		const float v0 = offsetY / (float)vfb->bufferHeight;
		const float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
		const float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

		if (1) {
			const u32 rw = PSP_CoreParameter().pixelWidth;
			const u32 rh = PSP_CoreParameter().pixelHeight;
			bool result = draw_->BlitFramebuffer(vfb->fbo,
				(LONG)(u0 * vfb->renderWidth), (LONG)(v0 * vfb->renderHeight), (LONG)(u1 * vfb->renderWidth), (LONG)(v1 * vfb->renderHeight),
				nullptr,
				(LONG)(x * rw / w), (LONG)(y * rh / h), (LONG)((x + w) * rw / w), (LONG)((y + h) * rh / h),
				Draw::FB_COLOR_BIT,
				g_Config.iBufFilter == SCALE_LINEAR ? Draw::FB_BLIT_LINEAR : Draw::FB_BLIT_NEAREST);
			if (!result) {
				ERROR_LOG_REPORT_ONCE(blit_fail, G3D, "fbo_blit_color failed on display");
				D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, 0.0f, 1.0f };
				context_->RSSetViewports(1, &vp);
				DrawActiveTexture(x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, u0, v0, u1, v1, uvRotation, g_Config.iBufFilter == SCALE_LINEAR);
			}
		}
		/*
		else if (usePostShader_ && extraFBOs_.size() == 1 && !postShaderAtOutputResolution_) {
		// An additional pass, post-processing shader to the extra FBO.
		BindFramebufferAsRenderTarget(extraFBOs_[0]);
		int fbo_w, fbo_h;
		fbo_get_dimensions(extraFBOs_[0], &fbo_w, &fbo_h);
		DXSetViewport(0, 0, fbo_w, fbo_h);
		DrawActiveTexture(colorTexture, 0, 0, fbo_w, fbo_h, fbo_w, fbo_h, true, 1.0f, 1.0f, postShaderProgram_);

		fbo_unbind();

		// Use the extra FBO, with applied post-processing shader, as a texture.
		// fbo_bind_color_as_texture(extraFBOs_[0], 0);
		if (extraFBOs_.size() == 0) {
		ERROR_LOG(G3D, "WTF?");
		return;
		}
		colorTexture = fbo_get_color_texture(extraFBOs_[0]);
		DXSetViewport(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		// These are in the output display coordinates
		DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height);
		} else {
		// Use post-shader, but run shader at output resolution.
		DXSetViewport(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		// These are in the output display coordinates
		DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height, postShaderProgram_);
		}
		*/
	}
	// gstate_c.Dirty(DIRTY_VIEWPORT_STATE);
}

void FramebufferManagerD3D11::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {
	if (vfb) {
		// We'll pseudo-blit framebuffers here to get a resized version of vfb.
		VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
		OptimizeDownloadRange(vfb, x, y, w, h);
		BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);

		PackFramebufferD3D11_(nvfb, x, y, w, h);

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
		ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
		return false;
	}

	draw_->BindFramebufferAsRenderTarget(nvfb->fbo);
	ClearBuffer();
	return true;
}

void FramebufferManagerD3D11::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// Nothing to do here.
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

	// Direct3D 9 doesn't support rect -> self.
	Draw::Framebuffer *srcFBO = src->fbo;
	if (src == dst) {
		Draw::Framebuffer *tempFBO = GetTempFBO(src->renderWidth, src->renderHeight, (Draw::FBColorDepth)src->colorDepth);
		bool result = draw_->BlitFramebuffer(
			src->fbo, srcX1, srcY1, srcX2, srcY2,
			tempFBO, dstX1, dstY1, dstX2, dstY2,
			Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST);
		if (result) {
			srcFBO = tempFBO;
		}
	}
	bool result = draw_->BlitFramebuffer(
		srcFBO, srcX1, srcY1, srcX2, srcY2,
		dst->fbo, dstX1, dstY1, dstX2, dstY2,
		Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST);
	if (!result) {
		ERROR_LOG_REPORT(G3D, "fbo_blit_color failed in blit: %08x (%08x -> %08x)", src->fb_address, dst->fb_address);
	}
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
				ConvertBGRA8888ToRGBA8888(dst32, src32, width);
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
				ConvertBGRA8888ToRGB565(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_5551: // ABGR 1555
			for (u32 y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA5551(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_4444: // ABGR 4444
			for (u32 y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA4444(dst16, src32, width);
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

void FramebufferManagerD3D11::PackFramebufferD3D11_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	/*
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferD3D11_: vfb->fbo == 0");
		draw_->BindBackbufferAsRenderTarget();
		return;
	}

	const u32 fb_address = (0x04000000) | vfb->fb_address;
	const int dstBpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;

	// We always need to convert from the framebuffer native format.
	// Right now that's always 8888.
	DEBUG_LOG(HLE, "Reading framebuffer to mem, fb_address = %08x", fb_address);

	LPDIRECT3DSURFACE9 renderTarget = (LPDIRECT3DSURFACE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_COLOR_BIT | Draw::FB_SURFACE_BIT, 0);
	D3DSURFACE_DESC desc;
	renderTarget->GetDesc(&desc);

	LPDIRECT3DSURFACE9 offscreen = GetOffscreenSurface(renderTarget, vfb);
	if (offscreen) {
		HRESULT hr = pD3Ddevice->GetRenderTargetData(renderTarget, offscreen);
		if (SUCCEEDED(hr)) {
			D3DLOCKED_RECT locked;
			u32 widthFactor = vfb->renderWidth / vfb->bufferWidth;
			u32 heightFactor = vfb->renderHeight / vfb->bufferHeight;
			RECT rect = { (LONG)(x * widthFactor), (LONG)(y * heightFactor), (LONG)((x + w) * widthFactor), (LONG)((y + h) * heightFactor) };
			hr = offscreen->LockRect(&locked, &rect, D3DLOCK_READONLY);
			if (SUCCEEDED(hr)) {
				// TODO: Handle the other formats?  We don't currently create them, I think.
				const int dstByteOffset = (y * vfb->fb_stride + x) * dstBpp;
				// Pixel size always 4 here because we always request BGRA8888.
				ConvertFromRGBA8888(Memory::GetPointer(fb_address + dstByteOffset), (u8 *)locked.pBits, vfb->fb_stride, locked.Pitch / 4, w, h, vfb->format);
				offscreen->UnlockRect();
			} else {
				ERROR_LOG_REPORT(G3D, "Unable to lock rect from %08x: %d,%d %dx%d of %dx%d", fb_address, rect.left, rect.top, rect.right, rect.bottom, vfb->renderWidth, vfb->renderHeight);
			}
		} else {
			ERROR_LOG_REPORT(G3D, "Unable to download render target data from %08x", fb_address);
		}
	}
	*/
}

void FramebufferManagerD3D11::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	// We always read the depth buffer in 24_8 format.
	const u32 z_address = (0x04000000) | vfb->z_address;

	/*
	DEBUG_LOG(SCEGE, "Reading depthbuffer to mem at %08x for vfb=%08x", z_address, vfb->fb_address);

	LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_DEPTH_BIT, 0);
	if (tex) {
		D3DSURFACE_DESC desc;
		D3DLOCKED_RECT locked;
		tex->GetLevelDesc(0, &desc);
		RECT rect = { 0, 0, (LONG)desc.Width, (LONG)desc.Height };
		HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);

		if (SUCCEEDED(hr)) {
			const int dstByteOffset = y * vfb->fb_stride * sizeof(s16);
			const u32 *packed = (const u32 *)locked.pBits;
			u16 *depth = (u16 *)Memory::GetPointer(z_address);

			// TODO: Optimize.
			for (int yp = 0; yp < h; ++yp) {
				for (int xp = 0; xp < w; ++xp) {
					const int offset = (yp + y) & vfb->z_stride + x + xp;

					float scaled = FromScaledDepth((packed[offset] & 0x00FFFFFF) * (1.0f / 16777215.0f));
					if (scaled <= 0.0f) {
						depth[offset] = 0;
					} else if (scaled >= 65535.0f) {
						depth[offset] = 65535;
					} else {
						depth[offset] = (int)scaled;
					}
				}
			}

			tex->UnlockRect(0);
		} else {
			ERROR_LOG_REPORT(G3D, "Unable to lock rect from depth %08x: %d,%d %dx%d of %dx%d", vfb->fb_address, rect.left, rect.top, rect.right, rect.bottom, vfb->renderWidth, vfb->renderHeight);
		}
	} else {
		ERROR_LOG_REPORT(G3D, "Unable to download render target depth from %08x", vfb->fb_address);
	}*/
}

void FramebufferManagerD3D11::EndFrame() {
	if (resized_) {
		DestroyAllFBOs(false);
		// Actually, auto mode should be more granular...
		// Round up to a zoom factor for the render size.
		int zoom = g_Config.iInternalResolution;
		if (zoom == 0) { // auto mode
											// Use the longest dimension
			if (!g_Config.IsPortrait()) {
				zoom = (PSP_CoreParameter().pixelWidth + 479) / 480;
			} else {
				zoom = (PSP_CoreParameter().pixelHeight + 479) / 480;
			}
		}
		if (zoom <= 1)
			zoom = 1;

		if (g_Config.IsPortrait()) {
			PSP_CoreParameter().renderWidth = 272 * zoom;
			PSP_CoreParameter().renderHeight = 480 * zoom;
		} else {
			PSP_CoreParameter().renderWidth = 480 * zoom;
			PSP_CoreParameter().renderHeight = 272 * zoom;
		}

		UpdateSize();
		// Seems related - if you're ok with numbers all the time, show some more :)
		if (g_Config.iShowFPSCounter != 0) {
			ShowScreenResolution();
		}
		resized_ = false;
	}
}

void FramebufferManagerD3D11::DeviceLost() {
	DestroyAllFBOs(false);
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

void FramebufferManagerD3D11::DestroyAllFBOs(bool forceDelete) {
	draw_->BindBackbufferAsRenderTarget();
	currentRenderVfb_ = 0;
	displayFramebuf_ = 0;
	prevDisplayFramebuf_ = 0;
	prevPrevDisplayFramebuf_ = 0;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		INFO_LOG(SCEGE, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
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

bool FramebufferManagerD3D11::GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) {
	return false;
}

bool FramebufferManagerD3D11::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	return false;
}

bool FramebufferManagerD3D11::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	return false;
}

bool FramebufferManagerD3D11::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	return false;
}