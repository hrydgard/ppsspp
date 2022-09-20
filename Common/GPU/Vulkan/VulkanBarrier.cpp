#include "VulkanLoader.h"
#include "VulkanContext.h"
#include "VulkanBarrier.h"

void VulkanBarrier::Flush(VkCommandBuffer cmd) {
	if (!imageBarriers_.empty()) {
		vkCmdPipelineBarrier(cmd, srcStageMask_, dstStageMask_, dependencyFlags_, 0, nullptr, 0, nullptr, (uint32_t)imageBarriers_.size(), imageBarriers_.data());
	}
	imageBarriers_.clear();
	srcStageMask_ = 0;
	dstStageMask_ = 0;
}
