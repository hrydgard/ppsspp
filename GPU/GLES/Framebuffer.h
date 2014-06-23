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

#include <list>
#include <set>
#include <algorithm>

#include "gfx/gl_common.h"
#include "gfx_es2/fbo.h"
// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.


#include "../Globals.h"
#include "GPU/GPUCommon.h"

struct GLSLProgram;
class TextureCache;
class TransformDrawEngine;
class ShaderManager;

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

struct VirtualFramebuffer {
	int last_frame_used;
	int last_frame_attached;
	int last_frame_render;
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
	FBOColorDepth colorDepth;
	FBO *fbo;

	bool dirtyAfterDisplay;
	bool reallyDirtyAfterDisplay;  // takes frame skipping into account
};

void CenterRect(float *x, float *y, float *w, float *h,
								float origW, float origH, float frameW, float frameH);

#ifndef USING_GLES2
// Simple struct for asynchronous PBO readbacks
struct AsyncPBO {
	GLuint handle;
	u32 maxSize;

	u32 fb_address;
	u32 stride;
	u32 height;
	u32 size;
	GEBufferFormat format;
	bool reading;
};

#endif

class FramebufferManager {
public:
	FramebufferManager();
	~FramebufferManager();

	void SetTextureCache(TextureCache *tc) {
		textureCache_ = tc;
	}
	void SetShaderManager(ShaderManager *sm) {
		shaderManager_ = sm;
	}
	void SetTransformDrawEngine(TransformDrawEngine *td) {
		transformDraw_ = td;
	}

	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);

	void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);
	void DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader);

	// If texture != 0, will bind it.
	// x,y,w,h are relative to destW, destH which fill out the target completely.
	void DrawActiveTexture(GLuint texture, float x, float y, float w, float h, float destW, float destH, bool flip = false, float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f, GLSLProgram *program = 0);

	void DrawPlainColor(u32 color);

	void DestroyAllFBOs();
	void DecimateFBOs();

	void Init();
	void BeginFrame();
	void EndFrame();
	void Resized();
	void DeviceLost();
	void CopyDisplayToOutput();
	void DoSetRenderFrameBuffer();  // Uses parameters computed from gstate
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
	void UpdateFromMemory(u32 addr, int size, bool safe);
	void SetLineWidth();

	void BlitFramebufferDepth(VirtualFramebuffer *sourceframebuffer, VirtualFramebuffer *targetframebuffer);

	// For use when texturing from a framebuffer.  May create a duplicate if target.
	void BindFramebufferColor(VirtualFramebuffer *framebuffer, bool skipCopy = false);

	// Returns true if it's sure this is a direct FBO->FBO transfer and it has already handle it.
	// In that case we hardly need to actually copy the bytes in VRAM, they will be wrong anyway (unless
	// read framebuffers is on, in which case this should always return false).
	bool NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp);
	void NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int w, int h, int bpp);

	// Reads a rectangular subregion of a framebuffer to the right position in its backing memory.
	void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h);

	// TODO: Break out into some form of FBO manager
	VirtualFramebuffer *GetVFBAt(u32 addr);
	VirtualFramebuffer *GetDisplayVFB() {
		return GetVFBAt(displayFramebufPtr_);
	}
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);
	size_t NumVFBs() const { return vfbs_.size(); }

	std::vector<FramebufferInfo> GetFramebufferList();

	int GetRenderWidth() const { return currentRenderVfb_ ? currentRenderVfb_->renderWidth : 480; }
	int GetRenderHeight() const { return currentRenderVfb_ ? currentRenderVfb_->renderHeight : 272; }
	int GetTargetWidth() const { return currentRenderVfb_ ? currentRenderVfb_->width : 480; }
	int GetTargetHeight() const { return currentRenderVfb_ ? currentRenderVfb_->height : 272; }
	int GetTargetBufferWidth() const { return currentRenderVfb_ ? currentRenderVfb_->bufferWidth : 480; }
	int GetTargetBufferHeight() const { return currentRenderVfb_ ? currentRenderVfb_->bufferHeight : 272; }
	int GetTargetStride() const { return currentRenderVfb_ ? currentRenderVfb_->fb_stride : 512; }
	GEBufferFormat GetTargetFormat() const { return currentRenderVfb_ ? currentRenderVfb_->format : displayFormat_; }

	u32 PrevDisplayFramebufAddr() {
		return prevDisplayFramebuf_ ? (0x04000000 | prevDisplayFramebuf_->fb_address) : 0;
	}
	u32 DisplayFramebufAddr() {
		return displayFramebuf_ ? (0x04000000 | displayFramebuf_->fb_address) : 0;
	}

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

	bool MayIntersectFramebuffer(u32 start) {
		// Clear the cache/kernel bits.
		start = start & 0x3FFFFFFF;
		// Most games only have two framebuffers at the start.
		if (start >= framebufRangeEnd_ || start < PSP_GetVidMemBase()) {
			return false;
		}
		return true;
	}
	inline bool ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const;

	bool NotifyFramebufferCopy(u32 src, u32 dest, int size, bool isMemset = false);
	bool NotifyStencilUpload(u32 addr, int size);

	void DestroyFramebuf(VirtualFramebuffer *vfb);
	void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false);

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);

	void RebindFramebuffer();

	FBO *GetTempFBO(u16 w, u16 h, FBOColorDepth depth = FBO_8888);

private:
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();
	void FlushBeforeCopy();

	void FindTransferFramebuffers(VirtualFramebuffer *&dstBuffer, VirtualFramebuffer *&srcBuffer, u32 dstBasePtr, int dstStride, int &dstX, int &dstY, u32 srcBasePtr, int srcStride, int &srcX, int &srcY, int &srcWidth, int &srcHeight, int &dstWidth, int &dstHeight, int bpp) const;
	u32 FramebufferByteSize(const VirtualFramebuffer *vfb) const;

	void SetNumExtraFBOs(int num);

	void EstimateDrawingSize(int &drawing_width, int &drawing_height);
	static void DisableState();
	static void ClearBuffer();
	static void ClearDepthBuffer();
	static bool MaskedEqual(u32 addr1, u32 addr2);

	void SetColorUpdated(VirtualFramebuffer *dstBuffer) {
		dstBuffer->memoryUpdated = false;
		dstBuffer->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			dstBuffer->reallyDirtyAfterDisplay = true;
	}

	u32 displayFramebufPtr_;
	u32 displayStride_;
	GEBufferFormat displayFormat_;

	VirtualFramebuffer *displayFramebuf_;
	VirtualFramebuffer *prevDisplayFramebuf_;
	VirtualFramebuffer *prevPrevDisplayFramebuf_;
	int frameLastFramebufUsed;

	std::vector<VirtualFramebuffer *> vfbs_;

	VirtualFramebuffer *currentRenderVfb_;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer_(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, bool flip = false);
#ifndef USING_GLES2
	void PackFramebufferAsync_(VirtualFramebuffer *vfb);
#endif
	void PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	// Used by DrawPixels
	unsigned int drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;
	int drawPixelsTexW_;
	int drawPixelsTexH_;

	u8 *convBuf_;
	u32 convBufSize_;
	GLSLProgram *draw2dprogram_;
	GLSLProgram *plainColorProgram_;
	GLSLProgram *postShaderProgram_;
	GLSLProgram *stencilUploadProgram_;
	int plainColorLoc_;
	int timeLoc_;

	TextureCache *textureCache_;
	ShaderManager *shaderManager_;
	TransformDrawEngine *transformDraw_;
	bool usePostShader_;
	bool postShaderAtOutputResolution_;

	// Used by post-processing shader
	std::vector<FBO *> extraFBOs_;

	bool resized_;
	bool useBufferedRendering_;
	bool updateVRAM_;
	bool gameUsesSequentialCopies_;

	bool hackForce04154000Download_;

	// The range of PSP memory that may contain FBOs.  So we can skip iterating.
	u32 framebufRangeEnd_;

	struct TempFBO {
		FBO *fbo;
		int last_frame_used;
	};

	std::vector<VirtualFramebuffer *> bvfbs_; // blitting framebuffers (for download)
	std::map<u64, TempFBO> tempFBOs_;

	std::set<std::pair<u32, u32>> knownFramebufferRAMCopies_;

#ifndef USING_GLES2
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif
};
