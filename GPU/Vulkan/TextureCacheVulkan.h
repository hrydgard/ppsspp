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

#include "Common/Data/Collections/Hashmaps.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "GPU/Vulkan/TextureScalerVulkan.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Vulkan/VulkanUtil.h"

struct VirtualFramebuffer;
class FramebufferManagerVulkan;
class DepalShaderCacheVulkan;
class ShaderManagerVulkan;
class DrawEngineVulkan;

class VulkanContext;
class VulkanTexture;
class VulkanPushBuffer;

class SamplerCache {
public:
	SamplerCache(VulkanContext *vulkan) : vulkan_(vulkan), cache_(16) {}
	~SamplerCache();
	VkSampler GetOrCreateSampler(const SamplerCacheKey &key);

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);

	std::vector<std::string> DebugGetSamplerIDs() const;
	std::string DebugGetSamplerString(std::string id, DebugShaderStringType stringType);

private:
	VulkanContext *vulkan_;
	DenseHashMap<SamplerCacheKey, VkSampler, (VkSampler)VK_NULL_HANDLE> cache_;
};

class Vulkan2D;

class TextureCacheVulkan : public TextureCacheCommon {
public:
	TextureCacheVulkan(Draw::DrawContext *draw, VulkanContext *vulkan);
	~TextureCacheVulkan();

	void StartFrame();
	void EndFrame();

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	void SetFramebufferManager(FramebufferManagerVulkan *fbManager);
	void SetDepalShaderCache(DepalShaderCacheVulkan *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerVulkan *sm) {
		shaderManagerVulkan_ = sm;
	}
	void SetDrawEngine(DrawEngineVulkan *td) {
		drawEngine_ = td;
	}
	void SetVulkan2D(Vulkan2D *vk2d);
	void SetPushBuffer(VulkanPushBuffer *push) {
		push_ = push;
	}

	void ForgetLastTexture() override {}
	void InvalidateLastTexture() override {}

	void NotifyConfigChanged() override;

	void GetVulkanHandles(VkImageView &imageView, VkSampler &sampler) {
		imageView = imageView_;
		sampler = curSampler_;
	}

	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level) override;

	void GetStats(char *ptr, size_t size);

	VulkanDeviceAllocator *GetAllocator() { return allocator_; }

	std::vector<std::string> DebugGetSamplerIDs() const;
	std::string DebugGetSamplerString(std::string id, DebugShaderStringType stringType);

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;

private:
	void LoadTextureLevel(TexCacheEntry &entry, uint8_t *writePtr, int rowPitch,  int level, int scaleFactor, VkFormat dstFmt);
	VkFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	static CheckAlphaResult CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int w);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;

	void ApplyTextureFramebuffer(VirtualFramebuffer *framebuffer, GETextureFormat texFormat, FramebufferNotificationChannel channel) override;
	void BuildTexture(TexCacheEntry *const entry) override;

	void CompileScalingShader();

	VulkanDeviceAllocator *allocator_ = nullptr;
	VulkanPushBuffer *push_ = nullptr;

	VulkanComputeShaderManager computeShaderManager_;

	SamplerCache samplerCache_;

	TextureScalerVulkan scaler;

	DepalShaderCacheVulkan *depalShaderCache_;
	ShaderManagerVulkan *shaderManagerVulkan_;
	DrawEngineVulkan *drawEngine_;
	Vulkan2D *vulkan2D_;

	std::string textureShader_;
	int shaderScaleFactor_ = 0;
	VkShaderModule uploadCS_ = VK_NULL_HANDLE;

	// Bound state to emulate an API similar to the others
	VkImageView imageView_ = VK_NULL_HANDLE;
	VkSampler curSampler_ = VK_NULL_HANDLE;

	VkSampler samplerNearest_ = VK_NULL_HANDLE;
};

VkFormat getClutDestFormatVulkan(GEPaletteFormat format);
