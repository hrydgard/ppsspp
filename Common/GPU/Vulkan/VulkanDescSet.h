#pragma once

#include "Common/Data/Collections/FastVec.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

#include <functional>
#include <vector>

// Only appropriate for use in a per-frame pool.
class VulkanDescSetPool {
public:
	VulkanDescSetPool(const char *tag, bool grow) : tag_(tag), grow_(grow) {}
	~VulkanDescSetPool();

	// Must call this before use: defines how to clear cache of ANY returned values from Allocate().
	void Setup(const std::function<void()> &clear) {
		clear_ = clear;
	}
	void Create(VulkanContext *vulkan, const VkDescriptorPoolCreateInfo &info, const std::vector<VkDescriptorPoolSize> &sizes);
	// Allocate a new set, which may resize and empty the current sets.
	// Use only for the current frame, unless in a cache cleared by clear_.
	VkDescriptorSet Allocate(int n, const VkDescriptorSetLayout *layouts, const char *tag);
	void Reset();
	void Destroy();

private:
	VkResult Recreate(bool grow);

	const char *tag_;
	VulkanContext *vulkan_ = nullptr;
	VkDescriptorPool descPool_ = VK_NULL_HANDLE;
	VkDescriptorPoolCreateInfo info_{};
	std::vector<VkDescriptorPoolSize> sizes_;
	std::function<void()> clear_;
	uint32_t usage_ = 0;
	bool grow_;
};
