#include "VulkanLoader.h"
#include "VulkanContext.h"
#include "VulkanBarrier.h"

void VulkanBarrier::Flush(VkCommandBuffer cmd) {
	imageBarriers_.clear();
	srcStageMask_ = 0;
	dstStageMask_ = 0;
	vkCmdPipelineBarrier(cmd, srcStageMask_, dstStageMask_, 0, 0, nullptr, 0, nullptr, 1, imageBarriers_.data());
}
