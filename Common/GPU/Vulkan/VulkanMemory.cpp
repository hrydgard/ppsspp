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

#include <algorithm>
#include <set>
#include <mutex>

#include "Common/Math/math_util.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/Data/Text/Parsers.h"

using namespace PPSSPP_VK;

// Always keep around push buffers at least this long (seconds).
static const double PUSH_GARBAGE_COLLECTION_DELAY = 10.0;

VulkanPushBuffer::VulkanPushBuffer(VulkanContext *vulkan, const char *name, size_t size, VkBufferUsageFlags usage)
		: vulkan_(vulkan), name_(name), size_(size), usage_(usage) {
	RegisterGPUMemoryManager(this);
	bool res = AddBuffer();
	_assert_(res);
}

VulkanPushBuffer::~VulkanPushBuffer() {
	UnregisterGPUMemoryManager(this);
	_dbg_assert_(!writePtr_);
	_assert_(buffers_.empty());
}

bool VulkanPushBuffer::AddBuffer() {
	BufInfo info;
	VkDevice device = vulkan_->GetDevice();

	VkBufferCreateInfo b{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	b.size = size_;
	b.flags = 0;
	b.usage = usage_;
	b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	b.queueFamilyIndexCount = 0;
	b.pQueueFamilyIndices = nullptr;

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	VmaAllocationInfo allocInfo{};

	VkResult res = vmaCreateBuffer(vulkan_->Allocator(), &b, &allocCreateInfo, &info.buffer, &info.allocation, &allocInfo);
	if (VK_SUCCESS != res) {
		_assert_msg_(false, "vkCreateBuffer failed! result=%d", (int)res);
		return false;
	}

	vulkan_->SetDebugName(info.buffer, VK_OBJECT_TYPE_BUFFER, name_);

	buffers_.push_back(info);
	buf_ = buffers_.size() - 1;
	return true;
}

void VulkanPushBuffer::Destroy(VulkanContext *vulkan) {
	_dbg_assert_(!writePtr_);
	for (BufInfo &info : buffers_) {
		vulkan->Delete().QueueDeleteBufferAllocation(info.buffer, info.allocation);
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
		_assert_(res);
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
	_assert_(res);
}

size_t VulkanPushBuffer::GetTotalSize() const {
	size_t sum = 0;
	if (buffers_.size() > 1)
		sum += size_ * (buffers_.size() - 1);
	sum += offset_;
	return sum;
}

void VulkanPushBuffer::GetDebugString(char *buffer, size_t bufSize) const {
	size_t sum = 0;
	if (buffers_.size() > 1)
		sum += size_ * (buffers_.size() - 1);
	sum += offset_;
	size_t capacity = size_ * buffers_.size();
	snprintf(buffer, bufSize, "Push %s: %s / %s", name_, NiceSizeFormat(sum).c_str(), NiceSizeFormat(capacity).c_str());
}

void VulkanPushBuffer::Map() {
	_dbg_assert_(!writePtr_);
	VkResult res = vmaMapMemory(vulkan_->Allocator(), buffers_[buf_].allocation, (void **)(&writePtr_));
	_dbg_assert_(writePtr_);
	_assert_(VK_SUCCESS == res);
}

void VulkanPushBuffer::Unmap() {
	_dbg_assert_msg_(writePtr_ != nullptr, "VulkanPushBuffer::Unmap: writePtr_ null here means we have a bug (map/unmap mismatch)");
	if (!writePtr_)
		return;

	vmaUnmapMemory(vulkan_->Allocator(), buffers_[buf_].allocation);
	writePtr_ = nullptr;
}

VulkanDescSetPool::~VulkanDescSetPool() {
	_assert_msg_(descPool_ == VK_NULL_HANDLE, "VulkanDescSetPool %s never destroyed", tag_);
}

void VulkanDescSetPool::Create(VulkanContext *vulkan, const VkDescriptorPoolCreateInfo &info, const std::vector<VkDescriptorPoolSize> &sizes) {
	_assert_msg_(descPool_ == VK_NULL_HANDLE, "VulkanDescSetPool::Create when already exists");

	vulkan_ = vulkan;
	info_ = info;
	sizes_ = sizes;

	VkResult res = Recreate(false);
	_assert_msg_(res == VK_SUCCESS, "Could not create VulkanDescSetPool %s", tag_);
}

VkDescriptorSet VulkanDescSetPool::Allocate(int n, const VkDescriptorSetLayout *layouts, const char *tag) {
	if (descPool_ == VK_NULL_HANDLE || usage_ + n >= info_.maxSets) {
		// Missing or out of space, need to recreate.
		VkResult res = Recreate(grow_);
		_assert_msg_(res == VK_SUCCESS, "Could not grow VulkanDescSetPool %s on usage %d", tag_, (int)usage_);
	}

	VkDescriptorSet desc;
	VkDescriptorSetAllocateInfo descAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	descAlloc.descriptorPool = descPool_;
	descAlloc.descriptorSetCount = n;
	descAlloc.pSetLayouts = layouts;
	VkResult result = vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, &desc);

	if (result == VK_ERROR_FRAGMENTED_POOL || result < 0) {
		// There seems to have been a spec revision. Here we should apparently recreate the descriptor pool,
		// so let's do that. See https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkAllocateDescriptorSets.html
		// Fragmentation shouldn't really happen though since we wipe the pool every frame.
		VkResult res = Recreate(false);
		_assert_msg_(res == VK_SUCCESS, "Ran out of descriptor space (frag?) and failed to recreate a descriptor pool. sz=%d res=%d", usage_, (int)res);

		// Need to update this pointer since we have allocated a new one.
		descAlloc.descriptorPool = descPool_;
		result = vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, &desc);
		_assert_msg_(result == VK_SUCCESS, "Ran out of descriptor space (frag?) and failed to allocate after recreating a descriptor pool. res=%d", (int)result);
	}

	if (result != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}

	vulkan_->SetDebugName(desc, VK_OBJECT_TYPE_DESCRIPTOR_SET, tag);
	return desc;
}

void VulkanDescSetPool::Reset() {
	_assert_msg_(descPool_ != VK_NULL_HANDLE, "VulkanDescSetPool::Reset without valid pool");
	vkResetDescriptorPool(vulkan_->GetDevice(), descPool_, 0);

	clear_();
	usage_ = 0;
}

void VulkanDescSetPool::Destroy() {
	_assert_msg_(vulkan_ != nullptr, "VulkanDescSetPool::Destroy without VulkanContext");

	if (descPool_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteDescriptorPool(descPool_);
		clear_();
		usage_ = 0;
	}
}

VkResult VulkanDescSetPool::Recreate(bool grow) {
	_assert_msg_(vulkan_ != nullptr, "VulkanDescSetPool::Recreate without VulkanContext");

	uint32_t prevSize = info_.maxSets;
	if (grow) {
		info_.maxSets *= 2;
		for (auto &size : sizes_)
			size.descriptorCount *= 2;
	}

	// Delete the pool if it already exists.
	if (descPool_ != VK_NULL_HANDLE) {
		DEBUG_LOG(G3D, "Reallocating %s desc pool from %d to %d", tag_, prevSize, info_.maxSets);
		vulkan_->Delete().QueueDeleteDescriptorPool(descPool_);
		clear_();
		usage_ = 0;
	}

	info_.pPoolSizes = &sizes_[0];
	info_.poolSizeCount = (uint32_t)sizes_.size();

	VkResult result = vkCreateDescriptorPool(vulkan_->GetDevice(), &info_, nullptr, &descPool_);
	if (result == VK_SUCCESS) {
		vulkan_->SetDebugName(descPool_, VK_OBJECT_TYPE_DESCRIPTOR_POOL, tag_);
	}
	return result;
}

VulkanPushPool::VulkanPushPool(VulkanContext *vulkan, const char *name, size_t originalBlockSize, VkBufferUsageFlags usage)
	: vulkan_(vulkan), name_(name), originalBlockSize_(originalBlockSize), usage_(usage) {
	RegisterGPUMemoryManager(this);
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		blocks_.push_back(CreateBlock(originalBlockSize));
		blocks_.back().original = true;
		blocks_.back().frameIndex = i;
	}
}

VulkanPushPool::~VulkanPushPool() {
	UnregisterGPUMemoryManager(this);
	_dbg_assert_(blocks_.empty());
}

void VulkanPushPool::Destroy() {
	for (auto &block : blocks_) {
		block.Destroy(vulkan_);
	}
	blocks_.clear();
}

VulkanPushPool::Block VulkanPushPool::CreateBlock(size_t size) {
	Block block{};
	block.size = size;
	block.frameIndex = -1;

	VkBufferCreateInfo b{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	b.size = size;
	b.usage = usage_;
	b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	VmaAllocationInfo allocInfo{};
	
	VkResult result = vmaCreateBuffer(vulkan_->Allocator(), &b, &allocCreateInfo, &block.buffer, &block.allocation, &allocInfo);
	_assert_(result == VK_SUCCESS);

	result = vmaMapMemory(vulkan_->Allocator(), block.allocation, (void **)(&block.writePtr));
	_assert_(result == VK_SUCCESS);

	_assert_msg_(block.writePtr != nullptr, "VulkanPushPool: Failed to map memory on block of size %d", (int)block.size);
	return block;
}

VulkanPushPool::Block::~Block() {}

void VulkanPushPool::Block::Destroy(VulkanContext *vulkan) {
	vmaUnmapMemory(vulkan->Allocator(), allocation);
	vulkan->Delete().QueueDeleteBufferAllocation(buffer, allocation);
}

void VulkanPushPool::BeginFrame() {
	double now = time_now_d();
	curBlockIndex_ = -1;
	for (auto &block : blocks_) {
		if (block.frameIndex == vulkan_->GetCurFrame()) {
			if (curBlockIndex_ == -1) {
				// Pick a block associated with the current frame to start at.
				// We always start with one block per frame index.
				curBlockIndex_ = block.frameIndex;
				block.lastUsed = now;
			}
			block.used = 0;
			if (!block.original) {
				// Return block to the common pool
				block.frameIndex = -1;
			}
		}
	}

	// Do a single pass of bubblesort to move the bigger buffers earlier in the sequence.
	// Over multiple frames this will quickly converge to the right order.
	for (size_t i = 3; i < blocks_.size() - 1; i++) {
		if (blocks_[i].frameIndex == -1 && blocks_[i + 1].frameIndex == -1 && blocks_[i].size < blocks_[i + 1].size) {
			std::swap(blocks_[i], blocks_[i + 1]);
		}
	}

	// If we have lots of little buffers and the last one hasn't been used in a while, drop it.
	// Still, let's keep around a few big ones (6 - 3).
	if (blocks_.size() > 6 && blocks_.back().lastUsed < now - PUSH_GARBAGE_COLLECTION_DELAY) {
		double start = time_now_d();
		size_t size = blocks_.back().size;
		blocks_.back().Destroy(vulkan_);
		blocks_.pop_back();
		DEBUG_LOG(G3D, "%s: Garbage collected block of size %s in %0.2f ms", name_, NiceSizeFormat(size).c_str(), time_now_d() - start);
	}
}

void VulkanPushPool::NextBlock(VkDeviceSize allocationSize) {
	_dbg_assert_(allocationSize != 0);  // If so, the logic in the caller is wrong, should never need a new block for this case.

	int curFrameIndex = vulkan_->GetCurFrame();
	curBlockIndex_++;
	while (curBlockIndex_ < blocks_.size()) {
		Block &block = blocks_[curBlockIndex_];
		// Grab the first matching block, or unused block (frameIndex == -1).
		if ((block.frameIndex == curFrameIndex || block.frameIndex == -1) && block.size >= allocationSize) {
			_assert_(block.used == 0);
			block.used = allocationSize;
			block.lastUsed = time_now_d();
			block.frameIndex = curFrameIndex;
			_assert_(block.writePtr != nullptr);
			return;
		}
		curBlockIndex_++;
	}

	double start = time_now_d();
	VkDeviceSize newBlockSize = std::max(originalBlockSize_ * 2, (VkDeviceSize)RoundUpToPowerOf2((uint32_t)allocationSize));

	// We're still here and ran off the end of blocks. Create a new one.
	blocks_.push_back(CreateBlock(newBlockSize));
	blocks_.back().frameIndex = curFrameIndex;
	blocks_.back().used = allocationSize;
	blocks_.back().lastUsed = time_now_d();
	// curBlockIndex_ is already set correctly here.
	DEBUG_LOG(G3D, "%s: Created new block of size %s in %0.2f ms", name_, NiceSizeFormat(newBlockSize).c_str(), 1000.0 * (time_now_d() - start));
}

size_t VulkanPushPool::GetUsedThisFrame() const {
	size_t used = 0;
	for (auto &block : blocks_) {
		if (block.frameIndex == vulkan_->GetCurFrame()) {
			used += block.used;
		}
	}
	return used;
}

void VulkanPushPool::GetDebugString(char *buffer, size_t bufSize) const {
	size_t used = 0;
	size_t capacity = 0;
	for (auto &block : blocks_) {
		used += block.used;
		capacity += block.size;
	}

	snprintf(buffer, bufSize, "Pool %s: %s / %s (%d extra blocks)", name_, NiceSizeFormat(used).c_str(), NiceSizeFormat(capacity).c_str(), (int)blocks_.size() - 3);
}
