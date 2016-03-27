#pragma once

#include <vector>
#include <unordered_map>
#include "Common/Vulkan/VulkanContext.h"

// VulkanMemory
//
// Vulkan memory management utils.

// VulkanPushBuffer
// Simple incrementing allocator.
// Use these to push vertex, index and uniform data. Generally you'll have two of these
// and alternate on each frame. Make sure not to reset until the fence from the last time you used it
// has completed.
//
// TODO: Make it possible to suballocate pushbuffers from a large DeviceMemory block.
class VulkanPushBuffer {
	struct BufInfo {
		VkBuffer buffer;
		VkDeviceMemory deviceMemory;
	};

public:
	VulkanPushBuffer(VulkanContext *vulkan, size_t size);

	~VulkanPushBuffer() {
		assert(buffers_.empty());
	}

	void Destroy(VulkanContext *vulkan) {
		for (const BufInfo &info : buffers_) {
			vulkan->Delete().QueueDeleteBuffer(info.buffer);
			vulkan->Delete().QueueDeleteDeviceMemory(info.deviceMemory);
		}

		buffers_.clear();
	}

	void Reset() { offset_ = 0; }

	// Needs context in case of defragment.
	void Begin(VulkanContext *vulkan) {
		buf_ = 0;
		offset_ = 0;
		// Note: we must defrag because some buffers may be smaller than size_.
		Defragment(vulkan);
		Map();
	}

	void End() {
		Unmap();
	}

	void Map() {
		assert(!writePtr_);
		VkResult res = vkMapMemory(device_, buffers_[buf_].deviceMemory, offset_, size_, 0, (void **)(&writePtr_));
		assert(VK_SUCCESS == res);
	}

	void Unmap() {
		assert(writePtr_);
		/*
		VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
		range.offset = 0;
		range.size = offset_;
		range.memory = buffers_[buf_].deviceMemory;
		vkFlushMappedMemoryRanges(device_, 1, &range);
		*/
		vkUnmapMemory(device_, buffers_[buf_].deviceMemory);
		writePtr_ = nullptr;
	}

	// When using the returned memory, make sure to bind the returned vkbuf.
	// This will later allow for handling overflow correctly.
	size_t Allocate(size_t numBytes, VkBuffer *vkbuf) {
		size_t out = offset_;
		offset_ += (numBytes + 3) & ~3;  // Round up to 4 bytes.

		if (offset_ >= size_) {
			NextBuffer(numBytes);
			out = offset_;
			offset_ += (numBytes + 3) & ~3;
		}
		*vkbuf = buffers_[buf_].buffer;
		return out;
	}

	// Returns the offset that should be used when binding this buffer to get this data.
	size_t Push(const void *data, size_t size, VkBuffer *vkbuf) {
		assert(writePtr_);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	uint32_t PushAligned(const void *data, size_t size, int align, VkBuffer *vkbuf) {
		assert(writePtr_);
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return (uint32_t)off;
	}

	size_t GetOffset() const {
		return offset_;
	}

	// "Zero-copy" variant - you can write the data directly as you compute it.
	void *Push(size_t size, uint32_t *bindOffset, VkBuffer *vkbuf) {
		assert(writePtr_);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}

private:
	bool AddBuffer();
	void NextBuffer(size_t minSize);
	void Defragment(VulkanContext *vulkan);

	VkDevice device_;
	std::vector<BufInfo> buffers_;
	size_t buf_;
	size_t offset_;
	size_t size_;
	uint32_t memoryTypeIndex_;
	uint8_t *writePtr_;
};

// VulkanDeviceAllocator
//
// Implements a slab based allocator that manages suballocations inside the slabs.
// Bitmaps are used to handle allocation state, with a 1KB grain.
class VulkanDeviceAllocator {
public:
	// Slab sizes start at minSlabSize and double until maxSlabSize.
	// Total slab count is unlimited, as long as there's free memory.
	VulkanDeviceAllocator(VulkanContext *vulkan, size_t minSlabSize, size_t maxSlabSize);
	~VulkanDeviceAllocator();

	// Requires all memory be free beforehand (including all pending deletes.)
	void Destroy();

	void Begin() {
		Decimate();
	}

	void End() {
	}

	// May return ALLOCATE_FAILED if the allocation fails.
	size_t Allocate(const VkMemoryRequirements &reqs, VkDeviceMemory *deviceMemory);

	// Crashes on a double or misfree.
	void Free(VkDeviceMemory deviceMemory, size_t offset);

	static const size_t ALLOCATE_FAILED = -1;

private:
	static const size_t SLAB_GRAIN_SIZE = 1024;
	static const uint8_t SLAB_GRAIN_SHIFT = 10;
	static const uint32_t UNDEFINED_MEMORY_TYPE = -1;

	struct Slab {
		VkDeviceMemory deviceMemory;
		std::vector<uint8_t> usage;
		std::unordered_map<size_t, size_t> allocSizes;
		size_t nextFree;

		size_t Size() {
			return usage.size() * SLAB_GRAIN_SIZE;
		}
	};

	struct FreeInfo {
		explicit FreeInfo(VulkanDeviceAllocator *a, VkDeviceMemory d, size_t o)
			: allocator(a), deviceMemory(d), offset(o) {
		}

		VulkanDeviceAllocator *allocator;
		VkDeviceMemory deviceMemory;
		size_t offset;
	};

	static void DispatchFree(void *userdata) {
		auto freeInfo = static_cast<FreeInfo *>(userdata);
		freeInfo->allocator->ExecuteFree(freeInfo);
	}

	bool AllocateSlab(size_t minBytes);
	bool AllocateFromSlab(Slab &slab, size_t &start, size_t blocks);
	void Decimate();
	void ExecuteFree(FreeInfo *userdata);

	VulkanContext *const vulkan_;
	std::vector<Slab> slabs_;
	size_t lastSlab_;
	size_t minSlabSize_;
	const size_t maxSlabSize_;
	uint32_t memoryTypeIndex_;
	bool destroyed_;
};
