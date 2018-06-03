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

#include "gfx/d3d9_state.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"

#include "ext/native/thin3d/thin3d.h"

#include <algorithm>

#ifdef _M_SSE
#include <emmintrin.h>
#endif

namespace DX9 {

static const char * vscode =
	"struct VS_IN {\n"
	"  float4 ObjPos   : POSITION;\n"
	"  float2 Uv    : TEXCOORD0;\n"
	"};"
	"struct VS_OUT {\n"
	"  float4 ProjPos  : POSITION;\n"
	"  float2 Uv    : TEXCOORD0;\n"
	"};\n"
	"VS_OUT main( VS_IN In ) {\n"
	"  VS_OUT Out;\n"
	"  Out.ProjPos = In.ObjPos;\n"
	"  Out.Uv = In.Uv;\n"
	"  return Out;\n"
	"}\n";

//--------------------------------------------------------------------------------------
// Pixel shader
//--------------------------------------------------------------------------------------
static const char * pscode =
	"sampler s: register(s0);\n"
	"struct PS_IN {\n"
	"  float2 Uv : TEXCOORD0;\n"
	"};\n"
	"float4 main( PS_IN In ) : COLOR {\n"
	"  float4 c =  tex2D(s, In.Uv);\n"
	"  return c;\n"
	"}\n";

static const D3DVERTEXELEMENT9 g_FramebufferVertexElements[] = {
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	D3DDECL_END()
};

	FramebufferManagerDX9::FramebufferManagerDX9(Draw::DrawContext *draw)
		: FramebufferManagerCommon(draw) {

		device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
		deviceEx_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE_EX);
		std::string errorMsg;
		if (!CompileVertexShader(device_, vscode, &pFramebufferVertexShader, nullptr, errorMsg)) {
			OutputDebugStringA(errorMsg.c_str());
		}

		if (!CompilePixelShader(device_, pscode, &pFramebufferPixelShader, nullptr, errorMsg)) {
			OutputDebugStringA(errorMsg.c_str());
			if (pFramebufferVertexShader) {
				pFramebufferVertexShader->Release();
			}
		}

		device_->CreateVertexDeclaration(g_FramebufferVertexElements, &pFramebufferVertexDecl);
	}

	FramebufferManagerDX9::~FramebufferManagerDX9() {
		if (pFramebufferVertexShader) {
			pFramebufferVertexShader->Release();
			pFramebufferVertexShader = nullptr;
		}
		if (pFramebufferPixelShader) {
			pFramebufferPixelShader->Release();
			pFramebufferPixelShader = nullptr;
		}
		pFramebufferVertexDecl->Release();
		if (drawPixelsTex_) {
			drawPixelsTex_->Release();
		}
		for (auto &it : offscreenSurfaces_) {
			it.second.surface->Release();
		}
		delete [] convBuf;
		if (stencilUploadPS_) {
			stencilUploadPS_->Release();
		}
		if (stencilUploadVS_) {
			stencilUploadVS_->Release();
		}
	}

	void FramebufferManagerDX9::SetTextureCache(TextureCacheDX9 *tc) {
		textureCacheDX9_ = tc;
		textureCache_ = tc;
	}

	void FramebufferManagerDX9::SetShaderManager(ShaderManagerDX9 *sm) {
		shaderManagerDX9_ = sm;
		shaderManager_ = sm;
	}

	void FramebufferManagerDX9::SetDrawEngine(DrawEngineDX9 *td) {
		drawEngineD3D9_ = td;
		drawEngine_ = td;
	}

	void FramebufferManagerDX9::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) {
		u8 *convBuf = NULL;
		D3DLOCKED_RECT rect;

		// TODO: Check / use D3DCAPS2_DYNAMICTEXTURES?
		if (drawPixelsTex_ && (drawPixelsTexW_ != width || drawPixelsTexH_ != height)) {
			drawPixelsTex_->Release();
			drawPixelsTex_ = nullptr;
		}

		if (!drawPixelsTex_) {
			int usage = 0;
			D3DPOOL pool = D3DPOOL_MANAGED;
			if (deviceEx_) {
				pool = D3DPOOL_DEFAULT;
				usage = D3DUSAGE_DYNAMIC;
			}
			HRESULT hr = device_->CreateTexture(width, height, 1, usage, D3DFMT_A8R8G8B8, pool, &drawPixelsTex_, NULL);
			if (FAILED(hr)) {
				drawPixelsTex_ = nullptr;
				ERROR_LOG(G3D, "Failed to create drawpixels texture");
			}
			drawPixelsTexW_ = width;
			drawPixelsTexH_ = height;
		}

		if (!drawPixelsTex_) {
			return;
		}

		drawPixelsTex_->LockRect(0, &rect, NULL, D3DLOCK_DISCARD);

		convBuf = (u8*)rect.pBits;

		// Final format is BGRA(directx)
		if (srcPixelFormat != GE_FORMAT_8888 || srcStride != 512) {
			for (int y = 0; y < height; y++) {
				switch (srcPixelFormat) {
				case GE_FORMAT_565:
					{
						const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
						u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
						ConvertRGB565ToBGRA8888(dst, src, width);
					}
					break;
					// faster
				case GE_FORMAT_5551:
					{
						const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
						u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
						ConvertRGBA5551ToBGRA8888(dst, src, width);
					}
					break;
				case GE_FORMAT_4444:
					{
						const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
						u8 *dst = (u8 *)(convBuf + rect.Pitch * y);
						ConvertRGBA4444ToBGRA8888((u32 *)dst, src, width);
					}
					break;

				case GE_FORMAT_8888:
					{
						const u32_le *src = (const u32_le *)srcPixels + srcStride * y;
						u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
						ConvertRGBA8888ToBGRA8888(dst, src, width);
					}
					break;
				}
			}
		} else {
			for (int y = 0; y < height; y++) {
				const u32_le *src = (const u32_le *)srcPixels + srcStride * y;
				u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
				ConvertRGBA8888ToBGRA8888(dst, src, width);
			}
		}

		drawPixelsTex_->UnlockRect(0);
		device_->SetTexture(0, drawPixelsTex_);
		// D3DXSaveTextureToFile("game:\\cc.png", D3DXIFF_PNG, drawPixelsTex_, NULL);
	}

	void FramebufferManagerDX9::SetViewport2D(int x, int y, int w, int h) {
		D3DVIEWPORT9 vp{ (DWORD)x, (DWORD)y, (DWORD)w, (DWORD)h, 0.0f, 1.0f };
		device_->SetViewport(&vp);
	}

	void FramebufferManagerDX9::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) {
		// TODO: StretchRect instead when possible?
		float coord[20] = {
			x,y,0, u0,v0,
			x+w,y,0, u1,v0,
			x+w,y+h,0, u1,v1,
			x,y+h,0, u0,v1,
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

		if (flags & DRAWTEX_LINEAR) {
			dxstate.texMagFilter.set(D3DTEXF_LINEAR);
			dxstate.texMinFilter.set(D3DTEXF_LINEAR);
		} else {
			dxstate.texMagFilter.set(D3DTEXF_POINT);
			dxstate.texMinFilter.set(D3DTEXF_POINT);
		}
		dxstate.texMipLodBias.set(0.0f);
		dxstate.texMaxMipLevel.set(0);
		dxstate.blend.disable();
		dxstate.cullMode.set(false, false);
		dxstate.depthTest.disable();
		dxstate.scissorTest.disable();
		dxstate.stencilTest.disable();
		dxstate.colorMask.set(true, true, true, true);
		dxstate.stencilMask.set(0xFF);
		HRESULT hr = device_->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, coord, 5 * sizeof(float));
		if (FAILED(hr)) {
			ERROR_LOG_REPORT(G3D, "DrawActiveTexture() failed: %08x", hr);
		}
	}

	void FramebufferManagerDX9::Bind2DShader() {
		device_->SetVertexDeclaration(pFramebufferVertexDecl);
		device_->SetPixelShader(pFramebufferPixelShader);
		device_->SetVertexShader(pFramebufferVertexShader);
	}

	void FramebufferManagerDX9::BindPostShader(const PostShaderUniforms &uniforms) {
		Bind2DShader();
	}

	void FramebufferManagerDX9::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
		if (!useBufferedRendering_ || !vfb->fbo) {
			return;
		}

		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::KEEP, Draw::RPAction::KEEP });

		// Technically, we should at this point re-interpret the bytes of the old format to the new.
		// That might get tricky, and could cause unnecessary slowness in some games.
		// For now, we just clear alpha/stencil from 565, which fixes shadow issues in Kingdom Hearts.
		// (it uses 565 to write zeros to the buffer, than 4444 to actually render the shadow.)
		//
		// The best way to do this may ultimately be to create a new FBO (combine with any resize?)
		// and blit with a shader to that, then replace the FBO on vfb.  Stencil would still be complex
		// to exactly reproduce in 4444 and 8888 formats.

		if (old == GE_FORMAT_565) {
			dxstate.scissorTest.disable();
			dxstate.depthWrite.set(FALSE);
			dxstate.colorMask.set(false, false, false, true);
			dxstate.stencilFunc.set(D3DCMP_ALWAYS, 0, 0);
			dxstate.stencilMask.set(0xFF);
			gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE);

			float coord[20] = {
				-1.0f,-1.0f,0, 0,0,
				1.0f,-1.0f,0, 0,0,
				1.0f,1.0f,0, 0,0,
				-1.0f,1.0f,0, 0,0,
			};

			dxstate.cullMode.set(false, false);
			device_->SetVertexDeclaration(pFramebufferVertexDecl);
			device_->SetPixelShader(pFramebufferPixelShader);
			device_->SetVertexShader(pFramebufferVertexShader);
			shaderManagerDX9_->DirtyLastShader();
			device_->SetTexture(0, nullptr);

			D3DVIEWPORT9 vp{ 0, 0, (DWORD)vfb->renderWidth, (DWORD)vfb->renderHeight, 0.0f, 1.0f };
			device_->SetViewport(&vp);

			// This should clear stencil and alpha without changing the other colors.
			HRESULT hr = device_->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, coord, 5 * sizeof(float));
			if (FAILED(hr)) {
				ERROR_LOG_REPORT(G3D, "ReformatFramebufferFrom() failed: %08x", hr);
			}
			dxstate.viewport.restore();
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

	void FramebufferManagerDX9::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
		if (g_Config.bDisableSlowFramebufEffects) {
			return;
		}

		bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
		bool matchingSize = src->width == dst->width && src->height == dst->height;
		if (matchingDepthBuffer && matchingSize) {
			// Should use StretchRect here?  Note: should only copy depth and NOT copy stencil.  See #9740.
		}
	}

	LPDIRECT3DSURFACE9 FramebufferManagerDX9::GetOffscreenSurface(LPDIRECT3DSURFACE9 similarSurface, VirtualFramebuffer *vfb) {
		D3DSURFACE_DESC desc = {};
		HRESULT hr = similarSurface->GetDesc(&desc);
		if (FAILED(hr)) {
			ERROR_LOG_REPORT(G3D, "Unable to get size for offscreen surface at %08x", vfb->fb_address);
			return nullptr;
		}

		return GetOffscreenSurface(desc.Format, desc.Width, desc.Height);
	}

	LPDIRECT3DSURFACE9 FramebufferManagerDX9::GetOffscreenSurface(D3DFORMAT fmt, u32 w, u32 h) {
		u64 key = ((u64)fmt << 32) | (w << 16) | h;
		auto it = offscreenSurfaces_.find(key);
		if (it != offscreenSurfaces_.end()) {
			it->second.last_frame_used = gpuStats.numFlips;
			return it->second.surface;
		}

		textureCacheDX9_->ForgetLastTexture();
		LPDIRECT3DSURFACE9 offscreen = nullptr;
		HRESULT hr = device_->CreateOffscreenPlainSurface(w, h, fmt, D3DPOOL_SYSTEMMEM, &offscreen, NULL);
		if (FAILED(hr) || !offscreen) {
			ERROR_LOG_REPORT(G3D, "Unable to create offscreen surface %dx%d @%d", w, h, fmt);
			return nullptr;
		}
		const OffscreenSurface info = {offscreen, gpuStats.numFlips};
		offscreenSurfaces_[key] = info;
		return offscreen;
	}

	void FramebufferManagerDX9::BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags) {
		if (framebuffer == NULL) {
			framebuffer = currentRenderVfb_;
		}

		if (!framebuffer->fbo || !useBufferedRendering_) {
			device_->SetTexture(stage, nullptr);
			gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
			return;
		}

		// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
		// Let's just not bother with the copy in that case.
		bool skipCopy = (flags & BINDFBCOLOR_MAY_COPY) == 0;
		if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
			skipCopy = true;
		}
		if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == gstate.getFrameBufRawAddress()) {
			// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
			Draw::Framebuffer *renderCopy = GetTempFBO(TempFBO::COPY, framebuffer->renderWidth, framebuffer->renderHeight, (Draw::FBColorDepth)framebuffer->colorDepth);
			if (renderCopy) {
				VirtualFramebuffer copyInfo = *framebuffer;
				copyInfo.fbo = renderCopy;

				CopyFramebufferForColorTexture(&copyInfo, framebuffer, flags);
				RebindFramebuffer();
				draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::FB_COLOR_BIT, 0);
			} else {
				draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
			}
		} else {
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		}
	}

	bool FramebufferManagerDX9::CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
		nvfb->colorDepth = Draw::FBO_8888;

		nvfb->fbo = draw_->CreateFramebuffer({ nvfb->bufferWidth, nvfb->bufferHeight, 1, 1, true, (Draw::FBColorDepth)nvfb->colorDepth });
		if (!(nvfb->fbo)) {
			ERROR_LOG(FRAMEBUF, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
			return false;
		}

		draw_->BindFramebufferAsRenderTarget(nvfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
		return true;
	}

	void FramebufferManagerDX9::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
		// Nothing to do here.
	}

	void FramebufferManagerDX9::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) {
		if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
			// This can happen if we recently switched from non-buffered.
			if (useBufferedRendering_)
				draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP });
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
			Draw::Framebuffer *tempFBO = GetTempFBO(TempFBO::BLIT, src->renderWidth, src->renderHeight, (Draw::FBColorDepth)src->colorDepth);
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
			ERROR_LOG_REPORT(G3D, "fbo_blit_color failed in blit (%08x -> %08x)", src->fb_address, dst->fb_address);
		}
	}

	void ConvertFromBGRA8888(u8 *dst, u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format) {
		// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
		const u32 *src32 = (const u32 *)src;

		if (format == GE_FORMAT_8888) {
			ConvertFromBGRA8888(dst, src, dstStride, srcStride, width, height, Draw::DataFormat::R8G8B8A8_UNORM);
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

	void FramebufferManagerDX9::PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
		if (!vfb->fbo) {
			ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferDirectx9_: vfb->fbo == 0");
			return;
		}

		const u32 fb_address = (0x04000000) | vfb->fb_address;
		const int dstBpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;

		// We always need to convert from the framebuffer native format.
		// Right now that's always 8888.
		DEBUG_LOG(G3D, "Reading framebuffer to mem, fb_address = %08x", fb_address);

		LPDIRECT3DSURFACE9 renderTarget = (LPDIRECT3DSURFACE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_COLOR_BIT | Draw::FB_SURFACE_BIT, 0);
		D3DSURFACE_DESC desc;
		renderTarget->GetDesc(&desc);

		LPDIRECT3DSURFACE9 offscreen = GetOffscreenSurface(renderTarget, vfb);
		if (offscreen) {
			HRESULT hr = device_->GetRenderTargetData(renderTarget, offscreen);
			if (SUCCEEDED(hr)) {
				D3DLOCKED_RECT locked;
				u32 widthFactor = vfb->renderWidth / vfb->bufferWidth;
				u32 heightFactor = vfb->renderHeight / vfb->bufferHeight;
				RECT rect = {(LONG)(x * widthFactor), (LONG)(y * heightFactor), (LONG)((x + w) * widthFactor), (LONG)((y + h) * heightFactor)};
				hr = offscreen->LockRect(&locked, &rect, D3DLOCK_READONLY);
				if (SUCCEEDED(hr)) {
					// TODO: Handle the other formats?  We don't currently create them, I think.
					const int dstByteOffset = (y * vfb->fb_stride + x) * dstBpp;
					// Pixel size always 4 here because we always request BGRA8888.
					ConvertFromBGRA8888(Memory::GetPointer(fb_address + dstByteOffset), (u8 *)locked.pBits, vfb->fb_stride, locked.Pitch / 4, w, h, vfb->format);
					offscreen->UnlockRect();
				} else {
					ERROR_LOG_REPORT(G3D, "Unable to lock rect from %08x: %d,%d %dx%d of %dx%d", fb_address, rect.left, rect.top, rect.right, rect.bottom, vfb->renderWidth, vfb->renderHeight);
				}
			} else {
				ERROR_LOG_REPORT(G3D, "Unable to download render target data from %08x", fb_address);
			}
		}
	}

	void FramebufferManagerDX9::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
		if (!vfb->fbo) {
			ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
			return;
		}

		// We always read the depth buffer in 24_8 format.
		const u32 z_address = (0x04000000) | vfb->z_address;

		DEBUG_LOG(FRAMEBUF, "Reading depthbuffer to mem at %08x for vfb=%08x", z_address, vfb->fb_address);

		LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_DEPTH_BIT, 0);
		if (tex) {
			D3DSURFACE_DESC desc;
			D3DLOCKED_RECT locked;
			tex->GetLevelDesc(0, &desc);
			RECT rect = {0, 0, (LONG)desc.Width, (LONG)desc.Height};
			HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);

			if (SUCCEEDED(hr)) {
				const int dstByteOffset = y * vfb->fb_stride * sizeof(s16);
				const u32 *packed = (const u32 *)locked.pBits;
				u16 *depth = (u16 *)Memory::GetPointer(z_address);

				// TODO: Optimize.
				for (int yp = 0; yp < h; ++yp) {
					for (int xp = 0; xp < w; ++xp) {
						const int offset = (yp + y) * vfb->z_stride + x + xp;

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
		}
	}

	void FramebufferManagerDX9::EndFrame() {
	}

	void FramebufferManagerDX9::DeviceLost() {
		DestroyAllFBOs();
	}

	void FramebufferManagerDX9::DecimateFBOs() {
		FramebufferManagerCommon::DecimateFBOs();
		for (auto it = offscreenSurfaces_.begin(); it != offscreenSurfaces_.end(); ) {
			int age = frameLastFramebufUsed_ - it->second.last_frame_used;
			if (age > FBO_OLD_AGE) {
				it->second.surface->Release();
				it = offscreenSurfaces_.erase(it);
			} else {
				++it;
			}
		}
	}

	void FramebufferManagerDX9::DestroyAllFBOs() {
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

		for (auto &it : offscreenSurfaces_) {
			it.second.surface->Release();
		}
		offscreenSurfaces_.clear();

		SetNumExtraFBOs(0);
	}

	void FramebufferManagerDX9::Resized() {
		FramebufferManagerCommon::Resized();

		if (UpdateSize()) {
			DestroyAllFBOs();
		}
	}

	bool FramebufferManagerDX9::GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat fb_format, GPUDebugBuffer &buffer, int maxRes) {
		VirtualFramebuffer *vfb = currentRenderVfb_;
		if (!vfb) {
			vfb = GetVFBAt(fb_address);
		}

		if (!vfb) {
			// If there's no vfb and we're drawing there, must be memory?
			buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, fb_format);
			return true;
		}
		LPDIRECT3DSURFACE9 renderTarget = vfb->fbo ? (LPDIRECT3DSURFACE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_COLOR_BIT | Draw::FB_SURFACE_BIT, 0) : nullptr;
		bool success = false;
		if (renderTarget) {
			Draw::Framebuffer *tempFBO = nullptr;
			int w = vfb->renderWidth, h = vfb->renderHeight;

			if (maxRes > 0 && vfb->renderWidth > vfb->width * maxRes) {
				// Let's resize.  We must stretch to a render target first.
				w = vfb->width * maxRes;
				h = vfb->height * maxRes;
				tempFBO = draw_->CreateFramebuffer({ w, h, 1, 1, false, Draw::FBO_8888 });
				if (draw_->BlitFramebuffer(vfb->fbo, 0, 0, vfb->renderWidth, vfb->renderHeight, tempFBO, 0, 0, w, h, Draw::FB_COLOR_BIT, g_Config.iBufFilter == SCALE_LINEAR ? Draw::FB_BLIT_LINEAR : Draw::FB_BLIT_NEAREST)) {
					renderTarget = (LPDIRECT3DSURFACE9)draw_->GetFramebufferAPITexture(tempFBO, Draw::FB_COLOR_BIT | Draw::FB_SURFACE_BIT, 0);
				}
			}

			LPDIRECT3DSURFACE9 offscreen = GetOffscreenSurface(renderTarget, vfb);
			if (offscreen) {
				success = GetRenderTargetFramebuffer(renderTarget, offscreen, w, h, buffer);
			}
		}

		return success;
	}

	bool FramebufferManagerDX9::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
		LPDIRECT3DSURFACE9 renderTarget = nullptr;
		HRESULT hr = device_->GetRenderTarget(0, &renderTarget);
		bool success = false;
		if (renderTarget && SUCCEEDED(hr)) {
			D3DSURFACE_DESC desc;
			renderTarget->GetDesc(&desc);

			LPDIRECT3DSURFACE9 offscreen = nullptr;
			HRESULT hr = device_->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &offscreen, NULL);
			if (offscreen && SUCCEEDED(hr)) {
				success = GetRenderTargetFramebuffer(renderTarget, offscreen, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, buffer);
				offscreen->Release();
			}
			renderTarget->Release();
		}
		return success;
	}

	bool FramebufferManagerDX9::GetRenderTargetFramebuffer(LPDIRECT3DSURFACE9 renderTarget, LPDIRECT3DSURFACE9 offscreen, int w, int h, GPUDebugBuffer &buffer) {
		D3DSURFACE_DESC desc;
		renderTarget->GetDesc(&desc);

		bool success = false;
		HRESULT hr = device_->GetRenderTargetData(renderTarget, offscreen);
		if (SUCCEEDED(hr)) {
			D3DLOCKED_RECT locked;
			RECT rect = {0, 0, w, h};
			hr = offscreen->LockRect(&locked, &rect, D3DLOCK_READONLY);
			if (SUCCEEDED(hr)) {
				// TODO: Handle the other formats?  We don't currently create them, I think.
				buffer.Allocate(locked.Pitch / 4, desc.Height, GPU_DBG_FORMAT_8888_BGRA, false);
				memcpy(buffer.GetData(), locked.pBits, locked.Pitch * desc.Height);
				offscreen->UnlockRect();
				success = true;
			}
		}

		return success;
	}

	bool FramebufferManagerDX9::GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) {
		VirtualFramebuffer *vfb = currentRenderVfb_;
		if (!vfb) {
			vfb = GetVFBAt(fb_address);
		}

		if (!vfb) {
			// If there's no vfb and we're drawing there, must be memory?
			buffer = GPUDebugBuffer(Memory::GetPointer(z_address | 0x04000000), z_stride, 512, GPU_DBG_FORMAT_16BIT);
			return true;
		}

		bool success = false;
		LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_DEPTH_BIT, 0);
		if (tex) {
			D3DSURFACE_DESC desc;
			D3DLOCKED_RECT locked;
			tex->GetLevelDesc(0, &desc);
			RECT rect = {0, 0, (LONG)desc.Width, (LONG)desc.Height};
			HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);

			if (SUCCEEDED(hr)) {
				GPUDebugBufferFormat fmt = GPU_DBG_FORMAT_24BIT_8X;
				if (gstate_c.Supports(GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT)) {
					fmt = GPU_DBG_FORMAT_24BIT_8X_DIV_256;
				}
				int pixelSize = 4;

				buffer.Allocate(locked.Pitch / pixelSize, desc.Height, fmt, false);
				memcpy(buffer.GetData(), locked.pBits, locked.Pitch * desc.Height);
				success = true;
				tex->UnlockRect(0);
			}
		}

		return success;
	}

	bool FramebufferManagerDX9::GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) {
		VirtualFramebuffer *vfb = currentRenderVfb_;
		if (!vfb) {
			vfb = GetVFBAt(fb_address);
		}

		if (!vfb) {
			// If there's no vfb and we're drawing there, must be memory?
			buffer = GPUDebugBuffer(Memory::GetPointer(vfb->z_address | 0x04000000), vfb->z_stride, 512, GPU_DBG_FORMAT_16BIT);
			return true;
		}

		bool success = false;
		LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_DEPTH_BIT, 0);
		if (tex) {
			D3DSURFACE_DESC desc;
			D3DLOCKED_RECT locked;
			tex->GetLevelDesc(0, &desc);
			RECT rect = {0, 0, (LONG)desc.Width, (LONG)desc.Height};
			HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);

			if (SUCCEEDED(hr)) {
				GPUDebugBufferFormat fmt = GPU_DBG_FORMAT_24X_8BIT;
				int pixelSize = 4;

				buffer.Allocate(locked.Pitch / pixelSize, desc.Height, fmt, false);
				memcpy(buffer.GetData(), locked.pBits, locked.Pitch * desc.Height);
				success = true;
				tex->UnlockRect(0);
			}
		}

		return success;
	}

}  // namespace DX9
