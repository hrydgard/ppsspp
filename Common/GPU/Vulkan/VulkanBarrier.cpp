#include "VulkanLoader.h"
#include "VulkanContext.h"
#include "VulkanBarrier.h"

void VulkanBarrier::Flush(VkCommandBuffer cmd) {
	if (!imageBarriers_.empty()) {
		if (imageBarriers_.size() >= 3) {
			NOTICE_LOG(G3D, "barriers: %d", (int)imageBarriers_.size());
		}
		vkCmdPipelineBarrier(cmd, srcStageMask_, dstStageMask_, 0, 0, nullptr, 0, nullptr, (uint32_t)imageBarriers_.size(), imageBarriers_.data());
	}
	imageBarriers_.clear();
	srcStageMask_ = 0;
	dstStageMask_ = 0;
}
