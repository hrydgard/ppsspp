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

// Simple memory pushbuffer pool that can share blocks between the "frames", to reduce the impact of push memory spikes -
// a later frame can gobble up redundant buffers from an earlier frame even if they don't share frame index.
// NOT thread safe! Can only be used from one thread (our main thread).
class VulkanPushPool : public GPUMemoryManager {
public:
	// Slack is reserved space at the end of each block, which can be useful if you do things like writing a vec3 with a vec4 store,
	// which can be faster when using SIMD, or decode two vertices in parallel.
	VulkanPushPool(VulkanContext *vulkan, const char *name, size_t originalBlockSize, size_t slack, VkBufferUsageFlags usage);
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
		if (offset + numBytes + slack_ <= block.size) {
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

		int frameIndex;  // -1 means that it's "common", it can be grabbed by any frame as needed.
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
	VkDeviceSize slack_;
	const char *name_;
};

// Just a plain old static buffer in GPU memory. You can upload to it
// using a PushPool.
class VulkanBuffer {
public:
	// Can be called again to resize/recreate. Will not preserve any data.
	void Create(VulkanContext *vulkan, const char *name, VkDeviceSize size, VkBufferUsageFlags usage);
	void Destroy(VulkanContext *vulkan);

	VkBuffer Buffer() const { return buffer_; }
	VkDeviceSize Size() const { return size_; }

private:
	VkBuffer buffer_ = VK_NULL_HANDLE;
	VmaAllocation allocation_ = VK_NULL_HANDLE;
	VkDeviceSize size_ = 0;  // convenience
};
