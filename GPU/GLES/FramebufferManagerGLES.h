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

#include "ext/native/thin3d/thin3d.h"
// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.

#include "Core/Config.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/FramebufferCommon.h"
#include "thin3d/GLRenderManager.h"

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

	void DestroyAllFBOs();

	virtual void Init() override;
	void EndFrame();
	void Resized() override;
	void DeviceLost();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) override;

	// For use when texturing from a framebuffer.  May create a duplicate if target.
	void BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags);

	bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override;

	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;

	void DeviceRestore(Draw::DrawContext *draw);

protected:
	void SetViewport2D(int x, int y, int w, int h) override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;

	bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) override;
	void Bind2DShader() override;
	void BindPostShader(const PostShaderUniforms &uniforms) override;
	void ShowPostShaderError(const std::string &errorMessage);
	void CompileDraw2DProgram();
	void CompilePostShader();

	void PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	GLRenderManager *render_;

	// Used by DrawPixels
	GLRTexture *drawPixelsTex_ = nullptr;
	GEBufferFormat drawPixelsTexFormat_ = GE_FORMAT_INVALID;
	int drawPixelsTexW_ = 0;
	int drawPixelsTexH_ = 0;

	u8 *convBuf_ = nullptr;
	u32 convBufSize_ = 0;
	GLRProgram *draw2dprogram_ = nullptr;
	GLRProgram *postShaderProgram_ = nullptr;
	std::vector<GLRShader *> postShaderModules_;

	GLRProgram *stencilUploadProgram_ = nullptr;
	int u_stencilUploadTex = -1;
	int u_stencilValue = -1;
	int u_postShaderTex = -1;
	
	// Cached uniform locs
	int u_draw2d_tex = -1;

	int plainColorLoc_ = -1;
	int videoLoc_ = -1;
	int timeLoc_ = -1;
	int pixelDeltaLoc_ = -1;
	int deltaLoc_ = -1;

	TextureCacheGLES *textureCacheGL_ = nullptr;
	ShaderManagerGLES *shaderManagerGL_ = nullptr;
	DrawEngineGLES *drawEngineGL_ = nullptr;

	struct Simple2DVertex {
		float pos[3];
		float uv[2];
	};
	GLRInputLayout *simple2DInputLayout_ = nullptr;
};
