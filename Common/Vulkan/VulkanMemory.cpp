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

#include "Common/Log.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "math/math_util.h"

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, size_t size) : device_(vulkan->GetDevice()), buf_(0), offset_(0), size_(size), writePtr_(nullptr) {
	vulkan->MemoryTypeFromProperties(0xFFFFFFFF, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memoryTypeIndex_);

	bool res = AddBuffer();
	assert(res);
}

VulkanPushBuffer::~VulkanPushBuffer() {
	assert(buffers_.empty());
}

bool VulkanPushBuffer::AddBuffer() {
	BufInfo info;

	VkBufferCreateInfo b = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	b.size = size_;
	b.flags = 0;
	b.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	b.queueFamilyIndexCount = 0;
	b.pQueueFamilyIndices = nullptr;

	VkResult res = vkCreateBuffer(device_, &b, nullptr, &info.buffer);
	if (VK_SUCCESS != res) {
		_assert_msg_(G3D, false, "vkCreateBuffer failed! result=%d", (int)res);
		return false;
	}

	// Make validation happy.
	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(device_, info.buffer, &reqs);
	// TODO: We really should use memoryTypeIndex here..

	// Okay, that's the buffer. Now let's allocate some memory for it.
	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	// TODO: Should check here that memoryTypeIndex_ matches reqs.memoryTypeBits.
	alloc.memoryTypeIndex = memoryTypeIndex_;
	alloc.allocationSize = reqs.size;

	res = vkAllocateMemory(device_, &alloc, nullptr, &info.deviceMemory);
	if (VK_SUCCESS != res) {
		_assert_msg_(G3D, false, "vkAllocateMemory failed! size=%d result=%d", (int)reqs.size, (int)res);
		vkDestroyBuffer(device_, info.buffer, nullptr);
		return false;
	}
	res = vkBindBufferMemory(device_, info.buffer, info.deviceMemory, 0);
	if (VK_SUCCESS != res) {
		ELOG("vkBindBufferMemory failed! result=%d", (int)res);
		vkFreeMemory(device_, info.deviceMemory, nullptr);
		vkDestroyBuffer(device_, info.buffer, nullptr);
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

size_t VulkanPushBuffer::GetTotalSize() const {
	size_t sum = 0;
	if (buffers_.size() > 1)
		sum += size_ * (buffers_.size() - 1);
	sum += offset_;
	return sum;
}

void VulkanPushBuffer::Map() {
	assert(!writePtr_);
	VkResult res = vkMapMemory(device_, buffers_[buf_].deviceMemory, 0, size_, 0, (void **)(&writePtr_));
	assert(writePtr_);
	assert(VK_SUCCESS == res);
}

void VulkanPushBuffer::Unmap() {
	assert(writePtr_);
	/*
	// Should not need this since we use coherent memory.
	VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
	range.offset = 0;
	range.size = offset_;
	range.memory = buffers_[buf_].deviceMemory;
	vkFlushMappedMemoryRanges(device_, 1, &range);
	*/
	vkUnmapMemory(device_, buffers_[buf_].deviceMemory);
	writePtr_ = nullptr;
}

VulkanDeviceAllocator::VulkanDeviceAllocator(VulkanContext *vulkan, size_t minSlabSize, size_t maxSlabSize)
	: vulkan_(vulkan), minSlabSize_(minSlabSize), maxSlabSize_(maxSlabSize) {
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
			int slabUsage = slab.usage[pair.first];
			// If it's not 2 (queued), there's a leak.
			// If it's zero, it means allocSizes is somehow out of sync.
			if (slabUsage == 1) {
				ERROR_LOG(G3D, "VulkanDeviceAllocator detected memory leak of size %d", (int)pair.second);
			} else {
				_dbg_assert_msg_(G3D, slabUsage == 2, "Destroy: slabUsage has unexpected value %d", slabUsage);
			}
		}

		assert(slab.deviceMemory);
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

	size_t size = reqs.size;

	size_t align = reqs.alignment <= SLAB_GRAIN_SIZE ? 1 : (size_t)(reqs.alignment >> SLAB_GRAIN_SHIFT);
	size_t blocks = (size_t)((size + SLAB_GRAIN_SIZE - 1) >> SLAB_GRAIN_SHIFT);

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
	if (!AllocateSlab(size)) {
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

int VulkanDeviceAllocator::ComputeUsagePercent() const {
	int blockSum = 0;
	int blocksUsed = 0;
	for (int i = 0; i < slabs_.size(); i++) {
		blockSum += (int)slabs_[i].usage.size();
		for (int j = 0; j < slabs_[i].usage.size(); j++) {
			blocksUsed += slabs_[i].usage[j] != 0 ? 1 : 0;
		}
	}
	return blockSum == 0 ? 0 : 100 * blocksUsed / blockSum;
}

void VulkanDeviceAllocator::Free(VkDeviceMemory deviceMemory, size_t offset) {
	assert(!destroyed_);

	_assert_msg_(G3D, !slabs_.empty(), "No slabs - can't be anything to free! double-freed?");

	// First, let's validate.  This will allow stack traces to tell us when frees are bad.
	size_t start = offset >> SLAB_GRAIN_SHIFT;
	bool found = false;
	for (Slab &slab : slabs_) {
		if (slab.deviceMemory != deviceMemory) {
			continue;
		}

		auto it = slab.allocSizes.find(start);
		_assert_msg_(G3D, it != slab.allocSizes.end(), "Double free?");
		// This means a double free, while queued to actually free.
		_assert_msg_(G3D, slab.usage[start] == 1, "Double free when queued to free!");

		// Mark it as "free in progress".
		slab.usage[start] = 2;
		found = true;
		break;
	}

	// Wrong deviceMemory even?  Maybe it was already decimated, but that means a double-free.
	_assert_msg_(G3D, found, "Failed to find allocation to free! Double-freed?");

	// Okay, now enqueue.  It's valid.
	FreeInfo *info = new FreeInfo(this, deviceMemory, offset);
	// Dispatches a call to ExecuteFree on the next delete round.
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
			_assert_msg_(G3D, false, "Double free? Block missing at offset %d", userdata->offset);
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

bool VulkanDeviceAllocator::AllocateSlab(VkDeviceSize minBytes) {
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
	slab.usage.resize((size_t)(alloc.allocationSize >> SLAB_GRAIN_SHIFT));

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
