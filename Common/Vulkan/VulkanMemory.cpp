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

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, size_t size) : device_(vulkan->GetDevice()), buf_(0), offset_(0), size_(size), writePtr_(nullptr) {
	vulkan->MemoryTypeFromProperties(0xFFFFFFFF, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memoryTypeIndex_);

	bool res = AddBuffer();
	assert(res);
}

bool VulkanPushBuffer::AddBuffer() {
	BufInfo info;

	VkBufferCreateInfo b = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	b.size = size_;
	b.flags = 0;
	b.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	b.queueFamilyIndexCount = 0;
	b.pQueueFamilyIndices = nullptr;

	VkResult res = vkCreateBuffer(device_, &b, nullptr, &info.buffer);
	if (VK_SUCCESS != res) {
		return false;
	}

	// Okay, that's the buffer. Now let's allocate some memory for it.
	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.memoryTypeIndex = memoryTypeIndex_;
	alloc.allocationSize = size_;

	res = vkAllocateMemory(device_, &alloc, nullptr, &info.deviceMemory);
	if (VK_SUCCESS != res) {
		return false;
	}
	res = vkBindBufferMemory(device_, info.buffer, info.deviceMemory, 0);
	if (VK_SUCCESS != res) {
		return false;
	}

	buf_ = buffers_.size();
	buffers_.resize(buf_ + 1);
	buffers_[buf_] = info;
	return true;
}

void VulkanPushBuffer::NextBuffer(size_t minSize) {
	// First, unmap the current memory.
	Unmap();

	buf_++;
	if (buf_ >= buffers_.size() || minSize > size_) {
		// Before creating the buffer, adjust to the new size_ if necessary.
		while (size_ < minSize) {
			size_ <<= 1;
		}

		bool res = AddBuffer();
		assert(res);
		if (!res) {
			// Let's try not to crash at least?
			buf_ = 0;
		}
	}

	// Now, move to the next buffer and map it.
	offset_ = 0;
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
	assert(res);
}

VulkanDeviceAllocator::VulkanDeviceAllocator(VulkanContext *vulkan, size_t minSlabSize, size_t maxSlabSize)
	: vulkan_(vulkan), lastSlab_(0), minSlabSize_(minSlabSize), maxSlabSize_(maxSlabSize), memoryTypeIndex_(UNDEFINED_MEMORY_TYPE), destroyed_(false) {
	assert((minSlabSize_ & (SLAB_GRAIN_SIZE - 1)) == 0);
}

VulkanDeviceAllocator::~VulkanDeviceAllocator() {
	assert(destroyed_);
	assert(slabs_.empty());
}

void VulkanDeviceAllocator::Destroy() {
	for (Slab &slab : slabs_) {
		// Did anyone forget to free?
		for (auto pair : slab.allocSizes) {
			if (slab.usage[pair.first] != 2) {
				// If it's not 2 (queued), there's a problem.
				// If it's zero, it means allocSizes is somehow out of sync.
				Crash();
			}
		}

		vulkan_->Delete().QueueDeleteDeviceMemory(slab.deviceMemory);
	}
	slabs_.clear();
	destroyed_ = true;
}

size_t VulkanDeviceAllocator::Allocate(const VkMemoryRequirements &reqs, VkDeviceMemory *deviceMemory) {
	assert(!destroyed_);
	uint32_t memoryTypeIndex;
	bool pass = vulkan_->MemoryTypeFromProperties(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memoryTypeIndex);
	assert(pass);
	if (!pass) {
		return ALLOCATE_FAILED;
	}
	if (memoryTypeIndex_ == UNDEFINED_MEMORY_TYPE) {
		memoryTypeIndex_ = memoryTypeIndex;
	} else if (memoryTypeIndex_ != memoryTypeIndex) {
		assert(memoryTypeIndex_ == memoryTypeIndex);
		return ALLOCATE_FAILED;
	}

	size_t align = reqs.alignment <= SLAB_GRAIN_SIZE ? 1 : (reqs.alignment >> SLAB_GRAIN_SHIFT);
	size_t blocks = (reqs.size + SLAB_GRAIN_SIZE - 1) >> SLAB_GRAIN_SHIFT;

	const size_t numSlabs = slabs_.size();
	for (size_t i = 0; i < numSlabs; ++i) {
		// We loop starting at the last successful allocation.
		// This helps us "creep forward", and also spend less time allocating.
		const size_t actualSlab = (lastSlab_ + i) % numSlabs;
		Slab &slab = slabs_[actualSlab];
		size_t start = slab.nextFree;

		while (start < slab.usage.size()) {
			start = (start + align - 1) & ~(align - 1);
			if (AllocateFromSlab(slab, start, blocks)) {
				// Allocated?  Great, let's return right away.
				*deviceMemory = slab.deviceMemory;
				lastSlab_ = actualSlab;
				return start << SLAB_GRAIN_SHIFT;
			}
		} 
	}

	// Okay, we couldn't fit it into any existing slabs.  We need a new one.
	if (!AllocateSlab(reqs.size)) {
		return ALLOCATE_FAILED;
	}

	// Guaranteed to be the last one, unless it failed to allocate.
	Slab &slab = slabs_[slabs_.size() - 1];
	size_t start = 0;
	if (AllocateFromSlab(slab, start, blocks)) {
		*deviceMemory = slab.deviceMemory;
		lastSlab_ = slabs_.size() - 1;
		return start << SLAB_GRAIN_SHIFT;
	}

	// Somehow... we're out of space.  Darn.
	return ALLOCATE_FAILED;
}

bool VulkanDeviceAllocator::AllocateFromSlab(Slab &slab, size_t &start, size_t blocks) {
	assert(!destroyed_);
	bool matched = true;

	if (start + blocks > slab.usage.size()) {
		start = slab.usage.size();
		return false;
	}

	for (size_t i = 0; i < blocks; ++i) {
		if (slab.usage[start + i]) {
			// If we just ran into one, there's probably an allocation size.
			auto it = slab.allocSizes.find(start + i);
			if (it != slab.allocSizes.end()) {
				start += i + it->second;
			} else {
				// We don't know how big it is, so just skip to the next one.
				start += i + 1;
			}
			return false;
		}
	}

	// Okay, this run is good.  Actually mark it.
	for (size_t i = 0; i < blocks; ++i) {
		slab.usage[start + i] = 1;
	}
	slab.nextFree = start + blocks;
	if (slab.nextFree >= slab.usage.size()) {
		slab.nextFree = 0;
	}

	// Remember the size so we can free.
	slab.allocSizes[start] = blocks;
	return true;
}

void VulkanDeviceAllocator::Free(VkDeviceMemory deviceMemory, size_t offset) {
	assert(!destroyed_);

	// First, let's validate.  This will allow stack traces to tell us when frees are bad.
	size_t start = offset >> SLAB_GRAIN_SHIFT;
	bool found = false;
	for (Slab &slab : slabs_) {
		if (slab.deviceMemory != deviceMemory) {
			continue;
		}

		auto it = slab.allocSizes.find(start);
		if (it == slab.allocSizes.end()) {
			// Ack, a double free?
			Crash();
		}
		if (slab.usage[start] != 1) {
			// This means a double free, while queued to actually free.
			Crash();
		}

		// Mark it as "free in progress".
		slab.usage[start] = 2;
		found = true;
		break;
	}

	// Wrong deviceMemory even?  Maybe it was already decimated, but that means a double-free.
	if (!found) {
		Crash();
	}

	// Okay, now enqueue.  It's valid.
	FreeInfo *info = new FreeInfo(this, deviceMemory, offset);
	vulkan_->Delete().QueueCallback(&DispatchFree, info);
}

void VulkanDeviceAllocator::ExecuteFree(FreeInfo *userdata) {
	if (destroyed_) {
		// We already freed this, and it's been validated.
		delete userdata;
		return;
	}

	VkDeviceMemory deviceMemory = userdata->deviceMemory;
	size_t offset = userdata->offset;

	// Revalidate in case something else got freed and made things inconsistent.
	size_t start = offset >> SLAB_GRAIN_SHIFT;
	bool found = false;
	for (Slab &slab : slabs_) {
		if (slab.deviceMemory != deviceMemory) {
			continue;
		}

		auto it = slab.allocSizes.find(start);
		if (it != slab.allocSizes.end()) {
			size_t size = it->second;
			for (size_t i = 0; i < size; ++i) {
				slab.usage[start + i] = 0;
			}
			slab.allocSizes.erase(it);
		} else {
			// Ack, a double free?
			Crash();
		}
		found = true;
		break;
	}

	// Wrong deviceMemory even?  Maybe it was already decimated, but that means a double-free.
	if (!found) {
		Crash();
	}

	delete userdata;
}

bool VulkanDeviceAllocator::AllocateSlab(size_t minBytes) {
	assert(!destroyed_);
	if (!slabs_.empty() && minSlabSize_ < maxSlabSize_) {
		// We're allocating an additional slab, so rachet up its size.
		minSlabSize_ <<= 1;
	}

	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = minSlabSize_;
	alloc.memoryTypeIndex = memoryTypeIndex_;

	while (alloc.allocationSize < minBytes) {
		alloc.allocationSize <<= 1;
	}

	VkDeviceMemory deviceMemory;
	VkResult res = vkAllocateMemory(vulkan_->GetDevice(), &alloc, NULL, &deviceMemory);
	if (res != VK_SUCCESS) {
		// If it's something else, we used it wrong?
		assert(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
		// Okay, so we ran out of memory.
		return false;
	}

	slabs_.resize(slabs_.size() + 1);
	Slab &slab = slabs_[slabs_.size() - 1];
	slab.deviceMemory = deviceMemory;
	slab.usage.resize(alloc.allocationSize >> SLAB_GRAIN_SHIFT);

	return true;
}

void VulkanDeviceAllocator::Decimate() {
	assert(!destroyed_);
	bool foundFree = false;

	for (size_t i = 0; i < slabs_.size(); ++i) {
		// Go backwards.  This way, we keep the largest free slab.
		// We do this here (instead of the for) since size_t is unsigned.
		size_t index = slabs_.size() - i - 1;

		if (!slabs_[index].allocSizes.empty()) {
			continue;
		}

		if (!foundFree) {
			// Let's allow one free slab, so we have room.
			foundFree = true;
			continue;
		}

		// Okay, let's free this one up.
		vulkan_->Delete().QueueDeleteDeviceMemory(slabs_[index].deviceMemory);
		slabs_.erase(slabs_.begin() + index);

		// Let's check the next one, which is now in this same slot.
		--i;
	}
}
