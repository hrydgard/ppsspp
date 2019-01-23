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
	~VulkanPushBuffer();

	void Destroy(VulkanContext *vulkan);

	void Reset() { offset_ = 0; }

	// Needs context in case of defragment.
	void Begin(VulkanContext *vulkan) {
		buf_ = 0;
		offset_ = 0;
		// Note: we must defrag because some buffers may be smaller than size_.
		Defragment(vulkan);
		Map();
	}

	void BeginNoReset() {
		Map();
	}

	void End() {
		Unmap();
	}

	void Map();

	void Unmap();

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
	// Recommended.
	void *Push(size_t size, uint32_t *bindOffset, VkBuffer *vkbuf) {
		assert(writePtr_);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}
	void *PushAligned(size_t size, uint32_t *bindOffset, VkBuffer *vkbuf, int align) {
		assert(writePtr_);
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}

	size_t GetTotalSize() const;

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
	size_t Allocate(const VkMemoryRequirements &reqs, VkDeviceMemory *deviceMemory, const std::string &tag);

	// Crashes on a double or misfree.
	void Free(VkDeviceMemory deviceMemory, size_t offset);

	inline void Touch(VkDeviceMemory deviceMemory, size_t offset) {
		if (TRACK_TOUCH) {
			DoTouch(deviceMemory, offset);
		}
	}

	static const size_t ALLOCATE_FAILED = -1;
	// Set to true to report potential leaks / long held textures.
	static const bool TRACK_TOUCH = false;

	int GetSlabCount() const { return (int)slabs_.size(); }
	int GetMinSlabSize() const { return (int)minSlabSize_; }
	int GetMaxSlabSize() const { return (int)maxSlabSize_; }

	int ComputeUsagePercent() const;
	std::vector<uint8_t> GetSlabUsage(int slab) const;

private:
	static const size_t SLAB_GRAIN_SIZE = 1024;
	static const uint8_t SLAB_GRAIN_SHIFT = 10;
	static const uint32_t UNDEFINED_MEMORY_TYPE = -1;

	struct UsageInfo {
		std::string tag;
		float created;
		float touched;
	};

	struct Slab {
		VkDeviceMemory deviceMemory;
		uint32_t memoryTypeIndex = UNDEFINED_MEMORY_TYPE;
		std::vector<uint8_t> usage;
		std::unordered_map<size_t, size_t> allocSizes;
		std::unordered_map<size_t, UsageInfo> tags;
		size_t nextFree;
		size_t totalUsage;

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
		freeInfo->allocator->ExecuteFree(freeInfo);  // this deletes freeInfo
	}

	bool AllocateSlab(VkDeviceSize minBytes, int memoryTypeIndex);
	bool AllocateFromSlab(Slab &slab, size_t &start, size_t blocks, const std::string &tag);
	void Decimate();
	void DoTouch(VkDeviceMemory deviceMemory, size_t offset);
	void ExecuteFree(FreeInfo *userdata);
	void ReportOldUsage();

	VulkanContext *const vulkan_;
	std::vector<Slab> slabs_;
	size_t lastSlab_ = 0;
	size_t minSlabSize_;
	const size_t maxSlabSize_;
	bool destroyed_ = false;
};
