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

struct SamplerCacheKey {
	SamplerCacheKey() : fullKey(0) {}

	union {
		u32 fullKey;
		struct {
			bool mipEnable : 1;
			bool minFilt : 1;
			bool mipFilt : 1;
			bool magFilt : 1;
			bool sClamp : 1;
			bool tClamp : 1;
			int lodBias : 4;
			int maxLevel : 4;
		};
	};

	bool operator < (const SamplerCacheKey &other) const {
		return fullKey < other.fullKey;
	}
};

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

private:
	VulkanContext *vulkan_;
	std::map<SamplerCacheKey, VkSampler> cache_;
};


class TextureCacheVulkan : public TextureCacheCommon {
public:
	TextureCacheVulkan(VulkanContext *vulkan);
	~TextureCacheVulkan();

	void SetTexture();
	virtual bool SetOffsetTexture(u32 offset) override;

	void Clear(bool delete_them);
	void StartFrame();
	void EndFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();

	void SetFramebufferManager(FramebufferManagerVulkan *fbManager) {
		framebufferManager_ = fbManager;
	}
	void SetDepalShaderCache(DepalShaderCacheVulkan *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerVulkan *sm) {
		shaderManager_ = sm;
	}
	void SetTransformDrawEngine(DrawEngineVulkan *td) {
		transformDraw_ = td;
	}

	size_t NumLoadedTextures() const {
		return cache.size();
	}

	void ForgetLastTexture() {
		lastBoundTexture = nullptr;
		gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	}

	void ApplyTexture(VulkanPushBuffer *uploadBuffer, VkImageView &imageView, VkSampler &sampler);

protected:
	void DownloadFramebufferForClut(u32 clutAddr, u32 bytes) override;

private:
	void Decimate();  // Run this once per frame to get rid of old textures.
	void DeleteTexture(TexCache::iterator it);
	void UpdateSamplingParams(TexCacheEntry &entry, SamplerCacheKey &key);
	void LoadTextureLevel(TexCacheEntry &entry, uint8_t *writePtr, int rowPitch,  int level, int scaleFactor, VkFormat dstFmt);
	VkFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int stride, int w, int h);
	u32 GetCurrentClutHash();
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple);
	bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0) override;
	void SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);
	void ApplyTextureFramebuffer(VkCommandBuffer cmd, TexCacheEntry *entry, VirtualFramebuffer *framebuffer, VkImageView &image, VkSampler &sampler);
	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key);

	bool CheckFullHash(TexCacheEntry *const entry, bool &doDelete);
	bool HandleTextureChange(TexCacheEntry *const entry, const char *reason, bool initialMatch, bool doDelete);
	void BuildTexture(TexCacheEntry *const entry, VulkanPushBuffer *uploadBuffer);

	VulkanContext *vulkan_;
	VulkanDeviceAllocator *allocator_;

	TexCache secondCache;
	u32 secondCacheSizeEstimate_;

	bool clearCacheNextFrame_;
	bool lowMemoryMode_;

	SamplerCache samplerCache_;

	TextureScalerVulkan scaler;

	u32 clutHash_;

	CachedTextureVulkan *lastBoundTexture;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManagerVulkan *framebufferManager_;
	DepalShaderCacheVulkan *depalShaderCache_;
	ShaderManagerVulkan *shaderManager_;
	DrawEngineVulkan *transformDraw_;

	const char *nextChangeReason_;
	bool nextNeedsRehash_;
	bool nextNeedsChange_;
	bool nextNeedsRebuild_;
};

VkFormat getClutDestFormatVulkan(GEPaletteFormat format);
