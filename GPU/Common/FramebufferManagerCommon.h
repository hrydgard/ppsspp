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

// TODO: We now have the tools in thin3d to nearly eliminate the backend-specific framebuffer managers.
// Here's a list of functionality to unify into FramebufferManagerCommon:
// * DrawActiveTexture
// * BlitFramebuffer
// * StencilBuffer*.cpp
//
// Also, in TextureCache we should be able to unify texture-based depal.

#pragma once

#include <vector>
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Core/MemMap.h"
#include "GPU/GPU.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUInterface.h"
#include "Common/GPU/thin3d.h"

enum {
	FB_USAGE_DISPLAYED_FRAMEBUFFER = 1,
	FB_USAGE_RENDERTARGET = 2,
	FB_USAGE_TEXTURE = 4,
	FB_USAGE_CLUT = 8,
	FB_USAGE_DOWNLOAD = 16,
	FB_USAGE_DOWNLOAD_CLEAR = 32,
	FB_USAGE_BLUE_TO_ALPHA = 64,
};

enum {
	FB_NON_BUFFERED_MODE = 0,
	FB_BUFFERED_MODE = 1,
};

namespace Draw {
	class Framebuffer;
}

class VulkanFBO;

struct VirtualFramebuffer {
	u32 fb_address;
	u32 z_address;  // If 0, it's a "RAM" framebuffer.
	int fb_stride;
	int z_stride;

	GEBufferFormat format;  // virtual, in reality they are all RGBA8888 for better quality but we can reinterpret that as necessary

	// width/height: The detected size of the current framebuffer, in original PSP pixels.
	u16 width;
	u16 height;

	// bufferWidth/bufferHeight: The pre-scaling size of the buffer itself. May only be bigger than or equal to width/height.
	// Actual physical buffer is this size times the render resolution multiplier.
	// The buffer may be used to render a width or height from 0 to these values without being recreated.
	u16 bufferWidth;
	u16 bufferHeight;

	// renderWidth/renderHeight: The scaled size we render at. May be scaled to render at higher resolutions.
	// The physical buffer may be larger than renderWidth/renderHeight.
	u16 renderWidth;
	u16 renderHeight;

	float renderScaleFactor;

	u16 usageFlags;

	u16 newWidth;
	u16 newHeight;

	int lastFrameNewSize;

	Draw::Framebuffer *fbo;

	u16 drawnWidth;
	u16 drawnHeight;
	GEBufferFormat drawnFormat;
	u16 safeWidth;
	u16 safeHeight;

	bool dirtyAfterDisplay;
	bool reallyDirtyAfterDisplay;  // takes frame skipping into account

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
};

struct FramebufferHeuristicParams {
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
	BINDFBCOLOR_MAY_COPY_WITH_UV = 3,  // includes BINDFBCOLOR_MAY_COPY
	BINDFBCOLOR_APPLY_TEX_OFFSET = 4,
	// Used when rendering to a temporary surface (e.g. not the current render target.)
	BINDFBCOLOR_FORCE_SELF = 8,
};

enum DrawTextureFlags {
	DRAWTEX_NEAREST = 0,
	DRAWTEX_LINEAR = 1,
	DRAWTEX_KEEP_STENCIL_ALPHA = 4,
	DRAWTEX_TO_BACKBUFFER = 8,
};

inline DrawTextureFlags operator | (const DrawTextureFlags &lhs, const DrawTextureFlags &rhs) {
	return DrawTextureFlags((u32)lhs | (u32)rhs);
}

enum class StencilUpload {
	NEEDS_CLEAR,
	STENCIL_IS_ZERO,
};

enum class TempFBO {
	DEPAL,
	BLIT,
	// For copies of framebuffers (e.g. shader blending.)
	COPY,
	// For another type of framebuffers that can happen together with COPY (see Outrun)
	REINTERPRET,
	// Used to copy stencil data, means we need a stencil backing.
	STENCIL,
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
class DrawEngineCommon;
class PresentationCommon;
class ShaderManagerCommon;
class TextureCacheCommon;

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
			_dbg_assert_msg_(vfb, "DoSetRenderFramebuffer must return a valid framebuffer.");
			_dbg_assert_msg_(currentRenderVfb_, "DoSetRenderFramebuffer must set a valid framebuffer.");
			return vfb;
		}
	}
	void RebindFramebuffer(const char *tag);
	std::vector<FramebufferInfo> GetFramebufferList();

	void CopyDisplayToOutput(bool reallyDirty);

	bool NotifyFramebufferCopy(u32 src, u32 dest, int size, bool isMemset, u32 skipDrawReason);
	void NotifyVideoUpload(u32 addr, int size, int width, GEBufferFormat fmt);
	void UpdateFromMemory(u32 addr, int size, bool safe);
	void ApplyClearToMemory(int x1, int y1, int x2, int y2, u32 clearColor);
	virtual bool NotifyStencilUpload(u32 addr, int size, StencilUpload flags = StencilUpload::NEEDS_CLEAR) = 0;
	// Returns true if it's sure this is a direct FBO->FBO transfer and it has already handle it.
	// In that case we hardly need to actually copy the bytes in VRAM, they will be wrong anyway (unless
	// read framebuffers is on, in which case this should always return false).
	bool NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);
	void NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);

	bool BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags);
	void ReadFramebufferToMemory(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes);
	void DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride);

	void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);

	size_t NumVFBs() const { return vfbs_.size(); }

	u32 PrevDisplayFramebufAddr() {
		return prevDisplayFramebuf_ ? prevDisplayFramebuf_->fb_address : 0;
	}
	u32 DisplayFramebufAddr() {
		return displayFramebuf_ ? displayFramebuf_->fb_address : 0;
	}

	u32 DisplayFramebufStride() {
		return displayFramebuf_ ? displayStride_ : 0;
	}
	GEBufferFormat DisplayFramebufFormat() {
		return displayFramebuf_ ? displayFormat_ : GE_FORMAT_INVALID;
	}

	bool UseBufferedRendering() {
		return useBufferedRendering_;
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
	virtual void DestroyAllFBOs();

	virtual void DeviceLost();
	virtual void DeviceRestore(Draw::DrawContext *draw);

	Draw::Framebuffer *GetTempFBO(TempFBO reason, u16 w, u16 h);

	// Debug features
	virtual bool GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes);
	virtual bool GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer);
	virtual bool GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer);
	virtual bool GetOutputFramebuffer(GPUDebugBuffer &buffer);

	const std::vector<VirtualFramebuffer *> &Framebuffers() {
		return vfbs_;
	}
	void ReinterpretFramebuffer(VirtualFramebuffer *vfb, GEBufferFormat oldFormat, GEBufferFormat newFormat);

protected:
	virtual void PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	void SetViewport2D(int x, int y, int w, int h);
	Draw::Texture *MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);
	virtual void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) = 0;
	virtual void Bind2DShader() = 0;

	bool UpdateSize();

	void FlushBeforeCopy();
	virtual void DecimateFBOs();  // keeping it virtual to let D3D do a little extra

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	virtual void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, const char *tag) = 0;
	void CopyFramebufferForColorTexture(VirtualFramebuffer *dst, VirtualFramebuffer *src, int flags);

	void EstimateDrawingSize(u32 fb_address, GEBufferFormat fb_format, int viewport_width, int viewport_height, int region_width, int region_height, int scissor_width, int scissor_height, int fb_stride, int &drawing_width, int &drawing_height);
	u32 ColorBufferByteSize(const VirtualFramebuffer *vfb) const;

	void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb);
	void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged);
	void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth);

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst);

	void ResizeFramebufFBO(VirtualFramebuffer *vfb, int w, int h, bool force = false, bool skipCopy = false);
	void ShowScreenResolution();

	bool ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const;
	void DownloadFramebufferOnSwitch(VirtualFramebuffer *vfb);
	void FindTransferFramebuffers(VirtualFramebuffer *&dstBuffer, VirtualFramebuffer *&srcBuffer, u32 dstBasePtr, int dstStride, int &dstX, int &dstY, u32 srcBasePtr, int srcStride, int &srcX, int &srcY, int &srcWidth, int &srcHeight, int &dstWidth, int &dstHeight, int bpp);
	VirtualFramebuffer *FindDownloadTempBuffer(VirtualFramebuffer *vfb);
	virtual void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {}

	VirtualFramebuffer *CreateRAMFramebuffer(uint32_t fbAddress, int width, int height, int stride, GEBufferFormat format);

	void UpdateFramebufUsage(VirtualFramebuffer *vfb);

	static void SetColorUpdated(VirtualFramebuffer *dstBuffer, int skipDrawReason) {
		dstBuffer->memoryUpdated = false;
		dstBuffer->clutUpdatedBytes = 0;
		dstBuffer->dirtyAfterDisplay = true;
		dstBuffer->drawnWidth = dstBuffer->width;
		dstBuffer->drawnHeight = dstBuffer->height;
		dstBuffer->drawnFormat = dstBuffer->format;
		if ((skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			dstBuffer->reallyDirtyAfterDisplay = true;
	}

	PresentationCommon *presentation_ = nullptr;

	Draw::DrawContext *draw_ = nullptr;
	TextureCacheCommon *textureCache_ = nullptr;
	ShaderManagerCommon *shaderManager_ = nullptr;
	DrawEngineCommon *drawEngine_ = nullptr;
	bool needBackBufferYSwap_ = false;

	u32 displayFramebufPtr_ = 0;
	u32 displayStride_ = 0;
	GEBufferFormat displayFormat_;
	u32 prevDisplayFramebufPtr_ = 0;

	VirtualFramebuffer *displayFramebuf_ = nullptr;
	VirtualFramebuffer *prevDisplayFramebuf_ = nullptr;
	VirtualFramebuffer *prevPrevDisplayFramebuf_ = nullptr;
	int frameLastFramebufUsed_ = 0;

	VirtualFramebuffer *currentRenderVfb_ = nullptr;

	// The range of PSP memory that may contain FBOs.  So we can skip iterating.
	u32 framebufRangeEnd_ = 0;

	bool useBufferedRendering_ = false;
	bool postShaderIsUpscalingFilter_ = false;
	bool postShaderIsSupersampling_ = false;

	std::vector<VirtualFramebuffer *> vfbs_;
	std::vector<VirtualFramebuffer *> bvfbs_; // blitting framebuffers (for download)

	bool gameUsesSequentialCopies_ = false;

	// Sampled in BeginFrame/UpdateSize for safety.
	float renderWidth_ = 0.0f;
	float renderHeight_ = 0.0f;
	float renderScaleFactor_ = 1.0f;
	int pixelWidth_;
	int pixelHeight_;
	int bloomHack_ = 0;

	bool needGLESRebinds_ = false;
	Draw::DataFormat preferredPixelsFormat_ = Draw::DataFormat::R8G8B8A8_UNORM;

	struct TempFBOInfo {
		Draw::Framebuffer *fbo;
		int last_frame_used;
	};

	std::unordered_map<u64, TempFBOInfo> tempFBOs_;

	std::vector<Draw::Framebuffer *> fbosToDelete_;

	// Aggressively delete unused FBOs to save gpu memory.
	enum {
		FBO_OLD_AGE = 5,
		FBO_OLD_USAGE_FLAG = 15,
	};

	// Thin3D stuff for reinterpreting image data between the various 16-bit formats.
	// Safe, not optimal - there might be input attachment tricks, etc, but we can't use them
	// since we don't want N different implementations.
	Draw::Pipeline *reinterpretFromTo_[3][3]{};
	Draw::ShaderModule *reinterpretVS_ = nullptr;
	Draw::SamplerState *reinterpretSampler_ = nullptr;
	Draw::Buffer *reinterpretVBuf_ = nullptr;
};
