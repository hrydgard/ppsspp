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

class ShaderManager;

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

	void DrawPixels(const u8 *framebuf, GEBufferFormat pixelFormat, int linesize, bool applyPostShader = false);

	// If texture != 0, will bind it.
	void DrawActiveTexture(GLuint texture, float x, float y, float w, float h, float destW, float destH, bool flip = false, float uscale = 1.0f, float vscale = 1.0f, GLSLProgram *program = 0);

	void DrawPlainColor(u32 color);

	void DestroyAllFBOs();
	void DecimateFBOs();

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

	void BindFramebufferDepth(VirtualFramebuffer *sourceframebuffer, VirtualFramebuffer *targetframebuffer);

	// For use when texturing from a framebuffer.  May create a duplicate if target.
	void BindFramebufferColor(VirtualFramebuffer *framebuffer);

	// Just for logging right now.  Might remove/change.
	void NotifyBlockTransfer(u32 dst, u32 src);

#ifdef USING_GLES2
  void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync = true);
#else
  void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync = false);
#endif 

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

	void NotifyFramebufferCopy(u32 src, u32 dest, int size);

	void DestroyFramebuf(VirtualFramebuffer *vfb);

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);

private:
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();

	void SetNumExtraFBOs(int num);

	u32 displayFramebufPtr_;
	u32 displayStride_;
	GEBufferFormat displayFormat_;

	VirtualFramebuffer *displayFramebuf_;
	VirtualFramebuffer *prevDisplayFramebuf_;
	VirtualFramebuffer *prevPrevDisplayFramebuf_;
	int frameLastFramebufUsed;

	std::vector<VirtualFramebuffer *> vfbs_;

	VirtualFramebuffer *currentRenderVfb_;

	// Used by ReadFramebufferToMemory
	void BlitFramebuffer_(VirtualFramebuffer *src, VirtualFramebuffer *dst, bool flip = false, float upscale = 1.0f, float vscale = 1.0f);
#ifndef USING_GLES2
	void PackFramebufferAsync_(VirtualFramebuffer *vfb);
#endif
	void PackFramebufferSync_(VirtualFramebuffer *vfb);

	// Used by DrawPixels
	unsigned int drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;

	u8 *convBuf;
	GLSLProgram *draw2dprogram_;
	GLSLProgram *plainColorProgram_;
	GLSLProgram *postShaderProgram_;
	int plainColorLoc_;
	int timeLoc_;

	TextureCache *textureCache_;
	ShaderManager *shaderManager_;
	bool usePostShader_;
	bool postShaderAtOutputResolution_;

	// Used by post-processing shader
	std::vector<FBO *> extraFBOs_;

	bool resized_;
	bool useBufferedRendering_;

	std::vector<VirtualFramebuffer *> bvfbs_; // blitting FBOs
	std::map<std::pair<int, int>, FBO *> renderCopies_;

	std::set<std::pair<u32, u32>> knownFramebufferCopies_;

#ifndef USING_GLES2
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif

	std::set<std::pair<u32, u32>> reportedBlits_;
};
