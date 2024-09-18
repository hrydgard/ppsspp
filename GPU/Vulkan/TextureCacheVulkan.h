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
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/TextureShaderCommon.h"
#include "GPU/Vulkan/VulkanUtil.h"

struct VirtualFramebuffer;
class FramebufferManagerVulkan;
class DepalShaderCacheVulkan;
class ShaderManagerVulkan;
class DrawEngineVulkan;

class VulkanContext;
class VulkanTexture;

class SamplerCache {
public:
	SamplerCache(VulkanContext *vulkan) : vulkan_(vulkan), cache_(16) {}
	~SamplerCache();
	VkSampler GetOrCreateSampler(const SamplerCacheKey &key);

	void DeviceLost();
	void DeviceRestore(VulkanContext *vulkan);

	std::vector<std::string> DebugGetSamplerIDs() const;
	static std::string DebugGetSamplerString(const std::string &id, DebugShaderStringType stringType);

private:
	VulkanContext *vulkan_;
	DenseHashMap<SamplerCacheKey, VkSampler> cache_;
};

class TextureCacheVulkan : public TextureCacheCommon {
public:
	TextureCacheVulkan(Draw::DrawContext *draw, Draw2D *draw2D, VulkanContext *vulkan);
	~TextureCacheVulkan();

	void StartFrame() override;

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	void SetFramebufferManager(FramebufferManagerVulkan *fbManager);
	void SetDrawEngine(DrawEngineVulkan *td) {
		drawEngine_ = td;
	}

	void ForgetLastTexture() override {}
	void NotifyConfigChanged() override;

	void GetVulkanHandles(VkImageView &imageView, VkSampler &sampler) {
		imageView = imageView_;
		sampler = curSampler_;
	}

	bool GetCurrentTextureDebug(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) override;

	void GetStats(char *ptr, size_t size);

	VulkanDeviceAllocator *GetAllocator() { return allocator_; }

	std::vector<std::string> DebugGetSamplerIDs() const;
	std::string DebugGetSamplerString(const std::string &id, DebugShaderStringType stringType);

protected:
	void BindTexture(TexCacheEntry *entry) override;
	void Unbind() override;
	void ReleaseTexture(TexCacheEntry *entry, bool delete_them) override;
	void BindAsClutTexture(Draw::Texture *tex, bool smooth) override;
	void ApplySamplingParams(const SamplerCacheKey &key) override;
	void BoundFramebufferTexture() override;
	void *GetNativeTextureView(const TexCacheEntry *entry) override;

private:
	void LoadVulkanTextureLevel(TexCacheEntry &entry, uint8_t *writePtr, int rowPitch,  int level, int scaleFactor, VkFormat dstFmt);
	static VkFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) ;
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple) override;

	void BuildTexture(TexCacheEntry *const entry) override;

	void CompileScalingShader();

	VulkanDeviceAllocator *allocator_ = nullptr;

	VulkanComputeShaderManager computeShaderManager_;

	SamplerCache samplerCache_;

	DrawEngineVulkan *drawEngine_;

	std::string textureShader_;
	VkShaderModule uploadCS_ = VK_NULL_HANDLE;

	// Bound state to emulate an API similar to the others
	VkImageView imageView_ = VK_NULL_HANDLE;
	VkSampler curSampler_ = VK_NULL_HANDLE;

	VkSampler samplerNearest_ = VK_NULL_HANDLE;
};

VkFormat getClutDestFormatVulkan(GEPaletteFormat format);
