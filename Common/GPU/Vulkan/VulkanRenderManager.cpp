#include <algorithm>
#include <cstdint>

#include <sstream>

#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "Common/GPU/Vulkan/VulkanAlloc.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"

#include "Common/Thread/ThreadUtil.h"

#if 0 // def _DEBUG
#define VLOG(...) INFO_LOG(G3D, __VA_ARGS__)
#else
#define VLOG(...)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

using namespace PPSSPP_VK;

bool VKRGraphicsPipeline::Create(VulkanContext *vulkan) {
	if (!desc) {
		// Already failed to create this one.
		return false;
	}
	VkPipeline vkpipeline;
	VkResult result = vkCreateGraphicsPipelines(vulkan->GetDevice(), desc->pipelineCache, 1, &desc->pipe, nullptr, &vkpipeline);

	bool success = true;
	if (result == VK_INCOMPLETE) {
		// Bad (disallowed by spec) return value seen on Adreno in Burnout :(  Try to ignore?
		// Would really like to log more here, we could probably attach more info to desc.
		//
		// At least create a null placeholder to avoid creating over and over if something is broken.
		pipeline = VK_NULL_HANDLE;
		success = false;
	} else if (result != VK_SUCCESS) {
		pipeline = VK_NULL_HANDLE;
		ERROR_LOG(G3D, "Failed creating graphics pipeline! result='%s'", VulkanResultToString(result));
		success = false;
	} else {
		pipeline = vkpipeline;
	}

	delete desc;
	desc = nullptr;
	return success;
}

bool VKRComputePipeline::Create(VulkanContext *vulkan) {
	if (!desc) {
		// Already failed to create this one.
		return false;
	}
	VkPipeline vkpipeline;
	VkResult result = vkCreateComputePipelines(vulkan->GetDevice(), desc->pipelineCache, 1, &desc->pipe, nullptr, &vkpipeline);

	bool success = true;
	if (result != VK_SUCCESS) {
		pipeline = VK_NULL_HANDLE;
		ERROR_LOG(G3D, "Failed creating compute pipeline! result='%s'", VulkanResultToString(result));
		success = false;
	} else {
		pipeline = vkpipeline;
	}

	delete desc;
	desc = nullptr;
	return success;
}

VKRFramebuffer::VKRFramebuffer(VulkanContext *vk, VkCommandBuffer initCmd, VkRenderPass renderPass, int _width, int _height, const char *tag) : vulkan_(vk) {
	width = _width;
	height = _height;

	CreateImage(vulkan_, initCmd, color, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, tag);
	CreateImage(vulkan_, initCmd, depth, width, height, vulkan_->GetDeviceInfo().preferredDepthStencilFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false, tag);

	VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	VkImageView views[2]{};

	fbci.renderPass = renderPass;
	fbci.attachmentCount = 2;
	fbci.pAttachments = views;
	views[0] = color.imageView;
	views[1] = depth.imageView;
	fbci.width = width;
	fbci.height = height;
	fbci.layers = 1;

	VkResult res = vkCreateFramebuffer(vulkan_->GetDevice(), &fbci, nullptr, &framebuf);
	_assert_(res == VK_SUCCESS);

	if (tag && vk->Extensions().EXT_debug_utils) {
		vk->SetDebugName(color.image, VK_OBJECT_TYPE_IMAGE, StringFromFormat("fb_color_%s", tag).c_str());
		vk->SetDebugName(depth.image, VK_OBJECT_TYPE_IMAGE, StringFromFormat("fb_depth_%s", tag).c_str());
		vk->SetDebugName(framebuf, VK_OBJECT_TYPE_FRAMEBUFFER, StringFromFormat("fb_%s", tag).c_str());
		this->tag = tag;
	}
}

VKRFramebuffer::~VKRFramebuffer() {
	if (color.imageView)
		vulkan_->Delete().QueueDeleteImageView(color.imageView);
	if (depth.imageView)
		vulkan_->Delete().QueueDeleteImageView(depth.imageView);
	if (color.image) {
		_dbg_assert_(color.alloc);
		vulkan_->Delete().QueueDeleteImageAllocation(color.image, color.alloc);
	}
	if (depth.image) {
		_dbg_assert_(depth.alloc);
		vulkan_->Delete().QueueDeleteImageAllocation(depth.image, depth.alloc);
	}
	if (depth.depthSampleView)
		vulkan_->Delete().QueueDeleteImageView(depth.depthSampleView);
	if (framebuf)
		vulkan_->Delete().QueueDeleteFramebuffer(framebuf);
}

void CreateImage(VulkanContext *vulkan, VkCommandBuffer cmd, VKRImage &img, int width, int height, VkFormat format, VkImageLayout initialLayout, bool color, const char *tag) {
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
	// Strictly speaking we don't yet need VK_IMAGE_USAGE_SAMPLED_BIT for depth buffers since we do not yet sample depth buffers.
	ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (color) {
		ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	} else {
		ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	VmaAllocationInfo allocInfo{};

	VkResult res = vmaCreateImage(vulkan->Allocator(), &ici, &allocCreateInfo, &img.image, &img.alloc, &allocInfo);
	_dbg_assert_(res == VK_SUCCESS);

	VkImageAspectFlags aspects = color ? VK_IMAGE_ASPECT_COLOR_BIT : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

	VkImageViewCreateInfo ivci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	ivci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
	ivci.format = ici.format;
	ivci.image = img.image;
	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ivci.subresourceRange.aspectMask = aspects;
	ivci.subresourceRange.layerCount = 1;
	ivci.subresourceRange.levelCount = 1;
	res = vkCreateImageView(vulkan->GetDevice(), &ivci, nullptr, &img.imageView);
	_dbg_assert_(res == VK_SUCCESS);

	// Separate view for texture sampling that only exposes depth.
	if (!color) {
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		res = vkCreateImageView(vulkan->GetDevice(), &ivci, nullptr, &img.depthSampleView);
		_dbg_assert_(res == VK_SUCCESS);
	} else {
		img.depthSampleView = VK_NULL_HANDLE;
	}

	VkPipelineStageFlags dstStage;
	VkAccessFlagBits dstAccessMask;
	switch (initialLayout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	default:
		Crash();
		return;
	}

	TransitionImageLayout2(cmd, img.image, 0, 1, aspects,
		VK_IMAGE_LAYOUT_UNDEFINED, initialLayout,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dstStage,
		0, dstAccessMask);
	img.layout = initialLayout;

	img.format = format;
	img.tag = tag ? tag : "N/A";
}

VulkanRenderManager::VulkanRenderManager(VulkanContext *vulkan) : vulkan_(vulkan), queueRunner_(vulkan) {
	VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	semaphoreCreateInfo.flags = 0;
	VkResult res = vkCreateSemaphore(vulkan_->GetDevice(), &semaphoreCreateInfo, nullptr, &acquireSemaphore_);
	_dbg_assert_(res == VK_SUCCESS);
	res = vkCreateSemaphore(vulkan_->GetDevice(), &semaphoreCreateInfo, nullptr, &renderingCompleteSemaphore_);
	_dbg_assert_(res == VK_SUCCESS);

	inflightFramesAtStart_ = vulkan_->GetInflightFrames();
	for (int i = 0; i < inflightFramesAtStart_; i++) {
		VkCommandPoolCreateInfo cmd_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		cmd_pool_info.queueFamilyIndex = vulkan_->GetGraphicsQueueFamilyIndex();
		cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		VkResult res = vkCreateCommandPool(vulkan_->GetDevice(), &cmd_pool_info, nullptr, &frameData_[i].cmdPoolInit);
		_dbg_assert_(res == VK_SUCCESS);
		res = vkCreateCommandPool(vulkan_->GetDevice(), &cmd_pool_info, nullptr, &frameData_[i].cmdPoolMain);
		_dbg_assert_(res == VK_SUCCESS);

		VkCommandBufferAllocateInfo cmd_alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cmd_alloc.commandPool = frameData_[i].cmdPoolInit;
		cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_alloc.commandBufferCount = 1;

		res = vkAllocateCommandBuffers(vulkan_->GetDevice(), &cmd_alloc, &frameData_[i].initCmd);
		_dbg_assert_(res == VK_SUCCESS);
		cmd_alloc.commandPool = frameData_[i].cmdPoolMain;
		res = vkAllocateCommandBuffers(vulkan_->GetDevice(), &cmd_alloc, &frameData_[i].mainCmd);
		_dbg_assert_(res == VK_SUCCESS);

		// Creating the frame fence with true so they can be instantly waited on the first frame
		frameData_[i].fence = vulkan_->CreateFence(true);

		// This fence one is used for synchronizing readbacks. Does not need preinitialization.
		frameData_[i].readbackFence = vulkan_->CreateFence(false);

		VkQueryPoolCreateInfo query_ci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		query_ci.queryCount = MAX_TIMESTAMP_QUERIES;
		query_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
		res = vkCreateQueryPool(vulkan_->GetDevice(), &query_ci, nullptr, &frameData_[i].profile.queryPool);
	}

	queueRunner_.CreateDeviceObjects();

	// AMD hack for issue #10097 (older drivers only.)
	const auto &props = vulkan_->GetPhysicalDeviceProperties().properties;
	if (props.vendorID == VULKAN_VENDOR_AMD && props.apiVersion < VK_API_VERSION_1_1) {
		useThread_ = false;
	}
}

bool VulkanRenderManager::CreateBackbuffers() {
	if (!vulkan_->GetSwapchain()) {
		ERROR_LOG(G3D, "No swapchain - can't create backbuffers");
		return false;
	}
	VkResult res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &swapchainImageCount_, nullptr);
	_dbg_assert_(res == VK_SUCCESS);

	VkImage *swapchainImages = new VkImage[swapchainImageCount_];
	res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &swapchainImageCount_, swapchainImages);
	if (res != VK_SUCCESS) {
		ERROR_LOG(G3D, "vkGetSwapchainImagesKHR failed");
		delete[] swapchainImages;
		return false;
	}

	VkCommandBuffer cmdInit = GetInitCmd();

	for (uint32_t i = 0; i < swapchainImageCount_; i++) {
		SwapchainImageData sc_buffer{};
		sc_buffer.image = swapchainImages[i];

		VkImageViewCreateInfo color_image_view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		color_image_view.format = vulkan_->GetSwapchainFormat();
		color_image_view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		color_image_view.subresourceRange.baseMipLevel = 0;
		color_image_view.subresourceRange.levelCount = 1;
		color_image_view.subresourceRange.baseArrayLayer = 0;
		color_image_view.subresourceRange.layerCount = 1;
		color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		color_image_view.flags = 0;
		color_image_view.image = sc_buffer.image;

		// We leave the images as UNDEFINED, there's no need to pre-transition them as
		// the backbuffer renderpass starts out with them being auto-transitioned from UNDEFINED anyway.
		// Also, turns out it's illegal to transition un-acquired images, thanks Hans-Kristian. See #11417.

		res = vkCreateImageView(vulkan_->GetDevice(), &color_image_view, nullptr, &sc_buffer.view);
		swapchainImages_.push_back(sc_buffer);
		_dbg_assert_(res == VK_SUCCESS);
	}
	delete[] swapchainImages;

	// Must be before InitBackbufferRenderPass.
	if (InitDepthStencilBuffer(cmdInit)) {
		InitBackbufferFramebuffers(vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	}
	curWidthRaw_ = -1;
	curHeightRaw_ = -1;

	if (HasBackbuffers()) {
		VLOG("Backbuffers Created");
	}

	if (newInflightFrames_ != -1) {
		INFO_LOG(G3D, "Updating inflight frames to %d", newInflightFrames_);
		vulkan_->UpdateInflightFrames(newInflightFrames_);
		newInflightFrames_ = -1;
	}

	outOfDateFrames_ = 0;

	// Start the thread.
	if (useThread_ && HasBackbuffers()) {
		run_ = true;
		// Won't necessarily be 0.
		threadInitFrame_ = vulkan_->GetCurFrame();
		INFO_LOG(G3D, "Starting Vulkan submission thread (threadInitFrame_ = %d)", vulkan_->GetCurFrame());
		thread_ = std::thread(&VulkanRenderManager::ThreadFunc, this);
		INFO_LOG(G3D, "Starting Vulkan compiler thread");
		compileThread_ = std::thread(&VulkanRenderManager::CompileThreadFunc, this);
	}
	return true;
}

void VulkanRenderManager::StopThread() {
	if (useThread_ && run_) {
		run_ = false;
		// Stop the thread.
		for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
			auto &frameData = frameData_[i];
			{
				std::unique_lock<std::mutex> lock(frameData.push_mutex);
				frameData.push_condVar.notify_all();
			}
			{
				std::unique_lock<std::mutex> lock(frameData.pull_mutex);
				frameData.pull_condVar.notify_all();
			}
			// Zero the queries so we don't try to pull them later.
			frameData.profile.timestampDescriptions.clear();
		}
		thread_.join();
		INFO_LOG(G3D, "Vulkan submission thread joined. Frame=%d", vulkan_->GetCurFrame());
		compileCond_.notify_all();
		compileThread_.join();
		INFO_LOG(G3D, "Vulkan compiler thread joined.");

		// Eat whatever has been queued up for this frame if anything.
		Wipe();

		// Wait for any fences to finish and be resignaled, so we don't have sync issues.
		// Also clean out any queued data, which might refer to things that might not be valid
		// when we restart...
		for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
			auto &frameData = frameData_[i];
			_assert_(!frameData.readyForRun);
			_assert_(frameData.steps.empty());
			if (frameData.hasInitCommands) {
				// Clear 'em out.  This can happen on restart sometimes.
				vkEndCommandBuffer(frameData.initCmd);
				frameData.hasInitCommands = false;
			}
			frameData.readyForRun = false;
			for (size_t i = 0; i < frameData.steps.size(); i++) {
				delete frameData.steps[i];
			}
			frameData.steps.clear();

			std::unique_lock<std::mutex> lock(frameData.push_mutex);
			while (!frameData.readyForFence) {
				VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (stop)", i);
				frameData.push_condVar.wait(lock);
			}
		}
	} else {
		INFO_LOG(G3D, "Vulkan submission thread was already stopped.");
	}
}

void VulkanRenderManager::DestroyBackbuffers() {
	StopThread();
	vulkan_->WaitUntilQueueIdle();

	for (auto &image : swapchainImages_) {
		vulkan_->Delete().QueueDeleteImageView(image.view);
	}
	swapchainImages_.clear();

	if (depth_.view) {
		vulkan_->Delete().QueueDeleteImageView(depth_.view);
	}
	if (depth_.image) {
		_dbg_assert_(depth_.alloc);
		vulkan_->Delete().QueueDeleteImageAllocation(depth_.image, depth_.alloc);
	}
	depth_ = {};
	for (uint32_t i = 0; i < framebuffers_.size(); i++) {
		_dbg_assert_(framebuffers_[i] != VK_NULL_HANDLE);
		vulkan_->Delete().QueueDeleteFramebuffer(framebuffers_[i]);
	}
	framebuffers_.clear();

	INFO_LOG(G3D, "Backbuffers destroyed");
}

VulkanRenderManager::~VulkanRenderManager() {
	INFO_LOG(G3D, "VulkanRenderManager destructor");
	StopThread();
	vulkan_->WaitUntilQueueIdle();

	DrainCompileQueue();
	VkDevice device = vulkan_->GetDevice();
	vkDestroySemaphore(device, acquireSemaphore_, nullptr);
	vkDestroySemaphore(device, renderingCompleteSemaphore_, nullptr);
	for (int i = 0; i < inflightFramesAtStart_; i++) {
		vkFreeCommandBuffers(device, frameData_[i].cmdPoolInit, 1, &frameData_[i].initCmd);
		vkFreeCommandBuffers(device, frameData_[i].cmdPoolMain, 1, &frameData_[i].mainCmd);
		vkDestroyCommandPool(device, frameData_[i].cmdPoolInit, nullptr);
		vkDestroyCommandPool(device, frameData_[i].cmdPoolMain, nullptr);
		vkDestroyFence(device, frameData_[i].fence, nullptr);
		vkDestroyFence(device, frameData_[i].readbackFence, nullptr);
		vkDestroyQueryPool(device, frameData_[i].profile.queryPool, nullptr);
	}
	queueRunner_.DestroyDeviceObjects();
}

void VulkanRenderManager::CompileThreadFunc() {
	SetCurrentThreadName("ShaderCompile");
	while (true) {
		std::vector<CompileQueueEntry> toCompile;
		{
			std::unique_lock<std::mutex> lock(compileMutex_);
			if (compileQueue_.empty()) {
				compileCond_.wait(lock);
			}
			toCompile = std::move(compileQueue_);
			compileQueue_.clear();
		}
		if (!run_) {
			break;
		}
		for (auto &entry : toCompile) {
			switch (entry.type) {
			case CompileQueueEntry::Type::GRAPHICS:
				entry.graphics->Create(vulkan_);
				break;
			case CompileQueueEntry::Type::COMPUTE:
				entry.compute->Create(vulkan_);
				break;
			}
		}
		queueRunner_.NotifyCompileDone();
	}
}

void VulkanRenderManager::DrainCompileQueue() {
	std::unique_lock<std::mutex> lock(compileMutex_);
	while (!compileQueue_.empty()) {
		queueRunner_.WaitForCompileNotification();
	}
}

void VulkanRenderManager::ThreadFunc() {
	SetCurrentThreadName("RenderMan");
	int threadFrame = threadInitFrame_;
	bool nextFrame = false;
	bool firstFrame = true;
	while (true) {
		{
			if (nextFrame) {
				threadFrame++;
				if (threadFrame >= vulkan_->GetInflightFrames())
					threadFrame = 0;
			}
			FrameData &frameData = frameData_[threadFrame];
			std::unique_lock<std::mutex> lock(frameData.pull_mutex);
			while (!frameData.readyForRun && run_) {
				VLOG("PULL: Waiting for frame[%d].readyForRun", threadFrame);
				frameData.pull_condVar.wait(lock);
			}
			if (!frameData.readyForRun && !run_) {
				// This means we're out of frames to render and run_ is false, so bail.
				break;
			}
			VLOG("PULL: frame[%d].readyForRun = false", threadFrame);
			frameData.readyForRun = false;
			// Previously we had a quick exit here that avoided calling Run() if run_ was suddenly false,
			// but that created a race condition where frames could end up not finished properly on resize etc.

			// Only increment next time if we're done.
			nextFrame = frameData.type == VKRRunType::END;
			_dbg_assert_(frameData.type == VKRRunType::END || frameData.type == VKRRunType::SYNC);
		}
		VLOG("PULL: Running frame %d", threadFrame);
		if (firstFrame) {
			INFO_LOG(G3D, "Running first frame (%d)", threadFrame);
			firstFrame = false;
		}
		Run(threadFrame);
		VLOG("PULL: Finished frame %d", threadFrame);
	}

	// Wait for the device to be done with everything, before tearing stuff down.
	vkDeviceWaitIdle(vulkan_->GetDevice());

	VLOG("PULL: Quitting");
}

void VulkanRenderManager::BeginFrame(bool enableProfiling, bool enableLogProfiler) {
	VLOG("BeginFrame");
	VkDevice device = vulkan_->GetDevice();

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];

	// Make sure the very last command buffer from the frame before the previous has been fully executed.
	if (useThread_) {
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		while (!frameData.readyForFence) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1", curFrame);
			frameData.push_condVar.wait(lock);
		}
		frameData.readyForFence = false;
	}

	VLOG("PUSH: Fencing %d", curFrame);

	vkWaitForFences(device, 1, &frameData.fence, true, UINT64_MAX);
	vkResetFences(device, 1, &frameData.fence);

	// Can't set this until after the fence.
	frameData.profilingEnabled_ = enableProfiling;
	frameData.readbackFenceUsed = false;

	uint64_t queryResults[MAX_TIMESTAMP_QUERIES];

	if (frameData.profilingEnabled_) {
		// Pull the profiling results from last time and produce a summary!
		if (!frameData.profile.timestampDescriptions.empty()) {
			int numQueries = (int)frameData.profile.timestampDescriptions.size();
			VkResult res = vkGetQueryPoolResults(
				vulkan_->GetDevice(),
				frameData.profile.queryPool, 0, numQueries, sizeof(uint64_t) * numQueries, &queryResults[0], sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT);
			if (res == VK_SUCCESS) {
				double timestampConversionFactor = (double)vulkan_->GetPhysicalDeviceProperties().properties.limits.timestampPeriod * (1.0 / 1000000.0);
				int validBits = vulkan_->GetQueueFamilyProperties(vulkan_->GetGraphicsQueueFamilyIndex()).timestampValidBits;
				uint64_t timestampDiffMask = validBits == 64 ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << validBits) - 1);
				std::stringstream str;

				char line[256];
				snprintf(line, sizeof(line), "Total GPU time: %0.3f ms\n", ((double)((queryResults[numQueries - 1] - queryResults[0]) & timestampDiffMask) * timestampConversionFactor));
				str << line;
				snprintf(line, sizeof(line), "Render CPU time: %0.3f ms\n", (frameData.profile.cpuEndTime - frameData.profile.cpuStartTime) * 1000.0);
				str << line;
				for (int i = 0; i < numQueries - 1; i++) {
					uint64_t diff = (queryResults[i + 1] - queryResults[i]) & timestampDiffMask;
					double milliseconds = (double)diff * timestampConversionFactor;
					snprintf(line, sizeof(line), "%s: %0.3f ms\n", frameData.profile.timestampDescriptions[i + 1].c_str(), milliseconds);
					str << line;
				}
				frameData.profile.profileSummary = str.str();
			} else {
				frameData.profile.profileSummary = "(error getting GPU profile - not ready?)";
			}
		} else {
			frameData.profile.profileSummary = "(no GPU profile data collected)";
		}
	}

	// Must be after the fence - this performs deletes.
	VLOG("PUSH: BeginFrame %d", curFrame);
	if (!run_) {
		WARN_LOG(G3D, "BeginFrame while !run_!");
	}

	vulkan_->BeginFrame(enableLogProfiler ? GetInitCmd() : VK_NULL_HANDLE);

	insideFrame_ = true;
	renderStepOffset_ = 0;

	frameData.profile.timestampDescriptions.clear();
	if (frameData.profilingEnabled_) {
		// For various reasons, we need to always use an init cmd buffer in this case to perform the vkCmdResetQueryPool,
		// unless we want to limit ourselves to only measure the main cmd buffer.
		// Later versions of Vulkan have support for clearing queries on the CPU timeline, but we don't want to rely on that.
		// Reserve the first two queries for initCmd.
		frameData.profile.timestampDescriptions.push_back("initCmd Begin");
		frameData.profile.timestampDescriptions.push_back("initCmd");
		VkCommandBuffer initCmd = GetInitCmd();
		vkCmdResetQueryPool(initCmd, frameData.profile.queryPool, 0, MAX_TIMESTAMP_QUERIES);
		vkCmdWriteTimestamp(initCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameData.profile.queryPool, 0);
	}
}

VkCommandBuffer VulkanRenderManager::GetInitCmd() {
	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	if (!frameData.hasInitCommands) {
		VkCommandBufferBeginInfo begin = {
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			nullptr,
			VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
		};
		vkResetCommandPool(vulkan_->GetDevice(), frameData.cmdPoolInit, 0);
		VkResult res = vkBeginCommandBuffer(frameData.initCmd, &begin);
		if (res != VK_SUCCESS) {
			return VK_NULL_HANDLE;
		}
		frameData.hasInitCommands = true;
	}
	return frameData_[curFrame].initCmd;
}

void VulkanRenderManager::EndCurRenderStep() {
	// Save the accumulated pipeline flags so we can use that to configure the render pass.
	// We'll often be able to avoid loading/saving the depth/stencil buffer.
	if (curRenderStep_) {
		curRenderStep_->render.pipelineFlags = curPipelineFlags_;
		// We don't do this optimization for very small targets, probably not worth it.
		if (!curRenderArea_.Empty() && (curWidth_ > 32 && curHeight_ > 32)) {
			curRenderStep_->render.renderArea = curRenderArea_.ToVkRect2D();
		} else {
			curRenderStep_->render.renderArea.offset = {};
			curRenderStep_->render.renderArea.extent = { (uint32_t)curWidth_, (uint32_t)curHeight_ };
		}
		curRenderArea_.Reset();

		// We no longer have a current render step.
		curRenderStep_ = nullptr;
		curPipelineFlags_ = 0;
	}
}

void VulkanRenderManager::BindFramebufferAsRenderTarget(VKRFramebuffer *fb, VKRRenderPassLoadAction color, VKRRenderPassLoadAction depth, VKRRenderPassLoadAction stencil, uint32_t clearColor, float clearDepth, uint8_t clearStencil, const char *tag) {
	_dbg_assert_(insideFrame_);
	// Eliminate dupes (bind of the framebuffer we already are rendering to), instantly convert to a clear if possible.
	if (!steps_.empty() && steps_.back()->stepType == VKRStepType::RENDER && steps_.back()->render.framebuffer == fb) {
		u32 clearMask = 0;
		if (color == VKRRenderPassLoadAction::CLEAR) {
			clearMask |= VK_IMAGE_ASPECT_COLOR_BIT;
		}
		if (depth == VKRRenderPassLoadAction::CLEAR) {
			clearMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (stencil == VKRRenderPassLoadAction::CLEAR) {
			clearMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		// If we need a clear and the previous step has commands already, it's best to just add a clear and keep going.
		// If there's no clear needed, let's also do that.
		//
		// However, if we do need a clear and there are no commands in the previous pass,
		// we want the queuerunner to have the opportunity to merge, so we'll go ahead and make a new renderpass.
		if (clearMask == 0 || !steps_.back()->commands.empty()) {
			curRenderStep_ = steps_.back();
			curStepHasViewport_ = false;
			curStepHasScissor_ = false;
			for (const auto &c : steps_.back()->commands) {
				if (c.cmd == VKRRenderCommand::VIEWPORT) {
					curStepHasViewport_ = true;
				} else if (c.cmd == VKRRenderCommand::SCISSOR) {
					curStepHasScissor_ = true;
				}
			}
			if (clearMask != 0) {
				VkRenderData data{ VKRRenderCommand::CLEAR };
				data.clear.clearColor = clearColor;
				data.clear.clearZ = clearDepth;
				data.clear.clearStencil = clearStencil;
				data.clear.clearMask = clearMask;
				curRenderStep_->commands.push_back(data);
				curRenderArea_.SetRect(0, 0, curWidth_, curHeight_);
			}
			return;
		}
	}

	// More redundant bind elimination.
	if (curRenderStep_) {
		if (curRenderStep_->commands.empty()) {
			if (curRenderStep_->render.colorLoad != VKRRenderPassLoadAction::CLEAR && curRenderStep_->render.depthLoad != VKRRenderPassLoadAction::CLEAR && curRenderStep_->render.stencilLoad != VKRRenderPassLoadAction::CLEAR) {
				// Can trivially kill the last empty render step.
				_dbg_assert_(steps_.back() == curRenderStep_);
				delete steps_.back();
				steps_.pop_back();
				curRenderStep_ = nullptr;
			}
			VLOG("Empty render step. Usually happens after uploading pixels..");
		}

		EndCurRenderStep();
	}

	// Older Mali drivers have issues with depth and stencil don't match load/clear/etc.
	// TODO: Determine which versions and do this only where necessary.
	u32 lateClearMask = 0;
	if (depth != stencil && vulkan_->GetPhysicalDeviceProperties().properties.vendorID == VULKAN_VENDOR_ARM) {
		if (stencil == VKRRenderPassLoadAction::DONT_CARE) {
			stencil = depth;
		} else if (depth == VKRRenderPassLoadAction::DONT_CARE) {
			depth = stencil;
		} else if (stencil == VKRRenderPassLoadAction::CLEAR) {
			depth = stencil;
			lateClearMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		} else if (depth == VKRRenderPassLoadAction::CLEAR) {
			stencil = depth;
			lateClearMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
	}

	VKRStep *step = new VKRStep{ VKRStepType::RENDER };
	step->render.framebuffer = fb;
	step->render.colorLoad = color;
	step->render.depthLoad = depth;
	step->render.stencilLoad = stencil;
	step->render.clearColor = clearColor;
	step->render.clearDepth = clearDepth;
	step->render.clearStencil = clearStencil;
	step->render.numDraws = 0;
	step->render.numReads = 0;
	step->render.finalColorLayout = !fb ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	step->render.finalDepthStencilLayout = !fb ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	step->tag = tag;
	steps_.push_back(step);

	if (fb) {
		// If there's a KEEP, we naturally read from the framebuffer.
		if (color == VKRRenderPassLoadAction::KEEP || depth == VKRRenderPassLoadAction::KEEP || stencil == VKRRenderPassLoadAction::KEEP) {
			step->dependencies.insert(fb);
		}
	}

	curRenderStep_ = step;
	curStepHasViewport_ = false;
	curStepHasScissor_ = false;
	if (fb) {
		curWidthRaw_ = fb->width;
		curHeightRaw_ = fb->height;
		curWidth_ = fb->width;
		curHeight_ = fb->height;
	} else {
		curWidthRaw_ = vulkan_->GetBackbufferWidth();
		curHeightRaw_ = vulkan_->GetBackbufferHeight();
		if (g_display_rotation == DisplayRotation::ROTATE_90 || g_display_rotation == DisplayRotation::ROTATE_270) {
			curWidth_ = curHeightRaw_;
			curHeight_ = curWidthRaw_;
		} else {
			curWidth_ = curWidthRaw_;
			curHeight_ = curHeightRaw_;
		}
	}

	if (color == VKRRenderPassLoadAction::CLEAR || depth == VKRRenderPassLoadAction::CLEAR || stencil == VKRRenderPassLoadAction::CLEAR) {
		curRenderArea_.SetRect(0, 0, curWidth_, curHeight_);
	}

	// See above - we add a clear afterward if only one side for depth/stencil CLEAR/KEEP.
	if (lateClearMask != 0) {
		VkRenderData data{ VKRRenderCommand::CLEAR };
		data.clear.clearColor = clearColor;
		data.clear.clearZ = clearDepth;
		data.clear.clearStencil = clearStencil;
		data.clear.clearMask = lateClearMask;
		curRenderStep_->commands.push_back(data);
	}
}

bool VulkanRenderManager::CopyFramebufferToMemorySync(VKRFramebuffer *src, VkImageAspectFlags aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, const char *tag) {
	_dbg_assert_(insideFrame_);
	for (int i = (int)steps_.size() - 1; i >= 0; i--) {
		if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == src) {
			steps_[i]->render.numReads++;
			break;
		}
	}

	EndCurRenderStep();

	VKRStep *step = new VKRStep{ VKRStepType::READBACK };
	step->readback.aspectMask = aspectBits;
	step->readback.src = src;
	step->readback.srcRect.offset = { x, y };
	step->readback.srcRect.extent = { (uint32_t)w, (uint32_t)h };
	step->dependencies.insert(src);
	step->tag = tag;
	steps_.push_back(step);

	FlushSync();

	Draw::DataFormat srcFormat = Draw::DataFormat::UNDEFINED;
	if (aspectBits & VK_IMAGE_ASPECT_COLOR_BIT) {
		if (src) {
			switch (src->color.format) {
			case VK_FORMAT_R8G8B8A8_UNORM: srcFormat = Draw::DataFormat::R8G8B8A8_UNORM; break;
			default: _assert_(false);
			}
		} else {
			// Backbuffer.
			if (!(vulkan_->GetSurfaceCapabilities().supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
				ERROR_LOG(G3D, "Copying from backbuffer not supported, can't take screenshots");
				return false;
			}
			switch (vulkan_->GetSwapchainFormat()) {
			case VK_FORMAT_B8G8R8A8_UNORM: srcFormat = Draw::DataFormat::B8G8R8A8_UNORM; break;
			case VK_FORMAT_R8G8B8A8_UNORM: srcFormat = Draw::DataFormat::R8G8B8A8_UNORM; break;
			// NOTE: If you add supported formats here, make sure to also support them in VulkanQueueRunner::CopyReadbackBuffer.
			default:
				ERROR_LOG(G3D, "Unsupported backbuffer format for screenshots");
				return false;
			}
		}
	} else if (aspectBits & VK_IMAGE_ASPECT_STENCIL_BIT) {
		// Copies from stencil are always S8.
		srcFormat = Draw::DataFormat::S8;
	} else if (aspectBits & VK_IMAGE_ASPECT_DEPTH_BIT) {
		switch (src->depth.format) {
		case VK_FORMAT_D24_UNORM_S8_UINT: srcFormat = Draw::DataFormat::D24_S8; break;
		case VK_FORMAT_D32_SFLOAT_S8_UINT: srcFormat = Draw::DataFormat::D32F; break;
		case VK_FORMAT_D16_UNORM_S8_UINT: srcFormat = Draw::DataFormat::D16; break;
		default: _assert_(false);
		}
	} else {
		_assert_(false);
	}
	// Need to call this after FlushSync so the pixels are guaranteed to be ready in CPU-accessible VRAM.
	queueRunner_.CopyReadbackBuffer(w, h, srcFormat, destFormat, pixelStride, pixels);
	return true;
}

void VulkanRenderManager::CopyImageToMemorySync(VkImage image, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, const char *tag) {
	_dbg_assert_(insideFrame_);

	EndCurRenderStep();

	VKRStep *step = new VKRStep{ VKRStepType::READBACK_IMAGE };
	step->readback_image.image = image;
	step->readback_image.srcRect.offset = { x, y };
	step->readback_image.srcRect.extent = { (uint32_t)w, (uint32_t)h };
	step->readback_image.mipLevel = mipLevel;
	step->tag = tag;
	steps_.push_back(step);

	FlushSync();

	// Need to call this after FlushSync so the pixels are guaranteed to be ready in CPU-accessible VRAM.
	queueRunner_.CopyReadbackBuffer(w, h, destFormat, destFormat, pixelStride, pixels);
}

bool VulkanRenderManager::InitBackbufferFramebuffers(int width, int height) {
	VkResult res;
	// We share the same depth buffer but have multiple color buffers, see the loop below.
	VkImageView attachments[2] = { VK_NULL_HANDLE, depth_.view };

	VLOG("InitFramebuffers: %dx%d", width, height);
	VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb_info.renderPass = queueRunner_.GetBackbufferRenderPass();
	fb_info.attachmentCount = 2;
	fb_info.pAttachments = attachments;
	fb_info.width = width;
	fb_info.height = height;
	fb_info.layers = 1;

	framebuffers_.resize(swapchainImageCount_);

	for (uint32_t i = 0; i < swapchainImageCount_; i++) {
		attachments[0] = swapchainImages_[i].view;
		res = vkCreateFramebuffer(vulkan_->GetDevice(), &fb_info, nullptr, &framebuffers_[i]);
		_dbg_assert_(res == VK_SUCCESS);
		if (res != VK_SUCCESS) {
			framebuffers_.clear();
			return false;
		}
	}

	return true;
}

bool VulkanRenderManager::InitDepthStencilBuffer(VkCommandBuffer cmd) {
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

	depth_.format = depth_format;

	VmaAllocationCreateInfo allocCreateInfo{};
	VmaAllocationInfo allocInfo{};

	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkResult res = vmaCreateImage(vulkan_->Allocator(), &image_info, &allocCreateInfo, &depth_.image, &depth_.alloc, &allocInfo);
	_dbg_assert_(res == VK_SUCCESS);
	if (res != VK_SUCCESS)
		return false;

	vulkan_->SetDebugName(depth_.image, VK_OBJECT_TYPE_IMAGE, "BackbufferDepth");

	TransitionImageLayout2(cmd, depth_.image, 0, 1,
		aspectMask,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

	VkImageViewCreateInfo depth_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	depth_view_info.image = depth_.image;
	depth_view_info.format = depth_format;
	depth_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.subresourceRange.aspectMask = aspectMask;
	depth_view_info.subresourceRange.baseMipLevel = 0;
	depth_view_info.subresourceRange.levelCount = 1;
	depth_view_info.subresourceRange.baseArrayLayer = 0;
	depth_view_info.subresourceRange.layerCount = 1;
	depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depth_view_info.flags = 0;

	VkDevice device = vulkan_->GetDevice();

	res = vkCreateImageView(device, &depth_view_info, NULL, &depth_.view);
	_dbg_assert_(res == VK_SUCCESS);
	if (res != VK_SUCCESS)
		return false;

	return true;
}

static void RemoveDrawCommands(std::vector<VkRenderData> *cmds) {
	// Here we remove any DRAW type commands when we hit a CLEAR.
	for (auto &c : *cmds) {
		if (c.cmd == VKRRenderCommand::DRAW || c.cmd == VKRRenderCommand::DRAW_INDEXED) {
			c.cmd = VKRRenderCommand::REMOVED;
		}
	}
}

static void CleanupRenderCommands(std::vector<VkRenderData> *cmds) {
	size_t lastCommand[(int)VKRRenderCommand::NUM_RENDER_COMMANDS];
	memset(lastCommand, -1, sizeof(lastCommand));

	// Find any duplicate state commands (likely from RemoveDrawCommands.)
	for (size_t i = 0; i < cmds->size(); ++i) {
		auto &c = cmds->at(i);
		auto &lastOfCmd = lastCommand[(uint8_t)c.cmd];

		switch (c.cmd) {
		case VKRRenderCommand::REMOVED:
			continue;

		case VKRRenderCommand::BIND_PIPELINE:
		case VKRRenderCommand::VIEWPORT:
		case VKRRenderCommand::SCISSOR:
		case VKRRenderCommand::BLEND:
		case VKRRenderCommand::STENCIL:
			if (lastOfCmd != -1) {
				cmds->at(lastOfCmd).cmd = VKRRenderCommand::REMOVED;
			}
			break;

		case VKRRenderCommand::PUSH_CONSTANTS:
			// TODO: For now, we have to keep this one (it has an offset.)  Still update lastCommand.
			break;

		case VKRRenderCommand::CLEAR:
			// Ignore, doesn't participate in state.
			continue;

		case VKRRenderCommand::DRAW_INDEXED:
		case VKRRenderCommand::DRAW:
		default:
			// Boundary - must keep state before this.
			memset(lastCommand, -1, sizeof(lastCommand));
			continue;
		}

		lastOfCmd = i;
	}

	// At this point, anything in lastCommand can be cleaned up too.
	// Note that it's safe to remove the last unused PUSH_CONSTANTS here.
	for (size_t i = 0; i < ARRAY_SIZE(lastCommand); ++i) {
		auto &lastOfCmd = lastCommand[i];
		if (lastOfCmd != -1) {
			cmds->at(lastOfCmd).cmd = VKRRenderCommand::REMOVED;
		}
	}
}

void VulkanRenderManager::Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask) {
	_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
	if (!clearMask)
		return;
	// If this is the first drawing command or clears everything, merge it into the pass.
	int allAspects = VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	if (curRenderStep_->render.numDraws == 0 || clearMask == allAspects) {
		curRenderStep_->render.clearColor = clearColor;
		curRenderStep_->render.clearDepth = clearZ;
		curRenderStep_->render.clearStencil = clearStencil;
		curRenderStep_->render.colorLoad = (clearMask & VK_IMAGE_ASPECT_COLOR_BIT) ? VKRRenderPassLoadAction::CLEAR : VKRRenderPassLoadAction::KEEP;
		curRenderStep_->render.depthLoad = (clearMask & VK_IMAGE_ASPECT_DEPTH_BIT) ? VKRRenderPassLoadAction::CLEAR : VKRRenderPassLoadAction::KEEP;
		curRenderStep_->render.stencilLoad = (clearMask & VK_IMAGE_ASPECT_STENCIL_BIT) ? VKRRenderPassLoadAction::CLEAR : VKRRenderPassLoadAction::KEEP;

		// In case there were commands already.
		curRenderStep_->render.numDraws = 0;
		RemoveDrawCommands(&curRenderStep_->commands);
	} else {
		VkRenderData data{ VKRRenderCommand::CLEAR };
		data.clear.clearColor = clearColor;
		data.clear.clearZ = clearZ;
		data.clear.clearStencil = clearStencil;
		data.clear.clearMask = clearMask;
		curRenderStep_->commands.push_back(data);
	}

	curRenderArea_.SetRect(0, 0, curWidth_, curHeight_);
}

void VulkanRenderManager::CopyFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkOffset2D dstPos, VkImageAspectFlags aspectMask, const char *tag) {
	_dbg_assert_msg_(srcRect.offset.x >= 0, "srcrect offset x (%d) < 0", srcRect.offset.x);
	_dbg_assert_msg_(srcRect.offset.y >= 0, "srcrect offset y (%d) < 0", srcRect.offset.y);
	_dbg_assert_msg_(srcRect.offset.x + srcRect.extent.width <= (uint32_t)src->width, "srcrect offset x (%d) + extent (%d) > width (%d)", srcRect.offset.x, srcRect.extent.width, (uint32_t)src->width);
	_dbg_assert_msg_(srcRect.offset.y + srcRect.extent.height <= (uint32_t)src->height, "srcrect offset y (%d) + extent (%d) > height (%d)", srcRect.offset.y, srcRect.extent.height, (uint32_t)src->height);

	_dbg_assert_msg_(srcRect.extent.width > 0, "copy srcwidth == 0");
	_dbg_assert_msg_(srcRect.extent.height > 0, "copy srcheight == 0");

	_dbg_assert_msg_(dstPos.x >= 0, "dstPos offset x (%d) < 0", dstPos.x);
	_dbg_assert_msg_(dstPos.y >= 0, "dstPos offset y (%d) < 0", dstPos.y);
	_dbg_assert_msg_(dstPos.x + srcRect.extent.width <= (uint32_t)dst->width, "dstPos + extent x > width");
	_dbg_assert_msg_(dstPos.y + srcRect.extent.height <= (uint32_t)dst->height, "dstPos + extent y > height");

	for (int i = (int)steps_.size() - 1; i >= 0; i--) {
		if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == src) {
			if (aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
				if (steps_[i]->render.finalColorLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalColorLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				}
			}
			if (aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
				if (steps_[i]->render.finalDepthStencilLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalDepthStencilLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				}
			}
			steps_[i]->render.numReads++;
			break;
		}
	}
	for (int i = (int)steps_.size() - 1; i >= 0; i--) {
		if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == dst) {
			if (aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
				if (steps_[i]->render.finalColorLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalColorLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				}
			}
			if (aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
				if (steps_[i]->render.finalDepthStencilLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalDepthStencilLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				}
			}
			break;
		}
	}

	EndCurRenderStep();

	VKRStep *step = new VKRStep{ VKRStepType::COPY };

	step->copy.aspectMask = aspectMask;
	step->copy.src = src;
	step->copy.srcRect = srcRect;
	step->copy.dst = dst;
	step->copy.dstPos = dstPos;
	step->dependencies.insert(src);
	step->tag = tag;
	bool fillsDst = dst && srcRect.offset.x == 0 && srcRect.offset.y == 0 && srcRect.extent.width == dst->width && srcRect.extent.height == dst->height;
	if (dstPos.x != 0 || dstPos.y != 0 || !fillsDst)
		step->dependencies.insert(dst);

	std::unique_lock<std::mutex> lock(mutex_);
	steps_.push_back(step);
}

void VulkanRenderManager::BlitFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkRect2D dstRect, VkImageAspectFlags aspectMask, VkFilter filter, const char *tag) {
	_dbg_assert_msg_(srcRect.offset.x >= 0, "srcrect offset x (%d) < 0", srcRect.offset.x);
	_dbg_assert_msg_(srcRect.offset.y >= 0, "srcrect offset y (%d) < 0", srcRect.offset.y);
	_dbg_assert_msg_(srcRect.offset.x + srcRect.extent.width <= (uint32_t)src->width, "srcrect offset x (%d) + extent (%d) > width (%d)", srcRect.offset.x, srcRect.extent.width, (uint32_t)src->width);
	_dbg_assert_msg_(srcRect.offset.y + srcRect.extent.height <= (uint32_t)src->height, "srcrect offset y (%d) + extent (%d) > height (%d)", srcRect.offset.y, srcRect.extent.height, (uint32_t)src->height);

	_dbg_assert_msg_(srcRect.extent.width > 0, "blit srcwidth == 0");
	_dbg_assert_msg_(srcRect.extent.height > 0, "blit srcheight == 0");

	_dbg_assert_msg_(dstRect.offset.x >= 0, "dstrect offset x < 0");
	_dbg_assert_msg_(dstRect.offset.y >= 0, "dstrect offset y < 0");
	_dbg_assert_msg_(dstRect.offset.x + dstRect.extent.width <= (uint32_t)dst->width, "dstrect offset x + extent > width");
	_dbg_assert_msg_(dstRect.offset.y + dstRect.extent.height <= (uint32_t)dst->height, "dstrect offset y + extent > height");

	_dbg_assert_msg_(dstRect.extent.width > 0, "blit dstwidth == 0");
	_dbg_assert_msg_(dstRect.extent.height > 0, "blit dstheight == 0");

	// TODO: Seem to be missing final layouts here like in Copy...

	for (int i = (int)steps_.size() - 1; i >= 0; i--) {
		if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == src) {
			steps_[i]->render.numReads++;
			break;
		}
	}

	EndCurRenderStep();

	VKRStep *step = new VKRStep{ VKRStepType::BLIT };

	step->blit.aspectMask = aspectMask;
	step->blit.src = src;
	step->blit.srcRect = srcRect;
	step->blit.dst = dst;
	step->blit.dstRect = dstRect;
	step->blit.filter = filter;
	step->dependencies.insert(src);
	step->tag = tag;
	bool fillsDst = dst && dstRect.offset.x == 0 && dstRect.offset.y == 0 && dstRect.extent.width == dst->width && dstRect.extent.height == dst->height;
	if (!fillsDst)
		step->dependencies.insert(dst);

	std::unique_lock<std::mutex> lock(mutex_);
	steps_.push_back(step);
}

VkImageView VulkanRenderManager::BindFramebufferAsTexture(VKRFramebuffer *fb, int binding, VkImageAspectFlags aspectBit, int attachment) {
	_dbg_assert_(curRenderStep_ != nullptr);
	// Mark the dependency, check for required transitions, and return the image.

	// Optimization: If possible, use final*Layout to put the texture into the correct layout "early".
	for (int i = (int)steps_.size() - 1; i >= 0; i--) {
		if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == fb) {
			if (aspectBit == VK_IMAGE_ASPECT_COLOR_BIT) {
				// If this framebuffer was rendered to earlier in this frame, make sure to pre-transition it to the correct layout.
				if (steps_[i]->render.finalColorLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
				// If we find some other layout, a copy after this is likely involved. It's fine though,
				// we'll just transition it right as we need it and lose a tiny optimization.
			} else if (aspectBit == VK_IMAGE_ASPECT_DEPTH_BIT) {
				// If this framebuffer was rendered to earlier in this frame, make sure to pre-transition it to the correct layout.
				if (steps_[i]->render.finalDepthStencilLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalDepthStencilLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}
			}  // We don't (yet?) support texturing from stencil images.
			steps_[i]->render.numReads++;
			break;
		}
	}

	// Track dependencies fully.
	curRenderStep_->dependencies.insert(fb);

	if (!curRenderStep_->preTransitions.empty() &&
		curRenderStep_->preTransitions.back().fb == fb &&
		curRenderStep_->preTransitions.back().targetLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		// We're done.
		return aspectBit == VK_IMAGE_ASPECT_COLOR_BIT ? fb->color.imageView : fb->depth.depthSampleView;
	} else {
		curRenderStep_->preTransitions.push_back({ aspectBit, fb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		return aspectBit == VK_IMAGE_ASPECT_COLOR_BIT ? fb->color.imageView : fb->depth.depthSampleView;
	}
}

void VulkanRenderManager::Finish() {
	EndCurRenderStep();

	// Let's do just a bit of cleanup on render commands now.
	for (auto &step : steps_) {
		if (step->stepType == VKRStepType::RENDER) {
			CleanupRenderCommands(&step->commands);
		}
	}

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	if (!useThread_) {
		frameData.steps = std::move(steps_);
		steps_.clear();
		frameData.type = VKRRunType::END;
		Run(curFrame);
	} else {
		std::unique_lock<std::mutex> lock(frameData.pull_mutex);
		VLOG("PUSH: Frame[%d].readyForRun = true", curFrame);
		frameData.steps = std::move(steps_);
		steps_.clear();
		frameData.readyForRun = true;
		frameData.type = VKRRunType::END;
		frameData.pull_condVar.notify_all();
	}
	vulkan_->EndFrame();

	insideFrame_ = false;
}

void VulkanRenderManager::Wipe() {
	for (auto step : steps_) {
		delete step;
	}
	steps_.clear();
}

// Can be called multiple times with no bad side effects. This is so that we can either begin a frame the normal way,
// or stop it in the middle for a synchronous readback, then start over again mostly normally but without repeating
// the backbuffer image acquisition.
void VulkanRenderManager::BeginSubmitFrame(int frame) {
	FrameData &frameData = frameData_[frame];
	if (!frameData.hasBegun) {
		// Get the index of the next available swapchain image, and a semaphore to block command buffer execution on.
		VkResult res = vkAcquireNextImageKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), UINT64_MAX, acquireSemaphore_, (VkFence)VK_NULL_HANDLE, &frameData.curSwapchainImage);
		if (res == VK_SUBOPTIMAL_KHR) {
			// Hopefully the resize will happen shortly. Ignore - one frame might look bad or something.
			WARN_LOG(G3D, "VK_SUBOPTIMAL_KHR returned - ignoring");
		} else if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			WARN_LOG(G3D, "VK_ERROR_OUT_OF_DATE_KHR returned - processing the frame, but not presenting");
			frameData.skipSwap = true;
		} else {
			_assert_msg_(res == VK_SUCCESS, "vkAcquireNextImageKHR failed! result=%s", VulkanResultToString(res));
		}

		vkResetCommandPool(vulkan_->GetDevice(), frameData.cmdPoolMain, 0);
		VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		res = vkBeginCommandBuffer(frameData.mainCmd, &begin);

		_assert_msg_(res == VK_SUCCESS, "vkBeginCommandBuffer failed! result=%s", VulkanResultToString(res));

		queueRunner_.SetBackbuffer(framebuffers_[frameData.curSwapchainImage], swapchainImages_[frameData.curSwapchainImage].image);

		frameData.hasBegun = true;
	}
}

void VulkanRenderManager::Submit(int frame, bool triggerFrameFence) {
	FrameData &frameData = frameData_[frame];
	if (frameData.hasInitCommands) {
		if (frameData.profilingEnabled_ && triggerFrameFence) {
			// Pre-allocated query ID 1.
			vkCmdWriteTimestamp(frameData.initCmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frameData.profile.queryPool, 1);
		}
		VkResult res = vkEndCommandBuffer(frameData.initCmd);
		_assert_msg_(res == VK_SUCCESS, "vkEndCommandBuffer failed (init)! result=%s", VulkanResultToString(res));
	}

	VkResult res = vkEndCommandBuffer(frameData.mainCmd);
	_assert_msg_(res == VK_SUCCESS, "vkEndCommandBuffer failed (main)! result=%s", VulkanResultToString(res));

	VkCommandBuffer cmdBufs[2];
	int numCmdBufs = 0;
	if (frameData.hasInitCommands) {
		cmdBufs[numCmdBufs++] = frameData.initCmd;
		if (splitSubmit_) {
			// Send the init commands off separately. Used this once to confirm that the cause of a device loss was in the init cmdbuf.
			VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			submit_info.commandBufferCount = (uint32_t)numCmdBufs;
			submit_info.pCommandBuffers = cmdBufs;
			res = vkQueueSubmit(vulkan_->GetGraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
			if (res == VK_ERROR_DEVICE_LOST) {
				_assert_msg_(false, "Lost the Vulkan device in split submit! If this happens again, switch Graphics Backend away from Vulkan");
			} else {
				_assert_msg_(res == VK_SUCCESS, "vkQueueSubmit failed (init)! result=%s", VulkanResultToString(res));
			}
			numCmdBufs = 0;
		}
	}
	cmdBufs[numCmdBufs++] = frameData.mainCmd;

	VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	VkPipelineStageFlags waitStage[1]{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	if (triggerFrameFence && !frameData.skipSwap) {
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &acquireSemaphore_;
		submit_info.pWaitDstStageMask = waitStage;
	}
	submit_info.commandBufferCount = (uint32_t)numCmdBufs;
	submit_info.pCommandBuffers = cmdBufs;
	if (triggerFrameFence && !frameData.skipSwap) {
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &renderingCompleteSemaphore_;
	}
	res = vkQueueSubmit(vulkan_->GetGraphicsQueue(), 1, &submit_info, triggerFrameFence ? frameData.fence : frameData.readbackFence);
	if (res == VK_ERROR_DEVICE_LOST) {
		_assert_msg_(false, "Lost the Vulkan device in vkQueueSubmit! If this happens again, switch Graphics Backend away from Vulkan");
	} else {
		_assert_msg_(res == VK_SUCCESS, "vkQueueSubmit failed (main, split=%d)! result=%s", (int)splitSubmit_, VulkanResultToString(res));
	}

	// When !triggerFence, we notify after syncing with Vulkan.
	if (useThread_ && triggerFrameFence) {
		VLOG("PULL: Frame %d.readyForFence = true", frame);
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		frameData.readyForFence = true;
		frameData.push_condVar.notify_all();
	}

	frameData.hasInitCommands = false;
}

void VulkanRenderManager::EndSubmitFrame(int frame) {
	FrameData &frameData = frameData_[frame];
	frameData.hasBegun = false;

	Submit(frame, true);

	if (!frameData.skipSwap) {
		VkSwapchainKHR swapchain = vulkan_->GetSwapchain();
		VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		present.swapchainCount = 1;
		present.pSwapchains = &swapchain;
		present.pImageIndices = &frameData.curSwapchainImage;
		present.pWaitSemaphores = &renderingCompleteSemaphore_;
		present.waitSemaphoreCount = 1;

		VkResult res = vkQueuePresentKHR(vulkan_->GetGraphicsQueue(), &present);
		if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			// We clearly didn't get this in vkAcquireNextImageKHR because of the skipSwap check above.
			// Do the increment.
			outOfDateFrames_++;
		} else if (res == VK_SUBOPTIMAL_KHR) {
			outOfDateFrames_++;
		} else if (res != VK_SUCCESS) {
			_assert_msg_(false, "vkQueuePresentKHR failed! result=%s", VulkanResultToString(res));
		} else {
			// Success
			outOfDateFrames_ = 0;
		}
	} else {
		// We only get here if vkAcquireNextImage returned VK_ERROR_OUT_OF_DATE.
		outOfDateFrames_++;
		frameData.skipSwap = false;
	}
}

void VulkanRenderManager::Run(int frame) {
	BeginSubmitFrame(frame);

	FrameData &frameData = frameData_[frame];
	auto &stepsOnThread = frameData_[frame].steps;
	VkCommandBuffer cmd = frameData.mainCmd;
	queueRunner_.PreprocessSteps(stepsOnThread);
	//queueRunner_.LogSteps(stepsOnThread, false);
	queueRunner_.RunSteps(cmd, stepsOnThread, frameData.profilingEnabled_ ? &frameData.profile : nullptr);
	stepsOnThread.clear();

	switch (frameData.type) {
	case VKRRunType::END:
		EndSubmitFrame(frame);
		break;

	case VKRRunType::SYNC:
		EndSyncFrame(frame);
		break;

	default:
		_dbg_assert_(false);
	}

	VLOG("PULL: Finished running frame %d", frame);
}

void VulkanRenderManager::EndSyncFrame(int frame) {
	FrameData &frameData = frameData_[frame];

	frameData.readbackFenceUsed = true;

	// The submit will trigger the readbackFence.
	Submit(frame, false);

	// Hard stall of the GPU, not ideal, but necessary so the CPU has the contents of the readback.
	vkWaitForFences(vulkan_->GetDevice(), 1, &frameData.readbackFence, true, UINT64_MAX);
	vkResetFences(vulkan_->GetDevice(), 1, &frameData.readbackFence);

	// At this point we can resume filling the command buffers for the current frame since
	// we know the device is idle - and thus all previously enqueued command buffers have been processed.
	// No need to switch to the next frame number.
	VkCommandBufferBeginInfo begin{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		nullptr,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkResetCommandPool(vulkan_->GetDevice(), frameData.cmdPoolMain, 0);
	VkResult res = vkBeginCommandBuffer(frameData.mainCmd, &begin);
	_assert_(res == VK_SUCCESS);

	if (useThread_) {
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		frameData.readyForFence = true;
		frameData.push_condVar.notify_all();
	}
}

void VulkanRenderManager::FlushSync() {
	renderStepOffset_ += (int)steps_.size();

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	if (!useThread_) {
		frameData.steps = std::move(steps_);
		steps_.clear();
		frameData.type = VKRRunType::SYNC;
		Run(curFrame);
	} else {
		std::unique_lock<std::mutex> lock(frameData.pull_mutex);
		VLOG("PUSH: Frame[%d].readyForRun = true (sync)", curFrame);
		frameData.steps = std::move(steps_);
		steps_.clear();
		frameData.readyForRun = true;
		_dbg_assert_(!frameData.readyForFence);
		frameData.type = VKRRunType::SYNC;
		frameData.pull_condVar.notify_all();
	}

	if (useThread_) {
		std::unique_lock<std::mutex> lock(frameData.push_mutex);
		// Wait for the flush to be hit, since we're syncing.
		while (!frameData.readyForFence) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (sync)", curFrame);
			frameData.push_condVar.wait(lock);
		}
		frameData.readyForFence = false;
	}
}
