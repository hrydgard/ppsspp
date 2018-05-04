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

#include <wiiu/gx2.h>

// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.

#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "Core/Config.h"
#include "Common/GPU/thin3d.h"

class TextureCacheGX2;
class DrawEngineGX2;
class ShaderManagerGX2;

class FramebufferManagerGX2 : public FramebufferManagerCommon {
public:
	FramebufferManagerGX2(Draw::DrawContext *draw);
	~FramebufferManagerGX2();

	void SetTextureCache(TextureCacheGX2 *tc);
	void SetShaderManager(ShaderManagerGX2 *sm);
	void SetDrawEngine(DrawEngineGX2 *td);
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) override;

	void EndFrame();
	void DeviceLost();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	void BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags);

	virtual bool NotifyStencilUpload(u32 addr, int size, StencilUpload flags = StencilUpload::NEEDS_CLEAR) override;

	// TODO: Remove
	void *GetDynamicQuadBuffer() { return quadBuffer_; }

protected:
	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;

	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void Bind2DShader() override;
	void PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	void SimpleBlit(Draw::Framebuffer *dest, float destX1, float destY1, float destX2, float destY2, Draw::Framebuffer *src, float srcX1, float srcY1, float srcX2, float srcY2, bool linearFilter);

	GX2ContextState *context_;

	GX2FetchShader quadFetchShader_ = {};
	static float fsQuadBuffer_[20];
	const u32 quadStride_ = sizeof(fsQuadBuffer_) / 4;
	// Dynamic
	float *quadBuffer_;

	int plainColorLoc_;
	struct __attribute__((aligned(64))) StencilValueUB {
		u32_le u_stencilValue[4];
	};
	StencilValueUB *stencilValueBuffer_ = nullptr;
	GX2StencilMaskReg stencilMaskStates_[256]{};

	TextureCacheGX2 *textureCacheGX2_;
	ShaderManagerGX2 *shaderManagerGX2_;
	DrawEngineGX2 *drawEngineGX2_;

	static const GX2AttribStream g_QuadAttribStream[2];
};
