#pragma once

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
// TODO: Make this auto-grow and shrink. Need to be careful about returning and using the new
// buffer handle on overflow.
class VulkanPushBuffer {
public:
	VulkanPushBuffer(VulkanContext *vulkan, size_t size);

	~VulkanPushBuffer() {
		assert(buffer_ == VK_NULL_HANDLE);
		assert(deviceMemory_ == VK_NULL_HANDLE);
	}

	void Destroy(VulkanContext *vulkan) {
		vulkan->Delete().QueueDeleteBuffer(buffer_);
		vulkan->Delete().QueueDeleteDeviceMemory(deviceMemory_);
		buffer_ = VK_NULL_HANDLE;
		deviceMemory_ = VK_NULL_HANDLE;
	}

	void Reset() { offset_ = 0; }

	void Begin(VkDevice device) {
		offset_ = 0;
		VkResult res = vkMapMemory(device, deviceMemory_, 0, size_, 0, (void **)(&writePtr_));
		assert(VK_SUCCESS == res);
	}

	void End(VkDevice device) {
		vkUnmapMemory(device, deviceMemory_);
		writePtr_ = nullptr;
	}

	// When using the returned memory, make sure to bind the returned vkbuf.
	// This will later allow for handling overflow correctly.
	size_t Allocate(size_t numBytes, VkBuffer *vkbuf) {
		size_t out = offset_;
		offset_ += (numBytes + 3) & ~3;  // Round up to 4 bytes.
		if (offset_ >= size_) {
			// TODO: Allocate a second buffer, then combine them on the next frame.
#ifdef _WIN32
			DebugBreak();
#endif
		}
		*vkbuf = buffer_;
		return out;
	}

	// TODO: Add alignment support?
	// Returns the offset that should be used when binding this buffer to get this data.
	size_t Push(const void *data, size_t size, VkBuffer *vkbuf) {
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	uint32_t PushAligned(const void *data, size_t size, int align, VkBuffer *vkbuf) {
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size, vkbuf);
		memcpy(writePtr_ + off, data, size);
		return (uint32_t)off;
	}

	size_t GetOffset() const {
		return offset_;
	}

	// "Zero-copy" variant - you can write the data directly as you compute it.
	void *Push(size_t size, size_t *bindOffset, VkBuffer *vkbuf) {
		size_t off = Allocate(size, vkbuf);
		*bindOffset = off;
		return writePtr_ + off;
	}

private:
	VkDeviceMemory deviceMemory_;
	VkBuffer buffer_;
	size_t offset_;
	size_t size_;
	uint8_t *writePtr_;
};
