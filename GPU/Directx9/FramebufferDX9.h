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
#include <map>

#include "d3d9.h"

#include "GPU/Directx9/helper/fbo.h"
// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.


#include "Globals.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferCommon.h"
#include "Core/Config.h"

namespace DX9 {

class TextureCacheDX9;
class TransformDrawEngineDX9;
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
	void SetTransformDrawEngine(TransformDrawEngineDX9 *td) {
		transformDraw_ = td;
	}

	virtual void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	virtual void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	virtual void DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) override;
	
	void DrawActiveTexture(LPDIRECT3DTEXTURE9 texture, float x, float y, float w, float h, float destW, float destH, bool flip = false, float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f, int uvRotation = ROTATION_LOCKED_HORIZONTAL);

	void DestroyAllFBOs();

	void EndFrame();
	void Resized();
	void DeviceLost();
	void CopyDisplayToOutput();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old);

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst);

	void BindFramebufferColor(int stage, VirtualFramebuffer *framebuffer, bool skipCopy = false);

	virtual void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) override;

	std::vector<FramebufferInfo> GetFramebufferList();

	virtual bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override;

	void DestroyFramebuf(VirtualFramebuffer *vfb);
	void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false);

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);
	static bool GetDisplayFramebuffer(GPUDebugBuffer &buffer);

	virtual void RebindFramebuffer() override;

	FBO *GetTempFBO(u16 w, u16 h, FBOColorDepth depth = FBO_8888);
	LPDIRECT3DSURFACE9 GetOffscreenSurface(LPDIRECT3DSURFACE9 similarSurface);

protected:
	virtual void DisableState() override;
	virtual void ClearBuffer() override;
	virtual void ClearDepthBuffer() override;
	virtual void FlushBeforeCopy() override;
	virtual void DecimateFBOs() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	virtual void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, bool flip = false) override;

	virtual void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) override;
	virtual void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb) override;
	virtual void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) override;

private:
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();

	void SetNumExtraFBOs(int num);

	void PackFramebufferDirectx9_(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	static bool GetRenderTargetFramebuffer(LPDIRECT3DSURFACE9 renderTarget, LPDIRECT3DSURFACE9 offscreen, int w, int h, GPUDebugBuffer &buffer);
	
	// Used by DrawPixels
	LPDIRECT3DTEXTURE9 drawPixelsTex_;
	int drawPixelsTexW_;
	int drawPixelsTexH_;

	u8 *convBuf;

	int plainColorLoc_;
	LPDIRECT3DPIXELSHADER9 stencilUploadPS_;
	LPDIRECT3DVERTEXSHADER9 stencilUploadVS_;
	bool stencilUploadFailed_;

	TextureCacheDX9 *textureCache_;
	ShaderManagerDX9 *shaderManager_;
	TransformDrawEngineDX9 *transformDraw_;
	bool usePostShader_;
	bool postShaderAtOutputResolution_;
	
	// Used by post-processing shader
	std::vector<FBO *> extraFBOs_;

	bool resized_;
	bool gameUsesSequentialCopies_;

	struct TempFBO {
		FBO *fbo;
		int last_frame_used;
	};
	struct OffscreenSurface {
		LPDIRECT3DSURFACE9 surface;
		int last_frame_used;
	};

	std::vector<VirtualFramebuffer *> bvfbs_; // blitting FBOs
	std::map<u64, TempFBO> tempFBOs_;
	std::map<u64, OffscreenSurface> offscreenSurfaces_;

#if 0
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif
};

};
