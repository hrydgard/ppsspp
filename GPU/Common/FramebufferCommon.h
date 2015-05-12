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

#pragma once

#include <set>
#include <vector>
#include "Common/CommonTypes.h"
#include "Core/MemMap.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

enum {
	FB_USAGE_DISPLAYED_FRAMEBUFFER = 1,
	FB_USAGE_RENDERTARGET = 2,
	FB_USAGE_TEXTURE = 4,
};

enum {
	FB_NON_BUFFERED_MODE = 0,
	FB_BUFFERED_MODE = 1,

	// Hm, it's unfortunate that GPU has ended up as two separate values in GL and GLES.
#ifndef USING_GLES2
	FB_READFBOMEMORY_CPU = 2,
	FB_READFBOMEMORY_GPU = 3,
#else
	FB_READFBOMEMORY_GPU = 2,
#endif
	FBO_READFBOMEMORY_MIN = 2
};

struct FBO;

struct VirtualFramebuffer {
	int last_frame_used;
	int last_frame_attached;
	int last_frame_render;
	int last_frame_displayed;
	bool memoryUpdated;
	bool depthUpdated;

	u32 fb_address;
	u32 z_address;
	int fb_stride;
	int z_stride;

	// There's also a top left of the drawing region, but meh...

	// width/height: The detected size of the current framebuffer.
	u16 width;
	u16 height;
	// renderWidth/renderHeight: The actual size we render at. May be scaled to render at higher resolutions.
	u16 renderWidth;
	u16 renderHeight;
	// bufferWidth/bufferHeight: The actual (but non scaled) size of the buffer we render to. May only be bigger than width/height.
	u16 bufferWidth;
	u16 bufferHeight;

	u16 usageFlags;

	u16 newWidth;
	u16 newHeight;
	int lastFrameNewSize;

	GEBufferFormat format;  // virtual, right now they are all RGBA8888
	// TODO: Handle fbo and colorDepth better.
	u8 colorDepth;
	FBO *fbo;

	u16 drawnWidth;
	u16 drawnHeight;
	GEBufferFormat drawnFormat;

	bool dirtyAfterDisplay;
	bool reallyDirtyAfterDisplay;  // takes frame skipping into account
};

class FramebufferManagerCommon {
public:
	FramebufferManagerCommon();
	virtual ~FramebufferManagerCommon();

	virtual void Init();
	void BeginFrame();
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);

	void DoSetRenderFrameBuffer();
	void SetRenderFrameBuffer() {
		// Inlining this part since it's so frequent.
		if (!gstate_c.framebufChanged && currentRenderVfb_) {
			currentRenderVfb_->last_frame_render = gpuStats.numFlips;
			currentRenderVfb_->dirtyAfterDisplay = true;
			if (!gstate_c.skipDrawReason)
				currentRenderVfb_->reallyDirtyAfterDisplay = true;
			return;
		}
		DoSetRenderFrameBuffer();
	}
	virtual void RebindFramebuffer() = 0;

	bool NotifyFramebufferCopy(u32 src, u32 dest, int size, bool isMemset = false);
	void UpdateFromMemory(u32 addr, int size, bool safe);
	virtual bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) = 0;
	// Returns true if it's sure this is a direct FBO->FBO transfer and it has already handle it.
	// In that case we hardly need to actually copy the bytes in VRAM, they will be wrong anyway (unless
	// read framebuffers is on, in which case this should always return false).
	bool NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp);
	void NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp);

	virtual void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) = 0;
	virtual void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) = 0;
	virtual void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) = 0;
	virtual void DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) = 0;

	size_t NumVFBs() const { return vfbs_.size(); }

	u32 PrevDisplayFramebufAddr() {
		return prevDisplayFramebuf_ ? (0x04000000 | prevDisplayFramebuf_->fb_address) : 0;
	}
	u32 DisplayFramebufAddr() {
		return displayFramebuf_ ? (0x04000000 | displayFramebuf_->fb_address) : 0;
	}

	bool MayIntersectFramebuffer(u32 start) {
		// Clear the cache/kernel bits.
		start = start & 0x3FFFFFFF;
		// Most games only have two framebuffers at the start.
		if (start >= framebufRangeEnd_ || start < PSP_GetVidMemBase()) {
			return false;
		}
		return true;
	}

	// TODO: Break out into some form of FBO manager
	VirtualFramebuffer *GetVFBAt(u32 addr);
	VirtualFramebuffer *GetDisplayVFB() {
		return GetVFBAt(displayFramebufPtr_);
	}

	int GetRenderWidth() const { return currentRenderVfb_ ? currentRenderVfb_->renderWidth : 480; }
	int GetRenderHeight() const { return currentRenderVfb_ ? currentRenderVfb_->renderHeight : 272; }
	int GetTargetWidth() const { return currentRenderVfb_ ? currentRenderVfb_->width : 480; }
	int GetTargetHeight() const { return currentRenderVfb_ ? currentRenderVfb_->height : 272; }
	int GetTargetBufferWidth() const { return currentRenderVfb_ ? currentRenderVfb_->bufferWidth : 480; }
	int GetTargetBufferHeight() const { return currentRenderVfb_ ? currentRenderVfb_->bufferHeight : 272; }
	int GetTargetStride() const { return currentRenderVfb_ ? currentRenderVfb_->fb_stride : 512; }
	GEBufferFormat GetTargetFormat() const { return currentRenderVfb_ ? currentRenderVfb_->format : displayFormat_; }

	void SetDepthUpdated() {
		if (currentRenderVfb_) {
			currentRenderVfb_->depthUpdated = true;
		}
	}
	void SetColorUpdated() {
		if (currentRenderVfb_) {
			SetColorUpdated(currentRenderVfb_);
		}
	}
	void SetRenderSize(VirtualFramebuffer *vfb);

protected:
	virtual void DisableState() = 0;
	virtual void ClearBuffer() = 0;
	virtual void ClearDepthBuffer() = 0;
	virtual void FlushBeforeCopy() = 0;
	virtual void DecimateFBOs() = 0;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	virtual void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, bool flip = false) = 0;

	void EstimateDrawingSize(int &drawing_width, int &drawing_height);
	u32 FramebufferByteSize(const VirtualFramebuffer *vfb) const;
	static bool MaskedEqual(u32 addr1, u32 addr2);

	virtual void DestroyFramebuf(VirtualFramebuffer *vfb) = 0;
	virtual void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false) = 0;
	virtual void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) = 0;
	virtual void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb) = 0;
	virtual void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) = 0;

	bool ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const;
	void FindTransferFramebuffers(VirtualFramebuffer *&dstBuffer, VirtualFramebuffer *&srcBuffer, u32 dstBasePtr, int dstStride, int &dstX, int &dstY, u32 srcBasePtr, int srcStride, int &srcX, int &srcY, int &srcWidth, int &srcHeight, int &dstWidth, int &dstHeight, int bpp) const;

	void UpdateFramebufUsage(VirtualFramebuffer *vfb);

	void SetColorUpdated(VirtualFramebuffer *dstBuffer) {
		dstBuffer->memoryUpdated = false;
		dstBuffer->dirtyAfterDisplay = true;
		dstBuffer->drawnWidth = dstBuffer->width;
		dstBuffer->drawnHeight = dstBuffer->height;
		dstBuffer->drawnFormat = dstBuffer->format;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			dstBuffer->reallyDirtyAfterDisplay = true;
	}

	u32 displayFramebufPtr_;
	u32 displayStride_;
	GEBufferFormat displayFormat_;

	VirtualFramebuffer *displayFramebuf_;
	VirtualFramebuffer *prevDisplayFramebuf_;
	VirtualFramebuffer *prevPrevDisplayFramebuf_;
	int frameLastFramebufUsed_;

	VirtualFramebuffer *currentRenderVfb_;

	// The range of PSP memory that may contain FBOs.  So we can skip iterating.
	u32 framebufRangeEnd_;

	bool useBufferedRendering_;
	bool updateVRAM_;

	std::vector<VirtualFramebuffer *> vfbs_;
	std::set<std::pair<u32, u32>> knownFramebufferRAMCopies_;

	bool hackForce04154000Download_;

	// Aggressively delete unused FBOs to save gpu memory.
	enum {
		FBO_OLD_AGE = 5,
		FBO_OLD_USAGE_FLAG = 15,
	};
};

void CenterRect(float *x, float *y, float *w, float *h, float origW, float origH, float frameW, float frameH);
