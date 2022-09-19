#include "VulkanFrameData.h"
#include "Common/Log.h"

void FrameData::Init(VulkanContext *vulkan, int index) {
	this->index = index;
	VkDevice device = vulkan->GetDevice();

	VkCommandPoolCreateInfo cmd_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cmd_pool_info.queueFamilyIndex = vulkan->GetGraphicsQueueFamilyIndex();
	cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	VkResult res = vkCreateCommandPool(device, &cmd_pool_info, nullptr, &cmdPoolInit);
	_dbg_assert_(res == VK_SUCCESS);
	res = vkCreateCommandPool(device, &cmd_pool_info, nullptr, &cmdPoolMain);
	_dbg_assert_(res == VK_SUCCESS);

	VkCommandBufferAllocateInfo cmd_alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmd_alloc.commandPool = cmdPoolInit;
	cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc.commandBufferCount = 1;
	res = vkAllocateCommandBuffers(device, &cmd_alloc, &initCmd);
	_dbg_assert_(res == VK_SUCCESS);
	cmd_alloc.commandPool = cmdPoolMain;
	res = vkAllocateCommandBuffers(device, &cmd_alloc, &mainCmd);
	res = vkAllocateCommandBuffers(device, &cmd_alloc, &presentCmd);
	_dbg_assert_(res == VK_SUCCESS);

	// Creating the frame fence with true so they can be instantly waited on the first frame
	fence = vulkan->CreateFence(true);

	// This fence one is used for synchronizing readbacks. Does not need preinitialization.
	readbackFence = vulkan->CreateFence(false);

	VkQueryPoolCreateInfo query_ci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	query_ci.queryCount = MAX_TIMESTAMP_QUERIES;
	query_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
	res = vkCreateQueryPool(device, &query_ci, nullptr, &profile.queryPool);
}

void FrameData::Destroy(VulkanContext *vulkan) {
	VkDevice device = vulkan->GetDevice();
	// TODO: I don't think free-ing command buffers is necessary before destroying a pool.
	vkFreeCommandBuffers(device, cmdPoolInit, 1, &initCmd);
	vkFreeCommandBuffers(device, cmdPoolMain, 1, &mainCmd);
	vkDestroyCommandPool(device, cmdPoolInit, nullptr);
	vkDestroyCommandPool(device, cmdPoolMain, nullptr);
	vkDestroyFence(device, fence, nullptr);
	vkDestroyFence(device, readbackFence, nullptr);
	vkDestroyQueryPool(device, profile.queryPool, nullptr);
}

void FrameData::AcquireNextImage(VulkanContext *vulkan, FrameDataShared &shared) {
	// Get the index of the next available swapchain image, and a semaphore to block command buffer execution on.
	VkResult res = vkAcquireNextImageKHR(vulkan->GetDevice(), vulkan->GetSwapchain(), UINT64_MAX, shared.acquireSemaphore, (VkFence)VK_NULL_HANDLE, &curSwapchainImage);
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

void FrameData::SubmitInitCommands(VulkanContext *vulkan) {
	if (!hasInitCommands) {
		return;
	}

	if (profilingEnabled_) {
		// Pre-allocated query ID 1.
		vkCmdWriteTimestamp(initCmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, profile.queryPool, 1);
	}

	VkResult res = vkEndCommandBuffer(initCmd);
	_assert_msg_(res == VK_SUCCESS, "vkEndCommandBuffer failed (init)! result=%s", VulkanResultToString(res));

	VkCommandBuffer cmdBufs[1];
	int numCmdBufs = 0;

	cmdBufs[numCmdBufs++] = initCmd;
	// Send the init commands off separately, so they can be processed while we're building the rest of the list.
	// (Likely the CPU will be more than a frame ahead anyway, but this will help when we try to work on latency).
	VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit_info.commandBufferCount = (uint32_t)numCmdBufs;
	submit_info.pCommandBuffers = cmdBufs;
	res = vkQueueSubmit(vulkan->GetGraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
	if (res == VK_ERROR_DEVICE_LOST) {
		_assert_msg_(false, "Lost the Vulkan device in split submit! If this happens again, switch Graphics Backend away from Vulkan");
	} else {
		_assert_msg_(res == VK_SUCCESS, "vkQueueSubmit failed (init)! result=%s", VulkanResultToString(res));
	}
	numCmdBufs = 0;

	hasInitCommands = false;
}

void FrameData::SubmitMainFinal(VulkanContext *vulkan, bool triggerFrameFence, FrameDataShared &sharedData) {
	VkCommandBuffer cmdBufs[2];
	int numCmdBufs = 0;

	cmdBufs[numCmdBufs++] = mainCmd;
	if (hasPresentCommands) {
		cmdBufs[numCmdBufs++] = presentCmd;
	}

	VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	VkPipelineStageFlags waitStage[1]{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	if (triggerFrameFence && !skipSwap) {
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &sharedData.acquireSemaphore;
		submit_info.pWaitDstStageMask = waitStage;
	}
	submit_info.commandBufferCount = (uint32_t)numCmdBufs;
	submit_info.pCommandBuffers = cmdBufs;
	if (triggerFrameFence && !skipSwap) {
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &sharedData.renderingCompleteSemaphore;
	}
	VkResult res = vkQueueSubmit(vulkan->GetGraphicsQueue(), 1, &submit_info, triggerFrameFence ? fence : readbackFence);
	if (res == VK_ERROR_DEVICE_LOST) {
		_assert_msg_(false, "Lost the Vulkan device in vkQueueSubmit! If this happens again, switch Graphics Backend away from Vulkan");
	} else {
		_assert_msg_(res == VK_SUCCESS, "vkQueueSubmit failed (main)! result=%s", VulkanResultToString(res));
	}

	// When !triggerFence, we notify after syncing with Vulkan.
	if (triggerFrameFence) {
		VERBOSE_LOG(G3D, "PULL: Frame %d.readyForFence = true", index);
		std::unique_lock<std::mutex> lock(push_mutex);
		readyForFence = true;
		push_condVar.notify_all();
	}

	hasInitCommands = false;
	hasPresentCommands = false;
}

void FrameDataShared::Init(VulkanContext *vulkan) {
	VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	semaphoreCreateInfo.flags = 0;
	VkResult res = vkCreateSemaphore(vulkan->GetDevice(), &semaphoreCreateInfo, nullptr, &acquireSemaphore);
	_dbg_assert_(res == VK_SUCCESS);
	res = vkCreateSemaphore(vulkan->GetDevice(), &semaphoreCreateInfo, nullptr, &renderingCompleteSemaphore);
	_dbg_assert_(res == VK_SUCCESS);
}

void FrameDataShared::Destroy(VulkanContext *vulkan) {
	VkDevice device = vulkan->GetDevice();
	vkDestroySemaphore(device, acquireSemaphore, nullptr);
	vkDestroySemaphore(device, renderingCompleteSemaphore, nullptr);
}
