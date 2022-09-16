#include "VulkanFrameData.h"

void FrameData::AcquireNextImage(VulkanContext *vulkan) {
	// Get the index of the next available swapchain image, and a semaphore to block command buffer execution on.
	VkResult res = vkAcquireNextImageKHR(vulkan->GetDevice(), vulkan->GetSwapchain(), UINT64_MAX, acquireSemaphore, (VkFence)VK_NULL_HANDLE, &curSwapchainImage);
	if (res == VK_SUBOPTIMAL_KHR) {
		// Hopefully the resize will happen shortly. Ignore - one frame might look bad or something.
		WARN_LOG(G3D, "VK_SUBOPTIMAL_KHR returned - ignoring");
	} else if (res == VK_ERROR_OUT_OF_DATE_KHR) {
		WARN_LOG(G3D, "VK_ERROR_OUT_OF_DATE_KHR returned - processing the frame, but not presenting");
		skipSwap = true;
	} else {
		_assert_msg_(res == VK_SUCCESS, "vkAcquireNextImageKHR failed! result=%s", VulkanResultToString(res));
	}
}
