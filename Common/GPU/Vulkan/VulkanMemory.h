#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "Common/Data/Collections/FastVec.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/GPUBackendCommon.h"

// Forward declaration
VK_DEFINE_HANDLE(VmaAllocation);

// VulkanMemory
//
// Vulkan memory management utils.

// VulkanPushBuffer
// Simple incrementing allocator.
// Use these to push vertex, index and uniform data. Generally you'll have two or three of these
// and alternate on each frame. Make sure not to reset until the fence from the last time you used it
// has completed.
// NOTE: This has now been replaced with VulkanPushPool for all uses except the vertex cache.
class VulkanPushBuffer : public GPUMemoryManager {
	struct BufInfo {
		VkBuffer buffer;
		VmaAllocation allocation;
	};

public:
	// NOTE: If you create a push buffer with PushBufferType::GPU_ONLY,
	// then you can't use any of the push functions as pointers will not be reachable from the CPU.
	// You must in this case use Allocate() only, and pass the returned offset and the VkBuffer to Vulkan APIs.
	VulkanPushBuffer(VulkanContext *vulkan, const char *name, size_t size, VkBufferUsageFlags usage);
	~VulkanPushBuffer();

	void Destroy(VulkanContext *vulkan);

	void Reset() { offset_ = 0; }

	void GetDebugString(char *buffer, size_t bufSize) const override;
	const char *Name() const override {
		return name_;
	}

	// Needs context in case of defragment.
	void Begin(VulkanContext *vulkan) {
		buf_ = 0;
		offset_ = 0;
		// Note: we must defrag because some buffers may be smaller than size_.
		Defragment(vulkan);
		Map();
	}

	void BeginNoReset() { Map(); }
	void End() { Unmap(); }

	void Map();
	void Unmap();

	// When using the returned memory, make sure to bind the returned vkbuf.
	uint8_t *Allocate(VkDeviceSize numBytes, VkDeviceSize alignment, VkBuffer *vkbuf, uint32_t *bindOffset) {
		size_t offset = (offset_ + alignment - 1) & ~(alignment - 1);
		if (offset + numBytes > size_) {
			NextBuffer(numBytes);
			offset = offset_;
		}
		offset_ = offset + numBytes;
		*bindOffset = (uint32_t)offset;
		*vkbuf = buffers_[buf_].buffer;
		return writePtr_ + offset;
	}

	VkDeviceSize Push(const void *data, VkDeviceSize numBytes, int alignment, VkBuffer *vkbuf) {
		uint32_t bindOffset;
		uint8_t *ptr = Allocate(numBytes, alignment, vkbuf, &bindOffset);
		memcpy(ptr, data, numBytes);
		return bindOffset;
	}

	size_t GetOffset() const { return offset_; }
	size_t GetTotalSize() const;

private:
	bool AddBuffer();
	void NextBuffer(size_t minSize);
	void Defragment(VulkanContext *vulkan);

	VulkanContext *vulkan_;

	std::vector<BufInfo> buffers_;
	size_t buf_ = 0;
	size_t offset_ = 0;
	size_t size_ = 0;
	uint8_t *writePtr_ = nullptr;
	VkBufferUsageFlags usage_;
	const char *name_;
};

// Simple memory pushbuffer pool that can share blocks between the "frames", to reduce the impact of push memory spikes -
// a later frame can gobble up redundant buffers from an earlier frame even if they don't share frame index.
// NOT thread safe! Can only be used from one thread (our main thread).
class VulkanPushPool : public GPUMemoryManager {
public:
	VulkanPushPool(VulkanContext *vulkan, const char *name, size_t originalBlockSize, VkBufferUsageFlags usage);
	~VulkanPushPool();

	void Destroy();
	void BeginFrame();

	const char *Name() const override {
		return name_;
	}
	void GetDebugString(char *buffer, size_t bufSize) const override;

	// When using the returned memory, make sure to bind the returned vkbuf.
	// It is okay to allocate 0 bytes.
	uint8_t *Allocate(VkDeviceSize numBytes, VkDeviceSize alignment, VkBuffer *vkbuf, uint32_t *bindOffset) {
		_dbg_assert_(curBlockIndex_ >= 0);
		
		Block &block = blocks_[curBlockIndex_];

		VkDeviceSize offset = (block.used + (alignment - 1)) & ~(alignment - 1);
		if (offset + numBytes <= block.size) {
			block.used = offset + numBytes;
			*vkbuf = block.buffer;
			*bindOffset = (uint32_t)offset;
			return block.writePtr + offset;
		}

		NextBlock(numBytes);

		*vkbuf = blocks_[curBlockIndex_].buffer;
		*bindOffset = 0;  // Newly allocated buffer will start at 0.
		return blocks_[curBlockIndex_].writePtr;
	}

	// NOTE: If you can avoid this by writing the data directly into memory returned from Allocate,
	// do so. Savings from avoiding memcpy can be significant.
	VkDeviceSize Push(const void *data, VkDeviceSize numBytes, int alignment, VkBuffer *vkbuf) {
		uint32_t bindOffset;
		uint8_t *ptr = Allocate(numBytes, alignment, vkbuf, &bindOffset);
		memcpy(ptr, data, numBytes);
		return bindOffset;
	}

	size_t GetUsedThisFrame() const;

private:
	void NextBlock(VkDeviceSize allocationSize);

	struct Block {
		~Block();
		VkBuffer buffer;
		VmaAllocation allocation;

		VkDeviceSize size;
		VkDeviceSize used;

		int frameIndex;
		bool original;  // these blocks aren't garbage collected.
		double lastUsed;

		uint8_t *writePtr;

		void Destroy(VulkanContext *vulkan);
	};

	Block CreateBlock(size_t sz);

	VulkanContext *vulkan_;
	VkDeviceSize originalBlockSize_;
	std::vector<Block> blocks_;
	VkBufferUsageFlags usage_;
	int curBlockIndex_ = -1;
	const char *name_;
};

// Only appropriate for use in a per-frame pool.
class VulkanDescSetPool {
public:
	VulkanDescSetPool(const char *tag, bool grow) : tag_(tag), grow_(grow) {}
	~VulkanDescSetPool();

	// Must call this before use: defines how to clear cache of ANY returned values from Allocate().
	void Setup(const std::function<void()> &clear) {
		clear_ = clear;
	}
	void Create(VulkanContext *vulkan, const VkDescriptorPoolCreateInfo &info, const std::vector<VkDescriptorPoolSize> &sizes);
	// Allocate a new set, which may resize and empty the current sets.
	// Use only for the current frame, unless in a cache cleared by clear_.
	VkDescriptorSet Allocate(int n, const VkDescriptorSetLayout *layouts, const char *tag);
	void Reset();
	void Destroy();

private:
	VkResult Recreate(bool grow);

	const char *tag_;
	VulkanContext *vulkan_ = nullptr;
	VkDescriptorPool descPool_ = VK_NULL_HANDLE;
	VkDescriptorPoolCreateInfo info_{};
	std::vector<VkDescriptorPoolSize> sizes_;
	std::function<void()> clear_;
	uint32_t usage_ = 0;
	bool grow_;
};
