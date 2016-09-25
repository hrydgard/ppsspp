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
// Keeps track of allocated FBOs.
// Also provides facilities for drawing and later converting raw
// pixel data.


#include "Globals.h"
#include "GPU/GPUCommon.h"
#include "GPU/GLES/FBO.h"
#include "GPU/Common/FramebufferCommon.h"
#include "Core/Config.h"

struct GLSLProgram;
class TextureCache;
class DrawEngineGLES;
class ShaderManager;

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

struct CardboardSettings {
	bool enabled;
	float leftEyeXPosition;
	float rightEyeXPosition;
	float screenYPosition;
	float screenWidth;
	float screenHeight;
};

class FramebufferManager : public FramebufferManagerCommon {
public:
	FramebufferManager();
	~FramebufferManager();

	void SetTextureCache(TextureCache *tc) {
		textureCache_ = tc;
	}
	void SetShaderManager(ShaderManager *sm) {
		shaderManager_ = sm;
	}
	void SetTransformDrawEngine(DrawEngineGLES *td) {
		transformDraw_ = td;
	}

	void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	void DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) override;

	// If texture != 0, will bind it.
	// x,y,w,h are relative to destW, destH which fill out the target completely.
	void DrawActiveTexture(GLuint texture, float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, GLSLProgram *program, int uvRotation);

	void DestroyAllFBOs(bool forceDelete);

	virtual void Init() override;
	void EndFrame();
	void Resized();
	void DeviceLost();
	void CopyDisplayToOutput();
	void SetLineWidth();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old);

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst);

	// For use when texturing from a framebuffer.  May create a duplicate if target.
	void BindFramebufferColor(int stage, u32 fbRawAddress, VirtualFramebuffer *framebuffer, int flags);

	// Reads a rectangular subregion of a framebuffer to the right position in its backing memory.
	void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) override;
	void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) override;

	std::vector<FramebufferInfo> GetFramebufferList();

	bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override;

	void DestroyFramebuf(VirtualFramebuffer *vfb) override;
	void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false, bool skipCopy = false) override;

	bool GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes);
	bool GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer);
	bool GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer);
	static bool GetOutputFramebuffer(GPUDebugBuffer &buffer);

	virtual void RebindFramebuffer() override;

	FBO *GetTempFBO(u16 w, u16 h, FBOColorDepth depth = FBO_8888);

	// Cardboard Settings Calculator
	struct CardboardSettings * GetCardboardSettings(struct CardboardSettings * cardboardSettings);

protected:
	void DisableState() override;
	void ClearBuffer(bool keepState = false) override;
	void FlushBeforeCopy() override;
	void DecimateFBOs() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;

	void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) override;
	void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) override;
	void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) override;
	bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:
	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height);
	void UpdatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight);
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();

	void SetNumExtraFBOs(int num);

	void PackFramebufferAsync_(VirtualFramebuffer *vfb);  // Not used under ES currently
	void PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h);
	void PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	// Used by DrawPixels
	unsigned int drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;
	int drawPixelsTexW_;
	int drawPixelsTexH_;

	u8 *convBuf_;
	u32 convBufSize_;
	GLSLProgram *draw2dprogram_;
	GLSLProgram *plainColorProgram_;
	GLSLProgram *postShaderProgram_;
	GLSLProgram *stencilUploadProgram_;
	int plainColorLoc_;
	int timeLoc_;
	int pixelDeltaLoc_;
	int deltaLoc_;

	TextureCache *textureCache_;
	ShaderManager *shaderManager_;
	DrawEngineGLES *transformDraw_;

	// Used by post-processing shader
	std::vector<FBO *> extraFBOs_;

	bool resized_;

	struct TempFBO {
		FBO *fbo;
		int last_frame_used;
	};

	std::map<u64, TempFBO> tempFBOs_;

	// Not used under ES currently.
	AsyncPBO *pixelBufObj_; //this isn't that large
	u8 currentPBO_;
};
