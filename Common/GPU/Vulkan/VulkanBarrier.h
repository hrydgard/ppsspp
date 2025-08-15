#pragma once

#include <string>
#include <vector>

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/Data/Collections/FastVec.h"
#include "Common/Data/Collections/TinySet.h"

class VulkanContext;
struct VKRImage;

class VulkanBarrierBatch {
public:
	VulkanBarrierBatch() : imageBarriers_(4) {}
	~VulkanBarrierBatch();

	bool empty() const { return imageBarriers_.empty(); }

	// TODO: Replace this with TransitionImage.
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

	void TransitionImage(
		VkImage image, int baseMip, int numMipLevels, int numLayers, VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
		VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
		VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask
	);

	// Automatically determines access and stage masks from layouts.
	// Not universally usable, but works for PPSSPP's use.
	void TransitionColorImageAuto(VkImage image, VkImageLayout *imageLayout, VkImageLayout newImageLayout, int baseMip, int numMipLevels, int numLayers);
	void TransitionDepthStencilImageAuto(VkImage image, VkImageLayout *imageLayout, VkImageLayout newImageLayout, int baseMip, int numMipLevels, int numLayers);

	void TransitionColorImageAuto(VKRImage *image, VkImageLayout newImageLayout);
	void TransitionDepthStencilImageAuto(VKRImage *image, VkImageLayout newImageLayout);

	void Flush(VkCommandBuffer cmd);

private:
	FastVec<VkImageMemoryBarrier> imageBarriers_;
	VkPipelineStageFlags srcStageMask_ = 0;
	VkPipelineStageFlags dstStageMask_ = 0;
	VkDependencyFlags dependencyFlags_ = 0;
};
