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

#include "d3d9.h"

#include "GPU/Directx9/helper/fbo.h"
// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.


#include "Globals.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferCommon.h"

namespace DX9 {

struct GLSLProgram;
class TextureCacheDX9;

void CenterRect(float *x, float *y, float *w, float *h,
								float origW, float origH, float frameW, float frameH);


class ShaderManagerDX9;

class FramebufferManagerDX9 : public FramebufferManagerCommon {
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
	virtual void DoSetRenderFrameBuffer() override;  // Uses parameters computed from gstate
	void UpdateFromMemory(u32 addr, int size, bool safe);

	void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync = true);

	// TODO: Break out into some form of FBO manager
	VirtualFramebuffer *GetVFBAt(u32 addr);
	VirtualFramebuffer *GetDisplayVFB() {
		return GetVFBAt(displayFramebufPtr_);
	}

	std::vector<FramebufferInfo> GetFramebufferList();

	void NotifyFramebufferCopy(u32 src, u32 dest, int size);

	void DestroyFramebuf(VirtualFramebuffer *vfb);

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);

protected:
	virtual void DisableState() override;
	virtual void ClearBuffer() override;
	virtual void ClearDepthBuffer() override;

private:
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();

	void SetNumExtraFBOs(int num);

	// Used by ReadFramebufferToMemory
	void BlitFramebuffer_(VirtualFramebuffer *src, VirtualFramebuffer *dst, bool flip = false, float upscale = 1.0f, float vscale = 1.0f);
	void PackFramebufferDirectx9_(VirtualFramebuffer *vfb);
	
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

	std::vector<VirtualFramebuffer *> bvfbs_; // blitting FBOs

	std::set<std::pair<u32, u32>> knownFramebufferCopies_;

#if 0
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif
};

};
