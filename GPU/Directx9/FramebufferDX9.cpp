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

#include "Common/ColorConv.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Debugger/Stepping.h"

#include "helper/dx_state.h"
#include "helper/fbo.h"

#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/TransformPipelineDX9.h"

#include <algorithm>

namespace DX9 {
	static void ConvertFromRGBA8888(u8 *dst, u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format);

	void FramebufferManagerDX9::ClearBuffer() {
		dxstate.scissorTest.disable();
		dxstate.depthWrite.set(TRUE);
		dxstate.colorMask.set(true, true, true, true);
		dxstate.stencilFunc.set(D3DCMP_ALWAYS, 0, 0);
		dxstate.stencilMask.set(0xFF);
		pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET |D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 0, 0);
	}

	void FramebufferManagerDX9::ClearDepthBuffer() {
		dxstate.scissorTest.disable();
		dxstate.depthWrite.set(TRUE);
		dxstate.colorMask.set(false, false, false, false);
		dxstate.stencilFunc.set(D3DCMP_NEVER, 0, 0);
		pD3Ddevice->Clear(0, NULL, D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 0, 0);
	}

	void FramebufferManagerDX9::DisableState() {
		dxstate.blend.disable();
		dxstate.cullMode.set(false, false);
		dxstate.depthTest.disable();
		dxstate.scissorTest.disable();
		dxstate.stencilTest.disable();
		dxstate.colorMask.set(true, true, true, true);
		dxstate.stencilMask.set(0xFF);
	}


	FramebufferManagerDX9::FramebufferManagerDX9() :
		drawPixelsTex_(0),
		convBuf(0),
		stencilUploadPS_(nullptr),
		stencilUploadVS_(nullptr),
		stencilUploadFailed_(false),
		gameUsesSequentialCopies_(false) {
	}

	FramebufferManagerDX9::~FramebufferManagerDX9() {
		if (drawPixelsTex_) {
			drawPixelsTex_->Release();
		}
		for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
			fbo_destroy(it->second.fbo);
		}
		for (auto it = offscreenSurfaces_.begin(), end = offscreenSurfaces_.end(); it != end; ++it) {
			it->second.surface->Release();
		}
		delete [] convBuf;
		if (stencilUploadPS_) {
			stencilUploadPS_->Release();
		}
		if (stencilUploadVS_) {
			stencilUploadVS_->Release();
		}
	}

	void FramebufferManagerDX9::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
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
			if (pD3DdeviceEx) {
				pool = D3DPOOL_DEFAULT;
				usage = D3DUSAGE_DYNAMIC;
			}
			HRESULT hr = pD3Ddevice->CreateTexture(width, height, 1, usage, D3DFMT(D3DFMT_A8R8G8B8), pool, &drawPixelsTex_, NULL);
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

		drawPixelsTex_->LockRect(0, &rect, NULL, 0);

		convBuf = (u8*)rect.pBits;

		// Final format is BGRA(directx)
		if (srcPixelFormat != GE_FORMAT_8888 || srcStride != 512) {
			for (int y = 0; y < height; y++) {
				switch (srcPixelFormat) {
				case GE_FORMAT_565:
					{
						const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
						u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
						ConvertBGR565ToRGBA8888(dst, src, width);
					}
					break;
					// faster
				case GE_FORMAT_5551:
					{
						const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
						u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
						ConvertBGRA5551ToRGBA8888(dst, src, width);
					}
					break;
				case GE_FORMAT_4444:
					{
						const u16_le *src = (const u16_le *)srcPixels + srcStride * y;
						u8 *dst = (u8 *)(convBuf + rect.Pitch * y);
						ConvertBGRA4444ToRGBA8888((u32 *)dst, src, width);
					}
					break;

				case GE_FORMAT_8888:
					{
						const u32_le *src = (const u32_le *)srcPixels + srcStride * y;
						u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
						ConvertBGRA8888ToRGBA8888(dst, src, width);
					}
					break;
				}
			}
		} else {
			for (int y = 0; y < height; y++) {
				const u32_le *src = (const u32_le *)srcPixels + srcStride * y;
				u32 *dst = (u32 *)(convBuf + rect.Pitch * y);
				ConvertBGRA8888ToRGBA8888(dst, src, width);
			}
		}

		drawPixelsTex_->UnlockRect(0);
		// D3DXSaveTextureToFile("game:\\cc.png", D3DXIFF_PNG, drawPixelsTex_, NULL);
	}

	void FramebufferManagerDX9::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
		if (useBufferedRendering_ && vfb->fbo) {
			fbo_bind_as_render_target(vfb->fbo);
		}
		dxstate.viewport.set(0, 0, vfb->renderWidth, vfb->renderHeight);
		MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height);
		DisableState();
		DrawActiveTexture(drawPixelsTex_, dstX, dstY, width, height, vfb->bufferWidth, vfb->bufferHeight, false, 0.0f, 0.0f, 1.0f, 1.0f);
		textureCache_->ForgetLastTexture();
	}

	void FramebufferManagerDX9::DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) {
		MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272);

		DisableState();

		// This might draw directly at the backbuffer (if so, applyPostShader is set) so if there's a post shader, we need to apply it here.
		// Should try to unify this path with the regular path somehow, but this simple solution works for most of the post shaders
		// (it always runs at output resolution so FXAA may look odd).
		float x, y, w, h;
		int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
		CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, uvRotation);
		DrawActiveTexture(drawPixelsTex_, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f, uvRotation);
	}

	void FramebufferManagerDX9::DrawActiveTexture(LPDIRECT3DTEXTURE9 tex, float x, float y, float w, float h, float destW, float destH, bool flip, float u0, float v0, float u1, float v1, int uvRotation) {
		if (flip) {
			std::swap(v0, v1);
		}

		// TODO: StretchRect instead?
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

		pD3Ddevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		pD3Ddevice->SetVertexDeclaration(pFramebufferVertexDecl);
		pD3Ddevice->SetPixelShader(pFramebufferPixelShader);
		pD3Ddevice->SetVertexShader(pFramebufferVertexShader);
		shaderManager_->DirtyLastShader();
		if (tex != NULL) {
			pD3Ddevice->SetTexture(0, tex);
		}
		HRESULT hr = pD3Ddevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, coord, 5 * sizeof(float));
		if (FAILED(hr)) {
			ERROR_LOG_REPORT(G3D, "DrawActiveTexture() failed: %08x", hr);
		}
	}

	void FramebufferManagerDX9::DestroyFramebuf(VirtualFramebuffer *v) {
		textureCache_->NotifyFramebuffer(v->fb_address, v, NOTIFY_FB_DESTROYED);
		if (v->fbo) {
			fbo_destroy(v->fbo);
			v->fbo = 0;
		}

		// Wipe some pointers
		if (currentRenderVfb_ == v)
			currentRenderVfb_ = 0;
		if (displayFramebuf_ == v)
			displayFramebuf_ = 0;
		if (prevDisplayFramebuf_ == v)
			prevDisplayFramebuf_ = 0;
		if (prevPrevDisplayFramebuf_ == v)
			prevPrevDisplayFramebuf_ = 0;

		delete v;
	}

	void FramebufferManagerDX9::RebindFramebuffer() {
		if (currentRenderVfb_ && currentRenderVfb_->fbo) {
			fbo_bind_as_render_target(currentRenderVfb_->fbo);
		} else {
			fbo_unbind();
		}
	}

	void FramebufferManagerDX9::ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force) {
		VirtualFramebuffer old = *vfb;

		if (force) {
			vfb->bufferWidth = w;
			vfb->bufferHeight = h;
		} else {
			if (vfb->bufferWidth >= w && vfb->bufferHeight >= h) {
				return;
			}

			// In case it gets thin and wide, don't resize down either side.
			vfb->bufferWidth = std::max(vfb->bufferWidth, w);
			vfb->bufferHeight = std::max(vfb->bufferHeight, h);
		}

		SetRenderSize(vfb);

		bool trueColor = g_Config.bTrueColor;
		if (hackForce04154000Download_ && vfb->fb_address == 0x00154000) {
			trueColor = true;
		}

		if (trueColor) {
			vfb->colorDepth = FBO_8888;
		} else {
			switch (vfb->format) {
			case GE_FORMAT_4444:
				vfb->colorDepth = FBO_4444;
				break;
			case GE_FORMAT_5551:
				vfb->colorDepth = FBO_5551;
				break;
			case GE_FORMAT_565:
				vfb->colorDepth = FBO_565;
				break;
			case GE_FORMAT_8888:
			default:
				vfb->colorDepth = FBO_8888;
				break;
			}
		}

		textureCache_->ForgetLastTexture();
		fbo_unbind();

		if (!useBufferedRendering_) {
			if (vfb->fbo) {
				fbo_destroy(vfb->fbo);
				vfb->fbo = 0;
			}
			return;
		}

		vfb->fbo = fbo_create(vfb->renderWidth, vfb->renderHeight, 1, true, (FBOColorDepth)vfb->colorDepth);
		if (old.fbo) {
			INFO_LOG(SCEGE, "Resizing FBO for %08x : %i x %i x %i", vfb->fb_address, w, h, vfb->format);
			if (vfb->fbo) {
				fbo_bind_as_render_target(vfb->fbo);
				ClearBuffer();
				if (!g_Config.bDisableSlowFramebufEffects) {
					BlitFramebuffer(vfb, 0, 0, &old, 0, 0, std::min(vfb->bufferWidth, vfb->width), std::min(vfb->height, vfb->bufferHeight), 0);
				}
			}
			fbo_destroy(old.fbo);
			if (vfb->fbo) {
				fbo_bind_as_render_target(vfb->fbo);
			}
		}

		if (!vfb->fbo) {
			ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", vfb->renderWidth, vfb->renderHeight);
		}
	}

	void FramebufferManagerDX9::NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) {
		if (!useBufferedRendering_) {
			fbo_unbind();
			// Let's ignore rendering to targets that have not (yet) been displayed.
			gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
		}

		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_CREATED);

		ClearBuffer();

		// ugly...
		if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
			shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		}
		if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
			shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		}
	}

	void FramebufferManagerDX9::NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb) {
		if (ShouldDownloadFramebuffer(vfb) && !vfb->memoryUpdated) {
			ReadFramebufferToMemory(vfb, true, 0, 0, vfb->width, vfb->height);
		}
		textureCache_->ForgetLastTexture();

		if (useBufferedRendering_) {
			if (vfb->fbo) {
				fbo_bind_as_render_target(vfb->fbo);
			} else {
				// wtf? This should only happen very briefly when toggling bBufferedRendering
				fbo_unbind();
			}
		} else {
			if (vfb->fbo) {
				// wtf? This should only happen very briefly when toggling bBufferedRendering
				textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_DESTROYED);
				fbo_destroy(vfb->fbo);
				vfb->fbo = 0;
			}
			fbo_unbind();

			// Let's ignore rendering to targets that have not (yet) been displayed.
			if (vfb->usageFlags & FB_USAGE_DISPLAYED_FRAMEBUFFER) {
				gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;
			} else {
				gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
			}
		}
		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);

		// Copy depth pixel value from the read framebuffer to the draw framebuffer
		if (prevVfb && !g_Config.bDisableSlowFramebufEffects) {
			BlitFramebufferDepth(prevVfb, vfb);
		}
		if (vfb->drawnFormat != vfb->format) {
			// TODO: Might ultimately combine this with the resize step in DoSetRenderFrameBuffer().
			ReformatFramebufferFrom(vfb, vfb->drawnFormat);
		}

		// ugly...
		if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
			shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		}
		if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
			shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		}
	}

	void FramebufferManagerDX9::NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) {
		if (vfbFormatChanged) {
			textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);
			if (vfb->drawnFormat != vfb->format) {
				ReformatFramebufferFrom(vfb, vfb->drawnFormat);
			}
		}

		// ugly...
		if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
			shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		}
		if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
			shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		}
	}

	void FramebufferManagerDX9::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
		if (!useBufferedRendering_ || !vfb->fbo) {
			return;
		}

		fbo_bind_as_render_target(vfb->fbo);

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

			float coord[20] = {
				-1.0f,-1.0f,0, 0,0,
				1.0f,-1.0f,0, 0,0,
				1.0f,1.0f,0, 0,0,
				-1.0f,1.0f,0, 0,0,
			};

			dxstate.cullMode.set(false, false);
			pD3Ddevice->SetVertexDeclaration(pFramebufferVertexDecl);
			pD3Ddevice->SetPixelShader(pFramebufferPixelShader);
			pD3Ddevice->SetVertexShader(pFramebufferVertexShader);
			shaderManager_->DirtyLastShader();
			pD3Ddevice->SetTexture(0, nullptr);

			D3DVIEWPORT9 vp;
			vp.MinZ = 0;
			vp.MaxZ = 1;
			vp.X = 0;
			vp.Y = 0;
			vp.Width = vfb->renderWidth;
			vp.Height = vfb->renderHeight;
			pD3Ddevice->SetViewport(&vp);

			// This should clear stencil and alpha without changing the other colors.
			HRESULT hr = pD3Ddevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, coord, 5 * sizeof(float));
			if (FAILED(hr)) {
				ERROR_LOG_REPORT(G3D, "ReformatFramebufferFrom() failed: %08x", hr);
			}
		}

		RebindFramebuffer();
	}

	void FramebufferManagerDX9::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
		if (!src->fbo || !dst->fbo || !useBufferedRendering_) {
			return;
		}

		// If depth wasn't updated, then we're at least "two degrees" away from the data.
		// This is an optimization: it probably doesn't need to be copied in this case.
		if (!src->depthUpdated) {
			return;
		}

		if (src->z_address == dst->z_address &&
			src->z_stride != 0 && dst->z_stride != 0 &&
			src->renderWidth == dst->renderWidth &&
			src->renderHeight == dst->renderHeight) {

			// Let's only do this if not clearing.
			if (!gstate.isModeClear() || !gstate.isClearModeDepthMask()) {
				// Doesn't work.  Use a shader maybe?
				/*fbo_unbind();

				LPDIRECT3DTEXTURE9 srcTex = fbo_get_depth_texture(src->fbo);
				LPDIRECT3DTEXTURE9 dstTex = fbo_get_depth_texture(dst->fbo);

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
						int pitch = std::min(srcLock.Pitch, dstLock.Pitch);
						u32 h = std::min(srcDesc.Height, dstDesc.Height);
						const u8 *srcp = (const u8 *)srcLock.pBits;
						u8 *dstp = (u8 *)dstLock.pBits;
						for (u32 y = 0; y < h; ++y) {
							memcpy(dstp, srcp, pitch);
							dstp += dstLock.Pitch;
							srcp += srcLock.Pitch;
						}
					}
					if (SUCCEEDED(srcLockRes)) {
						srcTex->UnlockRect(0);
					}
					if (SUCCEEDED(dstLockRes)) {
						dstTex->UnlockRect(0);
					}
				}

				RebindFramebuffer();*/
			}
		}
	}

	FBO *FramebufferManagerDX9::GetTempFBO(u16 w, u16 h, FBOColorDepth depth) {
		u64 key = ((u64)depth << 32) | ((u32)w << 16) | h;
		auto it = tempFBOs_.find(key);
		if (it != tempFBOs_.end()) {
			it->second.last_frame_used = gpuStats.numFlips;
			return it->second.fbo;
		}

		textureCache_->ForgetLastTexture();
		FBO *fbo = fbo_create(w, h, 1, false, depth);
		if (!fbo)
			return fbo;
		fbo_bind_as_render_target(fbo);
		ClearBuffer();
		const TempFBO info = {fbo, gpuStats.numFlips};
		tempFBOs_[key] = info;
		return fbo;
	}

	LPDIRECT3DSURFACE9 FramebufferManagerDX9::GetOffscreenSurface(LPDIRECT3DSURFACE9 similarSurface) {
		D3DSURFACE_DESC desc;
		similarSurface->GetDesc(&desc);

		u64 key = ((u64)desc.Format << 32) | (desc.Width << 16) | desc.Height;
		auto it = offscreenSurfaces_.find(key);
		if (it != offscreenSurfaces_.end()) {
			it->second.last_frame_used = gpuStats.numFlips;
			return it->second.surface;
		}

		textureCache_->ForgetLastTexture();
		LPDIRECT3DSURFACE9 offscreen = nullptr;
		HRESULT hr = pD3Ddevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &offscreen, NULL);
		if (FAILED(hr) || !offscreen) {
			ERROR_LOG_REPORT(G3D, "Unable to create offscreen surface %dx%d @%d", desc.Width, desc.Height, desc.Format);
			return nullptr;
		}
		const OffscreenSurface info = {offscreen, gpuStats.numFlips};
		offscreenSurfaces_[key] = info;
		return offscreen;
	}

	void FramebufferManagerDX9::BindFramebufferColor(int stage, VirtualFramebuffer *framebuffer, bool skipCopy) {
		if (framebuffer == NULL) {
			framebuffer = currentRenderVfb_;
		}

		if (!framebuffer->fbo || !useBufferedRendering_) {
			pD3Ddevice->SetTexture(stage, nullptr);
			gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
			return;
		}

		// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
		// Let's just not bother with the copy in that case.
		if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
			skipCopy = true;
		}
		if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == gstate.getFrameBufRawAddress()) {
			// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
			FBO *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (FBOColorDepth)framebuffer->colorDepth);
			if (renderCopy) {
				VirtualFramebuffer copyInfo = *framebuffer;
				copyInfo.fbo = renderCopy;
				BlitFramebuffer(&copyInfo, 0, 0, framebuffer, 0, 0, framebuffer->drawnWidth, framebuffer->drawnHeight, 0, false);

				RebindFramebuffer();
				pD3Ddevice->SetTexture(stage, fbo_get_color_texture(renderCopy));
			} else {
				pD3Ddevice->SetTexture(stage, fbo_get_color_texture(framebuffer->fbo));
			}
		} else {
			pD3Ddevice->SetTexture(stage, fbo_get_color_texture(framebuffer->fbo));
		}
	}

	void FramebufferManagerDX9::CopyDisplayToOutput() {

		fbo_unbind();
		dxstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		currentRenderVfb_ = 0;

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
					if (v_offsetX + 480 > (u32)v->fb_stride || v->bufferHeight < v_offsetY + 272) {
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
					DrawFramebuffer(Memory::GetPointer(displayFramebufPtr_), displayFormat_, displayStride_, true);
					return;
				}
			} else {
				DEBUG_LOG(SCEGE, "Found no FBO to display! displayFBPtr = %08x", displayFramebufPtr_);
				// No framebuffer to display! Clear to black.
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

		if (resized_) {
			ClearBuffer();
		}

		if (vfb->fbo) {
			DEBUG_LOG(SCEGE, "Displaying FBO %08x", vfb->fb_address);
			DisableState();
			LPDIRECT3DTEXTURE9 colorTexture = fbo_get_color_texture(vfb->fbo);

			// Output coordinates
			float x, y, w, h;
			int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
			CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, uvRotation);

			const float u0 = offsetX / (float)vfb->bufferWidth;
			const float v0 = offsetY / (float)vfb->bufferHeight;
			const float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
			const float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

			if (1) {
				const u32 rw = PSP_CoreParameter().pixelWidth;
				const u32 rh = PSP_CoreParameter().pixelHeight;
				const RECT srcRect = {(LONG)(u0 * vfb->renderWidth), (LONG)(v0 * vfb->renderHeight), (LONG)(u1 * vfb->renderWidth), (LONG)(v1 * vfb->renderHeight)};
				const RECT dstRect = {x * rw / w, y * rh / h, (x + w) * rw / w, (y + h) * rh / h};
				HRESULT hr = fbo_blit_color(vfb->fbo, &srcRect, nullptr, &dstRect, g_Config.iBufFilter == SCALE_LINEAR ? D3DTEXF_LINEAR : D3DTEXF_POINT);
				if (FAILED(hr)) {
					ERROR_LOG_REPORT_ONCE(blit_fail, G3D, "fbo_blit_color failed on display: %08x", hr);
					dxstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
					// These are in the output display coordinates
					if (g_Config.iBufFilter == SCALE_LINEAR) {
						dxstate.texMagFilter.set(D3DTEXF_LINEAR);
						dxstate.texMinFilter.set(D3DTEXF_LINEAR);
					} else {
						dxstate.texMagFilter.set(D3DTEXF_POINT);
						dxstate.texMinFilter.set(D3DTEXF_POINT);
					}
					dxstate.texMipFilter.set(D3DTEXF_NONE);
					dxstate.texMipLodBias.set(0);
					DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, u0, v0, u1, v1, uvRotation);
				}
			}
			/* 
			else if (usePostShader_ && extraFBOs_.size() == 1 && !postShaderAtOutputResolution_) {
			// An additional pass, post-processing shader to the extra FBO.
			fbo_bind_as_render_target(extraFBOs_[0]);
			int fbo_w, fbo_h;
			fbo_get_dimensions(extraFBOs_[0], &fbo_w, &fbo_h);
			glstate.viewport.set(0, 0, fbo_w, fbo_h);
			DrawActiveTexture(colorTexture, 0, 0, fbo_w, fbo_h, fbo_w, fbo_h, true, 1.0f, 1.0f, postShaderProgram_);

			fbo_unbind();

			// Use the extra FBO, with applied post-processing shader, as a texture.
			// fbo_bind_color_as_texture(extraFBOs_[0], 0);
			if (extraFBOs_.size() == 0) {
			ERROR_LOG(G3D, "WTF?");
			return;
			}
			colorTexture = fbo_get_color_texture(extraFBOs_[0]);
			glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			// These are in the output display coordinates
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height);
			} else {
			// Use post-shader, but run shader at output resolution.
			glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			// These are in the output display coordinates
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height, postShaderProgram_);
			}
			*/
			pD3Ddevice->SetTexture(0, NULL);
		}
	}

	void FramebufferManagerDX9::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {
#if 0
		if (sync) {
			PackFramebufferAsync_(NULL); // flush async just in case when we go for synchronous update
		}
#endif

		if (vfb) {
			// We'll pseudo-blit framebuffers here to get a resized and flipped version of vfb.
			// For now we'll keep these on the same struct as the ones that can get displayed
			// (and blatantly copy work already done above while at it).
			VirtualFramebuffer *nvfb = 0;

			// We maintain a separate vector of framebuffer objects for blitting.
			for (size_t i = 0; i < bvfbs_.size(); ++i) {
				VirtualFramebuffer *v = bvfbs_[i];
				if (v->fb_address == vfb->fb_address && v->format == vfb->format) {
					if (v->bufferWidth == vfb->bufferWidth && v->bufferHeight == vfb->bufferHeight) {
						nvfb = v;
						v->fb_stride = vfb->fb_stride;
						v->width = vfb->width;
						v->height = vfb->height;
						break;
					}
				}
			}

			// Create a new fbo if none was found for the size
			if(!nvfb) {
				nvfb = new VirtualFramebuffer();
				nvfb->fbo = 0;
				nvfb->fb_address = vfb->fb_address;
				nvfb->fb_stride = vfb->fb_stride;
				nvfb->z_address = vfb->z_address;
				nvfb->z_stride = vfb->z_stride;
				nvfb->width = vfb->width;
				nvfb->height = vfb->height;
				nvfb->renderWidth = vfb->bufferWidth;
				nvfb->renderHeight = vfb->bufferHeight;
				nvfb->bufferWidth = vfb->bufferWidth;
				nvfb->bufferHeight = vfb->bufferHeight;
				nvfb->format = vfb->format;
				nvfb->drawnWidth = vfb->drawnWidth;
				nvfb->drawnHeight = vfb->drawnHeight;
				nvfb->drawnFormat = vfb->format;
				nvfb->usageFlags = FB_USAGE_RENDERTARGET;
				nvfb->dirtyAfterDisplay = true;

				nvfb->colorDepth = FBO_8888;

				textureCache_->ForgetLastTexture();
				nvfb->fbo = fbo_create(nvfb->width, nvfb->height, 1, true, (FBOColorDepth)nvfb->colorDepth);
				if (!(nvfb->fbo)) {
					ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
					delete nvfb;
					return;
				}

				nvfb->last_frame_render = gpuStats.numFlips;
				bvfbs_.push_back(nvfb);
				fbo_bind_as_render_target(nvfb->fbo);
				ClearBuffer();
			} else {
				nvfb->usageFlags |= FB_USAGE_RENDERTARGET;
				gstate_c.textureChanged = true;
				nvfb->last_frame_render = gpuStats.numFlips;
				nvfb->dirtyAfterDisplay = true;

#if 0
				if (nvfb->fbo) {
					fbo_bind_as_render_target(nvfb->fbo);
				}

				// Some tiled mobile GPUs benefit IMMENSELY from clearing an FBO before rendering
				// to it. This broke stuff before, so now it only clears on the first use of an
				// FBO in a frame. This means that some games won't be able to avoid the on-some-GPUs
				// performance-crushing framebuffer reloads from RAM, but we'll have to live with that.
				if (nvfb->last_frame_render != gpuStats.numFlips)	{
					ClearBuffer();
				}
#endif
			}

			if (gameUsesSequentialCopies_) {
				// Ignore the x/y/etc., read the entire thing.
				x = 0;
				y = 0;
				w = vfb->width;
				h = vfb->height;
			}
			if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
				vfb->memoryUpdated = true;
			} else {
				const static int FREQUENT_SEQUENTIAL_COPIES = 3;
				static int frameLastCopy = 0;
				static u32 bufferLastCopy = 0;
				static int copiesThisFrame = 0;
				if (frameLastCopy != gpuStats.numFlips || bufferLastCopy != vfb->fb_address) {
					frameLastCopy = gpuStats.numFlips;
					bufferLastCopy = vfb->fb_address;
					copiesThisFrame = 0;
				}
				if (++copiesThisFrame > FREQUENT_SEQUENTIAL_COPIES) {
					gameUsesSequentialCopies_ = true;
				}
			}
			BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0, false);

			PackFramebufferDirectx9_(nvfb, x, y, w, h);
			RebindFramebuffer();
		}
	}

	void FramebufferManagerDX9::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, bool flip) {
		if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
			// This can happen if they recently switched from non-buffered.
			fbo_unbind();
			return;
		}

		float srcXFactor = flip ? 1.0f : (float)src->renderWidth / (float)src->bufferWidth;
		float srcYFactor = flip ? 1.0f : (float)src->renderHeight / (float)src->bufferHeight;
		const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
		if (srcBpp != bpp && bpp != 0) {
			srcXFactor = (srcXFactor * bpp) / srcBpp;
		}
		int srcX1 = srcX * srcXFactor;
		int srcX2 = (srcX + w) * srcXFactor;
		int srcY1 = srcY * srcYFactor;
		int srcY2 = (srcY + h) * srcYFactor;

		float dstXFactor = flip ? 1.0f : (float)dst->renderWidth / (float)dst->bufferWidth;
		float dstYFactor = flip ? 1.0f : (float)dst->renderHeight / (float)dst->bufferHeight;
		const int dstBpp = dst->format == GE_FORMAT_8888 ? 4 : 2;
		if (dstBpp != bpp && bpp != 0) {
			dstXFactor = (dstXFactor * bpp) / dstBpp;
		}
		int dstX1 = dstX * dstXFactor;
		int dstX2 = (dstX + w) * dstXFactor;
		int dstY1 = dstY * dstYFactor;
		int dstY2 = (dstY + h) * dstYFactor;

		if (flip) {
			fbo_bind_as_render_target(dst->fbo);
			dxstate.viewport.set(0, 0, dst->renderWidth, dst->renderHeight);
			DisableState();

			fbo_bind_color_as_texture(src->fbo, 0);

			float srcW = src->bufferWidth;
			float srcH = src->bufferHeight;
			DrawActiveTexture(0, dstX1, dstY, w * dstXFactor, h, dst->bufferWidth, dst->bufferHeight, flip, srcX1 / srcW, srcY / srcH, srcX2 / srcW, (srcY + h) / srcH);
			pD3Ddevice->SetTexture(0, NULL);
			textureCache_->ForgetLastTexture();
			dxstate.viewport.restore();

			RebindFramebuffer();
		} else {
			LPDIRECT3DSURFACE9 srcSurf = fbo_get_color_for_read(src->fbo);
			LPDIRECT3DSURFACE9 dstSurf = fbo_get_color_for_write(dst->fbo);
			RECT srcRect = {srcX1, srcY1, srcX2, srcY2};
			RECT dstRect = {dstX1, dstY1, dstX2, dstY2};

			D3DSURFACE_DESC desc;
			srcSurf->GetDesc(&desc);
			srcRect.right = std::min(srcRect.right, (LONG)desc.Width);
			srcRect.bottom = std::min(srcRect.bottom, (LONG)desc.Height);

			dstSurf->GetDesc(&desc);
			dstRect.right = std::min(dstRect.right, (LONG)desc.Width);
			dstRect.bottom = std::min(dstRect.bottom, (LONG)desc.Height);

			// Direct3D 9 doesn't support rect -> self.
			FBO *srcFBO = src->fbo;
			if (src == dst) {
				FBO *tempFBO = GetTempFBO(src->renderWidth, src->renderHeight, (FBOColorDepth)src->colorDepth);
				HRESULT hr = fbo_blit_color(src->fbo, &srcRect, tempFBO, &srcRect, D3DTEXF_POINT);
				if (SUCCEEDED(hr)) {
					srcFBO = tempFBO;
				}
			}

			HRESULT hr = fbo_blit_color(srcFBO, &srcRect, dst->fbo, &dstRect, D3DTEXF_POINT);
			if (FAILED(hr)) {
				ERROR_LOG_REPORT(G3D, "fbo_blit_color failed in blit: %08x (%08x -> %08x)", hr, src->fb_address, dst->fb_address);
			}
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

	void FramebufferManagerDX9::PackFramebufferDirectx9_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
		if (!vfb->fbo) {
			ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferDirectx9_: vfb->fbo == 0");
			fbo_unbind();
			return;
		}

		const u32 fb_address = (0x04000000) | vfb->fb_address;
		const int dstBpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;

		// We always need to convert from the framebuffer native format.
		// Right now that's always 8888.
		DEBUG_LOG(HLE, "Reading framebuffer to mem, fb_address = %08x", fb_address);

		LPDIRECT3DSURFACE9 renderTarget = fbo_get_color_for_read(vfb->fbo);
		D3DSURFACE_DESC desc;
		renderTarget->GetDesc(&desc);

		LPDIRECT3DSURFACE9 offscreen = GetOffscreenSurface(renderTarget);
		if (offscreen) {
			HRESULT hr = pD3Ddevice->GetRenderTargetData(renderTarget, offscreen);
			if (SUCCEEDED(hr)) {
				D3DLOCKED_RECT locked;
				u32 widthFactor = vfb->renderWidth / vfb->bufferWidth;
				u32 heightFactor = vfb->renderHeight / vfb->bufferHeight;
				RECT rect = {x * widthFactor, y * heightFactor, (x + w) * widthFactor, (y + h) * heightFactor};
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
	}

	void FramebufferManagerDX9::EndFrame() {
		if (resized_) {
			DestroyAllFBOs();
			dxstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			resized_ = false;
		}
#if 0
		// We flush to memory last requested framebuffer, if any
		PackFramebufferAsync_(NULL);
#endif
	}

	void FramebufferManagerDX9::DeviceLost() {
		DestroyAllFBOs();
		resized_ = false;
	}

	std::vector<FramebufferInfo> FramebufferManagerDX9::GetFramebufferList() {
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

	void FramebufferManagerDX9::DecimateFBOs() {
		fbo_unbind();
		currentRenderVfb_ = 0;
		bool updateVram = !(g_Config.iRenderingMode == FB_NON_BUFFERED_MODE || g_Config.iRenderingMode == FB_BUFFERED_MODE);

		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *vfb = vfbs_[i];
			int age = frameLastFramebufUsed_ - std::max(vfb->last_frame_render, vfb->last_frame_used);

			if (ShouldDownloadFramebuffer(vfb) && age == 0 && !vfb->memoryUpdated) {
				ReadFramebufferToMemory(vfb, false, 0, 0, vfb->width, vfb->height);
			}


			// Let's also "decimate" the usageFlags.
			UpdateFramebufUsage(vfb);

			if (vfb != displayFramebuf_ && vfb != prevDisplayFramebuf_ && vfb != prevPrevDisplayFramebuf_) {
				if (age > FBO_OLD_AGE) {
					INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age);
					DestroyFramebuf(vfb);
					vfbs_.erase(vfbs_.begin() + i--);
				}
			}
		}

		for (auto it = tempFBOs_.begin(); it != tempFBOs_.end(); ) {
			int age = frameLastFramebufUsed_ - it->second.last_frame_used;
			if (age > FBO_OLD_AGE) {
				fbo_destroy(it->second.fbo);
				tempFBOs_.erase(it++);
			} else {
				++it;
			}
		}

		for (auto it = offscreenSurfaces_.begin(); it != offscreenSurfaces_.end(); ) {
			int age = frameLastFramebufUsed_ - it->second.last_frame_used;
			if (age > FBO_OLD_AGE) {
				it->second.surface->Release();
				offscreenSurfaces_.erase(it++);
			} else {
				++it;
			}
		}

		// Do the same for ReadFramebuffersToMemory's VFBs
		for (size_t i = 0; i < bvfbs_.size(); ++i) {
			VirtualFramebuffer *vfb = bvfbs_[i];
			int age = frameLastFramebufUsed_ - vfb->last_frame_render;
			if (age > FBO_OLD_AGE) {
				INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age);
				DestroyFramebuf(vfb);
				bvfbs_.erase(bvfbs_.begin() + i--);
			}
		}
	}

	void FramebufferManagerDX9::DestroyAllFBOs() {
		fbo_unbind();
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
			fbo_destroy(it->second.fbo);
		}
		tempFBOs_.clear();

		for (auto it = offscreenSurfaces_.begin(), end = offscreenSurfaces_.end(); it != end; ++it) {
			it->second.surface->Release();
		}
		offscreenSurfaces_.clear();
		DisableState();
	}

	void FramebufferManagerDX9::FlushBeforeCopy() {
		// Flush anything not yet drawn before blitting, downloading, or uploading.
		// This might be a stalled list, or unflushed before a block transfer, etc.
		SetRenderFrameBuffer();
		transformDraw_->Flush();
	}

	void FramebufferManagerDX9::Resized() {
		resized_ = true;
	}

	bool FramebufferManagerDX9::GetCurrentFramebuffer(GPUDebugBuffer &buffer) {
		u32 fb_address = gstate.getFrameBufRawAddress();
		int fb_stride = gstate.FrameBufStride();

		VirtualFramebuffer *vfb = currentRenderVfb_;
		if (!vfb) {
			vfb = GetVFBAt(fb_address);
		}

		if (!vfb) {
			// If there's no vfb and we're drawing there, must be memory?
			buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, gstate.FrameBufFormat());
			return true;
		}

		LPDIRECT3DSURFACE9 renderTarget = vfb->fbo ? fbo_get_color_for_read(vfb->fbo) : nullptr;
		bool success = false;
		if (renderTarget) {
			LPDIRECT3DSURFACE9 offscreen = GetOffscreenSurface(renderTarget);
			if (offscreen) {
				success = GetRenderTargetFramebuffer(renderTarget, offscreen, vfb->renderWidth, vfb->renderHeight, buffer);
			}
		}

		return success;
	}

	bool FramebufferManagerDX9::GetDisplayFramebuffer(GPUDebugBuffer &buffer) {
		fbo_unbind();

		LPDIRECT3DSURFACE9 renderTarget = nullptr;
		HRESULT hr = pD3Ddevice->GetRenderTarget(0, &renderTarget);
		bool success = false;
		if (renderTarget && SUCCEEDED(hr)) {
			D3DSURFACE_DESC desc;
			renderTarget->GetDesc(&desc);

			LPDIRECT3DSURFACE9 offscreen = nullptr;
			HRESULT hr = pD3Ddevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &offscreen, NULL);
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
		HRESULT hr = pD3Ddevice->GetRenderTargetData(renderTarget, offscreen);
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

	bool FramebufferManagerDX9::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
		u32 fb_address = gstate.getFrameBufRawAddress();
		int fb_stride = gstate.FrameBufStride();

		u32 z_address = gstate.getDepthBufRawAddress();
		int z_stride = gstate.DepthBufStride();

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
		LPDIRECT3DTEXTURE9 tex = fbo_get_depth_texture(vfb->fbo);
		if (tex) {
			D3DSURFACE_DESC desc;
			D3DLOCKED_RECT locked;
			tex->GetLevelDesc(0, &desc);
			RECT rect = {0, 0, desc.Width, desc.Height};
			HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);

			if (SUCCEEDED(hr)) {
				GPUDebugBufferFormat fmt = GPU_DBG_FORMAT_24BIT_8X;
				int pixelSize = 4;

				buffer.Allocate(locked.Pitch / pixelSize, desc.Height, fmt, gstate_c.flipTexture);
				memcpy(buffer.GetData(), locked.pBits, locked.Pitch * desc.Height);
				success = true;
				tex->UnlockRect(0);
			}
		}

		return success;
	}

	bool FramebufferManagerDX9::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
		u32 fb_address = gstate.getFrameBufRawAddress();
		int fb_stride = gstate.FrameBufStride();

		u32 z_address = gstate.getDepthBufRawAddress();
		int z_stride = gstate.DepthBufStride();

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
		LPDIRECT3DTEXTURE9 tex = fbo_get_depth_texture(vfb->fbo);
		if (tex) {
			D3DSURFACE_DESC desc;
			D3DLOCKED_RECT locked;
			tex->GetLevelDesc(0, &desc);
			RECT rect = {0, 0, desc.Width, desc.Height};
			HRESULT hr = tex->LockRect(0, &locked, &rect, D3DLOCK_READONLY);

			if (SUCCEEDED(hr)) {
				GPUDebugBufferFormat fmt = GPU_DBG_FORMAT_24X_8BIT;
				int pixelSize = 4;

				buffer.Allocate(locked.Pitch / pixelSize, desc.Height, fmt, gstate_c.flipTexture);
				memcpy(buffer.GetData(), locked.pBits, locked.Pitch * desc.Height);
				success = true;
				tex->UnlockRect(0);
			}
		}

		return success;
	}

}  // namespace DX9
