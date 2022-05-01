#pragma once

#include <string>
#include <vector>

#include "VulkanLoader.h"

class VulkanContext;

// Collects multiple barriers into one, then flushes it.
// Reusable after a flush, in case you want to reuse the allocation made by the vector.
// However, not thread safe in any way!
class VulkanBarrier {
public:
	void TransitionImage(
		VkImage image, int baseMip, int numMipLevels, VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
		VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask) {

		srcStageMask_ |= srcStageMask;
		dstStageMask_ |= dstStageMask;

		VkImageMemoryBarrier imageBarrier;
		imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrier.pNext = nullptr;
		imageBarrier.srcAccessMask = srcAccessMask;
		imageBarrier.dstAccessMask = dstAccessMask;
		imageBarrier.oldLayout = oldImageLayout;
		imageBarrier.newLayout = newImageLayout;
		imageBarrier.image = image;
		imageBarrier.subresourceRange.aspectMask = aspectMask;
		imageBarrier.subresourceRange.baseMipLevel = baseMip;
		imageBarrier.subresourceRange.levelCount = numMipLevels;
		imageBarrier.subresourceRange.layerCount = 1;  // We never use more than one layer, and old Mali drivers have problems with VK_REMAINING_ARRAY_LAYERS/VK_REMAINING_MIP_LEVELS.
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarriers_.push_back(imageBarrier);
	}

	void Flush(VkCommandBuffer cmd);

private:
	VkPipelineStageFlags srcStageMask_ = 0;
	VkPipelineStageFlags dstStageMask_ = 0;
	std::vector<VkImageMemoryBarrier> imageBarriers_;
};
