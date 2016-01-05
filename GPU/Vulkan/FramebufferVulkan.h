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

#include "GPU/Common/FramebufferCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/Vulkan/VulkanUtil.h"

// TODO: WTF?
enum VulkanFBOColorDepth {
	VK_FBO_8888,
	VK_FBO_565,
	VK_FBO_4444,
	VK_FBO_5551,
};

class TextureCacheVulkan;

class FramebufferManagerVulkan : public FramebufferManagerCommon {
public:
	// Subsequent commands will be enqueued on this buffer.
	void SetInstance(VkInstance inst) { inst_ = inst; }
	void SetCmdBuffer(VkCommandBuffer cmd) { cmd_ = cmd; }
	
	virtual void ClearBuffer(bool keepState = false) override {
		throw std::logic_error("The method or operation is not implemented.");
	}
	void SetTextureCache(TextureCacheVulkan *texCache) { texCache_ = texCache; }
	VulkanFramebuffer *GetTempFBO(int width, int height, VulkanFBOColorDepth colorDepth);

	virtual void RebindFramebuffer() override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual bool NotifyStencilUpload(u32 addr, int size, bool skipZero = false) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void DisableState() override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void FlushBeforeCopy() override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void DecimateFBOs() override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void DestroyFramebuf(VirtualFramebuffer *vfb) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force = false) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) override {
		throw std::logic_error("The method or operation is not implemented.");
	}

	void DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) override {

	}

	bool CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) override {
		return false;
	}
	void UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) override {

	}
	void DestroyAllFBOs();
	void Resized();
	void DeviceLost();

	void CopyDisplayToOutput();
	void EndFrame();

	std::vector<FramebufferInfo> GetFramebufferList();

private:
	VkInstance inst_;
	VkCommandBuffer cmd_;

	TextureCacheVulkan *texCache_;
};
