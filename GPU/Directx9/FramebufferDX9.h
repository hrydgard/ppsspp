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
#include "d3d9.h"

#include "GPU/Directx9/helper/fbo.h"
// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.


#include "Globals.h"
#include "GPU/GPUCommon.h"

namespace DX9 {

struct GLSLProgram;
class TextureCacheDX9;

enum {
	FB_USAGE_DISPLAYED_FRAMEBUFFER = 1,
	FB_USAGE_RENDERTARGET = 2,
	FB_USAGE_TEXTURE = 4,
};

enum {	
	FB_NON_BUFFERED_MODE = 0,
	FB_BUFFERED_MODE = 1,
	FB_READFBOMEMORY_CPU = 2,
	FB_READFBOMEMORY_GPU = 3,
};

struct VirtualFramebufferDX9 {
	int last_frame_used;
	int last_frame_render;
	bool memoryUpdated; 

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

	GEBufferFormat format;  // virtual, right now they are all RGBA8888
	FBOColorDepth colorDepth;
	FBO *fbo;

	bool dirtyAfterDisplay;
	bool reallyDirtyAfterDisplay;  // takes frame skipping into account
};

void CenterRect(float *x, float *y, float *w, float *h,
								float origW, float origH, float frameW, float frameH);


class ShaderManagerDX9;

class FramebufferManagerDX9 {
public:
	FramebufferManagerDX9();
	~FramebufferManagerDX9();

	void SetTextureCache(TextureCacheDX9 *tc) {
		textureCache_ = tc;
	}
	void SetShaderManager(ShaderManagerDX9 *sm) {
		shaderManager_ = sm;
	}

	void DrawPixels(const u8 *framebuf, GEBufferFormat pixelFormat, int linesize);
	
	void DrawActiveTexture(LPDIRECT3DTEXTURE9 tex, float x, float y, float w, float h, float destW, float destH, bool flip = false, float uscale = 1.0f, float vscale = 1.0f);

	void DestroyAllFBOs();
	void DecimateFBOs();

	void BeginFrame();
	void EndFrame();
	void Resized();
	void DeviceLost();
	void CopyDisplayToOutput();
	void SetRenderFrameBuffer();  // Uses parameters computed from gstate	
	void UpdateFromMemory(u32 addr, int size, bool safe);

	void ReadFramebufferToMemory(VirtualFramebufferDX9 *vfb, bool sync = true);

	// TODO: Break out into some form of FBO manager
	VirtualFramebufferDX9 *GetVFBAt(u32 addr);
	VirtualFramebufferDX9 *GetDisplayVFB() {
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

	void NotifyFramebufferCopy(u32 src, u32 dest, int size);

	void DestroyFramebuf(VirtualFramebufferDX9 *vfb);

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

	VirtualFramebufferDX9 *displayFramebuf_;
	VirtualFramebufferDX9 *prevDisplayFramebuf_;
	VirtualFramebufferDX9 *prevPrevDisplayFramebuf_;
	int frameLastFramebufUsed;

	std::vector<VirtualFramebufferDX9 *> vfbs_;

	VirtualFramebufferDX9 *currentRenderVfb_;

	// Used by ReadFramebufferToMemory
	void BlitFramebuffer_(VirtualFramebufferDX9 *src, VirtualFramebufferDX9 *dst, bool flip = false, float upscale = 1.0f, float vscale = 1.0f);
	//void PackFramebufferDirectx9_(VirtualFramebufferDX9 *vfb);
	void PackFramebufferAsync_(VirtualFramebufferDX9 *vfb);
	void PackFramebufferSync_(VirtualFramebufferDX9 *vfb);
	
	// Used by DrawPixels
	LPDIRECT3DTEXTURE9 drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;

	u8 *convBuf;

	int plainColorLoc_;

	TextureCacheDX9 *textureCache_;
	ShaderManagerDX9 *shaderManager_;
	bool usePostShader_;
	bool postShaderAtOutputResolution_;
	
	// Used by post-processing shader
	std::vector<FBO *> extraFBOs_;

	bool resized_;
	bool useBufferedRendering_;

	std::vector<VirtualFramebufferDX9 *> bvfbs_; // blitting FBOs

	std::set<std::pair<u32, u32>> knownFramebufferCopies_;

#if 0
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif
};

};
