#pragma once

#include "Common/Data/Collections/FastVec.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

#include <functional>
#include <vector>

enum class BindingType {
	COMBINED_IMAGE_SAMPLER,
	UNIFORM_BUFFER_DYNAMIC_VERTEX,
	UNIFORM_BUFFER_DYNAMIC_ALL,
	STORAGE_BUFFER_VERTEX,
	STORAGE_BUFFER_COMPUTE,
	STORAGE_IMAGE_COMPUTE,
};

// Only appropriate for use in a per-frame pool.
class VulkanDescSetPool {
public:
	VulkanDescSetPool(const char *tag = "", bool grow = true) : tag_(tag), grow_(grow) {}
	~VulkanDescSetPool();

	// Must call this before use: defines how to clear cache of ANY returned values from Allocate().
	void Setup(const std::function<void()> &clear) {
		clear_ = clear;
	}
	void Create(VulkanContext *vulkan, const BindingType *bindingTypes, uint32_t bindingTypesCount, uint32_t descriptorCount);
	// Allocate a new set, which may resize and empty the current sets.
	// Use only for the current frame, unless in a cache cleared by clear_.
	VkDescriptorSet Allocate(int n, const VkDescriptorSetLayout *layouts, const char *tag);
	void Reset();
	void Destroy();

	void SetTag(const char *tag) {
		tag_ = tag;
	}
	bool IsDestroyed() const {
		return !descPool_;
	}

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
