#pragma once

#include <string>
#include <vector>

#include "Common/Log.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/Data/Collections/FastVec.h"

class VulkanContext;

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
