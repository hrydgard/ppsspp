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

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryPropertyMask)
		: vulkan_(vulkan), memoryPropertyMask_(memoryPropertyMask), size_(size), usage_(usage) {
	bool res = AddBuffer();
	_assert_(res);
}

VulkanPushBuffer::~VulkanPushBuffer() {
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

	VkResult res = vkCreateBuffer(device, &b, nullptr, &info.buffer);
	if (VK_SUCCESS != res) {
		_assert_msg_(false, "vkCreateBuffer failed! result=%d", (int)res);
		return false;
	}

	// Get the buffer memory requirements. None of this can be cached!
	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(device, info.buffer, &reqs);

	// Okay, that's the buffer. Now let's allocate some memory for it.
	VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = reqs.size;
	vulkan_->MemoryTypeFromProperties(reqs.memoryTypeBits, memoryPropertyMask_, &alloc.memoryTypeIndex);

	res = vkAllocateMemory(device, &alloc, nullptr, &info.deviceMemory);
	if (VK_SUCCESS != res) {
		_assert_msg_(false, "vkAllocateMemory failed! size=%d result=%d", (int)reqs.size, (int)res);
		vkDestroyBuffer(device, info.buffer, nullptr);
		return false;
	}
	res = vkBindBufferMemory(device, info.buffer, info.deviceMemory, 0);
	if (VK_SUCCESS != res) {
		ERROR_LOG(G3D, "vkBindBufferMemory failed! result=%d", (int)res);
		vkFreeMemory(device, info.deviceMemory, nullptr);
		vkDestroyBuffer(device, info.buffer, nullptr);
		return false;
	}

	buffers_.push_back(info);
	buf_ = buffers_.size() - 1;
	return true;
}

void VulkanPushBuffer::Destroy(VulkanContext *vulkan) {
	for (BufInfo &info : buffers_) {
		vulkan->Delete().QueueDeleteBuffer(info.buffer);
		vulkan->Delete().QueueDeleteDeviceMemory(info.deviceMemory);
	}
	buffers_.clear();
}

void VulkanPushBuffer::NextBuffer(size_t minSize) {
	// First, unmap the current memory.
	if (memoryPropertyMask_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
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
	if (memoryPropertyMask_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
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
	VkResult res = vkMapMemory(vulkan_->GetDevice(), buffers_[buf_].deviceMemory, 0, size_, 0, (void **)(&writePtr_));
	_dbg_assert_(writePtr_);
	_assert_(VK_SUCCESS == res);
}

void VulkanPushBuffer::Unmap() {
	_dbg_assert_msg_(writePtr_ != nullptr, "VulkanPushBuffer::Unmap: writePtr_ null here means we have a bug (map/unmap mismatch)");
	if (!writePtr_)
		return;

	if ((memoryPropertyMask_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
		VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.offset = 0;
		range.size = offset_;
		range.memory = buffers_[buf_].deviceMemory;
		vkFlushMappedMemoryRanges(vulkan_->GetDevice(), 1, &range);
	}

	vkUnmapMemory(vulkan_->GetDevice(), buffers_[buf_].deviceMemory);
	writePtr_ = nullptr;
}
