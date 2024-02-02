#pragma once

#include <string>
#include <vector>

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/Data/Collections/FastVec.h"
#include "Common/Data/Collections/TinySet.h"

class VulkanContext;

class VulkanBarrierBatch {
public:
	~VulkanBarrierBatch();

	VkImageMemoryBarrier *Add(VkImage image, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags) {
		srcStageMask_ |= srcStageMask;
		dstStageMask_ |= dstStageMask;
		dependencyFlags_ |= dependencyFlags;
		VkImageMemoryBarrier &barrier = imageBarriers_.push_uninitialized();
		// Initialize good defaults for the usual things.
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.pNext = nullptr;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.image = image;
		return &barrier;
	}

	void Flush(VkCommandBuffer cmd) {
		if (!imageBarriers_.empty()) {
			vkCmdPipelineBarrier(cmd, srcStageMask_, dstStageMask_, dependencyFlags_, 0, nullptr, 0, nullptr, (uint32_t)imageBarriers_.size(), imageBarriers_.data());
			imageBarriers_.clear();
			srcStageMask_ = 0;
			dstStageMask_ = 0;
			dependencyFlags_ = 0;
		}
	}

	bool empty() const { return imageBarriers_.empty(); }

private:
	FastVec<VkImageMemoryBarrier> imageBarriers_;
	VkPipelineStageFlags srcStageMask_ = 0;
	VkPipelineStageFlags dstStageMask_ = 0;
	VkDependencyFlags dependencyFlags_ = 0;
};

// Collects multiple barriers into one, then flushes it.
// Reusable after a flush, in case you want to reuse the allocation made by the vector.
// However, not thread safe in any way!
class VulkanBarrier {
public:
	VulkanBarrier() : imageBarriers_(4) {}

	bool empty() const { return imageBarriers_.empty(); }

	void TransitionImage(
		VkImage image, int baseMip, int numMipLevels, int numLayers, VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
		VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
		VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask
	);

	// Automatically determines access and stage masks from layouts.
	// Not universally usable, but works for PPSSPP's use.
	void TransitionImageAuto(VkImage image, int baseMip, int numMipLevels, int numLayers, VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout, VkImageLayout newImageLayout);

	void Flush(VkCommandBuffer cmd);

private:
	VkPipelineStageFlags srcStageMask_ = 0;
	VkPipelineStageFlags dstStageMask_ = 0;
	FastVec<VkImageMemoryBarrier> imageBarriers_;
	VkDependencyFlags dependencyFlags_ = 0;
};
