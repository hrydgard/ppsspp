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

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"

#include "Common/GPU/D3D9/D3D9StateCache.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Directx9/FramebufferManagerDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"

#include "Common/GPU/thin3d.h"

#include <algorithm>

#ifdef _M_SSE
#include <emmintrin.h>
#endif

namespace DX9 {

static const char *vscode = R"(
struct VS_IN {
	float4 ObjPos   : POSITION;
	float2 Uv    : TEXCOORD0;
};
struct VS_OUT {
	float4 ProjPos  : POSITION;
	float2 Uv    : TEXCOORD0;
};
VS_OUT main( VS_IN In ) {
	VS_OUT Out;
	Out.ProjPos = In.ObjPos;
	Out.Uv = In.Uv;
	return Out;
}
)";

static const char *pscode = R"(
sampler s: register(s0);
struct PS_IN {
	float2 Uv : TEXCOORD0;
};
float4 main( PS_IN In ) : COLOR {
	float4 c =  tex2D(s, In.Uv);
	return c;
}
)";

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
		if (!CompileVertexShaderD3D9(device_, vscode, &pFramebufferVertexShader, &errorMsg)) {
			OutputDebugStringA(errorMsg.c_str());
		}

		if (!CompilePixelShaderD3D9(device_, pscode, &pFramebufferPixelShader, &errorMsg)) {
			OutputDebugStringA(errorMsg.c_str());
			if (pFramebufferVertexShader) {
				pFramebufferVertexShader->Release();
			}
		}

		device_->CreateVertexDeclaration(g_FramebufferVertexElements, &pFramebufferVertexDecl);


		int usage = 0;
		D3DPOOL pool = D3DPOOL_MANAGED;
		if (deviceEx_) {
			pool = D3DPOOL_DEFAULT;
			usage = D3DUSAGE_DYNAMIC;
		}
		HRESULT hr = device_->CreateTexture(1, 1, 1, usage, D3DFMT_A8R8G8B8, pool, &nullTex_, nullptr);
		D3DLOCKED_RECT rect;
		nullTex_->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD);
		memset(rect.pBits, 0, 4);
		nullTex_->UnlockRect(0);

		presentation_->SetLanguage(HLSL_D3D9);
		preferredPixelsFormat_ = Draw::DataFormat::B8G8R8A8_UNORM;
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
		for (auto &it : offscreenSurfaces_) {
			it.second.surface->Release();
		}
		if (stencilUploadPS_) {
			stencilUploadPS_->Release();
		}
		if (stencilUploadVS_) {
			stencilUploadVS_->Release();
		}
		if (nullTex_)
			nullTex_->Release();
	}

	void FramebufferManagerDX9::SetTextureCache(TextureCacheDX9 *tc) {
		textureCache_ = tc;
	}

	void FramebufferManagerDX9::SetShaderManager(ShaderManagerDX9 *sm) {
		shaderManager_ = sm;
	}

	void FramebufferManagerDX9::SetDrawEngine(DrawEngineDX9 *td) {
		drawEngine_ = td;
	}

	void FramebufferManagerDX9::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) {
		// TODO: StretchRect instead when possible?
		float coord[20] = {
			x,y,0, u0,v0,
			x+w,y,0, u1,v0,
			x+w,y+h,0, u1,v1,
			x,y+h,0, u0,v1,
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
			ERROR_LOG_REPORT(G3D, "DrawActiveTexture() failed: %08x", (uint32_t)hr);
		}
	}

	void FramebufferManagerDX9::Bind2DShader() {
		device_->SetVertexDeclaration(pFramebufferVertexDecl);
		device_->SetPixelShader(pFramebufferPixelShader);
		device_->SetVertexShader(pFramebufferVertexShader);
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

		textureCache_->ForgetLastTexture();
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

	void FramebufferManagerDX9::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, const char *tag) {
		if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
			// This can happen if we recently switched from non-buffered.
			if (useBufferedRendering_)
				draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "BlitFramebuffer_Fail");
			return;
		}

		float srcXFactor = (float)src->renderScaleFactor;
		float srcYFactor = (float)src->renderScaleFactor;
		const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
		if (srcBpp != bpp && bpp != 0) {
			srcXFactor = (srcXFactor * bpp) / srcBpp;
		}
		int srcX1 = srcX * srcXFactor;
		int srcX2 = (srcX + w) * srcXFactor;
		int srcY1 = srcY * srcYFactor;
		int srcY2 = (srcY + h) * srcYFactor;

		float dstXFactor = (float)dst->renderScaleFactor;
		float dstYFactor = (float)dst->renderScaleFactor;
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
			Draw::Framebuffer *tempFBO = GetTempFBO(TempFBO::BLIT, src->renderWidth, src->renderHeight);
			bool result = draw_->BlitFramebuffer(
				src->fbo, srcX1, srcY1, srcX2, srcY2,
				tempFBO, dstX1, dstY1, dstX2, dstY2,
				Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST, tag);
			if (result) {
				srcFBO = tempFBO;
			}
		}
		bool result = draw_->BlitFramebuffer(
			srcFBO, srcX1, srcY1, srcX2, srcY2,
			dst->fbo, dstX1, dstY1, dstX2, dstY2,
			Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST, tag);
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

		const u32 fb_address = vfb->fb_address & 0x3FFFFFFF;
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
					ConvertFromBGRA8888(Memory::GetPointerWrite(fb_address + dstByteOffset), (u8 *)locked.pBits, vfb->fb_stride, locked.Pitch / 4, w, h, vfb->format);
					offscreen->UnlockRect();
				} else {
					ERROR_LOG_REPORT(G3D, "Unable to lock rect from %08x: %d,%d %dx%d of %dx%d", fb_address, (int)rect.left, (int)rect.top, (int)rect.right, (int)rect.bottom, vfb->renderWidth, vfb->renderHeight);
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
		const u32 z_address = vfb->z_address;

		DEBUG_LOG(FRAMEBUF, "Reading depthbuffer to mem at %08x for vfb=%08x", z_address, vfb->fb_address);

		LPDIRECT3DTEXTURE9 tex = (LPDIRECT3DTEXTURE9)draw_->GetFramebufferAPITexture(vfb->fbo, Draw::FB_DEPTH_BIT, 0);
		if (tex) {
			D3DSURFACE_DESC desc;
			D3DLOCKED_RECT locked;
			tex->GetLevelDesc(0, &desc);
			RECT rect = {0, 0, (LONG)desc.Width, (LONG)desc.Height};
			HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);

			if (SUCCEEDED(hr)) {
				const u32 *packed = (const u32 *)locked.pBits;
				u16 *depth = (u16 *)Memory::GetPointer(z_address);

				DepthScaleFactors depthScale = GetDepthScaleFactors();
				// TODO: Optimize.
				for (int yp = 0; yp < h; ++yp) {
					for (int xp = 0; xp < w; ++xp) {
						const int offset = (yp + y) * vfb->z_stride + x + xp;

						float scaled = depthScale.Apply((packed[offset] & 0x00FFFFFF) * (1.0f / 16777215.0f));
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
				ERROR_LOG_REPORT(G3D, "Unable to lock rect from depth %08x: %d,%d %dx%d of %dx%d", vfb->fb_address, (int)rect.left, (int)rect.top, (int)rect.right, (int)rect.bottom, vfb->renderWidth, vfb->renderHeight);
			}
		} else {
			ERROR_LOG_REPORT(G3D, "Unable to download render target depth from %08x", vfb->fb_address);
		}
	}

	void FramebufferManagerDX9::EndFrame() {
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
		FramebufferManagerCommon::DestroyAllFBOs();

		for (auto &it : offscreenSurfaces_) {
			it.second.surface->Release();
		}
		offscreenSurfaces_.clear();
	}

	bool FramebufferManagerDX9::GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat fb_format, GPUDebugBuffer &buffer, int maxRes) {
		VirtualFramebuffer *vfb = currentRenderVfb_;
		if (!vfb) {
			vfb = GetVFBAt(fb_address);
		}

		if (!vfb) {
			if (!Memory::IsValidAddress(fb_address))
				return false;
			// If there's no vfb and we're drawing there, must be memory?
			buffer = GPUDebugBuffer(Memory::GetPointerWrite(fb_address), fb_stride, 512, fb_format);
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
				tempFBO = draw_->CreateFramebuffer({ w, h, 1, 1, false });
				if (draw_->BlitFramebuffer(vfb->fbo, 0, 0, vfb->renderWidth, vfb->renderHeight, tempFBO, 0, 0, w, h, Draw::FB_COLOR_BIT, g_Config.iBufFilter == SCALE_LINEAR ? Draw::FB_BLIT_LINEAR : Draw::FB_BLIT_NEAREST, "GetFramebuffer")) {
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
			buffer = GPUDebugBuffer(Memory::GetPointerWrite(z_address), z_stride, 512, GPU_DBG_FORMAT_16BIT);
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
			buffer = GPUDebugBuffer(Memory::GetPointerWrite(vfb->z_address), vfb->z_stride, 512, GPU_DBG_FORMAT_16BIT);
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
