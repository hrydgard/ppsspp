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
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#include "helper/dx_state.h"
#include "helper/fbo.h"

#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"

#include <algorithm>

namespace DX9 {

// Aggressively delete unused FBO:s to save gpu memory.
enum {
	FBO_OLD_AGE = 5,
};

static bool MaskedEqual(u32 addr1, u32 addr2) {
	return (addr1 & 0x03FFFFFF) == (addr2 & 0x03FFFFFF);
}

inline u16 RGBA8888toRGB565(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 5) & 0x07E0) | ((px >> 8) & 0xF800);
}

inline u16 RGBA8888toRGBA4444(u32 px) {
	return ((px >> 4) & 0x000F) | ((px >> 8) & 0x00F0) | ((px >> 12) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u16 RGBA8888toRGBA5551(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 6) & 0x03E0) | ((px >> 9) & 0x7C00) | ((px >> 16) & 0x8000);
}

static void ConvertFromRGBA8888(u8 *dst, u8 *src, u32 stride, u32 height, GEBufferFormat format);

void CenterRect(float *x, float *y, float *w, float *h,
	float origW, float origH, float frameW, float frameH)
{
	if (g_Config.bStretchToDisplay)
	{
		*x = 0;
		*y = 0;
		*w = frameW;
		*h = frameH;
		return;
	}

	float origRatio = origW/origH;
	float frameRatio = frameW/frameH;

	if (origRatio > frameRatio)
	{
		// Image is wider than frame. Center vertically.
		float scale = origW / frameW;
		*x = 0.0f;
		*w = frameW;
		*h = frameW / origRatio;
		// Stretch a little bit
		if (g_Config.bPartialStretch)
			*h = (frameH + *h) / 2.0f; // (408 + 720) / 2 = 564
		*y = (frameH - *h) / 2.0f;
	}
	else
	{
		// Image is taller than frame. Center horizontally.
		float scale = origH / frameH;
		*y = 0.0f;
		*h = frameH;
		*w = frameH * origRatio;
		*x = (frameW - *w) / 2.0f;
	}
}

static void ClearBuffer() {
	dxstate.depthWrite.set(true);
	dxstate.colorMask.set(true, true, true, true);
	pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET |D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 0, 0);
}

static void DisableState() {
	dxstate.blend.disable();
	dxstate.cullMode.set(false, false);
	dxstate.depthTest.disable();
	dxstate.scissorTest.disable();
	dxstate.stencilTest.disable();
}


FramebufferManagerDX9::FramebufferManagerDX9() :
	displayFramebufPtr_(0),
	displayStride_(0),
	displayFormat_(GE_FORMAT_565),
	displayFramebuf_(0),
	prevDisplayFramebuf_(0),
	prevPrevDisplayFramebuf_(0),
	frameLastFramebufUsed(0),
	currentRenderVfb_(0),
	drawPixelsTex_(0),
	drawPixelsTexFormat_(GE_FORMAT_INVALID),
	convBuf(0)
{
	// And an initial clear. We don't clear per frame as the games are supposed to handle that
	// by themselves.
	ClearBuffer();

#ifdef _XBOX
	pD3Ddevice->CreateTexture(512, 272, 1, 0, D3DFMT(D3DFMT_A8R8G8B8), NULL, &drawPixelsTex_, NULL);
#else
	pD3Ddevice->CreateTexture(512, 272, 1, 0, D3DFMT(D3DFMT_A8R8G8B8), D3DPOOL_MANAGED, &drawPixelsTex_, NULL);
#endif
	useBufferedRendering_ = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
}

FramebufferManagerDX9::~FramebufferManagerDX9() {
	if(drawPixelsTex_) {
		drawPixelsTex_->Release();
	}
	delete [] convBuf;
}

static inline void ARGB8From4444(u16 c, u32 * dst) {
	*dst = ((c & 0xf) << 4) | (((c >> 4) & 0xf) << 12) | (((c >> 8) & 0xf) << 20) | ((c >> 12) << 28);
}
static inline void ARGB8From565(u16 c, u32 * dst) {
	*dst = ((c & 0x001f) << 19) | (((c >> 5) & 0x003f) << 11) | ((((c >> 10) & 0x001f) << 3)) | 0xFF000000;
}
static inline void ARGB8From5551(u16 c, u32 * dst) {
	*dst = ((c & 0x001f) << 19) | (((c >> 5) & 0x001f) << 11) | ((((c >> 10) & 0x001f) << 3)) | 0xFF000000;
}

static inline u32 ABGR2RGBA(u32 src) {
	return (src >> 8) | (src << 24); 
}

void FramebufferManagerDX9::DrawPixels(const u8 *framebuf, GEBufferFormat pixelFormat, int linesize) {
	u8 * convBuf = NULL;
	D3DLOCKED_RECT rect;

	drawPixelsTex_->LockRect(0, &rect, NULL, D3DLOCK_NOOVERWRITE);

	convBuf = (u8*)rect.pBits;

	// Final format is ARGB(directx)

	// TODO: We can just change the texture format and flip some bits around instead of this.
	if (pixelFormat != GE_FORMAT_8888 || linesize != 512) {
		for (int y = 0; y < 272; y++) {
			switch (pixelFormat) {
				// not tested
			case GE_FORMAT_565:
				{
					const u16 *src = (const u16 *)framebuf + linesize * y;
					u32 *dst = (u32*)(convBuf + rect.Pitch * y);
					for (int x = 0; x < 480; x++) {
						u16_le col0 = src[x+0];
						ARGB8From565(col0, &dst[x + 0]);
					}
				}
				break;
				// faster
			case GE_FORMAT_5551:
				{
					const u16 *src = (const u16 *)framebuf + linesize * y;
					u32 *dst = (u32*)(convBuf + rect.Pitch * y);
					for (int x = 0; x < 480; x++) {
						u16_le col0 = src[x+0];
						ARGB8From5551(col0, &dst[x + 0]);
					}
				}
				break;
				// not tested
			case GE_FORMAT_4444:
				{
					const u16 *src = (const u16 *)framebuf + linesize * y;
					u32 *dst = (u32*)(convBuf + rect.Pitch * y);
					for (int x = 0; x < 480; x++)
					{
						u16_le col = src[x];
						dst[x * 4 + 0] = (col >> 12) << 4;
						dst[x * 4 + 1] = ((col >> 8) & 0xf) << 4;
						dst[x * 4 + 2] = ((col >> 4) & 0xf) << 4;
						dst[x * 4 + 3] = (col & 0xf) << 4;
					}
				}
				break;

			case GE_FORMAT_8888:
				{
					const u32 *src = (const u32 *)framebuf + linesize * y;
					u32 *dst = (u32*)(convBuf + rect.Pitch * y);
					for (int x = 0; x < 480; x++)
					{
						dst[x] = ABGR2RGBA(src[x]);
					}
				}
				break;
			}
		}
	} else {
		for (int y = 0; y < 272; y++) {
			const u32 *src = (const u32 *)framebuf + linesize * y;
			u32 *dst = (u32*)(convBuf + rect.Pitch * y);
			for (int x = 0; x < 512; x++)
			{
				dst[x] = ABGR2RGBA(src[x]);
			}
		}
	}

	drawPixelsTex_->UnlockRect(0);
	// D3DXSaveTextureToFile("game:\\cc.png", D3DXIFF_PNG, drawPixelsTex_, NULL);

	pD3Ddevice->SetTexture(0, drawPixelsTex_);

	float x, y, w, h;
	CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);
	DrawActiveTexture(x, y, w, h, false, 480.0f / 512.0f);
}

// Depth in ogl is between -1;1 we need between 0;1
static void ConvertMatrices(Matrix4x4 & in) {
	/*
	in.zz *= 0.5f;
	in.wz += 1.f;
	*/
	Matrix4x4 s;
	Matrix4x4 t;
	s.setScaling(Vec3(1, 1, 0.5f));
	t.setTranslation(Vec3(0, 0, 0.5f));
	in = in * s;
	in = in * t;
}

void FramebufferManagerDX9::DrawActiveTexture(float x, float y, float w, float h, bool flip, float uscale, float vscale) {
	float u2 = uscale;
	// Since we're flipping, 0 is down.  That's where the scale goes.
	float v1 = flip ? 1.0f : 1.0f - vscale;
	float v2 = flip ? 1.0f - vscale : 1.0f;

	const float coord[] = { 
		x,	 y,	  0,	0,	v1,
		x+w, y,	  0,	u2, v1,
		x+w, y+h, 0,	u2, v2,
		x,	 y+h, 0,	0,	v2
	}; 

	Matrix4x4 ortho;
	ConvertMatrices(ortho);

	ortho.setOrtho(0, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, 0, -1, 1);

	//pD3Ddevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
	pD3Ddevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	pD3Ddevice->SetVertexShaderConstantF(0, ortho.getReadPtr(), 4);

	pD3Ddevice->SetVertexDeclaration(pFramebufferVertexDecl);
	pD3Ddevice->SetPixelShader(pFramebufferPixelShader);
	pD3Ddevice->SetVertexShader(pFramebufferVertexShader);
	//pD3Ddevice->SetTexture(0, drawPixelsTex_);
	pD3Ddevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, coord, 5 * sizeof(float));
}

VirtualFramebufferDX9 *FramebufferManagerDX9::GetDisplayFBO() {
	VirtualFramebufferDX9 *match = NULL;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebufferDX9 *v = vfbs_[i];
		if (MaskedEqual(v->fb_address, displayFramebufPtr_) && v->format == displayFormat_ && v->width >= 480) {
			// Could check w too but whatever
			if (match == NULL || match->last_frame_render < v->last_frame_render) {
				match = v;
			}
		}
	}
	if (match != NULL) {
		return match;
	}

	DEBUG_LOG(SCEGE, "Finding no FBO matching address %08x", displayFramebufPtr_);
#if 0  // defined(_DEBUG)
	std::string debug = "FBOs: ";
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		char temp[256];
		sprintf(temp, "%08x %i %i", vfbs_[i]->fb_address, vfbs_[i]->width, vfbs_[i]->height);
		debug += std::string(temp);
	}
	ERROR_LOG(SCEGE, "FBOs: %s", debug.c_str());
#endif
	return 0;
}

// Heuristics to figure out the size of FBO to create.
static void DrawingSize(int &drawing_width, int &drawing_height) {
	int default_width = 480; 
	int default_height = 272;
	int viewport_width = (int) gstate.getViewportX1(); 
	int viewport_height = (int) gstate.getViewportY1(); 
	int region_width = gstate.getRegionX2() + 1;
	int region_height = gstate.getRegionY2() + 1;
	int scissor_width = gstate.getScissorX2() + 1;
	int scissor_height = gstate.getScissorY2() + 1;
	int fb_width = gstate.fbwidth & 0x3C0;

	DEBUG_LOG(SCEGE,"viewport : %ix%i, region : %ix%i , scissor: %ix%i, stride: %i, %i", viewport_width,viewport_height, region_width, region_height, scissor_width, scissor_height, fb_width, gstate.isModeThrough());

	// Viewport may return 0x0 for example FF Type-0 and we set it to 480x272
	if (viewport_width <= 1 && viewport_height <=1) {
		viewport_width = default_width;
		viewport_height = default_height;
	}

	if (fb_width > 0 && fb_width < 512) {
		// Correct scissor size has to be used to render like character shadow in Mortal Kombat .
		if (fb_width == scissor_width && region_width != scissor_width) { 
			drawing_width = scissor_width;
			drawing_height = scissor_height;
		} else {
			drawing_width = viewport_width;
			drawing_height = viewport_height;
		}
		} else {
		// Correct region size has to be used when fb_width equals to region_width for exmaple GTA/Midnight Club/MSG Peace Maker .
		if (fb_width == region_width && region_width != scissor_width) { 
			drawing_width = region_width;
			drawing_height = region_height;
	} else {
			drawing_width = default_width;
			drawing_height = default_height;
		}
	}
}

void FramebufferManagerDX9::DestroyFramebuf(VirtualFramebufferDX9 *v) {
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

void FramebufferManagerDX9::SetRenderFrameBuffer() {
	if (!gstate_c.framebufChanged && currentRenderVfb_) {
		currentRenderVfb_->last_frame_render = gpuStats.numFlips;
		currentRenderVfb_->dirtyAfterDisplay = true;
		if (!gstate_c.skipDrawReason)
			currentRenderVfb_->reallyDirtyAfterDisplay = true;
		return;
	}
	gstate_c.framebufChanged = false;

	// Get parameters
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.fbwidth & 0x3C0;

	u32 z_address = gstate.getDepthBufRawAddress();
	int z_stride = gstate.zbwidth & 0x3C0;

	// Yeah this is not completely right. but it'll do for now.
	//int drawing_width = ((gstate.region2) & 0x3FF) + 1;
	//int drawing_height = ((gstate.region2 >> 10) & 0x3FF) + 1;

	// As there are no clear "framebuffer width" and "framebuffer height" registers,
	// we need to infer the size of the current framebuffer somehow. Let's try the viewport.

	GEBufferFormat fmt = gstate.FrameBufFormat();

	int drawing_width, drawing_height;
	DrawingSize(drawing_width, drawing_height);

	int buffer_width = drawing_width;
	int buffer_height = drawing_height;

	// Find a matching framebuffer
	VirtualFramebufferDX9 *vfb = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebufferDX9 *v = vfbs_[i];
		if (MaskedEqual(v->fb_address, fb_address) && v->format == fmt) {
			// Let's not be so picky for now. Let's say this is the one.
			vfb = v;
			// Update fb stride in case it changed
			vfb->fb_stride = fb_stride;
			vfb->format = fmt;
			if (v->bufferWidth >= drawing_width && v->bufferHeight >= drawing_height) { 
				v->width = drawing_width;
				v->height = drawing_height;
			}
			break; 
		}
	}

	float renderWidthFactor = (float)PSP_CoreParameter().renderWidth / 480.0f;
	float renderHeightFactor = (float)PSP_CoreParameter().renderHeight / 272.0f;

	// None found? Create one.
	if (!vfb) {
		gstate_c.textureChanged = TEXCHANGE_UPDATED;
		vfb = new VirtualFramebufferDX9();
		vfb->fbo = 0;
		vfb->fb_address = fb_address;
		vfb->fb_stride = fb_stride;
		vfb->z_address = z_address;
		vfb->z_stride = z_stride;
		vfb->width = drawing_width;
		vfb->height = drawing_height;
		vfb->renderWidth = (u16)(drawing_width * renderWidthFactor);
		vfb->renderHeight = (u16)(drawing_height * renderHeightFactor);
		vfb->bufferWidth = buffer_width;
		vfb->bufferHeight = buffer_height;
		vfb->format = fmt;
		vfb->usageFlags = FB_USAGE_RENDERTARGET;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;
		vfb->memoryUpdated = false; 

		if (g_Config.bTrueColor) {
			vfb->colorDepth = FBO_8888;
		} else { 
			switch (fmt) {
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
				vfb->colorDepth = FBO_8888; 
				break;
			default: 
				vfb->colorDepth = FBO_8888; 
				break;
			}
		}

		//#ifdef ANDROID
		//	vfb->colorDepth = FBO_8888;
		//#endif

		if (useBufferedRendering_) {
			vfb->fbo = fbo_create(vfb->renderWidth, vfb->renderHeight, 1, true, vfb->colorDepth);
			if (vfb->fbo) {
				fbo_bind_as_render_target(vfb->fbo);
			} else {
				ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", vfb->renderWidth, vfb->renderHeight);
			}
		} else {
			fbo_unbind();
			// Let's ignore rendering to targets that have not (yet) been displayed.
			gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
		}

		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_CREATED);

		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfbs_.push_back(vfb);
		ClearBuffer();

		currentRenderVfb_ = vfb;

		INFO_LOG(SCEGE, "Creating FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);

		// We already have it!
	} else if (vfb != currentRenderVfb_) {
		// TODO: This doesn't make sense for DirectX?
#ifndef USING_GLES2
		bool useMem = g_Config.iRenderingMode == FB_READFBOMEMORY_GPU || g_Config.iRenderingMode == FB_READFBOMEMORY_CPU;
#else
		bool useMem = g_Config.iRenderingMode == FB_READFBOMEMORY_GPU;
#endif 
		if(useMem && !vfb->memoryUpdated) {
			ReadFramebufferToMemory(vfb, true);
		} 
		// Use it as a render target.
		DEBUG_LOG(SCEGE, "Switching render target to FBO for %08x: %i x %i x %i ", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		vfb->usageFlags |= FB_USAGE_RENDERTARGET;
		gstate_c.textureChanged = TEXCHANGE_UPDATED;
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;
		vfb->memoryUpdated = false;

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
			if (vfb->usageFlags & FB_USAGE_DISPLAYED_FRAMEBUFFER)
				gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;
			else
				gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;

			/*
			if (drawing_width == 480 && drawing_height == 272) {
			gstate_c.skipDrawReason &= ~SKIPDRAW_SKIPNONFB;
			// OK!
			} else {
			gstate_c.skipDrawReason |= ~SKIPDRAW_SKIPNONFB;
			}*/
		}
		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);
					
#if 0
		// Some tiled mobile GPUs benefit IMMENSELY from clearing an FBO before rendering
		// to it. This broke stuff before, so now it only clears on the first use of an
		// FBO in a frame. This means that some games won't be able to avoid the on-some-GPUs
		// performance-crushing framebuffer reloads from RAM, but we'll have to live with that.
		if (vfb->last_frame_render != gpuStats.numFlips)	{
			ClearBuffer();
		}
#endif
		currentRenderVfb_ = vfb;
	} else {
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;
	}

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		gstate_c.curRTWidth = vfb->width;
		gstate_c.curRTHeight = vfb->height;
	}
}

void FramebufferManagerDX9::CopyDisplayToOutput() {

#ifdef _XBOX
	//if (currentRenderVfb_ && (!currentRenderVfb_->usageFlags & FB_USAGE_DISPLAYED_FRAMEBUFFER))
	//if (currentRenderVfb_ && (currentRenderVfb_->usageFlags == FB_USAGE_RENDERTARGET))
	//if (currentRenderVfb_ && (currentRenderVfb_->fb_address == 0))

	if (currentRenderVfb_)
	{
		if (currentRenderVfb_->fbo && currentRenderVfb_->usageFlags == FB_USAGE_RENDERTARGET) {
			fbo_resolve(currentRenderVfb_->fbo);
		}
	}
	
#endif

	
	fbo_unbind();
	
	currentRenderVfb_ = 0;

	VirtualFramebufferDX9 *vfb = GetDisplayFBO();
	if (!vfb) {
		if (Memory::IsValidAddress(ramDisplayFramebufPtr_)) {
			// The game is displaying something directly from RAM. In GTA, it's decoded video.
			DrawPixels(Memory::GetPointer(ramDisplayFramebufPtr_), displayFormat_, displayStride_);
		} else if (Memory::IsValidAddress(displayFramebufPtr_)) {
			// The game is displaying something directly from RAM. In GTA, it's decoded video.
			DrawPixels(Memory::GetPointer(displayFramebufPtr_), displayFormat_, displayStride_);
		} else {
			DEBUG_LOG(SCEGE, "Found no FBO to display! displayFBPtr = %08x", displayFramebufPtr_);
			// No framebuffer to display! Clear to black.
			ClearBuffer();
		}
		return;
	}

	vfb->usageFlags |= FB_USAGE_DISPLAYED_FRAMEBUFFER;
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
		dxstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		DEBUG_LOG(SCEGE, "Displaying FBO %08x", vfb->fb_address);
		DisableState();
		fbo_bind_color_as_texture(vfb->fbo, 0);

		// These are in the output display coordinates
		float x, y, w, h;
		CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);
		DrawActiveTexture(x, y, w, h, false, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height);
		pD3Ddevice->SetTexture(0, NULL);
	}
}

void FramebufferManagerDX9::ReadFramebufferToMemory(VirtualFramebufferDX9 *vfb, bool sync) {
	// This only works with buffered rendering
	if (!useBufferedRendering_) {
		return;
	}

#if 0
	if(sync) {
		PackFramebufferAsync_(NULL); // flush async just in case when we go for synchronous update
	}
#endif 

	if(vfb) {
		// We'll pseudo-blit framebuffers here to get a resized and flipped version of vfb.
		// For now we'll keep these on the same struct as the ones that can get displayed
		// (and blatantly copy work already done above while at it).
		VirtualFramebufferDX9 *nvfb = 0;

		// We maintain a separate vector of framebuffer objects for blitting.
		for (size_t i = 0; i < bvfbs_.size(); ++i) {
			VirtualFramebufferDX9 *v = bvfbs_[i];
			if (MaskedEqual(v->fb_address, vfb->fb_address) && v->format == vfb->format) {
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
			nvfb = new VirtualFramebufferDX9();
			nvfb->fbo = 0;
			nvfb->fb_address = vfb->fb_address;
			nvfb->fb_stride = vfb->fb_stride;
			nvfb->z_address = vfb->z_address;
			nvfb->z_stride = vfb->z_stride;
			nvfb->width = vfb->width;
			nvfb->height = vfb->height;
			nvfb->renderWidth = vfb->width;
			nvfb->renderHeight = vfb->height;
			nvfb->bufferWidth = vfb->bufferWidth;
			nvfb->bufferHeight = vfb->bufferHeight;
			nvfb->format = vfb->format;
			nvfb->usageFlags = FB_USAGE_RENDERTARGET;
			nvfb->dirtyAfterDisplay = true;

			if(g_Config.bTrueColor) {
				nvfb->colorDepth = FBO_8888;
			} else {
				switch (vfb->format) {
				case GE_FORMAT_4444:
					nvfb->colorDepth = FBO_4444;
					break;
				case GE_FORMAT_5551:
					nvfb->colorDepth = FBO_5551;
					break;
				case GE_FORMAT_565: 
					nvfb->colorDepth = FBO_565;
					break;
				case GE_FORMAT_8888:
				default: 
					nvfb->colorDepth = FBO_8888;
					break;
				}
			}

			nvfb->fbo = fbo_create(nvfb->width, nvfb->height, 1, true, nvfb->colorDepth);
			if (!(nvfb->fbo)) {
				ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
				return;
			}

			nvfb->last_frame_render = gpuStats.numFlips;
			bvfbs_.push_back(nvfb);
			fbo_bind_as_render_target(nvfb->fbo); 
			ClearBuffer();
		} else {
			nvfb->usageFlags |= FB_USAGE_RENDERTARGET;
			nvfb->last_frame_render = gpuStats.numFlips;
			nvfb->dirtyAfterDisplay = true;

#if 0
			fbo_bind_as_render_target(nvfb->fbo);

			// Some tiled mobile GPUs benefit IMMENSELY from clearing an FBO before rendering
			// to it. This broke stuff before, so now it only clears on the first use of an
			// FBO in a frame. This means that some games won't be able to avoid the on-some-GPUs
			// performance-crushing framebuffer reloads from RAM, but we'll have to live with that.
			if (nvfb->last_frame_render != gpuStats.numFlips)	{
				ClearBuffer();
			}
#endif
		}

		vfb->memoryUpdated = true;
		BlitFramebuffer_(vfb, nvfb, false);

		PackFramebufferDirectx9_(nvfb);
	}
}

void FramebufferManagerDX9::BlitFramebuffer_(VirtualFramebufferDX9 *src, VirtualFramebufferDX9 *dst, bool flip, float upscale, float vscale) {
	// This only works with buffered rendering
	if (!useBufferedRendering_ || !src->fbo) {
		return;
	}

	fbo_bind_as_render_target(dst->fbo);

	/*
	if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
	ERROR_LOG(HLE, "Incomplete target framebuffer, aborting blit");
	fbo_unbind();
	return;
	}
	*/

	dxstate.viewport.set(0, 0, dst->width, dst->height);
	DisableState();

	fbo_bind_color_as_texture(src->fbo, 0);

	float x, y, w, h;
	CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);

	DrawActiveTexture(x, y, w, h, flip, upscale, vscale);

	pD3Ddevice->SetTexture(0, NULL);

#ifdef _XBOX
	fbo_resolve(dst->fbo);
#endif
	fbo_unbind();
}

// TODO: SSE/NEON
static void ConvertFromRGBA8888(u8 *dst, u8 *src, u32 stride, u32 height, GEBufferFormat format) {
	if(format == GE_FORMAT_8888) {
		if(src == dst) {
			return;
		} else { // Here lets assume they don't intersect
			memcpy(dst, src, stride * height * 4);
		}
	} else { // But here it shouldn't matter if they do
		int size = height * stride;
		const u32 *src32 = (const u32 *)src;
		u16 *dst16 = (u16 *)dst;
		switch (format) {
		case GE_FORMAT_565: // BGR 565
			for(int i = 0; i < size; i++) {
				dst16[i] = RGBA8888toRGB565(src32[i]);
			}
			break;
		case GE_FORMAT_5551: // ABGR 1555
			for(int i = 0; i < size; i++) {
				dst16[i] = RGBA8888toRGBA5551(src32[i]);
			}

			break;
		case GE_FORMAT_4444: // ABGR 4444
			for(int i = 0; i < size; i++) {
				dst16[i] = RGBA8888toRGBA4444(src32[i]);
			}
			break;
		case GE_FORMAT_8888:
			// Not possible.
			break;
		default:
			break;
		}
	}
}

#ifdef _XBOX
#include <xgraphics.h>
#endif

static void Resolve(u8* data, VirtualFramebufferDX9 *vfb) {
#ifdef _XBOX
	D3DTexture * rtt = (D3DTexture*)fbo_get_rtt(vfb->fbo);
	pD3Ddevice->Resolve(D3DRESOLVE_RENDERTARGET0, NULL, rtt, NULL, 0, 0, NULL, 0.f, 0, NULL);

	D3DLOCKED_RECT p;
	rtt->LockRect(0, &p, NULL, 0);
	rtt->UnlockRect(0);

	// vfb->fbo->tex is tilled !!!!
	XGUntileTextureLevel(vfb->width/2, vfb->height/2, 0,  XGGetGpuFormat(D3DFMT_LIN_A8R8G8B8), XGTILE_NONPACKED, data, p.Pitch, NULL, p.pBits, NULL);
#endif
}

void FramebufferManagerDX9::PackFramebufferDirectx9_(VirtualFramebufferDX9 *vfb) {
	if (useBufferedRendering_ && vfb->fbo) {
		fbo_bind_for_read(vfb->fbo);
	} else {
		fbo_unbind();
		return;
	}

	// Pixel size always 4 here because we always request RGBA8888
	size_t bufSize = vfb->fb_stride * vfb->height * 4;
	u32 fb_address = (0x04000000) | vfb->fb_address;

	u8 *packed = 0;
	if(vfb->format == GE_FORMAT_8888) {
		packed = (u8 *)Memory::GetPointer(fb_address);
	} else { // End result may be 16-bit but we are reading 32-bit, so there may not be enough space at fb_address
		packed = (u8 *)malloc(bufSize * sizeof(u8));
	}

	if(packed) {
		DEBUG_LOG(HLE, "Reading framebuffer to mem, bufSize = %u, packed = %p, fb_address = %08x", 
			(u32)bufSize, packed, fb_address);

		// Resolve(packed, vfb);

		if(vfb->format != GE_FORMAT_8888) { // If not RGBA 8888 we need to convert
			ConvertFromRGBA8888(Memory::GetPointer(fb_address), packed, vfb->fb_stride, vfb->height, vfb->format);
			free(packed);
		}
	}

	fbo_unbind();
}
void FramebufferManagerDX9::EndFrame() {
	if (resized_) {
		DestroyAllFBOs();
		dxstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		resized_ = false;
	}
}

void FramebufferManagerDX9::DeviceLost() {
	DestroyAllFBOs();
	resized_ = false;
}

void FramebufferManagerDX9::BeginFrame() {
	DecimateFBOs();
	currentRenderVfb_ = 0;
	useBufferedRendering_ = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
}

void FramebufferManagerDX9::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {

	if ((framebuf & 0x04000000) == 0) {
		DEBUG_LOG(SCEGE, "Non-VRAM display framebuffer address set: %08x", framebuf);
		ramDisplayFramebufPtr_ = framebuf;
		displayStride_ = stride;
		displayFormat_ = format;
	} else {
		ramDisplayFramebufPtr_ = 0;
		displayFramebufPtr_ = framebuf;
		displayStride_ = stride;
		displayFormat_ = format;
	}
}

std::vector<FramebufferInfo> FramebufferManagerDX9::GetFramebufferList() {
	std::vector<FramebufferInfo> list;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebufferDX9 *vfb = vfbs_[i];

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
	// TODO: This doesn't make sense for DirectX?
#ifndef USING_GLES2
	bool useMem = g_Config.iRenderingMode == FB_READFBOMEMORY_GPU || g_Config.iRenderingMode == FB_READFBOMEMORY_CPU;
#else
	bool useMem = g_Config.iRenderingMode == FB_READFBOMEMORY_GPU;
#endif
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebufferDX9 *vfb = vfbs_[i];
		int age = frameLastFramebufUsed - std::max(vfb->last_frame_render, vfb->last_frame_used);

		if(useMem && age == 0 && !vfb->memoryUpdated) { 
			ReadFramebufferToMemory(vfb);
		}

		if (vfb == displayFramebuf_ || vfb == prevDisplayFramebuf_ || vfb == prevPrevDisplayFramebuf_) {
			continue;
		}

		if (age > FBO_OLD_AGE) {
			INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age)
				DestroyFramebuf(vfb);
			vfbs_.erase(vfbs_.begin() + i--);
		}
	}

	// Do the same for ReadFramebuffersToMemory's VFBs
	for (size_t i = 0; i < bvfbs_.size(); ++i) {
		VirtualFramebufferDX9 *vfb = bvfbs_[i];
		int age = frameLastFramebufUsed - vfb->last_frame_render;
		if (age > FBO_OLD_AGE) {
			INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age)
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
		VirtualFramebufferDX9 *vfb = vfbs_[i];
		INFO_LOG(SCEGE, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		DestroyFramebuf(vfb);
	}
	vfbs_.clear();
}

void FramebufferManagerDX9::UpdateFromMemory(u32 addr, int size) {
	addr &= ~0x40000000;
	// TODO: Could go through all FBOs, but probably not important?
	// TODO: Could also check for inner changes, but video is most important.
	if (addr == DisplayFramebufAddr() || addr == PrevDisplayFramebufAddr()) {
		// TODO: Deleting the FBO is a heavy hammer solution, so let's only do it if it'd help.
		if (!Memory::IsValidAddress(displayFramebufPtr_))
			return;

		fbo_unbind();
		currentRenderVfb_ = 0;

		bool needUnbind = false;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebufferDX9 *vfb = vfbs_[i];
			if (MaskedEqual(vfb->fb_address, addr)) {
				vfb->dirtyAfterDisplay = true;
				vfb->reallyDirtyAfterDisplay = true;
				// TODO: This without the fbo_unbind() above would be better than destroying the FBO.
				// However, it doesn't seem to work for Star Ocean, at least
				if (useBufferedRendering_) {
					fbo_bind_as_render_target(vfb->fbo);
					needUnbind = true;
					DrawPixels(Memory::GetPointer(addr), vfb->format, vfb->fb_stride);
				} else {
					INFO_LOG(SCEGE, "Invalidating FBO for %08x (%i x %i x %i)", vfb->fb_address, vfb->width, vfb->height, vfb->format)
						DestroyFramebuf(vfb);
					vfbs_.erase(vfbs_.begin() + i--);
				}
			}
		}

		if (needUnbind)
			fbo_unbind();
	}
}

void FramebufferManagerDX9::Resized() {
	resized_ = true;
}

};
