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

#include <d3d11.h>

// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.


#include "Globals.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferCommon.h"
#include "Core/Config.h"
#include "ext/native/thin3d/thin3d.h"

class TextureCacheD3D11;
class DrawEngineD3D11;
class ShaderManagerD3D11;

class FramebufferManagerD3D11 : public FramebufferManagerCommon {
public:
	FramebufferManagerD3D11(Draw::DrawContext *draw);
	~FramebufferManagerD3D11();

	void SetTextureCache(TextureCacheD3D11 *tc);
	void SetShaderManager(ShaderManagerD3D11 *sm) {
		shaderManager_ = sm;
	}
	void SetDrawEngine(DrawEngineD3D11 *td) {
		drawEngine_ = td;
	}

	virtual void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	virtual void DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) override;

	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, bool linearFilter);

	void DestroyAllFBOs(bool forceDelete);

	void EndFrame();
	void Resized() override;
	void DeviceLost();
	void CopyDisplayToOutput();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) override;

	void BindFramebufferColor(int stage, VirtualFramebuffer *framebuffer, int flags);

	void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) override;
	void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) override;

	std::vector<FramebufferInfo> GetFramebufferList();

	virtual bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override;

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);
	bool GetOutputFramebuffer(GPUDebugBuffer &buffer);

	virtual void RebindFramebuffer() override;

protected:
	void DisableState() override;
	void ClearBuffer(bool keepState = false) override;
	void FlushBeforeCopy() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;

	bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();

	void PackFramebufferD3D11_(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	void PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;

	// Used by DrawPixels
	ID3D11Texture2D *drawPixelsTex_;
	ID3D11ShaderResourceView *drawPixelsTexView_;
	int drawPixelsTexW_;
	int drawPixelsTexH_;

	ID3D11VertexShader *pFramebufferVertexShader_;
	ID3D11PixelShader *pFramebufferPixelShader_;
	ID3D11InputLayout *pFramebufferVertexDecl_;

	u8 *convBuf;

	int plainColorLoc_;
	ID3D11PixelShader *stencilUploadPS_;
	ID3D11VertexShader *stencilUploadVS_;
	ID3D11InputLayout *stencilUploadInputLayout_;
	bool stencilUploadFailed_;

	TextureCacheD3D11 *textureCacheD3D11_;
	ShaderManagerD3D11 *shaderManager_;
	DrawEngineD3D11 *drawEngine_;

	ID3D11Buffer *vbFullScreenRect_;
	UINT vbFullScreenStride_ = 20;
	UINT vbFullScreenOffset_ = 0;

	// Used by post-processing shader
	std::vector<Draw::Framebuffer *> extraFBOs_;

	bool resized_;

	struct TempFBO {
		Draw::Framebuffer *fbo;
		int last_frame_used;
	};

	std::map<u64, TempFBO> tempFBOs_;

#if 0
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif
};