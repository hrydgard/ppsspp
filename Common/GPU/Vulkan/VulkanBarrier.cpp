#include "VulkanLoader.h"
#include "VulkanContext.h"
#include "VulkanBarrier.h"

#include "Common/Log.h"

VulkanBarrierBatch::~VulkanBarrierBatch() {
	// _dbg_assert_(imageBarriers_.empty());
	if (!imageBarriers_.empty()) {
		ERROR_LOG(G3D, "~VulkanBarrierBatch: %d barriers remaining", (int)imageBarriers_.size());
	}
}

void VulkanBarrierBatch::Flush(VkCommandBuffer cmd) {
	if (!imageBarriers_.empty()) {
		vkCmdPipelineBarrier(cmd, srcStageMask_, dstStageMask_, dependencyFlags_, 0, nullptr, 0, nullptr, (uint32_t)imageBarriers_.size(), imageBarriers_.data());
	}
	imageBarriers_.clear();
	srcStageMask_ = 0;
	dstStageMask_ = 0;
	dependencyFlags_ = 0;
}

void VulkanBarrierBatch::TransitionImage(
	VkImage image, int baseMip, int numMipLevels, int numLayers, VkImageAspectFlags aspectMask,
	VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
	VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
	VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask
) {
	_dbg_assert_(image != VK_NULL_HANDLE);

	srcStageMask_ |= srcStageMask;
	dstStageMask_ |= dstStageMask;
	dependencyFlags_ |= VK_DEPENDENCY_BY_REGION_BIT;

	VkImageMemoryBarrier &imageBarrier = imageBarriers_.push_uninitialized();
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
	imageBarrier.subresourceRange.layerCount = numLayers;  // NOTE: We could usually use VK_REMAINING_ARRAY_LAYERS/VK_REMAINING_MIP_LEVELS, but really old Mali drivers have problems with those.
	imageBarrier.subresourceRange.baseArrayLayer = 0;
	imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
}

void VulkanBarrierBatch::TransitionImageAuto(
	VkImage image, int baseMip, int numMipLevels, int numLayers, VkImageAspectFlags aspectMask,
	VkImageLayout oldImageLayout, VkImageLayout newImageLayout) {
	_dbg_assert_(image != VK_NULL_HANDLE);

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

	VkImageMemoryBarrier &imageBarrier = imageBarriers_.push_uninitialized();
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
	imageBarrier.subresourceRange.layerCount = numLayers;  // NOTE: We could usually use VK_REMAINING_ARRAY_LAYERS/VK_REMAINING_MIP_LEVELS, but really old Mali drivers have problems with those.
	imageBarrier.subresourceRange.baseArrayLayer = 0;
	imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
}

void TransitionImageLayout2(VkCommandBuffer cmd, VkImage image, int baseMip, int numMipLevels, int numLayers, VkImageAspectFlags aspectMask,
	VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
	VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
	VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask) {
	VkImageMemoryBarrier image_memory_barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	image_memory_barrier.srcAccessMask = srcAccessMask;
	image_memory_barrier.dstAccessMask = dstAccessMask;
	image_memory_barrier.oldLayout = oldImageLayout;
	image_memory_barrier.newLayout = newImageLayout;
	image_memory_barrier.image = image;
	image_memory_barrier.subresourceRange.aspectMask = aspectMask;
	image_memory_barrier.subresourceRange.baseMipLevel = baseMip;
	image_memory_barrier.subresourceRange.levelCount = numMipLevels;
	image_memory_barrier.subresourceRange.layerCount = numLayers;  // We never use more than one layer, and old Mali drivers have problems with VK_REMAINING_ARRAY_LAYERS/VK_REMAINING_MIP_LEVELS.
	image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	vkCmdPipelineBarrier(cmd, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
}
