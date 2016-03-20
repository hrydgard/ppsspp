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

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, size_t size) : offset_(0), size_(size), writePtr_(nullptr), deviceMemory_(0) {
	VkDevice device = vulkan->GetDevice();

	VkBufferCreateInfo b = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	b.size = size;
	b.flags = 0;
	b.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	b.queueFamilyIndexCount = 0;
	b.pQueueFamilyIndices = nullptr;
	VkResult res = vkCreateBuffer(device, &b, nullptr, &buffer_);
	assert(VK_SUCCESS == res);

	// Okay, that's the buffer. Now let's allocate some memory for it.
	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.pNext = nullptr;
	vulkan->MemoryTypeFromProperties(0xFFFFFFFF, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &alloc.memoryTypeIndex);
	alloc.allocationSize = size;

	res = vkAllocateMemory(device, &alloc, nullptr, &deviceMemory_);
	assert(VK_SUCCESS == res);
	res = vkBindBufferMemory(device, buffer_, deviceMemory_, 0);
	assert(VK_SUCCESS == res);
}
