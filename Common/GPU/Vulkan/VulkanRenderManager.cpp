#include <algorithm>
#include <cstdint>

#include <sstream>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#include "Common/GPU/Vulkan/VulkanAlloc.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"

#include "Common/Thread/ThreadUtil.h"

#if 0 // def _DEBUG
#define VLOG(...) NOTICE_LOG(G3D, __VA_ARGS__)
#else
#define VLOG(...)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

using namespace PPSSPP_VK;

// renderPass is an example of the "compatibility class" or RenderPassType type.
bool VKRGraphicsPipeline::Create(VulkanContext *vulkan, VkRenderPass compatibleRenderPass, RenderPassType rpType) {
	// Fill in the last part of the desc since now it's time to block.
	VkShaderModule vs = desc->vertexShader->BlockUntilReady();
	VkShaderModule fs = desc->fragmentShader->BlockUntilReady();

	if (!vs || !fs) {
		ERROR_LOG(G3D, "Failed creating graphics pipeline - missing shader modules");
		// We're kinda screwed here?
		return false;
	}

	VkPipelineShaderStageCreateInfo ss[2]{};
	ss[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	ss[0].pSpecializationInfo = nullptr;
	ss[0].module = vs;
	ss[0].pName = "main";
	ss[0].flags = 0;
	ss[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	ss[1].pSpecializationInfo = nullptr;
	ss[1].module = fs;
	ss[1].pName = "main";
	ss[1].flags = 0;

	VkGraphicsPipelineCreateInfo pipe{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipe.pStages = ss;
	pipe.stageCount = 2;
	pipe.renderPass = compatibleRenderPass;
	pipe.basePipelineIndex = 0;
	pipe.pColorBlendState = &desc->cbs;
	pipe.pDepthStencilState = &desc->dss;
	pipe.pRasterizationState = &desc->rs;

	// We will use dynamic viewport state.
	pipe.pVertexInputState = &desc->vis;
	pipe.pViewportState = &desc->views;
	pipe.pTessellationState = nullptr;
	pipe.pDynamicState = &desc->ds;
	pipe.pInputAssemblyState = &desc->inputAssembly;
	pipe.pMultisampleState = &desc->ms;
	pipe.layout = desc->pipelineLayout;
	pipe.basePipelineHandle = VK_NULL_HANDLE;
	pipe.basePipelineIndex = 0;
	pipe.subpass = 0;

	double start = time_now_d();
	VkPipeline vkpipeline;
	VkResult result = vkCreateGraphicsPipelines(vulkan->GetDevice(), desc->pipelineCache, 1, &pipe, nullptr, &vkpipeline);

	INFO_LOG(G3D, "Pipeline creation time: %0.2f ms", (time_now_d() - start) * 1000.0);

	bool success = true;
	if (result == VK_INCOMPLETE) {
		// Bad (disallowed by spec) return value seen on Adreno in Burnout :(  Try to ignore?
		// Would really like to log more here, we could probably attach more info to desc.
		//
		// At least create a null placeholder to avoid creating over and over if something is broken.
		pipeline[rpType]->Post(VK_NULL_HANDLE);
		success = false;
	} else if (result != VK_SUCCESS) {
		pipeline[rpType]->Post(VK_NULL_HANDLE);
		ERROR_LOG(G3D, "Failed creating graphics pipeline! result='%s'", VulkanResultToString(result));
		success = false;
	} else {
		// Success!
		if (!tag.empty()) {
			vulkan->SetDebugName(vkpipeline, VK_OBJECT_TYPE_PIPELINE, tag.c_str());
		}
		pipeline[rpType]->Post(vkpipeline);
	}

	return success;
}

void VKRGraphicsPipeline::QueueForDeletion(VulkanContext *vulkan) {
	for (int i = 0; i < RP_TYPE_COUNT; i++) {
		if (!pipeline[i])
			continue;
		VkPipeline pipeline = this->pipeline[i]->BlockUntilReady();
		vulkan->Delete().QueueDeletePipeline(pipeline);
	}
	vulkan->Delete().QueueCallback([](void *p) {
		VKRGraphicsPipeline *pipeline = (VKRGraphicsPipeline *)p;
		delete pipeline;
	}, this);
}

u32 VKRGraphicsPipeline::GetVariantsBitmask() const {
	u32 bitmask = 0;
	for (int i = 0; i < RP_TYPE_COUNT; i++) {
		if (pipeline[i]) {
			bitmask |= 1 << i;
		}
	}
	return bitmask;
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
		pipeline->Post(VK_NULL_HANDLE);
		ERROR_LOG(G3D, "Failed creating compute pipeline! result='%s'", VulkanResultToString(result));
		success = false;
	} else {
		pipeline->Post(vkpipeline);
	}

	delete desc;
	desc = nullptr;
	return success;
}

VKRFramebuffer::VKRFramebuffer(VulkanContext *vk, VkCommandBuffer initCmd, VKRRenderPass *compatibleRenderPass, int _width, int _height, bool createDepthStencilBuffer, const char *tag) : vulkan_(vk), tag_(tag) {
	width = _width;
	height = _height;

	_dbg_assert_(tag);

	CreateImage(vulkan_, initCmd, color, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, tag);
	vulkan_->SetDebugName(color.image, VK_OBJECT_TYPE_IMAGE, StringFromFormat("fb_color_%s", tag).c_str());
	if (createDepthStencilBuffer) {
		CreateImage(vulkan_, initCmd, depth, width, height, vulkan_->GetDeviceInfo().preferredDepthStencilFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false, tag);
		vulkan_->SetDebugName(depth.image, VK_OBJECT_TYPE_IMAGE, StringFromFormat("fb_depth_%s", tag).c_str());
	}

	// We create the actual framebuffer objects on demand, because some combinations might not make sense.
	// Framebuffer objects are just pointers to a set of images, so no biggie.
}

VkFramebuffer VKRFramebuffer::Get(VKRRenderPass *compatibleRenderPass, RenderPassType rpType) {
	if (framebuf[(int)rpType]) {
		return framebuf[(int)rpType];
	}

	VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	VkImageView views[2]{};

	bool hasDepth = rpType == RP_TYPE_BACKBUFFER || rpType == RP_TYPE_COLOR_DEPTH || rpType == RP_TYPE_COLOR_DEPTH_INPUT;

	views[0] = color.imageView;
	if (hasDepth) {
		_dbg_assert_(depth.imageView != VK_NULL_HANDLE);
		views[1] = depth.imageView;
	}
	fbci.renderPass = compatibleRenderPass->Get(vulkan_, rpType);
	fbci.attachmentCount = hasDepth ? 2 : 1;
	fbci.pAttachments = views;
	fbci.width = width;
	fbci.height = height;
	fbci.layers = 1;

	VkResult res = vkCreateFramebuffer(vulkan_->GetDevice(), &fbci, nullptr, &framebuf[(int)rpType]);
	_assert_(res == VK_SUCCESS);

	if (!tag_.empty() && vulkan_->Extensions().EXT_debug_utils) {
		vulkan_->SetDebugName(framebuf[(int)rpType], VK_OBJECT_TYPE_FRAMEBUFFER, StringFromFormat("fb_%s", tag_.c_str()).c_str());
	}

	return framebuf[(int)rpType];
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
	for (auto &fb : framebuf) {
		if (fb)
			vulkan_->Delete().QueueDeleteFramebuffer(fb);
	}
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
		ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
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
	inflightFramesAtStart_ = vulkan_->GetInflightFrames();

	frameDataShared_.Init(vulkan);

	for (int i = 0; i < inflightFramesAtStart_; i++) {
		frameData_[i].Init(vulkan, i);
	}

	queueRunner_.CreateDeviceObjects();
}

bool VulkanRenderManager::CreateBackbuffers() {
	if (!vulkan_->GetSwapchain()) {
		ERROR_LOG(G3D, "No swapchain - can't create backbuffers");
		return false;
	}


	VkCommandBuffer cmdInit = GetInitCmd();

	if (!queueRunner_.CreateSwapchain(cmdInit)) {
		return false;
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
	if (HasBackbuffers()) {
		run_ = true;  // For controlling the compiler thread's exit

		INFO_LOG(G3D, "Starting Vulkan submission thread");
		thread_ = std::thread(&VulkanRenderManager::ThreadFunc, this);
		INFO_LOG(G3D, "Starting Vulkan compiler thread");
		compileThread_ = std::thread(&VulkanRenderManager::CompileThreadFunc, this);
	}
	return true;
}

// Called from main thread.
void VulkanRenderManager::StopThread() {
	{
		// Tell the render thread to quit when it's done.
		VKRRenderThreadTask task;
		task.frame = vulkan_->GetCurFrame();
		task.runType = VKRRunType::EXIT;
		std::unique_lock<std::mutex> lock(pushMutex_);
		renderThreadQueue_.push(task);
		pushCondVar_.notify_one();
	}

	// Compiler thread still relies on this.
	run_ = false;

	// Stop the thread.
	thread_.join();

	for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
		auto &frameData = frameData_[i];
		// Zero the queries so we don't try to pull them later.
		frameData.profile.timestampDescriptions.clear();
	}

	INFO_LOG(G3D, "Vulkan submission thread joined. Frame=%d", vulkan_->GetCurFrame());

	compileCond_.notify_all();
	compileThread_.join();
	INFO_LOG(G3D, "Vulkan compiler thread joined.");

	// Eat whatever has been queued up for this frame if anything.
	Wipe();

	// Clean out any remaining queued data, which might refer to things that might not be valid
	// when we restart the thread...

	// Not sure if this is still needed
	for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
		auto &frameData = frameData_[i];
		if (frameData.hasInitCommands) {
			// Clear 'em out.  This can happen on restart sometimes.
			vkEndCommandBuffer(frameData.initCmd);
			frameData.hasInitCommands = false;
		}
		if (frameData.hasMainCommands) {
			vkEndCommandBuffer(frameData.mainCmd);
			frameData.hasMainCommands = false;
		}
		if (frameData.hasPresentCommands) {
			vkEndCommandBuffer(frameData.presentCmd);
			frameData.hasPresentCommands = false;
		}
	}
}

void VulkanRenderManager::DestroyBackbuffers() {
	StopThread();
	vulkan_->WaitUntilQueueIdle();

	queueRunner_.DestroyBackBuffers();
}

VulkanRenderManager::~VulkanRenderManager() {
	INFO_LOG(G3D, "VulkanRenderManager destructor");

	_dbg_assert_(!run_);  // StopThread should already have been called from DestroyBackbuffers.

	vulkan_->WaitUntilQueueIdle();

	DrainCompileQueue();
	VkDevice device = vulkan_->GetDevice();
	frameDataShared_.Destroy(vulkan_);
	for (int i = 0; i < inflightFramesAtStart_; i++) {
		frameData_[i].Destroy(vulkan_);
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

		if (!toCompile.empty()) {
			INFO_LOG(G3D, "Compilation thread has %d pipelines to create", (int)toCompile.size());
		}

		// TODO: Here we can sort the pending pipelines by vertex and fragment shaders,
		// and split up further.
		// Those with the same pairs of shaders should be on the same thread.
		for (auto &entry : toCompile) {
			switch (entry.type) {
			case CompileQueueEntry::Type::GRAPHICS:
				entry.graphics->Create(vulkan_, entry.compatibleRenderPass, entry.renderPassType);
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
	compileCond_.notify_all();
	while (!compileQueue_.empty()) {
		queueRunner_.WaitForCompileNotification();
	}
}

void VulkanRenderManager::ThreadFunc() {
	SetCurrentThreadName("RenderMan");
	while (true) {
		// Pop a task of the queue and execute it.
		VKRRenderThreadTask task;
		{
			std::unique_lock<std::mutex> lock(pushMutex_);
			while (renderThreadQueue_.empty()) {
				pushCondVar_.wait(lock);
			}
			task = renderThreadQueue_.front();
			renderThreadQueue_.pop();
		}

		// Oh, we got a task! We can now have pushMutex_ unlocked, allowing the host to
		// push more work when it feels like it, and just start working.
		if (task.runType == VKRRunType::EXIT) {
			// Oh, host wanted out. Let's leave.
			break;
		}

		Run(task);
	}

	// Wait for the device to be done with everything, before tearing stuff down.
	// TODO: Do we need this?
	vkDeviceWaitIdle(vulkan_->GetDevice());

	VLOG("PULL: Quitting");
}

void VulkanRenderManager::BeginFrame(bool enableProfiling, bool enableLogProfiler) {
	VLOG("BeginFrame");
	VkDevice device = vulkan_->GetDevice();

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];

	VLOG("PUSH: Fencing %d", curFrame);

	// Makes sure the submission from the previous time around has happened. Otherwise
	// we are not allowed to wait from another thread here..
	{
		std::unique_lock<std::mutex> lock(frameData.fenceMutex);
		while (!frameData.readyForFence) {
			frameData.fenceCondVar.wait(lock);
		}
		frameData.readyForFence = false;
	}

	// This must be the very first Vulkan call we do in a new frame.
	// Makes sure the very last command buffer from the frame before the previous has been fully executed.
	if (vkWaitForFences(device, 1, &frameData.fence, true, UINT64_MAX) == VK_ERROR_DEVICE_LOST) {
		_assert_msg_(false, "Device lost in vkWaitForFences");
	}
	vkResetFences(device, 1, &frameData.fence);

	// Can't set this until after the fence.
	frameData.profilingEnabled_ = enableProfiling;

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
	return frameData_[curFrame].GetInitCmd(vulkan_);
}

VKRGraphicsPipeline *VulkanRenderManager::CreateGraphicsPipeline(VKRGraphicsPipelineDesc *desc, PipelineFlags pipelineFlags, uint32_t variantBitmask, const char *tag) {
	VKRGraphicsPipeline *pipeline = new VKRGraphicsPipeline();
	_dbg_assert_(desc->vertexShader);
	_dbg_assert_(desc->fragmentShader);
	pipeline->desc = desc;
	pipeline->tag = tag;
	if (curRenderStep_) {
		// The common case
		pipelinesToCheck_.push_back(pipeline);
	} else {
		if (!variantBitmask) {
			WARN_LOG(G3D, "WARNING: Will not compile any variants of pipeline, not in renderpass and empty variantBitmask");
		}
		// Presumably we're in initialization, loading the shader cache.
		// Look at variantBitmask to see what variants we should queue up.
		RPKey key{
			VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR,
			VKRRenderPassStoreAction::STORE, VKRRenderPassStoreAction::DONT_CARE, VKRRenderPassStoreAction::DONT_CARE,
		};
		VKRRenderPass *compatibleRenderPass = queueRunner_.GetRenderPass(key);
		compileMutex_.lock();
		bool needsCompile = false;
		for (int i = 0; i < RP_TYPE_COUNT; i++) {
			if (!(variantBitmask & (1 << i)))
				continue;
			RenderPassType rpType = (RenderPassType)i;

			// Sanity check - don't compile incompatible types (could be caused by corrupt caches, changes in data structures, etc).
			if (pipelineFlags & PipelineFlags::USES_DEPTH_STENCIL) {
				if (!RenderPassTypeHasDepth(rpType)) {
					WARN_LOG(G3D, "Not compiling pipeline that requires depth, for non depth renderpass type");
					continue;
				}
			}
			if (pipelineFlags & PipelineFlags::USES_INPUT_ATTACHMENT) {
				if (!RenderPassTypeHasInput(rpType)) {
					WARN_LOG(G3D, "Not compiling pipeline that requires input attachment, for non input renderpass type");
					continue;
				}
			}

			pipeline->pipeline[rpType] = Promise<VkPipeline>::CreateEmpty();
			compileQueue_.push_back(CompileQueueEntry(pipeline, compatibleRenderPass->Get(vulkan_, rpType), rpType));
			needsCompile = true;
		}
		if (needsCompile)
			compileCond_.notify_one();
		compileMutex_.unlock();
	}
	return pipeline;
}

VKRComputePipeline *VulkanRenderManager::CreateComputePipeline(VKRComputePipelineDesc *desc) {
	VKRComputePipeline *pipeline = new VKRComputePipeline();
	pipeline->desc = desc;
	compileMutex_.lock();
	compileQueue_.push_back(CompileQueueEntry(pipeline));
	compileCond_.notify_one();
	compileMutex_.unlock();
	return pipeline;
}

void VulkanRenderManager::EndCurRenderStep() {
	if (!curRenderStep_)
		return;

	RPKey key{
		curRenderStep_->render.colorLoad, curRenderStep_->render.depthLoad, curRenderStep_->render.stencilLoad,
		curRenderStep_->render.colorStore, curRenderStep_->render.depthStore, curRenderStep_->render.stencilStore,
	};
	// Save the accumulated pipeline flags so we can use that to configure the render pass.
	// We'll often be able to avoid loading/saving the depth/stencil buffer.
	curRenderStep_->render.pipelineFlags = curPipelineFlags_;
	bool depthStencil = (curPipelineFlags_ & PipelineFlags::USES_DEPTH_STENCIL) != 0;
	RenderPassType rpType = depthStencil ? RP_TYPE_COLOR_DEPTH : RP_TYPE_COLOR;
	if (!curRenderStep_->render.framebuffer) {
		rpType = RP_TYPE_BACKBUFFER;
	} else if (curPipelineFlags_ & PipelineFlags::USES_INPUT_ATTACHMENT) {
		// Not allowed on backbuffers.
		rpType = depthStencil ? RP_TYPE_COLOR_DEPTH_INPUT : RP_TYPE_COLOR_INPUT;
	}
	// TODO: Also add render pass types for depth/stencil-less.

	VKRRenderPass *renderPass = queueRunner_.GetRenderPass(key);
	curRenderStep_->render.renderPassType = rpType;

	compileMutex_.lock();
	bool needsCompile = false;
	for (VKRGraphicsPipeline *pipeline : pipelinesToCheck_) {
		if (!pipeline->pipeline[rpType]) {
			pipeline->pipeline[rpType] = Promise<VkPipeline>::CreateEmpty();
			compileQueue_.push_back(CompileQueueEntry(pipeline, renderPass->Get(vulkan_, rpType), rpType));
			needsCompile = true;
		}
	}
	if (needsCompile)
		compileCond_.notify_one();
	compileMutex_.unlock();
	pipelinesToCheck_.clear();

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
	curPipelineFlags_ = (PipelineFlags)0;
}

void VulkanRenderManager::BindCurrentFramebufferAsInputAttachment0(VkImageAspectFlags aspectBits) {
	_dbg_assert_(curRenderStep_);
	curRenderStep_->commands.push_back(VkRenderData{ VKRRenderCommand::SELF_DEPENDENCY_BARRIER });
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
			curPipelineFlags_ |= PipelineFlags::USES_DEPTH_STENCIL;
		}
		if (stencil == VKRRenderPassLoadAction::CLEAR) {
			clearMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			curPipelineFlags_ |= PipelineFlags::USES_DEPTH_STENCIL;
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
	step->render.colorStore = VKRRenderPassStoreAction::STORE;
	step->render.depthStore = VKRRenderPassStoreAction::STORE;
	step->render.stencilStore = VKRRenderPassStoreAction::STORE;
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

		if (clearMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
			curPipelineFlags_ |= PipelineFlags::USES_DEPTH_STENCIL;
		}

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
		curRenderStep_->preTransitions.push_back({ fb, aspectBit, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
		return aspectBit == VK_IMAGE_ASPECT_COLOR_BIT ? fb->color.imageView : fb->depth.depthSampleView;
	}
}

// Called on main thread.
// Sends the collected commands to the render thread. Submit-latency should be
// measured from here, probably.
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

	{
		VLOG("PUSH: Frame[%d]", curFrame);
		VKRRenderThreadTask task;
		task.frame = curFrame;
		task.runType = VKRRunType::PRESENT;
		std::unique_lock<std::mutex> lock(pushMutex_);
		renderThreadQueue_.push(task);
		renderThreadQueue_.back().steps = std::move(steps_);
		pushCondVar_.notify_one();
	}

	steps_.clear();
	vulkan_->EndFrame();
	insideFrame_ = false;
}

void VulkanRenderManager::Wipe() {
	for (auto step : steps_) {
		delete step;
	}
	steps_.clear();
}

// Called on the render thread.
//
// Can be called again after a VKRRunType::SYNC on the same frame.
void VulkanRenderManager::Run(VKRRenderThreadTask &task) {
	FrameData &frameData = frameData_[task.frame];

	_dbg_assert_(!frameData.hasPresentCommands);
	frameData.SubmitPending(vulkan_, FrameSubmitType::Pending, frameDataShared_);

	if (!frameData.hasMainCommands) {
		// Effectively resets both main and present command buffers, since they both live in this pool.
		// We always record main commands first, so we don't need to reset the present command buffer separately.
		vkResetCommandPool(vulkan_->GetDevice(), frameData.cmdPoolMain, 0);

		VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VkResult res = vkBeginCommandBuffer(frameData.mainCmd, &begin);
		frameData.hasMainCommands = true;
		_assert_msg_(res == VK_SUCCESS, "vkBeginCommandBuffer failed! result=%s", VulkanResultToString(res));
	}

	queueRunner_.PreprocessSteps(task.steps);
	// Likely during shutdown, happens in headless.
	if (task.steps.empty() && !frameData.hasAcquired)
		frameData.skipSwap = true;
	//queueRunner_.LogSteps(stepsOnThread, false);
	queueRunner_.RunSteps(task.steps, frameData, frameDataShared_);

	switch (task.runType) {
	case VKRRunType::PRESENT:
		frameData.SubmitPending(vulkan_, FrameSubmitType::Present, frameDataShared_);

		if (!frameData.skipSwap) {
			VkResult res = frameData.QueuePresent(vulkan_, frameDataShared_);
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
		break;

	case VKRRunType::SYNC:
		// The submit will trigger the readbackFence, and also do the wait for it.
		frameData.SubmitPending(vulkan_, FrameSubmitType::Sync, frameDataShared_);

		{
			std::unique_lock<std::mutex> lock(syncMutex_);
			syncCondVar_.notify_one();
		}

		// At this point the GPU is idle, and we can resume filling the command buffers for the
		// current frame since and thus all previously enqueued command buffers have been
		// processed. No need to switch to the next frame number, would just be confusing.
		break;

	default:
		_dbg_assert_(false);
	}

	VLOG("PULL: Finished running frame %d", task.frame);
}

// Called from main thread.
void VulkanRenderManager::FlushSync() {
	renderStepOffset_ += (int)steps_.size();

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	
	{
		VLOG("PUSH: Frame[%d]", curFrame);
		VKRRenderThreadTask task;
		task.frame = curFrame;
		task.runType = VKRRunType::SYNC;
		std::unique_lock<std::mutex> lock(pushMutex_);
		renderThreadQueue_.push(task);
		renderThreadQueue_.back().steps = std::move(steps_);
		pushCondVar_.notify_one();
	}

	{
		std::unique_lock<std::mutex> lock(syncMutex_);
		// Wait for the flush to be hit, since we're syncing.
		while (!frameData.syncDone) {
			VLOG("PUSH: Waiting for frame[%d].readyForFence = 1 (sync)", curFrame);
			syncCondVar_.wait(lock);
		}
		frameData.syncDone = false;
	}
}
