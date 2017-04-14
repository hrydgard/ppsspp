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

// TODO: Remove?
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
class VulkanPushBuffer;

static const char *ub_post_shader =
R"(	vec2 texelDelta;
	vec2 pixelDelta;
	vec4 time;
)";

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

class FramebufferManagerVulkan : public FramebufferManagerCommon {
public:
	FramebufferManagerVulkan(Draw::DrawContext *draw, VulkanContext *vulkan);
	~FramebufferManagerVulkan();

	void SetTextureCache(TextureCacheVulkan *tc);
	void SetShaderManager(ShaderManagerVulkan *sm);
	void SetDrawEngine(DrawEngineVulkan *td) {
		drawEngine_ = td;
	}

	// If texture != 0, will bind it.
	// x,y,w,h are relative to destW, destH which fill out the target completely.
	void DrawTexture(VulkanTexture *texture, float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, VkPipeline pipeline, int uvRotation);
	void DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, bool linearFilter) override;

	void DestroyAllFBOs();

	virtual void Init() override;

	void BeginFrameVulkan();  // there's a BeginFrame in the base class, which this calls
	void EndFrame();

	void Resized() override;
	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);
	int GetLineWidth();
	void ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) override;

	void BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) override;

	// For use when texturing from a framebuffer.  May create a duplicate if target.
	VulkanTexture *GetFramebufferColor(u32 fbRawAddress, VirtualFramebuffer *framebuffer, int flags);

	// Reads a rectangular subregion of a framebuffer to the right position in its backing memory.
	void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) override;
	void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) override;

	std::vector<FramebufferInfo> GetFramebufferList();

	bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override;

	bool GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes) override;
	bool GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) override;
	bool GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) override;
	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;

	virtual void RebindFramebuffer() override;

	// VulkanFBO *GetTempFBO(u16 w, u16 h, VulkanFBOColorDepth depth = VK_FBO_8888);

	// Pass management
	// void BeginPassClear()

	// If within a render pass, this will just issue a regular clear. If beginning a new render pass,
	// do that.
	void NotifyClear(bool clearColor, bool clearAlpha, bool clearDepth, uint32_t color, float depth);
	void NotifyDraw() {
		DoNotifyDraw();
	}

protected:
	void Bind2DShader() override;
	void BindPostShader(const PostShaderUniforms &uniforms) override;
	void SetViewport2D(int x, int y, int w, int h) override;
	void DisableState() override {}
	void ClearBuffer(bool keepState = false) override;
	void FlushBeforeCopy() override;

	// Used by ReadFramebufferToMemory and later framebuffer block copies
	void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override;
	bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override;

private:

	// The returned texture does not need to be free'd, might be returned from a pool (currently single entry)
	void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) override;
	void DoNotifyDraw();

	VkCommandBuffer AllocFrameCommandBuffer();
	void UpdatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight);

	void PackFramebufferAsync_(VirtualFramebuffer *vfb);
	void PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h);

	void InitDeviceObjects();
	void DestroyDeviceObjects();

	VulkanContext *vulkan_;

	// The command buffer of the current framebuffer pass being rendered to.
	// One framebuffer can be used as a texturing source at multiple times in a frame,
	// but then the contents have to be copied out into a new texture every time.
	VkCommandBuffer curCmd_;
	VkCommandBuffer cmdInit_;

	// Used by DrawPixels
	VulkanTexture *drawPixelsTex_;
	GEBufferFormat drawPixelsTexFormat_;

	u8 *convBuf_;
	u32 convBufSize_;

	TextureCacheVulkan *textureCacheVulkan_;
	ShaderManagerVulkan *shaderManagerVulkan_;
	DrawEngineVulkan *drawEngine_;

	bool resized_;

	AsyncPBOVulkan *pixelBufObj_;
	int currentPBO_;

	enum {
		MAX_COMMAND_BUFFERS = 32,
	};

	struct FrameData {
		VkCommandPool cmdPool_;
		// Keep track of command buffers we allocated so we can reset or free them at an appropriate point.
		VkCommandBuffer commandBuffers_[MAX_COMMAND_BUFFERS];
		VulkanPushBuffer *push_;
		int numCommandBuffers_;
		int totalCommandBuffers_;
	};

	FrameData frameData_[2];
	int curFrame_;

	// This gets copied to the current frame's push buffer as needed.
	PostShaderUniforms postUniforms_;

	// Renderpasses, all combination of preserving or clearing fb contents
	VkRenderPass rpLoadColorLoadDepth_;
	VkRenderPass rpClearColorLoadDepth_;
	VkRenderPass rpLoadColorClearDepth_;
	VkRenderPass rpClearColorClearDepth_;

	VkPipelineCache pipelineCache2D_;

	// Basic shaders
	VkShaderModule fsBasicTex_;
	VkShaderModule vsBasicTex_;
	VkPipeline pipelineBasicTex_;

	// Postprocessing
	VkPipeline pipelinePostShader_;

	VkSampler linearSampler_;
	VkSampler nearestSampler_;

	// Simple 2D drawing engine.
	Vulkan2D vulkan2D_;
};
