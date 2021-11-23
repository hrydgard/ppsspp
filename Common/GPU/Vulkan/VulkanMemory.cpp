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

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, size_t size, VkBufferUsageFlags usage, PushBufferType type)
		: vulkan_(vulkan), size_(size), usage_(usage), type_(type) {
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

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = type_ == PushBufferType::CPU_TO_GPU ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
	VmaAllocationInfo allocInfo{};

	VkResult res = vmaCreateBuffer(vulkan_->Allocator(), &b, &allocCreateInfo, &info.buffer, &info.allocation, &allocInfo);
	if (VK_SUCCESS != res) {
		_assert_msg_(false, "vkCreateBuffer failed! result=%d", (int)res);
		return false;
	}

	buffers_.push_back(info);
	buf_ = buffers_.size() - 1;
	return true;
}

void VulkanPushBuffer::Destroy(VulkanContext *vulkan) {
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
