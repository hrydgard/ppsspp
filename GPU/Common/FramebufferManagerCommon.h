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
//
// Also, in TextureCache we should be able to unify texture-based depal.

#pragma once

#include <vector>
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Core/ConfigValues.h"
#include "GPU/GPU.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/Draw2D.h"

enum {
	FB_USAGE_DISPLAYED_FRAMEBUFFER = 1,
	FB_USAGE_RENDER_COLOR = 2,
	FB_USAGE_TEXTURE = 4,
	FB_USAGE_CLUT = 8,
	FB_USAGE_DOWNLOAD = 16,
	FB_USAGE_DOWNLOAD_CLEAR = 32,
	FB_USAGE_BLUE_TO_ALPHA = 64,
	FB_USAGE_FIRST_FRAME_SAVED = 128,
	FB_USAGE_RENDER_DEPTH = 256,
	FB_USAGE_COLOR_MIXED_DEPTH = 512,
	FB_USAGE_INVALIDATE_DEPTH = 1024,  // used to clear depth buffers.
};

enum {
	FB_NON_BUFFERED_MODE = 0,
	FB_BUFFERED_MODE = 1,
};

namespace Draw {
	class Framebuffer;
}

class VulkanFBO;
class ShaderWriter;

// We have to track VFBs and depth buffers together, since bits are shared between the color alpha channel
// and the stencil buffer on the PSP.
// Sometimes, virtual framebuffers need to share a Z buffer. We emulate this by copying from on to the next
// when such a situation is detected. In order to reliably detect this, we separately track depth buffers,
// and they know which color buffer they were used with last.
// Two VirtualFramebuffer can occupy the same address range as long as they have different fb_format.
// In that case, the one with the highest colorBindSeq number is the valid one.
struct VirtualFramebuffer {
	u32 fb_address;
	u32 z_address;  // If 0, it's a "RAM" framebuffer.
	u16 fb_stride;
	u16 z_stride;

	// The original PSP format of the framebuffer.
	// In reality they are all RGBA8888 for better quality but this is what the PSP thinks it is. This is necessary
	// when we need to interpret the bits directly (depal or buffer aliasing).
	// NOTE: CANNOT be changed after creation anymore!
	GEBufferFormat fb_format;

	Draw::Framebuffer *fbo;

	// width/height: The detected size of the current framebuffer, in original PSP pixels.
	u16 width;
	u16 height;

	// bufferWidth/bufferHeight: The pre-scaling size of the buffer itself. May only be bigger than or equal to width/height.
	// In original PSP pixels - actual framebuffer is this size times the render resolution multiplier.
	// The buffer may be used to render a width or height from 0 to these values without being recreated.
	u16 bufferWidth;
	u16 bufferHeight;

	// renderWidth/renderHeight: The scaled size we render at. May be scaled to render at higher resolutions.
	// These are simply bufferWidth/Height * renderScaleFactor and are thus redundant.
	u16 renderWidth;
	u16 renderHeight;

	// Attempt to keep track of a bounding rectangle of what's been actually drawn. Coarse, but might be smaller
	// than width/height if framebuffer has been enlarged. In PSP pixels.
	u16 drawnWidth;
	u16 drawnHeight;

	// The dimensions at which we are confident that we can read back this buffer without stomping on irrelevant memory.
	u16 safeWidth;
	u16 safeHeight;

	// The scale factor at which we are rendering (to achieve higher resolution).
	u8 renderScaleFactor;

	u16 usageFlags;

	// These are used to track state to try to avoid buffer size shifting back and forth.
	// You might think that doesn't happen since we mostly grow framebuffers, but we do resize down,
	// if the size has shrunk for a while and the framebuffer is also larger than the stride.
	// At this point, the "safe" size is probably a lie, and we have had various issues with readbacks, so this resizes down to avoid them.
	// An example would be a game that always uses the address 0x00154000 for temp buffers, and uses it for a full-screen effect for 3 frames, then goes back to using it for character shadows or something much smaller.
	u16 newWidth;
	u16 newHeight;

	// The frame number at which this was last resized.
	int lastFrameNewSize;

	// Tracking for downloads-to-CLUT.
	u16 clutUpdatedBytes;

	// Means that the whole image has already been read back to memory - used when combining small readbacks (gameUsesSequentialCopies_).
	bool memoryUpdated;

	// TODO: Fold into usageFlags?
	bool dirtyAfterDisplay;
	bool reallyDirtyAfterDisplay;  // takes frame skipping into account

	// Global sequence numbers for the last time these were bound.
	// Not based on frames at all. Can be used to determine new-ness of one framebuffer over another,
	// can even be within a frame.
	int colorBindSeq;
	int depthBindSeq;

	// These are mainly used for garbage collection purposes and similar.
	// Cannot be used to determine new-ness against a similar other buffer, since they are
	// only at frame granularity.
	int last_frame_used;
	int last_frame_attached;
	int last_frame_render;
	int last_frame_displayed;
	int last_frame_clut;
	int last_frame_failed;
	int last_frame_depth_updated;
	int last_frame_depth_render;

	// Convenience methods
	inline int WidthInBytes() const { return width * BufferFormatBytesPerPixel(fb_format); }
	inline int BufferWidthInBytes() const { return bufferWidth * BufferFormatBytesPerPixel(fb_format); }
	inline int FbStrideInBytes() const { return fb_stride * BufferFormatBytesPerPixel(fb_format); }
	inline int ZStrideInBytes() const { return z_stride * 2; }

	inline int Stride(RasterChannel channel) const { return channel == RASTER_COLOR ? fb_stride : z_stride; }
	inline u32 Address(RasterChannel channel) const { return channel == RASTER_COLOR ? fb_address : z_address; }
	inline GEBufferFormat Format(RasterChannel channel) const { return channel == RASTER_COLOR ? fb_format : GE_FORMAT_DEPTH16; }
	inline int BindSeq(RasterChannel channel) const { return channel == RASTER_COLOR ? colorBindSeq : depthBindSeq; }

	// Computed from stride.
	int BufferByteSize(RasterChannel channel) const { return BufferByteStride(channel) * height; }
	int BufferByteStride(RasterChannel channel) const {
		return channel == RASTER_COLOR ? fb_stride * (fb_format == GE_FORMAT_8888 ? 4 : 2) : z_stride * 2;
	}
	int BufferByteWidth(RasterChannel channel) const {
		return channel == RASTER_COLOR ? width * (fb_format == GE_FORMAT_8888 ? 4 : 2) : width * 2;
	}
};

struct FramebufferHeuristicParams {
	u32 fb_address;
	u32 z_address;
	u16 fb_stride;
	u16 z_stride;
	GEBufferFormat fb_format;
	bool isClearingDepth;
	bool isWritingDepth;
	bool isDrawing;
	bool isModeThrough;
	bool isBlending;
	int viewportWidth;
	int viewportHeight;
	int16_t regionWidth;
	int16_t regionHeight;
	int16_t scissorLeft;
	int16_t scissorTop;
	int16_t scissorRight;
	int16_t scissorBottom;
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
	BINDFBCOLOR_UNCACHED = 16,
};

enum DrawTextureFlags {
	DRAWTEX_NEAREST = 0,
	DRAWTEX_LINEAR = 1,
	DRAWTEX_TO_BACKBUFFER = 8,
	DRAWTEX_DEPTH = 16,
};

inline DrawTextureFlags operator | (const DrawTextureFlags &lhs, const DrawTextureFlags &rhs) {
	return DrawTextureFlags((u32)lhs | (u32)rhs);
}

enum class TempFBO {
	DEPAL,
	BLIT,
	// For copies of framebuffers (e.g. shader blending.)
	COPY,
	// Used for copies when setting color to depth.
	Z_COPY,
	// Used to copy stencil data, means we need a stencil backing.
	STENCIL,
};

inline Draw::DataFormat GEFormatToThin3D(GEBufferFormat geFormat) {
	switch (geFormat) {
	case GE_FORMAT_4444:
		return Draw::DataFormat::A4R4G4B4_UNORM_PACK16;
	case GE_FORMAT_5551:
		return Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
	case GE_FORMAT_565:
		return Draw::DataFormat::R5G6B5_UNORM_PACK16;
	case GE_FORMAT_8888:
		return Draw::DataFormat::R8G8B8A8_UNORM;
	case GE_FORMAT_DEPTH16:
		return Draw::DataFormat::D16;
	default:
		// TODO: Assert?
		return Draw::DataFormat::UNDEFINED;
	}
}

// Dimensions are in bytes, later steps get to convert back into real coordinates as appropriate.
// Makes it easy to see if blits match etc.
struct BlockTransferRect {
	VirtualFramebuffer *vfb;
	RasterChannel channel;  // We usually only deal with color, but we have limited depth block transfer support now.

	int x_bytes;
	int y;
	int w_bytes;
	int h;

	std::string ToString() const;

	int w_pixels() const {
		return w_bytes / BufferFormatBytesPerPixel(vfb->fb_format);
	}
	int x_pixels() const {
		return x_bytes / BufferFormatBytesPerPixel(vfb->fb_format);
	}
};

namespace Draw {
class DrawContext;
}

struct DrawPixelsEntry {
	Draw::Texture *tex;
	uint64_t contentsHash;
	int frameNumber;
};

struct GPUDebugBuffer;
class DrawEngineCommon;
class PresentationCommon;
class ShaderManagerCommon;
class TextureCacheCommon;

class FramebufferManagerCommon {
public:
	FramebufferManagerCommon(Draw::DrawContext *draw);
	virtual ~FramebufferManagerCommon();

	void SetTextureCache(TextureCacheCommon *tc) {
		textureCache_ = tc;
	}
	void SetShaderManager(ShaderManagerCommon * sm) {
		shaderManager_ = sm;
	}
	void SetDrawEngine(DrawEngineCommon *td) {
		drawEngine_ = td;
	}

	void Init(int msaaLevel);
	virtual void BeginFrame();
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);
	void DestroyFramebuf(VirtualFramebuffer *v);

	VirtualFramebuffer *DoSetRenderFrameBuffer(FramebufferHeuristicParams &params, u32 skipDrawReason);
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
	void SetDepthFrameBuffer(bool isClearingDepth);

	void RebindFramebuffer(const char *tag);
	std::vector<const VirtualFramebuffer *> GetFramebufferList() const;

	void CopyDisplayToOutput(bool reallyDirty);

	bool NotifyFramebufferCopy(u32 src, u32 dest, int size, GPUCopyFlag flags, u32 skipDrawReason);
	void PerformWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat fmt);
	void UpdateFromMemory(u32 addr, int size);
	void ApplyClearToMemory(int x1, int y1, int x2, int y2, u32 clearColor);
	bool PerformWriteStencilFromMemory(u32 addr, int size, WriteStencil flags);

	// We changed our depth mode, gotta start over.
	// Ideally, we should convert depth buffers here, not just clear them.
	void ClearAllDepthBuffers();

	// Returns true if it's sure this is a direct FBO->FBO transfer and it has already handle it.
	// In that case we hardly need to actually copy the bytes in VRAM, they will be wrong anyway (unless
	// read framebuffers is on, in which case this should always return false).
	// If this returns false, a memory copy will happen and NotifyBlockTransferAfter will be called.
	bool NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);

	// This gets called after the memory copy, in case NotifyBlockTransferBefore returned false.
	// Otherwise it doesn't get called.
	void NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp, u32 skipDrawReason);

	bool BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags, int layer);
	void ReadFramebufferToMemory(VirtualFramebuffer *vfb, int x, int y, int w, int h, RasterChannel channel, Draw::ReadbackMode mode);

	void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes);
	void DrawFramebufferToOutput(const u8 *srcPixels, int srcStride, GEBufferFormat srcPixelFormat);

	void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, RasterChannel channel, const char *tag);

	size_t NumVFBs() const { return vfbs_.size(); }

	u32 PrevDisplayFramebufAddr() const {
		return prevDisplayFramebuf_ ? prevDisplayFramebuf_->fb_address : 0;
	}
	u32 CurrentDisplayFramebufAddr() const {
		return displayFramebuf_ ? displayFramebuf_->fb_address : 0;
	}

	u32 DisplayFramebufAddr() const {
		return displayFramebufPtr_;
	}
	u32 DisplayFramebufStride() const {
		return displayStride_;
	}
	GEBufferFormat DisplayFramebufFormat() const {
		return displayFormat_;
	}

	bool UseBufferedRendering() const {
		return useBufferedRendering_;
	}

	// TODO: Maybe just include the last depth buffer address in this, too.
	bool MayIntersectFramebufferColor(u32 start) const {
		// Clear the cache/kernel bits.
		start &= 0x3FFFFFFF;
		if (Memory::IsVRAMAddress(start))
			start &= 0x041FFFFF;
		// Most games only have two framebuffers at the start.
		if (start >= framebufColorRangeEnd_ || start < PSP_GetVidMemBase()) {
			return false;
		}
		return true;
	}

	VirtualFramebuffer *GetCurrentRenderVFB() const {
		return currentRenderVfb_;
	}

	// This only checks for the color channel, and if there are multiple overlapping ones
	// with different color depth, this might get things wrong.
	// DEPRECATED FOR NEW USES - avoid whenever possible.
	VirtualFramebuffer *GetVFBAt(u32 addr) const;

	// This will only return exact matches of addr+stride+format.
	VirtualFramebuffer *GetExactVFB(u32 addr, int stride, GEBufferFormat format) const;

	// If this doesn't find the exact VFB, but one with a different color format with matching stride,
	// it'll resolve the newest one at address to the format requested, and return that.
	VirtualFramebuffer *ResolveVFB(u32 addr, int stride, GEBufferFormat format);

	// Utility to get the display VFB.
	VirtualFramebuffer *GetDisplayVFB();

	int GetRenderWidth() const { return currentRenderVfb_ ? currentRenderVfb_->renderWidth : 480; }
	int GetRenderHeight() const { return currentRenderVfb_ ? currentRenderVfb_->renderHeight : 272; }
	int GetTargetWidth() const { return currentRenderVfb_ ? currentRenderVfb_->width : 480; }
	int GetTargetHeight() const { return currentRenderVfb_ ? currentRenderVfb_->height : 272; }
	int GetTargetBufferWidth() const { return currentRenderVfb_ ? currentRenderVfb_->bufferWidth : 480; }
	int GetTargetBufferHeight() const { return currentRenderVfb_ ? currentRenderVfb_->bufferHeight : 272; }
	int GetTargetStride() const { return currentRenderVfb_ ? currentRenderVfb_->fb_stride : 512; }
	GEBufferFormat GetTargetFormat() const { return currentRenderVfb_ ? currentRenderVfb_->fb_format : displayFormat_; }

	void SetColorUpdated(int skipDrawReason) {
		if (currentRenderVfb_) {
			SetColorUpdated(currentRenderVfb_, skipDrawReason);
		}
	}
	void SetSafeSize(u16 w, u16 h);

	void NotifyRenderResized(int msaaLevel);
	virtual void NotifyDisplayResized();
	void NotifyConfigChanged();

	void CheckPostShaders();

	virtual void DestroyAllFBOs();

	virtual void DeviceLost();
	virtual void DeviceRestore(Draw::DrawContext *draw);

	Draw::Framebuffer *GetTempFBO(TempFBO reason, u16 w, u16 h);

	// Debug features
	virtual bool GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes);
	virtual bool GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer);
	virtual bool GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer);
	virtual bool GetOutputFramebuffer(GPUDebugBuffer &buffer);

	const std::vector<VirtualFramebuffer *> &Framebuffers() const {
		return vfbs_;
	}

	Draw2D *GetDraw2D() {
		return &draw2D_;
	}

	// If a vfb with the target format exists, resolve it (run CopyToColorFromOverlappingFramebuffers).
	// If it doesn't exist, create it and do the same.
	// Returns the resolved framebuffer.
	VirtualFramebuffer *ResolveFramebufferColorToFormat(VirtualFramebuffer *vfb, GEBufferFormat newFormat);

	Draw2DPipeline *Get2DPipeline(Draw2DShader shader);

	// If from==to, returns a copy pipeline.
	Draw2DPipeline *GetReinterpretPipeline(GEBufferFormat from, GEBufferFormat to, float *scaleFactorX);

	// Public to be used from the texture cache's depal shenanigans.
	void BlitUsingRaster(
		Draw::Framebuffer *src, float srcX1, float srcY1, float srcX2, float srcY2,
		Draw::Framebuffer *dest, float destX1, float destY1, float destX2, float destY2,
		bool linearFilter,
		int scaleFactor,  // usually unused, except for swizzle...
		Draw2DPipeline *pipeline, const char *tag);

	void ReleasePipelines();

	int GetMSAALevel() const {
		return msaaLevel_;
	}

	void DiscardFramebufferCopy() {
		currentFramebufferCopy_ = nullptr;
	}

	bool PresentedThisFrame() const;

protected:
	virtual void ReadbackFramebuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h, RasterChannel channel, Draw::ReadbackMode mode);
	// Used for when a shader is required, such as GLES.
	virtual bool ReadbackDepthbuffer(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint16_t *pixels, int pixelsStride, int destW, int destH, Draw::ReadbackMode mode);
	virtual bool ReadbackStencilbuffer(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint8_t *pixels, int pixelsStride, Draw::ReadbackMode mode);
	void SetViewport2D(int x, int y, int w, int h);
	Draw::Texture *MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags);

	void CopyToColorFromOverlappingFramebuffers(VirtualFramebuffer *dest);
	void CopyToDepthFromOverlappingFramebuffers(VirtualFramebuffer *dest);

	bool UpdateRenderSize(int msaaLevel);

	void FlushBeforeCopy();
	virtual void DecimateFBOs();  // keeping it virtual to let D3D do a little extra

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, RasterChannel channel, const char *tag);

	void CopyFramebufferForColorTexture(VirtualFramebuffer *dst, VirtualFramebuffer *src, int flags, int layer, bool *partial);

	void EstimateDrawingSize(u32 fb_address, int fb_stride, GEBufferFormat fb_format, int viewport_width, int viewport_height, int region_width, int region_height, int scissor_width, int scissor_height, int &drawing_width, int &drawing_height);

	void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb);
	static void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb);
	void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth);

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst, bool allowSizeMismatch = false);

	void ResizeFramebufFBO(VirtualFramebuffer *vfb, int w, int h, bool force = false, bool skipCopy = false);

	static bool ShouldDownloadFramebufferColor(const VirtualFramebuffer *vfb);
	static bool ShouldDownloadFramebufferDepth(const VirtualFramebuffer *vfb);
	void DownloadFramebufferOnSwitch(VirtualFramebuffer *vfb);

	bool FindTransferFramebuffer(u32 basePtr, int stride, int x, int y, int w, int h, int bpp, bool destination, BlockTransferRect *rect);

	VirtualFramebuffer *FindDownloadTempBuffer(VirtualFramebuffer *vfb, RasterChannel channel);
	virtual void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {}

	VirtualFramebuffer *CreateRAMFramebuffer(uint32_t fbAddress, int width, int height, int stride, GEBufferFormat format);

	void UpdateFramebufUsage(VirtualFramebuffer *vfb) const;

	int GetFramebufferLayers() const;

	static void SetColorUpdated(VirtualFramebuffer *dstBuffer, int skipDrawReason) {
		dstBuffer->memoryUpdated = false;
		dstBuffer->clutUpdatedBytes = 0;
		dstBuffer->dirtyAfterDisplay = true;
		dstBuffer->drawnWidth = dstBuffer->width;
		dstBuffer->drawnHeight = dstBuffer->height;
		if ((skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			dstBuffer->reallyDirtyAfterDisplay = true;
	}

	inline int GetBindSeqCount() {
		return fbBindSeqCount_++;
	}

	static SkipGPUReadbackMode GetSkipGPUReadbackMode();

	PresentationCommon *presentation_ = nullptr;

	Draw::DrawContext *draw_ = nullptr;

	TextureCacheCommon *textureCache_ = nullptr;
	ShaderManagerCommon *shaderManager_ = nullptr;
	DrawEngineCommon *drawEngine_ = nullptr;

	bool needBackBufferYSwap_ = false;

	u32 displayFramebufPtr_ = 0;
	u32 displayStride_ = 0;
	GEBufferFormat displayFormat_ = GE_FORMAT_565;
	u32 prevDisplayFramebufPtr_ = 0;

	int fbBindSeqCount_ = 0;

	VirtualFramebuffer *displayFramebuf_ = nullptr;
	VirtualFramebuffer *prevDisplayFramebuf_ = nullptr;
	VirtualFramebuffer *prevPrevDisplayFramebuf_ = nullptr;
	int frameLastFramebufUsed_ = 0;

	VirtualFramebuffer *currentRenderVfb_ = nullptr;

	Draw::Framebuffer *currentFramebufferCopy_ = nullptr;

	// The range of PSP memory that may contain FBOs.  So we can skip iterating.
	u32 framebufColorRangeEnd_ = 0;

	bool useBufferedRendering_ = false;
	bool postShaderIsUpscalingFilter_ = false;
	bool postShaderIsSupersampling_ = false;

	std::vector<VirtualFramebuffer *> vfbs_;
	std::vector<VirtualFramebuffer *> bvfbs_; // blitting framebuffers (for download)

	std::vector<DrawPixelsEntry> drawPixelsCache_;

	bool gameUsesSequentialCopies_ = false;

	// Sampled in BeginFrame/UpdateSize for safety.
	float renderWidth_ = 0.0f;
	float renderHeight_ = 0.0f;

	int msaaLevel_ = 0;
	int renderScaleFactor_ = 1;
	int pixelWidth_ = 0;
	int pixelHeight_ = 0;
	int bloomHack_ = 0;
	bool updatePostShaders_ = false;

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

	// Thin3D stuff for reinterpreting image data between the various 16-bit color formats.
	// Safe, not optimal - there might be input attachment tricks, etc, but we can't use them
	// since we don't want N different implementations.
	Draw2DPipeline *reinterpretFromTo_[4][4]{};

	// Common implementation of stencil buffer upload. Also not 100% optimal, but not performance
	// critical either.
	Draw::Pipeline *stencilWritePipeline_ = nullptr;
	Draw::SamplerState *stencilWriteSampler_ = nullptr;

	// Used on GLES where we can't directly readback depth or stencil, but here for simplicity.
	Draw::Pipeline *stencilReadbackPipeline_ = nullptr;
	Draw::SamplerState *stencilReadbackSampler_ = nullptr;
	Draw::Pipeline *depthReadbackPipeline_ = nullptr;
	Draw::SamplerState *depthReadbackSampler_ = nullptr;

	// Draw2D pipelines
	Draw2DPipeline *draw2DPipelineCopyColor_ = nullptr;
	Draw2DPipeline *draw2DPipelineColorRect2Lin_ = nullptr;
	Draw2DPipeline *draw2DPipelineCopyDepth_ = nullptr;
	Draw2DPipeline *draw2DPipelineEncodeDepth_ = nullptr;
	Draw2DPipeline *draw2DPipeline565ToDepth_ = nullptr;
	Draw2DPipeline *draw2DPipeline565ToDepthDeswizzle_ = nullptr;

	Draw2D draw2D_;
	// The fragment shaders are "owned" by the pipelines since they're 1:1.

	// Depth readback helper state
	u8 *convBuf_ = nullptr;
	u32 convBufSize_ = 0;
};

// Should probably live elsewhere.
bool GetOutputFramebuffer(Draw::DrawContext *draw, GPUDebugBuffer &buffer);
