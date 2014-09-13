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

class TextureCacheDX9;
class TransformDrawEngineDX9;
class ShaderManagerDX9;

void CenterRect(float *x, float *y, float *w, float *h,
								float origW, float origH, float frameW, float frameH);


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
	void SetTransformDrawEngine(TransformDrawEngineDX9 *td) {
		transformDraw_ = td;
	}

	virtual void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	virtual void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	virtual void DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) override;
	
	void DrawActiveTexture(LPDIRECT3DTEXTURE9 texture, float x, float y, float w, float h, float destW, float destH, bool flip = false, float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);

	void DestroyAllFBOs();

	void EndFrame();
	void Resized();
	void DeviceLost();
	void CopyDisplayToOutput();

	virtual void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) override;

	std::vector<FramebufferInfo> GetFramebufferList();

	bool NotifyFramebufferCopy(u32 src, u32 dest, int size, bool isMemset = false);
	bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false);

	void DestroyFramebuf(VirtualFramebuffer *vfb);
	void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false);

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);

	virtual void RebindFramebuffer() override;

protected:
	virtual void DisableState() override;
	virtual void ClearBuffer() override;
	virtual void ClearDepthBuffer() override;
	virtual void FlushBeforeCopy() override;
	virtual void DecimateFBOs() override;

	virtual void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) override;
	virtual void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb) override;
	virtual void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) override;

private:
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();

	void SetNumExtraFBOs(int num);

	// Used by ReadFramebufferToMemory
	void BlitFramebuffer_(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, bool flip = false);
	void PackFramebufferDirectx9_(VirtualFramebuffer *vfb);
	
	// Used by DrawPixels
	LPDIRECT3DTEXTURE9 drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;

	u8 *convBuf;

	int plainColorLoc_;

	TextureCacheDX9 *textureCache_;
	ShaderManagerDX9 *shaderManager_;
	TransformDrawEngineDX9 *transformDraw_;
	bool usePostShader_;
	bool postShaderAtOutputResolution_;
	
	// Used by post-processing shader
	std::vector<FBO *> extraFBOs_;

	bool resized_;
	bool gameUsesSequentialCopies_;

	std::vector<VirtualFramebuffer *> bvfbs_; // blitting FBOs

	std::set<std::pair<u32, u32>> knownFramebufferCopies_;

#if 0
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif
};

};
