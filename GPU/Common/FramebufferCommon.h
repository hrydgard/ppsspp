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
#include "GPU/GPU.h"
#include "GPU/ge_constants.h"

enum {
	FB_USAGE_DISPLAYED_FRAMEBUFFER = 1,
	FB_USAGE_RENDERTARGET = 2,
	FB_USAGE_TEXTURE = 4,
	FB_USAGE_CLUT = 8,
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
namespace DX9 {
	struct FBO_DX9;
}

class VulkanFBO;

struct VirtualFramebuffer {
	int last_frame_used;
	int last_frame_attached;
	int last_frame_render;
	int last_frame_displayed;
	int last_frame_clut;
	u32 clutUpdatedBytes;
	bool memoryUpdated;
	bool depthUpdated;
	bool firstFrameSaved;

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
	union {
		FBO *fbo;
		DX9::FBO_DX9 *fbo_dx9;
		VulkanFBO *fbo_vk;
	};

	u16 drawnWidth;
	u16 drawnHeight;
	GEBufferFormat drawnFormat;
	u16 safeWidth;
	u16 safeHeight;

	bool dirtyAfterDisplay;
	bool reallyDirtyAfterDisplay;  // takes frame skipping into account
};

struct FramebufferHeuristicParams {
	u32 fb_addr;
	u32 fb_address;
	int fb_stride;
	u32 z_address;
	int z_stride;
	GEBufferFormat fmt;
	bool isClearingDepth;
	bool isWritingDepth;
	bool isDrawing;
	bool isModeThrough;
	int viewportWidth;
	int viewportHeight;
	int regionWidth;
	int regionHeight;
	int scissorWidth;
	int scissorHeight;
};

struct GPUgstate;
extern GPUgstate gstate;

void GetFramebufferHeuristicInputs(FramebufferHeuristicParams *params, const GPUgstate &gstate);

enum BindFramebufferColorFlags {
	BINDFBCOLOR_SKIP_COPY = 0,
	BINDFBCOLOR_MAY_COPY = 1,
	BINDFBCOLOR_MAY_COPY_WITH_UV = 3,
	BINDFBCOLOR_APPLY_TEX_OFFSET = 4,
};

class FramebufferManagerCommon {
public:
	FramebufferManagerCommon();
	virtual ~FramebufferManagerCommon();

	virtual void Init();
	void BeginFrame();
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);

	VirtualFramebuffer *DoSetRenderFrameBuffer(const FramebufferHeuristicParams &params, u32 skipDrawReason);
	VirtualFramebuffer *SetRenderFrameBuffer(bool framebufChanged, int skipDrawReason) {
		// Inlining this part since it's so frequent.
		if (!framebufChanged && currentRenderVfb_) {
			currentRenderVfb_->last_frame_render = gpuStats.numFlips;
			currentRenderVfb_->dirtyAfterDisplay = true;
			if (!skipDrawReason)
				currentRenderVfb_->reallyDirtyAfterDisplay = true;
			return currentRenderVfb_;
		} else {
			// This is so that we will be able to drive DoSetRenderFramebuffer with inputs
			// that come from elsewhere than gstate.
			FramebufferHeuristicParams inputs;
			GetFramebufferHeuristicInputs(&inputs, gstate);
			VirtualFramebuffer *vfb = DoSetRenderFrameBuffer(inputs, skipDrawReason);
			return vfb;
		}
	}
	virtual void RebindFramebuffer() = 0;

	bool NotifyFramebufferCopy(u32 src, u32 dest, int size, bool isMemset, u32 skipDrawReason);
	void NotifyVideoUpload(u32 addr, int size, int width, GEBufferFormat fmt);
	void UpdateFromMemory(u32 addr, int size, bool safe);
	virtual bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) = 0;
	// Returns true if it's sure this is a direct FBO->FBO transfer and it has already handle it.
	// In that case we hardly need to actually copy the bytes in VRAM, they will be wrong anyway (unless
	// read framebuffers is on, in which case this should always return false).
	bool NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);
	void NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);

	virtual void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) = 0;
	virtual void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) = 0;
	virtual void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) = 0;
	virtual void DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) = 0;

	size_t NumVFBs() const { return vfbs_.size(); }

	u32 PrevDisplayFramebufAddr() {
		return prevDisplayFramebuf_ ? (0x04000000 | prevDisplayFramebuf_->fb_address) : 0;
	}
	u32 DisplayFramebufAddr() {
		return displayFramebuf_ ? (0x04000000 | displayFramebuf_->fb_address) : 0;
	}

	u32 DisplayFramebufStride() {
		return displayFramebuf_ ? displayStride_ : 0;
	}
	GEBufferFormat DisplayFramebufFormat() {
		return displayFramebuf_ ? displayFormat_ : GE_FORMAT_INVALID;
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
	void SetColorUpdated(int skipDrawReason) {
		if (currentRenderVfb_) {
			SetColorUpdated(currentRenderVfb_, skipDrawReason);
		}
	}
	void SetRenderSize(VirtualFramebuffer *vfb);
	void SetSafeSize(u16 w, u16 h);

protected:
	void UpdateSize();

	virtual void DisableState() = 0;
	virtual void ClearBuffer(bool keepState = false) = 0;
	virtual void FlushBeforeCopy() = 0;
	virtual void DecimateFBOs() = 0;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	virtual void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) = 0;

	void EstimateDrawingSize(u32 fb_address, GEBufferFormat fb_format, int viewport_width, int viewport_height, int region_width, int region_height, int scissor_width, int scissor_height, int fb_stride, int &drawing_width, int &drawing_height);
	u32 FramebufferByteSize(const VirtualFramebuffer *vfb) const;
	static bool MaskedEqual(u32 addr1, u32 addr2);

	virtual void DestroyFramebuf(VirtualFramebuffer *vfb) = 0;
	virtual void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false, bool skipCopy = false) = 0;
	virtual void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) = 0;
	virtual void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) = 0;
	virtual void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) = 0;

	void ShowScreenResolution();

	bool ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const;
	void DownloadFramebufferOnSwitch(VirtualFramebuffer *vfb);
	void FindTransferFramebuffers(VirtualFramebuffer *&dstBuffer, VirtualFramebuffer *&srcBuffer, u32 dstBasePtr, int dstStride, int &dstX, int &dstY, u32 srcBasePtr, int srcStride, int &srcX, int &srcY, int &srcWidth, int &srcHeight, int &dstWidth, int &dstHeight, int bpp) const;
	VirtualFramebuffer *FindDownloadTempBuffer(VirtualFramebuffer *vfb);
	virtual bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) = 0;
	virtual void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) = 0;
	void OptimizeDownloadRange(VirtualFramebuffer *vfb, int &x, int &y, int &w, int &h);

	void UpdateFramebufUsage(VirtualFramebuffer *vfb);

	void SetColorUpdated(VirtualFramebuffer *dstBuffer, int skipDrawReason) {
		dstBuffer->memoryUpdated = false;
		dstBuffer->clutUpdatedBytes = 0;
		dstBuffer->dirtyAfterDisplay = true;
		dstBuffer->drawnWidth = dstBuffer->width;
		dstBuffer->drawnHeight = dstBuffer->height;
		dstBuffer->drawnFormat = dstBuffer->format;
		if ((skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
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
	bool usePostShader_;
	bool postShaderAtOutputResolution_;
	bool postShaderIsUpscalingFilter_;

	std::vector<VirtualFramebuffer *> vfbs_;
	std::vector<VirtualFramebuffer *> bvfbs_; // blitting framebuffers (for download)
	std::set<std::pair<u32, u32>> knownFramebufferRAMCopies_;

	bool hackForce04154000Download_;
	bool gameUsesSequentialCopies_;

	// Sampled in BeginFrame for safety.
	float renderWidth_;
	float renderHeight_;
	int pixelWidth_;
	int pixelHeight_;

	// Aggressively delete unused FBOs to save gpu memory.
	enum {
		FBO_OLD_AGE = 5,
		FBO_OLD_USAGE_FLAG = 15,
	};
};

void CenterDisplayOutputRect(float *x, float *y, float *w, float *h, float origW, float origH, float frameW, float frameH, int rotation);
