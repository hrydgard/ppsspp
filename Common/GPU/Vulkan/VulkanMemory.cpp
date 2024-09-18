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
	_assert_msg_(result == VK_SUCCESS, "VulkanPushPool: Failed to map memory (result = %s)", VulkanResultToString(result));

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
		DEBUG_LOG(Log::G3D, "%s: Garbage collected block of size %s in %0.2f ms", name_, NiceSizeFormat(size).c_str(), time_now_d() - start);
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
	DEBUG_LOG(Log::G3D, "%s: Created new block of size %s in %0.2f ms", name_, NiceSizeFormat(newBlockSize).c_str(), 1000.0 * (time_now_d() - start));
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
