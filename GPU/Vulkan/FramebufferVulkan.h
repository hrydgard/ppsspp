// Copyright (c) 2015- PPSSPP Project.

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

#include "Common/Vulkan/VulkanLoader.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Vulkan/VulkanUtil.h"

// TODO: WTF?
enum VulkanFBOColorDepth {
	VK_FBO_8888,
	VK_FBO_565,
	VK_FBO_4444,
	VK_FBO_5551,
};

class TextureCacheVulkan;
class DrawEngineVulkan;
class VulkanContext;
class ShaderManagerVulkan;
class VulkanTexture;

struct PostShaderUniforms {
	float texelDelta[2]; float pad[2];
	float pixelDelta[2]; float pad0[2];
	float time[4];
};

// Simple struct for asynchronous PBO readbacks
// TODO: Probably will need a complete redesign.
struct AsyncPBOVulkan {
	//  handle;
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

class FramebufferManagerVulkan : public FramebufferManagerCommon {
public:
	FramebufferManagerVulkan(VulkanContext *vulkan);
	~FramebufferManagerVulkan();

	void SetTextureCache(TextureCacheVulkan *tc) {
		textureCache_ = tc;
	}
	void SetShaderManager(ShaderManagerVulkan *sm) {
		shaderManager_ = sm;
	}
	void SetDrawEngine(DrawEngineVulkan *td) {
		transformDraw_ = td;
	}

	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override;
	void DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) override;

	// If texture != 0, will bind it.
	// x,y,w,h are relative to destW, destH which fill out the target completely.
	void DrawActiveTexture(VulkanTexture *texture, float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, VkPipeline pipeline, int uvRotation);

	void DestroyAllFBOs();

	virtual void Init() override;
	void EndFrame();
	void Resized();
	void DeviceLost();
	void CopyDisplayToOutput();
	int GetLineWidth();
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
	void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false) override;

	bool GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer);
	bool GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer);
	bool GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer);
	static bool GetDisplayFramebuffer(GPUDebugBuffer &buffer);

	virtual void RebindFramebuffer() override;

	VulkanFBO *GetTempFBO(u16 w, u16 h, VulkanFBOColorDepth depth = VK_FBO_8888);

	// Cardboard Settings Calculator
	struct CardboardSettings * GetCardboardSettings(struct CardboardSettings * cardboardSettings);

protected:
	virtual void DisableState() override;
	virtual void ClearBuffer(bool keepState = false);
	virtual void FlushBeforeCopy() override;
	virtual void DecimateFBOs() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	virtual void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;

	virtual void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) override;
	virtual void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) override;
	virtual void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) override;
	virtual bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	virtual void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;


private:
	void UpdatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight);
	void CompileDraw2DProgram();
	void DestroyDraw2DProgram();

	void SetNumExtraFBOs(int num);

	void PackFramebufferAsync_(VirtualFramebuffer *vfb);  // Not used under ES currently
	void PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	VulkanContext *vulkan_;

	// The command buffer of the current framebuffer pass being rendered to.
	// One framebuffer can be used as a texturing source at multiple times in a frame,
	// but then the contents have to be copied out into a new texture every time.
	VkCommandBuffer curCmd_;

	DrawEngineVulkan *drawEngine_;

	// Used by DrawPixels
	VulkanTexture *drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;

	int drawPixelsTexW_;
	int drawPixelsTexH_;

	u8 *convBuf_;
	u32 convBufSize_;

	TextureCacheVulkan *textureCache_;
	ShaderManagerVulkan *shaderManager_;
	DrawEngineVulkan *transformDraw_;

	// Used by post-processing shader
	std::vector<FBO *> extraFBOs_;

	bool resized_;

	struct TempFBO {
		VulkanFBO *fbo_vk;
		int last_frame_used;
	};

	std::map<u64, TempFBO> tempFBOs_;

	// Not used under ES currently.
	AsyncPBOVulkan *pixelBufObj_; //this isn't that large
	u8 currentPBO_;

	// This gets copied to the current frame's push buffer as needed.
	PostShaderUniforms postUniforms_;
};
