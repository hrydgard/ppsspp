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

#include "Common/Vulkan/VulkanMemory.h"

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, size_t size) : ctx_(vulkan), buf_(0), offset_(0), size_(size), writePtr_(nullptr) {
	bool res = AddBuffer();
	assert(res);
}

bool VulkanPushBuffer::AddBuffer() {
	VkDevice device = ctx_->GetDevice();

	buf_ = buffers_.size();
	buffers_.resize(buf_ + 1);

	VkBufferCreateInfo b = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	b.size = size_;
	b.flags = 0;
	b.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	b.queueFamilyIndexCount = 0;
	b.pQueueFamilyIndices = nullptr;

	VkResult res = vkCreateBuffer(device, &b, nullptr, &buffers_[buf_].buffer);
	if (VK_SUCCESS != res) {
		return false;
	}

	// Okay, that's the buffer. Now let's allocate some memory for it.
	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	ctx_->MemoryTypeFromProperties(0xFFFFFFFF, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &alloc.memoryTypeIndex);
	alloc.allocationSize = size_;

	res = vkAllocateMemory(device, &alloc, nullptr, &buffers_[buf_].deviceMemory);
	if (VK_SUCCESS != res) {
		return false;
	}
	res = vkBindBufferMemory(device, buffers_[buf_].buffer, buffers_[buf_].deviceMemory, 0);
	if (VK_SUCCESS != res) {
		return false;
	}
	return true;
}

void VulkanPushBuffer::NextBuffer() {
	VkDevice device = ctx_->GetDevice();

	// First, unmap the current memory.
	Unmap(device);

	buf_++;
	if (buf_ >= buffers_.size()) {
		bool res = AddBuffer();
		assert(res);
	}

	// Now, move to the next buffer and map it.
	offset_ = 0;
	Map(device);
}

void VulkanPushBuffer::Defragment() {
	if (buffers_.size() <= 1) {
		return;
	}

	// Okay, we have more than one.  Destroy them all and start over with a larger one.
	size_t newSize = size_ * buffers_.size();
	Destroy(ctx_);

	size_ = newSize;
	bool res = AddBuffer();
	assert(res);
}
