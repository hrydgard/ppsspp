#pragma once

#include <string>
#include <vector>

#include "Common/Log.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"

class VulkanContext;

// Collects multiple barriers into one, then flushes it.
// Reusable after a flush, in case you want to reuse the allocation made by the vector.
// However, not thread safe in any way!
class VulkanBarrier {
public:
	void TransitionImage(
		VkImage image, int baseMip, int numMipLevels, VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
		VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
		VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask
	) {
		srcStageMask_ |= srcStageMask;
		dstStageMask_ |= dstStageMask;
		dependencyFlags_ |= VK_DEPENDENCY_BY_REGION_BIT;

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

	// Automatically determines access and stage masks from layouts.
	// Not universally usable, but works for PPSSPP's use.
	void TransitionImageAuto(
		VkImage image, int baseMip, int numMipLevels, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout
	) {
		VkAccessFlags srcAccessMask = 0;
		VkAccessFlags dstAccessMask = 0;
		switch (oldImageLayout) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			// Assert aspect here?
			srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			srcStageMask_ |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			// Assert aspect here?
			srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			srcStageMask_ |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			break;
		case VK_IMAGE_LAYOUT_UNDEFINED:
			// Actually this seems wrong?
			if (aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
				srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				srcStageMask_ |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			}
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStageMask_ |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			srcStageMask_ |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		default:
			_assert_msg_(false, "Unexpected oldLayout: %d", (int)oldImageLayout);
			break;
		}

		switch (newImageLayout) {
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstStageMask_ |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;
		default:
			_assert_msg_(false, "Unexpected newLayout: %d", (int)newImageLayout);
			break;
		}

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
	VkDependencyFlags dependencyFlags_ = 0;
};
