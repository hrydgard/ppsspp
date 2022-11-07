// Copyright (c) 2016- PPSSPP Project.

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

// Additionally, Common/Vulkan/* , including this file, are also licensed
// under the public domain.

#include "Common/Math/math_util.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"

using namespace PPSSPP_VK;

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, const char *name, size_t size, VkBufferUsageFlags usage, PushBufferType type)
		: vulkan_(vulkan), name_(name), size_(size), usage_(usage), type_(type) {
	bool res = AddBuffer();
	_assert_(res);
}

VulkanPushBuffer::~VulkanPushBuffer() {
	_dbg_assert_(!writePtr_);
	_assert_(buffers_.empty());
}

bool VulkanPushBuffer::AddBuffer() {
	BufInfo info;
	VkDevice device = vulkan_->GetDevice();

	VkBufferCreateInfo b{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	b.size = size_;
	b.flags = 0;
	b.usage = usage_;
	b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	b.queueFamilyIndexCount = 0;
	b.pQueueFamilyIndices = nullptr;

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = type_ == PushBufferType::CPU_TO_GPU ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
	VmaAllocationInfo allocInfo{};

	VkResult res = vmaCreateBuffer(vulkan_->Allocator(), &b, &allocCreateInfo, &info.buffer, &info.allocation, &allocInfo);
	if (VK_SUCCESS != res) {
		_assert_msg_(false, "vkCreateBuffer failed! result=%d", (int)res);
		return false;
	}

	vulkan_->SetDebugName(info.buffer, VK_OBJECT_TYPE_BUFFER, name_);

	buffers_.push_back(info);
	buf_ = buffers_.size() - 1;
	return true;
}

void VulkanPushBuffer::Destroy(VulkanContext *vulkan) {
	_dbg_assert_(!writePtr_);
	for (BufInfo &info : buffers_) {
		vulkan->Delete().QueueDeleteBufferAllocation(info.buffer, info.allocation);
	}
	buffers_.clear();
}

void VulkanPushBuffer::NextBuffer(size_t minSize) {
	// First, unmap the current memory.
	if (type_ == PushBufferType::CPU_TO_GPU)
		Unmap();

	buf_++;
	if (buf_ >= buffers_.size() || minSize > size_) {
		// Before creating the buffer, adjust to the new size_ if necessary.
		while (size_ < minSize) {
			size_ <<= 1;
		}

		bool res = AddBuffer();
		_assert_(res);
		if (!res) {
			// Let's try not to crash at least?
			buf_ = 0;
		}
	}

	// Now, move to the next buffer and map it.
	offset_ = 0;
	if (type_ == PushBufferType::CPU_TO_GPU)
		Map();
}

void VulkanPushBuffer::Defragment(VulkanContext *vulkan) {
	if (buffers_.size() <= 1) {
		return;
	}

	// Okay, we have more than one.  Destroy them all and start over with a larger one.
	size_t newSize = size_ * buffers_.size();
	Destroy(vulkan);

	size_ = newSize;
	bool res = AddBuffer();
	_assert_(res);
}

size_t VulkanPushBuffer::GetTotalSize() const {
	size_t sum = 0;
	if (buffers_.size() > 1)
		sum += size_ * (buffers_.size() - 1);
	sum += offset_;
	return sum;
}

void VulkanPushBuffer::Map() {
	_dbg_assert_(!writePtr_);
	VkResult res = vmaMapMemory(vulkan_->Allocator(), buffers_[buf_].allocation, (void **)(&writePtr_));
	_dbg_assert_(writePtr_);
	_assert_(VK_SUCCESS == res);
}

void VulkanPushBuffer::Unmap() {
	_dbg_assert_msg_(writePtr_ != nullptr, "VulkanPushBuffer::Unmap: writePtr_ null here means we have a bug (map/unmap mismatch)");
	if (!writePtr_)
		return;

	vmaUnmapMemory(vulkan_->Allocator(), buffers_[buf_].allocation);
	writePtr_ = nullptr;
}

VulkanDescSetPool::~VulkanDescSetPool() {
	_assert_msg_(descPool_ == VK_NULL_HANDLE, "VulkanDescSetPool %s never destroyed", tag_);
}

void VulkanDescSetPool::Create(VulkanContext *vulkan, const VkDescriptorPoolCreateInfo &info, const std::vector<VkDescriptorPoolSize> &sizes) {
	_assert_msg_(descPool_ == VK_NULL_HANDLE, "VulkanDescSetPool::Create when already exists");

	vulkan_ = vulkan;
	info_ = info;
	sizes_ = sizes;

	VkResult res = Recreate(false);
	_assert_msg_(res == VK_SUCCESS, "Could not create VulkanDescSetPool %s", tag_);
}

VkDescriptorSet VulkanDescSetPool::Allocate(int n, const VkDescriptorSetLayout *layouts, const char *tag) {
	if (descPool_ == VK_NULL_HANDLE || usage_ + n >= info_.maxSets) {
		// Missing or out of space, need to recreate.
		VkResult res = Recreate(grow_);
		_assert_msg_(res == VK_SUCCESS, "Could not grow VulkanDescSetPool %s on usage %d", tag_, (int)usage_);
	}

	VkDescriptorSet desc;
	VkDescriptorSetAllocateInfo descAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	descAlloc.descriptorPool = descPool_;
	descAlloc.descriptorSetCount = n;
	descAlloc.pSetLayouts = layouts;
	VkResult result = vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, &desc);

	if (result == VK_ERROR_FRAGMENTED_POOL || result < 0) {
		// There seems to have been a spec revision. Here we should apparently recreate the descriptor pool,
		// so let's do that. See https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkAllocateDescriptorSets.html
		// Fragmentation shouldn't really happen though since we wipe the pool every frame.
		VkResult res = Recreate(false);
		_assert_msg_(res == VK_SUCCESS, "Ran out of descriptor space (frag?) and failed to recreate a descriptor pool. sz=%d res=%d", usage_, (int)res);

		// Need to update this pointer since we have allocated a new one.
		descAlloc.descriptorPool = descPool_;
		result = vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, &desc);
		_assert_msg_(result == VK_SUCCESS, "Ran out of descriptor space (frag?) and failed to allocate after recreating a descriptor pool. res=%d", (int)result);
	}

	if (result != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}

	vulkan_->SetDebugName(desc, VK_OBJECT_TYPE_DESCRIPTOR_SET, tag);
	return desc;
}

void VulkanDescSetPool::Reset() {
	_assert_msg_(descPool_ != VK_NULL_HANDLE, "VulkanDescSetPool::Reset without valid pool");
	vkResetDescriptorPool(vulkan_->GetDevice(), descPool_, 0);

	clear_();
	usage_ = 0;
}

void VulkanDescSetPool::Destroy() {
	_assert_msg_(vulkan_ != nullptr, "VulkanDescSetPool::Destroy without VulkanContext");

	if (descPool_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteDescriptorPool(descPool_);
		clear_();
		usage_ = 0;
	}
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
		DEBUG_LOG(G3D, "Reallocating %s desc pool from %d to %d", tag_, prevSize, info_.maxSets);
		vulkan_->Delete().QueueDeleteDescriptorPool(descPool_);
		clear_();
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
