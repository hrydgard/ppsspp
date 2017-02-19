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

#include <map>

#include "Globals.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "Common/Vulkan/VulkanContext.h"
#include "GPU/Vulkan/TextureScalerVulkan.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;
class FramebufferManagerVulkan;
class DepalShaderCacheVulkan;
class ShaderManagerVulkan;
class DrawEngineVulkan;

class VulkanContext;
class VulkanTexture;
class VulkanPushBuffer;
class VulkanDeviceAllocator;

class CachedTextureVulkan {
public:
	CachedTextureVulkan() : texture_(nullptr) {
	}
	~CachedTextureVulkan();

	// TODO: Switch away from VulkanImage to some kind of smart suballocating texture pool.
	VulkanTexture *texture_;
};

class SamplerCache {
public:
	SamplerCache(VulkanContext *vulkan) : vulkan_(vulkan) {}
	~SamplerCache();
	VkSampler GetOrCreateSampler(const SamplerCacheKey &key);

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);

private:
	VulkanContext *vulkan_;
	std::map<SamplerCacheKey, VkSampler> cache_;
};


class TextureCacheVulkan : public TextureCacheCommon {
public:
	TextureCacheVulkan(Draw::DrawContext *draw, VulkanContext *vulkan);
	~TextureCacheVulkan();

	void StartFrame();
	void EndFrame();

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);

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

	void ForgetLastTexture() override {
		lastBoundTexture = nullptr;
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	}
	void InvalidateLastTexture(TexCacheEntry *entry = nullptr) override {
		if (!entry || entry->vkTex == lastBoundTexture) {
			lastBoundTexture = nullptr;
		}
	}

	void GetVulkanHandles(VkImageView &imageView, VkSampler &sampler) {
		imageView = imageView_;
		sampler = sampler_;
	}
	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key);

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry) override;

private:
	void UpdateSamplingParams(TexCacheEntry &entry, SamplerCacheKey &key);
	void LoadTextureLevel(TexCacheEntry &entry, uint8_t *writePtr, int rowPitch,  int level, int scaleFactor, VkFormat dstFmt);
	VkFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int stride, int w, int h);
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;

	void ApplyTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer) override;
	void BuildTexture(TexCacheEntry *const entry, bool replaceImages) override;

	VulkanContext *vulkan_;
	VulkanDeviceAllocator *allocator_;

	SamplerCache samplerCache_;

	TextureScalerVulkan scaler;

	CachedTextureVulkan *lastBoundTexture;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManagerVulkan *framebufferManagerVulkan_;
	DepalShaderCacheVulkan *depalShaderCache_;
	ShaderManagerVulkan *shaderManagerVulkan_;
	DrawEngineVulkan *drawEngine_;

	// Bound state to emulate an API similar to the others
	VkImageView imageView_ = VK_NULL_HANDLE;
	VkSampler sampler_ = VK_NULL_HANDLE;
};

VkFormat getClutDestFormatVulkan(GEPaletteFormat format);
