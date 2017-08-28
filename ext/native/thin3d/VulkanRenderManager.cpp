#include "base/logging.h"

#include "Common/Vulkan/VulkanContext.h"
#include "thin3d/VulkanRenderManager.h"
#include "thread/threadutil.h"

// TODO: Using a thread here is unfinished and does not work correctly.
const bool useThread = false;

void CreateImage(VulkanContext *vulkan, VkCommandBuffer cmd, VKRImage &img, int width, int height, VkFormat format, VkImageLayout initialLayout, bool color) {
	VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.arrayLayers = 1;
	ici.mipLevels = 1;
	ici.extent.width = width;
	ici.extent.height = height;
	ici.extent.depth = 1;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.format = format;
	ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (color) {
		ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	} else {
		ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	vkCreateImage(vulkan->GetDevice(), &ici, nullptr, &img.image);

	// TODO: If available, use nVidia's VK_NV_dedicated_allocation for framebuffers

	VkMemoryRequirements memreq;
	vkGetImageMemoryRequirements(vulkan->GetDevice(), img.image, &memreq);

	VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = memreq.size;
	vulkan->MemoryTypeFromProperties(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &alloc.memoryTypeIndex);
	VkResult res = vkAllocateMemory(vulkan->GetDevice(), &alloc, nullptr, &img.memory);
	assert(res == VK_SUCCESS);
	res = vkBindImageMemory(vulkan->GetDevice(), img.image, img.memory, 0);
	assert(res == VK_SUCCESS);

	VkImageAspectFlags aspects = color ? VK_IMAGE_ASPECT_COLOR_BIT : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

	VkImageViewCreateInfo ivci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	ivci.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	ivci.format = ici.format;
	ivci.image = img.image;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.subresourceRange.aspectMask = aspects;
	ivci.subresourceRange.layerCount = 1;
	ivci.subresourceRange.levelCount = 1;
	res = vkCreateImageView(vulkan->GetDevice(), &ivci, nullptr, &img.imageView);
	assert(res == VK_SUCCESS);

	VkPipelineStageFlagBits dstStage;
	VkAccessFlagBits dstAccessMask;
	switch (initialLayout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		break;
	}

	TransitionImageLayout2(cmd, img.image, aspects,
		VK_IMAGE_LAYOUT_UNDEFINED, initialLayout,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dstStage,
		0, dstAccessMask);
	img.layout = initialLayout;
}

VulkanRenderManager::VulkanRenderManager(VulkanContext *vulkan) : vulkan_(vulkan) {
	VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	semaphoreCreateInfo.flags = 0;
	VkResult res = vkCreateSemaphore(vulkan_->GetDevice(), &semaphoreCreateInfo, NULL, &acquireSemaphore_);
	assert(res == VK_SUCCESS);
	res = vkCreateSemaphore(vulkan_->GetDevice(), &semaphoreCreateInfo, NULL, &renderingCompleteSemaphore_);
	assert(res == VK_SUCCESS);

	for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
		VkCommandPoolCreateInfo cmd_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		cmd_pool_info.queueFamilyIndex = vulkan_->GetGraphicsQueueFamilyIndex();
		cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VkResult res = vkCreateCommandPool(vulkan_->GetDevice(), &cmd_pool_info, nullptr, &frameData_[i].cmdPoolInit);
		assert(res == VK_SUCCESS);
		res = vkCreateCommandPool(vulkan_->GetDevice(), &cmd_pool_info, nullptr, &frameData_[i].cmdPoolMain);
		assert(res == VK_SUCCESS);

		VkCommandBufferAllocateInfo cmd_alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cmd_alloc.commandPool = frameData_[i].cmdPoolInit;
		cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_alloc.commandBufferCount = 1;

		res = vkAllocateCommandBuffers(vulkan_->GetDevice(), &cmd_alloc, &frameData_[i].initCmd);
		assert(res == VK_SUCCESS);
		cmd_alloc.commandPool = frameData_[i].cmdPoolMain;
		res = vkAllocateCommandBuffers(vulkan_->GetDevice(), &cmd_alloc, &frameData_[i].mainCmd);
		assert(res == VK_SUCCESS);
		frameData_[i].fence = vulkan_->CreateFence(true);  // So it can be instantly waited on
	}
}

void VulkanRenderManager::CreateBackbuffers() {
	VkResult res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &swapchainImageCount_, nullptr);
	assert(res == VK_SUCCESS);

	VkImage* swapchainImages = new VkImage[swapchainImageCount_];
	res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &swapchainImageCount_, swapchainImages);
	assert(res == VK_SUCCESS);

	VkCommandBuffer cmdInit = GetInitCmd();

	for (uint32_t i = 0; i < swapchainImageCount_; i++) {
		SwapchainImageData sc_buffer;
		sc_buffer.image = swapchainImages[i];

		VkImageViewCreateInfo color_image_view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		color_image_view.format = vulkan_->GetSwapchainFormat();
		color_image_view.components.r = VK_COMPONENT_SWIZZLE_R;
		color_image_view.components.g = VK_COMPONENT_SWIZZLE_G;
		color_image_view.components.b = VK_COMPONENT_SWIZZLE_B;
		color_image_view.components.a = VK_COMPONENT_SWIZZLE_A;
		color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		color_image_view.subresourceRange.baseMipLevel = 0;
		color_image_view.subresourceRange.levelCount = 1;
		color_image_view.subresourceRange.baseArrayLayer = 0;
		color_image_view.subresourceRange.layerCount = 1;
		color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		color_image_view.flags = 0;
		color_image_view.image = sc_buffer.image;

		// Pre-set them to PRESENT_SRC_KHR, as the first thing we do after acquiring
		// in image to render to will be to transition them away from that.
		TransitionImageLayout2(cmdInit, sc_buffer.image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		res = vkCreateImageView(vulkan_->GetDevice(), &color_image_view, NULL, &sc_buffer.view);
		swapchainImages_.push_back(sc_buffer);
		assert(res == VK_SUCCESS);
	}
	delete[] swapchainImages;

	InitDepthStencilBuffer(cmdInit);  // Must be before InitBackbufferRenderPass.
	InitBackbufferRenderPass();  // Must be before InitFramebuffers.
	InitBackbufferFramebuffers(vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	InitRenderpasses();
	curWidth_ = -1;
	curHeight_ = -1;

	// Start the thread.
	if (useThread) {
		run_ = true;
		thread_ = std::thread(&VulkanRenderManager::ThreadFunc, this);
	}
}

void VulkanRenderManager::DestroyBackbuffers() {
	if (useThread) {
		run_ = false;
		// Stop the thread.
		for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
			{
				std::unique_lock<std::mutex> lock(frameData_[i].push_mutex);
				frameData_[i].push_condVar.notify_all();
			}
			{
				std::unique_lock<std::mutex> lock(frameData_[i].pull_mutex);
				frameData_[i].pull_condVar.notify_all();
			}
		}
		thread_.join();
	}
	VkDevice device = vulkan_->GetDevice();
	for (uint32_t i = 0; i < swapchainImageCount_; i++) {
		vulkan_->Delete().QueueDeleteImageView(swapchainImages_[i].view);
	}
	vulkan_->Delete().QueueDeleteImageView(depth_.view);
	vulkan_->Delete().QueueDeleteImage(depth_.image);
	vulkan_->Delete().QueueDeleteDeviceMemory(depth_.mem);
	swapchainImages_.clear();
}

VulkanRenderManager::~VulkanRenderManager() {
	run_ = false;
	VkDevice device = vulkan_->GetDevice();
	vulkan_->WaitUntilQueueIdle();
	vkDestroySemaphore(device, acquireSemaphore_, nullptr);
	vkDestroySemaphore(device, renderingCompleteSemaphore_, nullptr);
	for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
		VkCommandBuffer cmdBuf[2]{ frameData_[i].mainCmd, frameData_[i].initCmd };
		vkFreeCommandBuffers(device, frameData_[i].cmdPoolInit, 1, &frameData_[i].initCmd);
		vkFreeCommandBuffers(device, frameData_[i].cmdPoolMain, 1, &frameData_[i].mainCmd);
		vkDestroyFence(device, frameData_[i].fence, nullptr);
	}
	if (backbufferRenderPass_ != VK_NULL_HANDLE)
		vkDestroyRenderPass(device, backbufferRenderPass_, nullptr);
	for (uint32_t i = 0; i < framebuffers_.size(); i++) {
		vkDestroyFramebuffer(device, framebuffers_[i], nullptr);
	}
	framebuffers_.clear();
	for (int i = 0; i < ARRAY_SIZE(renderPasses_); i++) {
		vkDestroyRenderPass(device, renderPasses_[i], nullptr);
	}
}

// TODO: Activate this code.
void VulkanRenderManager::ThreadFunc() {
	setCurrentThreadName("RenderMan");
	int threadFrame = -1;  // Increment first, start at 0.
	while (run_) {
		{
			threadFrame++;
			if (threadFrame >= vulkan_->GetInflightFrames())
				threadFrame = 0;
			FrameData &frameData = frameData_[threadFrame];
			std::unique_lock<std::mutex> lock(frameData.pull_mutex);
			while (!frameData.readyForRun && run_) {
				ILOG("PULL: Waiting for frame[%d].readyForRun", threadFrame);
				frameData.pull_condVar.wait(lock);
			}
			ILOG("PULL: frame[%d].readyForRun = false", threadFrame);
			frameData.readyForRun = false;
			if (!run_)  // quick exit if bailing.
				return;
		}
		ILOG("PULL: Running frame %d", threadFrame);
		Run(threadFrame);
	}
}

void VulkanRenderManager::BeginFrame() {
	VkDevice device = vulkan_->GetDevice();

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];

	// Make sure the very last command buffer from the frame before the previous has been fully executed.
	if (useThread) {
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		while (!frameData.readyForFence) {
			ILOG("PUSH: Waiting for frame[%d].readyForFence = 1", curFrame);
			frameData.push_condVar.wait(lock);
		}
		frameData.readyForFence = false;
	}

	ILOG("PUSH: Fencing %d", curFrame);
	vkWaitForFences(device, 1, &frameData.fence, true, UINT64_MAX);
	vkResetFences(device, 1, &frameData.fence);

	// Must be after the fence - this performs deletes.
	ILOG("PUSH: BeginFrame %d", curFrame);
	vulkan_->BeginFrame();

	insideFrame_ = true;
}

VkCommandBuffer VulkanRenderManager::GetInitCmd() {
	// assert(insideFrame_ || firstFrame_);

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	if (!frameData.hasInitCommands) {
		VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begin.pInheritanceInfo = nullptr;
		VkResult res = vkBeginCommandBuffer(frameData.initCmd, &begin);
		assert(res == VK_SUCCESS);
		frameData.hasInitCommands = true;
	}
	return frameData_[curFrame].initCmd;
}

void VulkanRenderManager::Sync() {

}

void VulkanRenderManager::BindFramebufferAsRenderTarget(VKRFramebuffer *fb, VKRRenderPassAction color, VKRRenderPassAction depth, uint32_t clearColor, float clearDepth, uint8_t clearStencil) {
	// Eliminate dupes.
	if (steps_.size() && steps_.back()->stepType == VKRStepType::RENDER && steps_.back()->render.framebuffer == fb) {
		if (color != VKRRenderPassAction::CLEAR && depth != VKRRenderPassAction::CLEAR) {
			// We don't move to a new step, this bind was unnecessary.
			return;
		}
	}

	VKRStep *step = new VKRStep{ VKRStepType::RENDER };
	// This is what queues up new passes, and can end previous ones.
	step->render.framebuffer = fb;
	step->render.color = color;
	step->render.depthStencil = depth;
	step->render.clearColor = clearColor;
	step->render.clearDepth = clearDepth;
	step->render.clearStencil = clearStencil;
	step->render.numDraws = 0;
	step->render.finalColorLayout = !fb ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	steps_.push_back(step);

	curRenderStep_ = step;
	curWidth_ = fb ? fb->width : vulkan_->GetBackbufferWidth();
	curHeight_ = fb ? fb->height : vulkan_->GetBackbufferHeight();
}

void VulkanRenderManager::InitBackbufferFramebuffers(int width, int height) {
	VkResult U_ASSERT_ONLY res;
	// We share the same depth buffer but have multiple color buffers, see the loop below.
	VkImageView attachments[2] = { VK_NULL_HANDLE, depth_.view };

	ILOG("InitFramebuffers: %dx%d", width, height);
	VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb_info.renderPass = backbufferRenderPass_;
	fb_info.attachmentCount = 2;
	fb_info.pAttachments = attachments;
	fb_info.width = width;
	fb_info.height = height;
	fb_info.layers = 1;

	framebuffers_.resize(swapchainImageCount_);

	for (uint32_t i = 0; i < swapchainImageCount_; i++) {
		attachments[0] = swapchainImages_[i].view;
		res = vkCreateFramebuffer(vulkan_->GetDevice(), &fb_info, nullptr, &framebuffers_[i]);
		assert(res == VK_SUCCESS);
	}
}

void VulkanRenderManager::InitBackbufferRenderPass() {
	VkResult U_ASSERT_ONLY res;

	VkAttachmentDescription attachments[2];
	attachments[0].format = vulkan_->GetSwapchainFormat();
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].flags = 0;

	assert(depth_.format != VK_FORMAT_UNDEFINED);
	attachments[1].format = depth_.format;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].flags = 0;

	VkAttachmentReference color_reference = {};
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_reference{};
	depth_reference.attachment = 1;
	depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = nullptr;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;
	subpass.pResolveAttachments = nullptr;
	subpass.pDepthStencilAttachment = &depth_reference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = nullptr;

	VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp_info.pNext = nullptr;
	rp_info.attachmentCount = 2;
	rp_info.pAttachments = attachments;
	rp_info.subpassCount = 1;
	rp_info.pSubpasses = &subpass;
	rp_info.dependencyCount = 0;
	rp_info.pDependencies = nullptr;

	res = vkCreateRenderPass(vulkan_->GetDevice(), &rp_info, NULL, &backbufferRenderPass_);
	assert(res == VK_SUCCESS);
}

void VulkanRenderManager::InitDepthStencilBuffer(VkCommandBuffer cmd) {
	VkResult U_ASSERT_ONLY res;
	bool U_ASSERT_ONLY pass;

	const VkFormat depth_format = vulkan_->GetDeviceInfo().preferredDepthStencilFormat;
	int aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = depth_format;
	image_info.extent.width = vulkan_->GetBackbufferWidth();
	image_info.extent.height = vulkan_->GetBackbufferHeight();
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.queueFamilyIndexCount = 0;
	image_info.pQueueFamilyIndices = nullptr;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	image_info.flags = 0;

	VkMemoryAllocateInfo mem_alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	mem_alloc.allocationSize = 0;
	mem_alloc.memoryTypeIndex = 0;

	VkMemoryRequirements mem_reqs;

	depth_.format = depth_format;

	VkDevice device = vulkan_->GetDevice();
	res = vkCreateImage(device, &image_info, NULL, &depth_.image);
	assert(res == VK_SUCCESS);

	vkGetImageMemoryRequirements(device, depth_.image, &mem_reqs);

	mem_alloc.allocationSize = mem_reqs.size;
	// Use the memory properties to determine the type of memory required
	pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits,
		0, /* No requirements */
		&mem_alloc.memoryTypeIndex);
	assert(pass);

	res = vkAllocateMemory(device, &mem_alloc, NULL, &depth_.mem);
	assert(res == VK_SUCCESS);

	res = vkBindImageMemory(device, depth_.image, depth_.mem, 0);
	assert(res == VK_SUCCESS);

	TransitionImageLayout2(cmd, depth_.image,
		aspectMask,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

	VkImageViewCreateInfo depth_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	depth_view_info.image = depth_.image;
	depth_view_info.format = depth_format;
	depth_view_info.components.r = VK_COMPONENT_SWIZZLE_R;
	depth_view_info.components.g = VK_COMPONENT_SWIZZLE_G;
	depth_view_info.components.b = VK_COMPONENT_SWIZZLE_B;
	depth_view_info.components.a = VK_COMPONENT_SWIZZLE_A;
	depth_view_info.subresourceRange.aspectMask = aspectMask;
	depth_view_info.subresourceRange.baseMipLevel = 0;
	depth_view_info.subresourceRange.levelCount = 1;
	depth_view_info.subresourceRange.baseArrayLayer = 0;
	depth_view_info.subresourceRange.layerCount = 1;
	depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depth_view_info.flags = 0;

	res = vkCreateImageView(device, &depth_view_info, NULL, &depth_.view);
	assert(res == VK_SUCCESS);
}

void VulkanRenderManager::InitRenderpasses() {
	// Create a bunch of render pass objects, for normal rendering with a depth buffer,
	// with clearing, without clearing, and dont-care for both depth/stencil and color, so 3*3=9 combos.
	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].flags = 0;

	attachments[1].format = vulkan_->GetDeviceInfo().preferredDepthStencilFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].flags = 0;

	VkAttachmentReference color_reference = {};
	color_reference.attachment = 0;
	color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_reference = {};
	depth_reference.attachment = 1;
	depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = nullptr;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;
	subpass.pResolveAttachments = nullptr;
	subpass.pDepthStencilAttachment = &depth_reference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = nullptr;

	VkRenderPassCreateInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp.attachmentCount = 2;
	rp.pAttachments = attachments;
	rp.subpassCount = 1;
	rp.pSubpasses = &subpass;
	rp.dependencyCount = 0;
	rp.pDependencies = nullptr;

	for (int depth = 0; depth < 3; depth++) {
		switch ((VKRRenderPassAction)depth) {
		case VKRRenderPassAction::CLEAR:
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			break;
		case VKRRenderPassAction::KEEP:
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			break;
		case VKRRenderPassAction::DONT_CARE:
			attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			break;
		}
		for (int color = 0; color < 3; color++) {
			switch ((VKRRenderPassAction)color) {
			case VKRRenderPassAction::CLEAR: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; break;
			case VKRRenderPassAction::KEEP: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; break;
			case VKRRenderPassAction::DONT_CARE: attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; break;
			}
			vkCreateRenderPass(vulkan_->GetDevice(), &rp, nullptr, &renderPasses_[RPIndex((VKRRenderPassAction)color, (VKRRenderPassAction)depth)]);
		}
	}
}

void VulkanRenderManager::Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask) {
	_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
	// If this is the first drawing command, merge it into the pass.
	if (curRenderStep_->render.numDraws == 0) {
		curRenderStep_->render.clearColor = clearColor;
		curRenderStep_->render.clearDepth = clearZ;
		curRenderStep_->render.clearStencil = clearStencil;
		curRenderStep_->render.color = (clearMask & VK_IMAGE_ASPECT_COLOR_BIT) ? VKRRenderPassAction::CLEAR : VKRRenderPassAction::KEEP;
		curRenderStep_->render.depthStencil = (clearMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) ? VKRRenderPassAction::CLEAR : VKRRenderPassAction::KEEP;
	} else {
		VkRenderData data{ VKRRenderCommand::CLEAR };
		data.clear.clearColor = clearColor;
		data.clear.clearZ = clearZ;
		data.clear.clearStencil = clearStencil;
		data.clear.clearMask = clearMask;
		curRenderStep_->commands.push_back(data);
	}
}

void VulkanRenderManager::CopyFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkOffset2D dstPos, int aspectMask) {
	VKRStep *step = new VKRStep{ VKRStepType::COPY };
	step->copy.aspectMask = aspectMask;
	step->copy.src = src;
	step->copy.srcRect = srcRect;
	step->copy.dst = dst;
	step->copy.dstPos = dstPos;
	std::unique_lock<std::mutex> lock(mutex_);
	steps_.push_back(step);
	curRenderStep_ = nullptr;
}

void VulkanRenderManager::BlitFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkRect2D dstRect, int aspectMask, VkFilter filter) {
	VKRStep *step = new VKRStep{ VKRStepType::BLIT };
	step->blit.aspectMask = aspectMask;
	step->blit.src = src;
	step->blit.srcRect = srcRect;
	step->blit.dst = dst;
	step->blit.dstRect = dstRect;
	step->blit.filter = filter;
	std::unique_lock<std::mutex> lock(mutex_);
	steps_.push_back(step);
	curRenderStep_ = nullptr;
}

VkImageView VulkanRenderManager::BindFramebufferAsTexture(VKRFramebuffer *fb, int binding, int aspectBit, int attachment) {
	// Should just mark the dependency and return the image.
	for (int i = (int)steps_.size() - 1; i >= 0; i--) {
		if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == fb) {
			// If this framebuffer was rendered to earlier in this frame, make sure to pre-transition it to the correct layout.
			if (steps_[i]->render.finalColorLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
				steps_[i]->render.finalColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				break;
			}
			else {
				// May need to shadow the framebuffer if we re-order passes later.
			}
		}
	}

	curRenderStep_->preTransitions.push_back({ fb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	return fb->color.imageView;
}

void VulkanRenderManager::Flush() {
	curRenderStep_ = nullptr;
	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	if (frameData.hasInitCommands) {
		vkEndCommandBuffer(frameData.initCmd);
	}
	if (!useThread) {
		frameData.steps = std::move(steps_);
		Run(curFrame);
	} else {
		std::unique_lock<std::mutex> lock(frameData.pull_mutex);
		ILOG("PUSH: Frame[%d].readyForRun = true", curFrame);
		frameData.steps = std::move(steps_);
		frameData.readyForRun = true;
		frameData.pull_condVar.notify_all();
	}
	vulkan_->EndFrame();
}

void VulkanRenderManager::Run(int frame) {
	FrameData &frameData = frameData_[frame];
	auto &stepsOnThread_ = frameData_[frame].steps;
	VkDevice device = vulkan_->GetDevice();

	uint32_t curSwapchainImage = 0;
	// Get the index of the next available swapchain image, and a semaphore to block command buffer execution on.
	// Now, I wonder if we should do this early in the frame or late? Right now we do it early, which should be fine.
	VkResult res = vkAcquireNextImageKHR(device, vulkan_->GetSwapchain(), UINT64_MAX, acquireSemaphore_, (VkFence)VK_NULL_HANDLE, &curSwapchainImage);
	assert(res == VK_SUCCESS);

	VkCommandBuffer cmd = frameData.mainCmd;

	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin.pInheritanceInfo = nullptr;
	res = vkBeginCommandBuffer(cmd, &begin);
	assert(res == VK_SUCCESS);

	// TODO: Deal with the VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR
	// return codes
	// TODO: Is it best to do this here, or combine with some other transition, or just do it right before the backbuffer bind-for-render?
	assert(res == VK_SUCCESS);
	TransitionFromPresent(cmd, swapchainImages_[curSwapchainImage].image);

	// Optimizes renderpasses, then sequences them.
	for (int i = 0; i < stepsOnThread_.size(); i++) {
		const VKRStep &step = *stepsOnThread_[i];
		switch (step.stepType) {
		case VKRStepType::RENDER:
			PerformRenderPass(step, cmd, curSwapchainImage);
			break;
		case VKRStepType::COPY:
			PerformCopy(step, cmd);
			break;
		case VKRStepType::BLIT:
			PerformBlit(step, cmd);
			break;
		case VKRStepType::READBACK:
			// PerformReadback
			break;
		}
		delete stepsOnThread_[i];
	}
	stepsOnThread_.clear();
	insideFrame_ = false;

	TransitionToPresent(frameData.mainCmd, swapchainImages_[curSwapchainImage].image);

	res = vkEndCommandBuffer(frameData.mainCmd);
	assert(res == VK_SUCCESS);

	// So the sequence will be, cmdInit, [cmdQueue_], frame->cmdBuf.
	// This way we bunch up all the initialization needed for the frame, we render to
	// other buffers before the back buffer, and then last we render to the backbuffer.

	int numCmdBufs = 0;
	std::vector<VkCommandBuffer> cmdBufs;
	if (frameData.hasInitCommands) {
		cmdBufs.push_back(frameData.initCmd);
		frameData.hasInitCommands = false;
	}

	cmdBufs.push_back(frameData.mainCmd);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &acquireSemaphore_;
	VkPipelineStageFlags waitStage[1] = { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
	submit_info.pWaitDstStageMask = waitStage;
	submit_info.commandBufferCount = (uint32_t)cmdBufs.size();
	submit_info.pCommandBuffers = cmdBufs.data();
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &renderingCompleteSemaphore_;
	res = vkQueueSubmit(vulkan_->GetGraphicsQueue(), 1, &submit_info, frameData.fence);
	assert(res == VK_SUCCESS);

	if (useThread) {
		ILOG("PULL: Frame %d.readyForFence = true", frame);
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		frameData.readyForFence = true;
		frameData.push_condVar.notify_all();
	}

	VkSwapchainKHR swapchain = vulkan_->GetSwapchain();
	VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &curSwapchainImage;
	present.pWaitSemaphores = &renderingCompleteSemaphore_;
	present.waitSemaphoreCount = 1;
	present.pResults = nullptr;
	res = vkQueuePresentKHR(vulkan_->GetGraphicsQueue(), &present);
	// TODO: Deal with the VK_SUBOPTIMAL_WSI and VK_ERROR_OUT_OF_DATE_WSI
	// return codes
	assert(res == VK_SUCCESS);

	ILOG("PULL: Finished running frame %d", frame);
}

void VulkanRenderManager::PerformRenderPass(const VKRStep &step, VkCommandBuffer cmd, int swapChainImage) {
	// TODO: If there are multiple, we can transition them together.
	for (const auto &iter : step.preTransitions) {
		if (iter.fb->color.layout != iter.targetLayout) {
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = iter.fb->color.layout;
			barrier.subresourceRange.layerCount = 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.image = iter.fb->color.image;
			barrier.srcAccessMask = 0;
			VkPipelineStageFlags srcStage;
			VkPipelineStageFlags dstStage;
			switch (barrier.oldLayout) {
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			default:
				Crash();
				break;
			}
			barrier.newLayout = iter.targetLayout;
			switch (barrier.newLayout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			default:
				Crash();
				break;
			}
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			iter.fb->color.layout = barrier.newLayout;
		}
	}

	PerformBindFramebufferAsRenderTarget(step, cmd, swapChainImage);

	VKRFramebuffer *fb = step.render.framebuffer;

	VkPipeline lastPipeline = VK_NULL_HANDLE;

	auto &commands = step.commands;

	// TODO: Dynamic state commands (SetViewport, SetScissor, SetBlendConstants, SetStencil*) are only
	// valid when a pipeline is bound with those as dynamic state. So we need to add some state tracking here
	// for this to be correct. This is a bit of a pain but also will let us eliminate redundant calls.

	for (const auto &c : commands) {
		switch (c.cmd) {
		case VKRRenderCommand::VIEWPORT:
			vkCmdSetViewport(cmd, 0, 1, &c.viewport.vp);
			break;

		case VKRRenderCommand::SCISSOR:
			vkCmdSetScissor(cmd, 0, 1, &c.scissor.scissor);
			break;

		case VKRRenderCommand::BLEND:
			vkCmdSetBlendConstants(cmd, c.blendColor.color);
			break;

		case VKRRenderCommand::STENCIL:
			vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilWriteMask);
			vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilCompareMask);
			vkCmdSetStencilReference(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilRef);
			break;

		case VKRRenderCommand::DRAW_INDEXED:
			if (c.drawIndexed.pipeline != lastPipeline) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.drawIndexed.pipeline);
				lastPipeline = c.drawIndexed.pipeline;
			}
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.drawIndexed.pipelineLayout, 0, 1, &c.drawIndexed.ds, c.drawIndexed.numUboOffsets, c.drawIndexed.uboOffsets);
			vkCmdBindIndexBuffer(cmd, c.drawIndexed.ibuffer, c.drawIndexed.ioffset, VK_INDEX_TYPE_UINT16);
			vkCmdBindVertexBuffers(cmd, 0, 1, &c.drawIndexed.vbuffer, &c.drawIndexed.voffset);
			vkCmdDrawIndexed(cmd, c.drawIndexed.count, c.drawIndexed.instances, 0, 0, 0);
			break;

		case VKRRenderCommand::DRAW:
			if (c.draw.pipeline != lastPipeline) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.draw.pipeline);
				lastPipeline = c.draw.pipeline;
			}
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.draw.pipelineLayout, 0, 1, &c.draw.ds, c.draw.numUboOffsets, c.draw.uboOffsets);
			vkCmdBindVertexBuffers(cmd, 0, 1, &c.draw.vbuffer, &c.draw.voffset);
			vkCmdDraw(cmd, c.draw.count, 1, 0, 0);
			break;

		case VKRRenderCommand::CLEAR:
		{
			int numAttachments = 0;
			VkClearRect rc{};
			rc.baseArrayLayer = 0;
			rc.layerCount = 1;
			rc.rect.extent.width = curWidth_;
			rc.rect.extent.height = curHeight_;
			VkClearAttachment attachments[2];
			if (c.clear.clearMask & VK_IMAGE_ASPECT_COLOR_BIT) {
				VkClearAttachment &attachment = attachments[numAttachments++];
				attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				attachment.colorAttachment = 0;
				Uint8x4ToFloat4(attachment.clearValue.color.float32, c.clear.clearColor);
			}
			if (c.clear.clearMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
				VkClearAttachment &attachment = attachments[numAttachments++];
				attachment.aspectMask = 0;
				if (c.clear.clearMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
					attachment.clearValue.depthStencil.depth = c.clear.clearZ;
					attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
				}
				if (c.clear.clearMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
					attachment.clearValue.depthStencil.stencil = c.clear.clearStencil;
					attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			if (numAttachments) {
				vkCmdClearAttachments(cmd, numAttachments, attachments, 1, &rc);
			}
			break;
		}
		default:
			ELOG("Unimpl queue command");
			;
		}
	}
	vkCmdEndRenderPass(cmd);

	// Transition the framebuffer if requested.
	if (fb && step.render.finalColorLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = fb->color.layout;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.image = fb->color.image;
		barrier.srcAccessMask = 0;
		switch (barrier.oldLayout) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		default:
			Crash();
		}
		barrier.newLayout = step.render.finalColorLayout;
		switch (barrier.newLayout) {
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			Crash();
		}
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		// we're between passes so it's OK.
		// ARM Best Practices guide recommends these stage bits.
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		fb->color.layout = barrier.newLayout;
	}
}

void VulkanRenderManager::PerformBindFramebufferAsRenderTarget(const VKRStep &step, VkCommandBuffer cmd, int swapChainImage) {
	VkFramebuffer framebuf;
	int w;
	int h;
	VkImageLayout prevLayout;
	if (step.render.framebuffer) {
		VKRFramebuffer *fb = step.render.framebuffer;
		framebuf = fb->framebuf;
		w = fb->width;
		h = fb->height;
		prevLayout = fb->color.layout;
	} else {
		framebuf = framebuffers_[swapChainImage];
		w = vulkan_->GetBackbufferWidth();
		h = vulkan_->GetBackbufferHeight();
	}

	if (framebuf == curFramebuffer_) {
		if (framebuf == 0)
			Crash();

		// If we're asking to clear, but already bound, we'll just keep it bound but send a clear command.
		// We will try to avoid this as much as possible.
		VkClearAttachment clear[2]{};
		int count = 0;
		if (step.render.color == VKRRenderPassAction::CLEAR) {
			clear[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			Uint8x4ToFloat4(clear[count].clearValue.color.float32, step.render.clearColor);
			clear[count].colorAttachment = 0;
			count++;
		}

		if (step.render.depthStencil == VKRRenderPassAction::CLEAR) {
			clear[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			clear[count].clearValue.depthStencil.depth = step.render.clearDepth;
			clear[count].clearValue.depthStencil.stencil = step.render.clearStencil;
			clear[count].colorAttachment = 0;
			count++;
		}

		if (count > 0) {
			VkClearRect rc{ { 0,0,(uint32_t)w,(uint32_t)h }, 0, 1 };
			vkCmdClearAttachments(cmd, count, clear, 1, &rc);
		}
		// We're done.
		return;
	}

	VkRenderPass renderPass;
	int numClearVals = 0;
	VkClearValue clearVal[2];
	memset(clearVal, 0, sizeof(clearVal));
	if (step.render.framebuffer) {
		VKRFramebuffer *fb = step.render.framebuffer;
		// Now, if the image needs transitioning, let's transition.
		// The backbuffer does not, that's handled by VulkanContext.
		if (step.render.framebuffer->color.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			VkAccessFlags srcAccessMask;
			VkPipelineStageFlags srcStage;
			switch (fb->color.layout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			}

			TransitionImageLayout2(cmd, fb->color.image, VK_IMAGE_ASPECT_COLOR_BIT,
				fb->color.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				srcStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				srcAccessMask, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
			fb->color.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (fb->depth.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			VkAccessFlags srcAccessMask;
			VkPipelineStageFlags srcStage;
			switch (fb->depth.layout) {
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				break;
			}
			TransitionImageLayout2(cmd, fb->color.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				fb->color.layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				srcStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				srcAccessMask, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
			fb->depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		renderPass = renderPasses_[RPIndex(step.render.color, step.render.depthStencil)];
		// ILOG("Switching framebuffer to FBO (fc=%d, cmd=%x, rp=%x)", frameNum_, (int)(uintptr_t)cmd_, (int)(uintptr_t)renderPass);
		if (step.render.color == VKRRenderPassAction::CLEAR) {
			Uint8x4ToFloat4(clearVal[0].color.float32, step.render.clearColor);
			numClearVals = 1;
		}
		if (step.render.depthStencil == VKRRenderPassAction::CLEAR) {
			clearVal[1].depthStencil.depth = step.render.clearDepth;
			clearVal[1].depthStencil.stencil = step.render.clearStencil;
			numClearVals = 2;
		}
	} else {
		renderPass = GetBackbufferRenderpass();
		numClearVals = 2;  // We don't bother with a depth buffer here.
		clearVal[1].depthStencil.depth = 0.0f;
		clearVal[1].depthStencil.stencil = 0;
	}

	VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rp_begin.renderPass = renderPass;
	rp_begin.framebuffer = framebuf;
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = w;
	rp_begin.renderArea.extent.height = h;
	rp_begin.clearValueCount = numClearVals;
	rp_begin.pClearValues = numClearVals ? clearVal : nullptr;
	vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
	curWidth_ = w;
	curHeight_ = h;
	curFramebuffer_ = framebuf;
}

void VulkanRenderManager::PerformCopy(const VKRStep &step, VkCommandBuffer cmd) {
	VKRFramebuffer *src = step.copy.src;
	VKRFramebuffer *dst = step.copy.dst;

	VkImageCopy copy{};
	copy.srcOffset.x = step.copy.srcRect.offset.x;
	copy.srcOffset.y = step.copy.srcRect.offset.y;
	copy.srcOffset.z = 0;
	copy.srcSubresource.mipLevel = 0;
	copy.srcSubresource.layerCount = 1;
	copy.dstOffset.x = step.copy.dstPos.x;
	copy.dstOffset.y = step.copy.dstPos.y;
	copy.dstOffset.z = 0;
	copy.dstSubresource.mipLevel = 0;
	copy.dstSubresource.layerCount = 1;
	copy.extent.width = step.copy.srcRect.extent.width;
	copy.extent.height = step.copy.srcRect.extent.height;
	copy.extent.depth = 1;

	VkImageMemoryBarrier srcBarriers[2]{};
	VkImageMemoryBarrier dstBarriers[2]{};
	int srcCount = 0;
	int dstCount = 0;

	// First source barriers.
	if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		if (src->color.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->color, srcBarriers[srcCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
		if (dst->color.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->color, dstBarriers[dstCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	// We can't copy only depth or only stencil unfortunately.
	if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		if (src->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->depth, srcBarriers[srcCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
		if (dst->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->depth, dstBarriers[dstCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
	}

	// TODO: Fix the pipe bits to be bit less conservative.
	if (srcCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, srcCount, srcBarriers);
	}
	if (dstCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, dstCount, dstBarriers);
	}

	if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdCopyImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &copy);
	}
	if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		copy.srcSubresource.aspectMask = 0;
		copy.dstSubresource.aspectMask = 0;
		if (step.copy.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
			copy.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			copy.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (step.copy.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
			copy.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			copy.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		vkCmdCopyImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &copy);
	}
}

void VulkanRenderManager::PerformBlit(const VKRStep &step, VkCommandBuffer cmd) {
	VkImageMemoryBarrier srcBarriers[2]{};
	VkImageMemoryBarrier dstBarriers[2]{};

	VKRFramebuffer *src = step.blit.src;
	VKRFramebuffer *dst = step.blit.dst;

	int srcCount = 0;
	int dstCount = 0;

	VkImageBlit blit{};
	blit.srcOffsets[0].x = step.blit.srcRect.offset.x;
	blit.srcOffsets[0].y = step.blit.srcRect.offset.y;
	blit.srcOffsets[0].z = 0;
	blit.srcOffsets[1].x = step.blit.srcRect.offset.x + step.blit.srcRect.extent.width;
	blit.srcOffsets[1].y = step.blit.srcRect.offset.y + step.blit.srcRect.extent.height;
	blit.srcOffsets[1].z = 1;
	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.layerCount = 1;
	blit.dstOffsets[0].x = step.blit.dstRect.offset.x;
	blit.dstOffsets[0].y = step.blit.dstRect.offset.y;
	blit.dstOffsets[0].z = 0;
	blit.dstOffsets[1].x = step.blit.dstRect.offset.x + step.blit.dstRect.extent.width;
	blit.dstOffsets[1].y = step.blit.dstRect.offset.y + step.blit.dstRect.extent.height;
	blit.dstOffsets[1].z = 1;
	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.layerCount = 1;

	// First source barriers.
	if (step.blit.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		if (src->color.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->color, srcBarriers[srcCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
		if (dst->color.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->color, dstBarriers[dstCount++], VK_IMAGE_ASPECT_COLOR_BIT);
		}
	}

	// We can't copy only depth or only stencil unfortunately.
	if (step.blit.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		if (src->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(src->depth, srcBarriers[srcCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
		if (dst->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->depth, dstBarriers[dstCount++], VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		}
	}

	// TODO: Fix the pipe bits to be bit less conservative.
	if (srcCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, srcCount, srcBarriers);
	}
	if (dstCount) {
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, dstCount, dstBarriers);
	}

	if (step.blit.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdBlitImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &blit, step.blit.filter);
	}
	if (step.blit.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		blit.srcSubresource.aspectMask = 0;
		blit.dstSubresource.aspectMask = 0;
		if (step.blit.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (step.blit.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		vkCmdBlitImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &blit, step.blit.filter);
	}
}

void VulkanRenderManager::SetupTransitionToTransferSrc(VKRImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect) {
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = img.layout;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.image = img.image;
	barrier.srcAccessMask = 0;
	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		Crash();
	}
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.subresourceRange.aspectMask = aspect;
	img.layout = barrier.newLayout;
}

void VulkanRenderManager::SetupTransitionToTransferDst(VKRImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect) {
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = img.layout;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.image = img.image;
	barrier.srcAccessMask = 0;
	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		Crash();
	}
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.subresourceRange.aspectMask = aspect;
	img.layout = barrier.newLayout;
}
