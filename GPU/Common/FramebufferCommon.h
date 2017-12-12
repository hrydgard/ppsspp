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
#include <map>

#include "Common/CommonTypes.h"
#include "Core/MemMap.h"
#include "GPU/GPU.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUInterface.h"
#include "thin3d/thin3d.h"

enum {
	FB_USAGE_DISPLAYED_FRAMEBUFFER = 1,
	FB_USAGE_RENDERTARGET = 2,
	FB_USAGE_TEXTURE = 4,
	FB_USAGE_CLUT = 8,
	FB_USAGE_DOWNLOAD = 16,
	FB_USAGE_DOWNLOAD_CLEAR = 32,
};

enum {
	FB_NON_BUFFERED_MODE = 0,
	FB_BUFFERED_MODE = 1,
};

namespace Draw {
	class Framebuffer;
}

struct CardboardSettings {
	bool enabled;
	float leftEyeXPosition;
	float rightEyeXPosition;
	float screenYPosition;
	float screenWidth;
	float screenHeight;
};

class VulkanFBO;

struct PostShaderUniforms {
	float texelDelta[2]; float pixelDelta[2];
	float time[4];
	float video;
};

struct VirtualFramebuffer {
	int last_frame_used;
	int last_frame_attached;
	int last_frame_render;
	int last_frame_displayed;
	int last_frame_clut;
	int last_frame_failed;
	int last_frame_depth_updated;
	int last_frame_depth_render;
	u32 clutUpdatedBytes;
	bool memoryUpdated;
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
	Draw::Framebuffer *fbo;

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

enum DrawTextureFlags {
	DRAWTEX_NEAREST = 0,
	DRAWTEX_LINEAR = 1,
	DRAWTEX_KEEP_TEX = 2,
};

inline Draw::DataFormat GEFormatToThin3D(int geFormat) {
	switch (geFormat) {
	case GE_FORMAT_4444:
		return Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
	case GE_FORMAT_5551:
		return Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
	case GE_FORMAT_565:
		return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_FORMAT_8888:
		return Draw::DataFormat::R8G8B8A8_UNORM;
	default:
		return Draw::DataFormat::UNDEFINED;
	}
}

namespace Draw {
class DrawContext;
}

struct GPUDebugBuffer;
class TextureCacheCommon;
class ShaderManagerCommon;
class DrawEngineCommon;

class FramebufferManagerCommon {
public:
	FramebufferManagerCommon(Draw::DrawContext *draw);
	virtual ~FramebufferManagerCommon();

	virtual void Init();
	virtual void BeginFrame();
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);
	void DestroyFramebuf(VirtualFramebuffer *v);

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
			_dbg_assert_msg_(G3D, vfb, "DoSetRenderFramebuffer must return a valid framebuffer.");
			_dbg_assert_msg_(G3D, currentRenderVfb_, "DoSetRenderFramebuffer must set a valid framebuffer.");
			return vfb;
		}
	}
	void RebindFramebuffer();
	std::vector<FramebufferInfo> GetFramebufferList();

	void CopyDisplayToOutput();

	bool NotifyFramebufferCopy(u32 src, u32 dest, int size, bool isMemset, u32 skipDrawReason);
	void NotifyVideoUpload(u32 addr, int size, int width, GEBufferFormat fmt);
	void UpdateFromMemory(u32 addr, int size, bool safe);
	void ApplyClearToMemory(int x1, int y1, int x2, int y2, u32 clearColor);
	virtual bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) = 0;
	// Returns true if it's sure this is a direct FBO->FBO transfer and it has already handle it.
	// In that case we hardly need to actually copy the bytes in VRAM, they will be wrong anyway (unless
	// read framebuffers is on, in which case this should always return false).
	bool NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);
	void NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);

	virtual void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h);

	virtual void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes);
	void DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader);

	void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);

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

	VirtualFramebuffer *GetCurrentRenderVFB() const {
		return currentRenderVfb_;
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
			currentRenderVfb_->last_frame_depth_render = gpuStats.numFlips;
			currentRenderVfb_->last_frame_depth_updated = gpuStats.numFlips;
		}
	}
	void SetColorUpdated(int skipDrawReason) {
		if (currentRenderVfb_) {
			SetColorUpdated(currentRenderVfb_, skipDrawReason);
		}
	}
	void SetRenderSize(VirtualFramebuffer *vfb);
	void SetSafeSize(u16 w, u16 h);

	virtual void Resized();

	Draw::Framebuffer *GetTempFBO(u16 w, u16 h, Draw::FBColorDepth colorDepth = Draw::FBO_8888);

	// Debug features
	virtual bool GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes);
	virtual bool GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer);
	virtual bool GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer);
	virtual bool GetOutputFramebuffer(GPUDebugBuffer &buffer);

protected:
	virtual void PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	virtual void SetViewport2D(int x, int y, int w, int h);
	void CalculatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight, PostShaderUniforms *uniforms);
	virtual void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) = 0;
	virtual void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) = 0;
	virtual void Bind2DShader() = 0;
	virtual void BindPostShader(const PostShaderUniforms &uniforms) = 0;

	// Cardboard Settings Calculator
	void GetCardboardSettings(CardboardSettings *cardboardSettings);

	bool UpdateSize();
	void SetNumExtraFBOs(int num);

	virtual void DisableState() = 0;
	void FlushBeforeCopy();
	virtual void DecimateFBOs();  // keeping it virtual to let D3D do a little extra

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	virtual void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) = 0;
	void CopyFramebufferForColorTexture(VirtualFramebuffer *dst, VirtualFramebuffer *src, int flags);

	void EstimateDrawingSize(u32 fb_address, GEBufferFormat fb_format, int viewport_width, int viewport_height, int region_width, int region_height, int scissor_width, int scissor_height, int fb_stride, int &drawing_width, int &drawing_height);
	u32 FramebufferByteSize(const VirtualFramebuffer *vfb) const;
	static bool MaskedEqual(u32 addr1, u32 addr2);

	void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb);
	void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged);
	void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth);

	virtual void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) = 0;
	virtual void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) = 0;

	void ResizeFramebufFBO(VirtualFramebuffer *vfb, int w, int h, bool force = false, bool skipCopy = false);
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

	Draw::DrawContext *draw_ = nullptr;
	TextureCacheCommon *textureCache_ = nullptr;
	ShaderManagerCommon *shaderManager_ = nullptr;
	DrawEngineCommon *drawEngine_ = nullptr;
	bool needBackBufferYSwap_ = false;

	u32 displayFramebufPtr_ = 0;
	u32 displayStride_ = 0;
	GEBufferFormat displayFormat_;

	VirtualFramebuffer *displayFramebuf_ = nullptr;
	VirtualFramebuffer *prevDisplayFramebuf_ = nullptr;
	VirtualFramebuffer *prevPrevDisplayFramebuf_ = nullptr;
	int frameLastFramebufUsed_ = 0;

	VirtualFramebuffer *currentRenderVfb_ = nullptr;

	// The range of PSP memory that may contain FBOs.  So we can skip iterating.
	u32 framebufRangeEnd_ = 0;

	bool useBufferedRendering_ = false;
	bool usePostShader_ = false;
	bool postShaderAtOutputResolution_ = false;
	bool postShaderIsUpscalingFilter_ = false;

	std::vector<VirtualFramebuffer *> vfbs_;
	std::vector<VirtualFramebuffer *> bvfbs_; // blitting framebuffers (for download)
	std::set<std::pair<u32, u32>> knownFramebufferRAMCopies_;

	bool gameUsesSequentialCopies_ = false;

	// Sampled in BeginFrame for safety.
	float renderWidth_;
	float renderHeight_;
	int pixelWidth_;
	int pixelHeight_;
	int bloomHack_ = 0;
	bool trueColor_ = false;

	// Used by post-processing shaders
	std::vector<Draw::Framebuffer *> extraFBOs_;

	bool needGLESRebinds_ = false;

	struct TempFBO {
		Draw::Framebuffer *fbo;
		int last_frame_used;
	};

	std::map<u64, TempFBO> tempFBOs_;

	std::vector<Draw::Framebuffer *> fbosToDelete_;

	// Aggressively delete unused FBOs to save gpu memory.
	enum {
		FBO_OLD_AGE = 5,
		FBO_OLD_USAGE_FLAG = 15,
	};
};

void CenterDisplayOutputRect(float *x, float *y, float *w, float *h, float origW, float origH, float frameW, float frameH, int rotation);
