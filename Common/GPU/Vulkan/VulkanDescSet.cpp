#include "Common/GPU/Vulkan/VulkanDescSet.h"

VulkanDescSetPool::~VulkanDescSetPool() {
	_assert_msg_(descPool_ == VK_NULL_HANDLE, "VulkanDescSetPool %s never destroyed", tag_);
}

void VulkanDescSetPool::Create(VulkanContext *vulkan, const BindingType *bindingTypes, uint32_t bindingTypesCount, uint32_t descriptorCount) {
	_assert_msg_(descPool_ == VK_NULL_HANDLE, "VulkanDescSetPool::Create when already exists");

	vulkan_ = vulkan;
	info_ = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	info_.maxSets = descriptorCount;
	_dbg_assert_(sizes_.empty());

	uint32_t storageImageCount = 0;
	uint32_t storageBufferCount = 0;
	uint32_t combinedImageSamplerCount = 0;
	uint32_t uniformBufferDynamicCount = 0;
	for (uint32_t i = 0; i < bindingTypesCount; i++) {
		switch (bindingTypes[i]) {
		case BindingType::COMBINED_IMAGE_SAMPLER: combinedImageSamplerCount++; break;
		case BindingType::UNIFORM_BUFFER_DYNAMIC_VERTEX:
		case BindingType::UNIFORM_BUFFER_DYNAMIC_ALL: uniformBufferDynamicCount++; break;
		case BindingType::STORAGE_BUFFER_VERTEX:
		case BindingType::STORAGE_BUFFER_COMPUTE: storageBufferCount++; break;
		case BindingType::STORAGE_IMAGE_COMPUTE: storageImageCount++; break;
		}
	}
	if (combinedImageSamplerCount) {
		sizes_.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, combinedImageSamplerCount * descriptorCount });
	}
	if (uniformBufferDynamicCount) {
		sizes_.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, uniformBufferDynamicCount * descriptorCount });
	}
	if (storageBufferCount) {
		sizes_.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount * descriptorCount });
	}
	if (storageImageCount) {
		sizes_.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImageCount * descriptorCount });
	}
	VkResult res = Recreate(false);
	_assert_msg_(res == VK_SUCCESS, "Could not create VulkanDescSetPool %s", tag_);
}

bool VulkanDescSetPool::Allocate(VkDescriptorSet *descriptorSets, int count, const VkDescriptorSetLayout *layouts) {
	if (descPool_ == VK_NULL_HANDLE || usage_ + count >= info_.maxSets) {
		// Missing or out of space, need to recreate.
		VkResult res = Recreate(grow_);
		_assert_msg_(res == VK_SUCCESS, "Could not grow VulkanDescSetPool %s on usage %d", tag_, (int)usage_);
	}

	VkDescriptorSetAllocateInfo descAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	descAlloc.descriptorPool = descPool_;
	descAlloc.descriptorSetCount = count;
	descAlloc.pSetLayouts = layouts;
	VkResult result = vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, descriptorSets);

	if (result == VK_ERROR_FRAGMENTED_POOL || result < 0) {
		WARN_LOG(Log::G3D, "Pool %s %s - recreating", tag_, result == VK_ERROR_FRAGMENTED_POOL ? "fragmented" : "full");
		// There seems to have been a spec revision. Here we should apparently recreate the descriptor pool,
		// so let's do that. See https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkAllocateDescriptorSets.html
		// Fragmentation shouldn't really happen though since we wipe the pool every frame.
		VkResult res = Recreate(false);
		_assert_msg_(res == VK_SUCCESS, "Ran out of descriptor space (frag?) and failed to recreate a descriptor pool. sz=%d res=%d", usage_, (int)res);

		// Need to update this pointer since we have allocated a new one.
		descAlloc.descriptorPool = descPool_;
		result = vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, descriptorSets);
		_assert_msg_(result == VK_SUCCESS, "Ran out of descriptor space (frag?) and failed to allocate after recreating a descriptor pool. res=%d", (int)result);
	}

	if (result != VK_SUCCESS) {
		return false;
	}

	usage_ += count;
	return true;
}

void VulkanDescSetPool::Reset() {
	_assert_msg_(descPool_ != VK_NULL_HANDLE, "VulkanDescSetPool::Reset without valid pool");
	vkResetDescriptorPool(vulkan_->GetDevice(), descPool_, 0);

	usage_ = 0;
}

void VulkanDescSetPool::Destroy() {
	if (descPool_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteDescriptorPool(descPool_);
		usage_ = 0;
	}
	sizes_.clear();
}

void VulkanDescSetPool::DestroyImmediately() {
	if (descPool_ != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(vulkan_->GetDevice(), descPool_, nullptr);
		descPool_ = VK_NULL_HANDLE;
		usage_ = 0;
	}
	sizes_.clear();
}

VkResult VulkanDescSetPool::Recreate(bool grow) {
	_assert_msg_(vulkan_ != nullptr, "VulkanDescSetPool::Recreate without VulkanContext");

	uint32_t prevSize = info_.maxSets;
	if (grow) {
		info_.maxSets *= 2;
		for (auto &size : sizes_)
			size.descriptorCount *= 2;
	}

	// Delete the pool if it already exists.
	if (descPool_ != VK_NULL_HANDLE) {
		INFO_LOG(Log::G3D, "Reallocating %s desc pool from %d to %d", tag_, prevSize, info_.maxSets);
		vulkan_->Delete().QueueDeleteDescriptorPool(descPool_);
		usage_ = 0;
	}

	info_.pPoolSizes = &sizes_[0];
	info_.poolSizeCount = (uint32_t)sizes_.size();

	VkResult result = vkCreateDescriptorPool(vulkan_->GetDevice(), &info_, nullptr, &descPool_);
	if (result == VK_SUCCESS) {
		vulkan_->SetDebugName(descPool_, VK_OBJECT_TYPE_DESCRIPTOR_POOL, tag_);
	}
	return result;
}
