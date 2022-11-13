#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "Common/GPU/Vulkan/VulkanContext.h"

// Forward declaration
VK_DEFINE_HANDLE(VmaAllocation);

// VulkanMemory
//
// Vulkan memory management utils.

enum class PushBufferType {
	CPU_TO_GPU,
	GPU_ONLY,
};

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
		VmaAllocation allocation;
	};

public:
	// NOTE: If you create a push buffer with PushBufferType::GPU_ONLY,
	// then you can't use any of the push functions as pointers will not be reachable from the CPU.
	// You must in this case use Allocate() only, and pass the returned offset and the VkBuffer to Vulkan APIs.
	VulkanPushBuffer(VulkanContext *vulkan, const char *name, size_t size, VkBufferUsageFlags usage, PushBufferType type);
	~VulkanPushBuffer();

	void Destroy(VulkanContext *vulkan);

	void Reset() { offset_ = 0; }

	// Needs context in case of defragment.
	void Begin(VulkanContext *vulkan) {
		buf_ = 0;
		offset_ = 0;
		// Note: we must defrag because some buffers may be smaller than size_.
		Defragment(vulkan);
		if (type_ == PushBufferType::CPU_TO_GPU)
			Map();
	}

	void BeginNoReset() {
		if (type_ == PushBufferType::CPU_TO_GPU)
			Map();
	}

	void End() {
		if (type_ == PushBufferType::CPU_TO_GPU)
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
		_dbg_assert_(writePtr_);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	uint32_t PushAligned(const void *data, size_t size, int align, VkBuffer *vkbuf) {
		_dbg_assert_(writePtr_);
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return (uint32_t)off;
	}

	size_t GetOffset() const {
		return offset_;
	}

	const char *Name() const {
		return name_;
	}

	// "Zero-copy" variant - you can write the data directly as you compute it.
	// Recommended.
	void *Push(size_t size, uint32_t *bindOffset, VkBuffer *vkbuf) {
		_dbg_assert_(writePtr_);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}
	void *PushAligned(size_t size, uint32_t *bindOffset, VkBuffer *vkbuf, int align) {
		_dbg_assert_(writePtr_);
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size, vkbuf);
		*bindOffset = (uint32_t)off;
		return writePtr_ + off;
	}

	template<class T>
	void PushUBOData(const T &data, VkDescriptorBufferInfo *info) {
		uint32_t bindOffset;
		void *ptr = PushAligned(sizeof(T), &bindOffset, &info->buffer, vulkan_->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment);
		memcpy(ptr, &data, sizeof(T));
		info->offset = bindOffset;
		info->range = sizeof(T);
	}

	size_t GetTotalSize() const;

private:
	bool AddBuffer();
	void NextBuffer(size_t minSize);
	void Defragment(VulkanContext *vulkan);

	VulkanContext *vulkan_;
	PushBufferType type_;

	std::vector<BufInfo> buffers_;
	size_t buf_ = 0;
	size_t offset_ = 0;
	size_t size_ = 0;
	uint8_t *writePtr_ = nullptr;
	VkBufferUsageFlags usage_;
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
	VkDescriptorPoolCreateInfo info_;
	std::vector<VkDescriptorPoolSize> sizes_;
	std::function<void()> clear_;
	uint32_t usage_ = 0;
	bool grow_;
};
