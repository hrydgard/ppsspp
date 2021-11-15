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

#include "Common/GPU/thin3d.h"
// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.

#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"

struct GLSLProgram;
class TextureCacheGLES;
class DrawEngineGLES;
class ShaderManagerGLES;

class FramebufferManagerGLES : public FramebufferManagerCommon {
public:
	FramebufferManagerGLES(Draw::DrawContext *draw, GLRenderManager *render);
	~FramebufferManagerGLES();

	void SetTextureCache(TextureCacheGLES *tc);
	void SetShaderManager(ShaderManagerGLES *sm);
	void SetDrawEngine(DrawEngineGLES *td);

	// x,y,w,h are relative to destW, destH which fill out the target completely.
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) override;

	virtual void Init() override;
	void EndFrame();
	void Resized() override;

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	bool NotifyStencilUpload(u32 addr, int size, StencilUpload flags = StencilUpload::NEEDS_CLEAR) override;

	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;

protected:
	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, const char *tag) override;

	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	void Bind2DShader() override;
	void CompileDraw2DProgram();

	void PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	GLRenderManager *render_;

	u8 *convBuf_ = nullptr;
	u32 convBufSize_ = 0;

	GLRProgram *draw2dprogram_ = nullptr;

	GLRProgram *stencilUploadProgram_ = nullptr;
	int u_stencilUploadTex = -1;
	int u_stencilValue = -1;

	GLRProgram *depthDownloadProgram_ = nullptr;
	int u_depthDownloadTex = -1;
	int u_depthDownloadFactor = -1;
	int u_depthDownloadShift = -1;
	int u_depthDownloadTo8 = -1;
	
	// Cached uniform locs
	int u_draw2d_tex = -1;

	DrawEngineGLES *drawEngineGL_ = nullptr;

	struct Simple2DVertex {
		float pos[3];
		float uv[2];
	};
	GLRInputLayout *simple2DInputLayout_ = nullptr;
};
