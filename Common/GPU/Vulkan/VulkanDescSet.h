#pragma once

#include "Common/Data/Collections/FastVec.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

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
	VulkanDescSetPool(const char *tag, bool grow = true) : tag_(tag), grow_(grow) {}
	~VulkanDescSetPool();

	void Create(VulkanContext *vulkan, const BindingType *bindingTypes, uint32_t bindingTypesCount, uint32_t descriptorCount);
	// Allocate a new set, which may resize and empty the current sets.
	// Use only for the current frame.
	bool Allocate(VkDescriptorSet *descriptorSets, int count, const VkDescriptorSetLayout *layouts);
	void Reset();

	// This queues up destruction.
	void Destroy();
	// This actually destroys immediately.
	void DestroyImmediately();

	bool IsDestroyed() const {
		return !descPool_;
	}

	void SetTag(const char *tag) {
		tag_ = tag;
	}

private:
	VkResult Recreate(bool grow);

	const char *tag_;
	VulkanContext *vulkan_ = nullptr;
	VkDescriptorPool descPool_ = VK_NULL_HANDLE;
	VkDescriptorPoolCreateInfo info_{};
	std::vector<VkDescriptorPoolSize> sizes_;
	uint32_t usage_ = 0;
	bool grow_;
};
