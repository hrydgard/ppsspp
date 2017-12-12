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
	void SetShaderManager(ShaderManagerD3D11 *sm);
	void SetDrawEngine(DrawEngineD3D11 *td);
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) override;

	void DestroyAllFBOs();

	void EndFrame();
	void Resized() override;
	void DeviceLost();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) override;

	void BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags);

	virtual bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override;
	void RebindFramebuffer();

	// TODO: Remove
	ID3D11Buffer *GetDynamicQuadBuffer() {
		return quadBuffer_;
	}

protected:
	void DisableState() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;

	bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void CompilePostShader();
	void BindPostShader(const PostShaderUniforms &uniforms) override;
	void Bind2DShader() override;
	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) override;
	void PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	void SimpleBlit(
		Draw::Framebuffer *dest, float destX1, float destY1, float destX2, float destY2,
		Draw::Framebuffer *src, float srcX1, float srcY1, float srcX2, float srcY2,
		bool linearFilter);

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;
	D3D_FEATURE_LEVEL featureLevel_;

	// Used by DrawPixels
	ID3D11Texture2D *drawPixelsTex_ = nullptr;
	ID3D11ShaderResourceView *drawPixelsTexView_ = nullptr;
	int drawPixelsTexW_ = 0;
	int drawPixelsTexH_ = 0;

	ID3D11VertexShader *quadVertexShader_;
	ID3D11PixelShader *quadPixelShader_;
	ID3D11InputLayout *quadInputLayout_;
	// Dynamic
	ID3D11Buffer *quadBuffer_;
	ID3D11Buffer *fsQuadBuffer_;
	const UINT quadStride_ = 20;
	const UINT quadOffset_ = 0;
	static const D3D11_INPUT_ELEMENT_DESC g_QuadVertexElements[2];

	u8 *convBuf = nullptr;

	int plainColorLoc_;
	ID3D11PixelShader *stencilUploadPS_ = nullptr;
	ID3D11VertexShader *stencilUploadVS_ = nullptr;
	ID3D11InputLayout *stencilUploadInputLayout_ = nullptr;
	ID3D11Buffer *stencilValueBuffer_ = nullptr;
	ID3D11DepthStencilState *stencilMaskStates_[256]{};

	TextureCacheD3D11 *textureCacheD3D11_;
	ShaderManagerD3D11 *shaderManagerD3D11_;
	DrawEngineD3D11 *drawEngineD3D11_;

	// Used by post-processing shader
	// Postprocessing
	ID3D11VertexShader *postVertexShader_ = nullptr;
	ID3D11PixelShader *postPixelShader_ = nullptr;
	ID3D11InputLayout *postInputLayout_ = nullptr;
	ID3D11Buffer *postConstants_ = nullptr;
	static const D3D11_INPUT_ELEMENT_DESC g_PostVertexElements[2];

#if 0
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
#endif
};
