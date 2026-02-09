#include <cstdint>

#include <map>
#include <sstream>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#include "Common/GPU/Vulkan/VulkanAlloc.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"

#include "Common/LogReporting.h"
#include "Common/Thread/ThreadUtil.h"

#if 0 // def _DEBUG
#define VLOG(...) NOTICE_LOG(Log::G3D, __VA_ARGS__)
#else
#define VLOG(...)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

using namespace PPSSPP_VK;

// renderPass is an example of the "compatibility class" or RenderPassType type.
bool VKRGraphicsPipeline::Create(VulkanContext *vulkan, VkRenderPass compatibleRenderPass, RenderPassType rpType, VkSampleCountFlagBits sampleCount, double scheduleTime, int countToCompile) {
	// Good torture test to test the shutdown-while-precompiling-shaders issue on PC where it's normally
	// hard to catch because shaders compile so fast.
	// sleep_ms(200);

	bool multisample = RenderPassTypeHasMultisample(rpType);
	if (multisample) {
		if (sampleCount_ != VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM) {
			_assert_(sampleCount == sampleCount_);
		} else {
			sampleCount_ = sampleCount;
		}
	}

	// Sanity check.
	// Seen in crash reports from PowerVR GE8320, presumably we failed creating some shader modules.
	if (!desc->vertexShader || !desc->fragmentShader) {
		ERROR_LOG(Log::G3D, "Failed creating graphics pipeline - missing vs/fs shader module pointers!");
		pipeline[(size_t)rpType]->Post(VK_NULL_HANDLE);
		return false;
	}

	// Fill in the last part of the desc since now it's time to block.
	VkShaderModule vs = desc->vertexShader->BlockUntilReady();
	VkShaderModule fs = desc->fragmentShader->BlockUntilReady();
	VkShaderModule gs = desc->geometryShader ? desc->geometryShader->BlockUntilReady() : VK_NULL_HANDLE;

	if (!vs || !fs || (!gs && desc->geometryShader)) {
		ERROR_LOG(Log::G3D, "Failed creating graphics pipeline - missing shader modules");
		pipeline[(size_t)rpType]->Post(VK_NULL_HANDLE);
		return false;
	}

	if (!compatibleRenderPass) {
		ERROR_LOG(Log::G3D, "Failed creating graphics pipeline - compatible render pass was nullptr");
		pipeline[(size_t)rpType]->Post(VK_NULL_HANDLE);
		return false;
	}

	uint32_t stageCount = 2;
	VkPipelineShaderStageCreateInfo ss[3]{};
	ss[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	ss[0].pSpecializationInfo = nullptr;
	ss[0].module = vs;
	ss[0].pName = "main";
	ss[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	ss[1].pSpecializationInfo = nullptr;
	ss[1].module = fs;
	ss[1].pName = "main";
	if (gs) {
		stageCount++;
		ss[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ss[2].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
		ss[2].pSpecializationInfo = nullptr;
		ss[2].module = gs;
		ss[2].pName = "main";
	}

	VkGraphicsPipelineCreateInfo pipe{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipe.pStages = ss;
	pipe.stageCount = stageCount;
	pipe.renderPass = compatibleRenderPass;
	pipe.basePipelineIndex = 0;
	pipe.pColorBlendState = &desc->cbs;
	pipe.pDepthStencilState = &desc->dss;
	pipe.pRasterizationState = &desc->rs;

	VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = multisample ? sampleCount : VK_SAMPLE_COUNT_1_BIT;
	if (multisample && (flags_ & PipelineFlags::USES_DISCARD)) {
		// Extreme quality
		ms.sampleShadingEnable = true;
		ms.minSampleShading = 1.0f;
	}

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = desc->topology;

	// We will use dynamic viewport state.
	pipe.pVertexInputState = &desc->vis;
	pipe.pViewportState = &desc->views;
	pipe.pTessellationState = nullptr;
	pipe.pDynamicState = &desc->ds;
	pipe.pInputAssemblyState = &inputAssembly;
	pipe.pMultisampleState = &ms;
	pipe.layout = desc->pipelineLayout->pipelineLayout;
	pipe.basePipelineHandle = VK_NULL_HANDLE;
	pipe.basePipelineIndex = 0;
	pipe.subpass = 0;

	double start = time_now_d();
	VkPipeline vkpipeline;
	VkResult result = vkCreateGraphicsPipelines(vulkan->GetDevice(), desc->pipelineCache, 1, &pipe, nullptr, &vkpipeline);

	double now = time_now_d();
	double taken_ms_since_scheduling = (now - scheduleTime) * 1000.0;
	double taken_ms = (now - start) * 1000.0;

#ifndef _DEBUG
	if (taken_ms < 0.1) {
		DEBUG_LOG(Log::G3D, "Pipeline (x/%d) time on %s: %0.2f ms, %0.2f ms since scheduling (fast) rpType: %04x sampleBits: %d (%s)",
			countToCompile, GetCurrentThreadName(), taken_ms, taken_ms_since_scheduling, (u32)rpType, (u32)sampleCount, tag_.c_str());
	} else {
		INFO_LOG(Log::G3D, "Pipeline (x/%d) time on %s: %0.2f ms, %0.2f ms since scheduling  rpType: %04x sampleBits: %d (%s)",
			countToCompile, GetCurrentThreadName(), taken_ms, taken_ms_since_scheduling, (u32)rpType, (u32)sampleCount, tag_.c_str());
	}
#endif

	bool success = true;
	if (result == VK_INCOMPLETE) {
		// Bad (disallowed by spec) return value seen on Adreno in Burnout :(  Try to ignore?
		// Would really like to log more here, we could probably attach more info to desc.
		//
		// At least create a null placeholder to avoid creating over and over if something is broken.
		pipeline[(size_t)rpType]->Post(VK_NULL_HANDLE);
		ERROR_LOG(Log::G3D, "Failed creating graphics pipeline! VK_INCOMPLETE");
		LogCreationFailure();
		success = false;
	} else if (result != VK_SUCCESS) {
		pipeline[(size_t)rpType]->Post(VK_NULL_HANDLE);
		ERROR_LOG(Log::G3D, "Failed creating graphics pipeline! result='%s'", VulkanResultToString(result));
		LogCreationFailure();
		success = false;
	} else {
		// Success!
		if (!tag_.empty()) {
			vulkan->SetDebugName(vkpipeline, VK_OBJECT_TYPE_PIPELINE, tag_.c_str());
		}
		pipeline[(size_t)rpType]->Post(vkpipeline);
	}

	return success;
}

void VKRGraphicsPipeline::DestroyVariants(VulkanContext *vulkan, bool msaaOnly) {
	for (size_t i = 0; i < (size_t)RenderPassType::TYPE_COUNT; i++) {
		if (!this->pipeline[i])
			continue;
		if (msaaOnly && (i & (int)RenderPassType::MULTISAMPLE) == 0)
			continue;

		VkPipeline pipeline = this->pipeline[i]->BlockUntilReady();
		// pipeline can be nullptr here, if it failed to compile before.
		if (pipeline) {
			vulkan->Delete().QueueDeletePipeline(pipeline);
		}
		this->pipeline[i] = nullptr;
	}
	sampleCount_ = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
}

void VKRGraphicsPipeline::DestroyVariantsInstant(VkDevice device) {
	for (size_t i = 0; i < (size_t)RenderPassType::TYPE_COUNT; i++) {
		if (pipeline[i]) {
			vkDestroyPipeline(device, pipeline[i]->BlockUntilReady(), nullptr);
			delete pipeline[i];
			pipeline[i] = nullptr;
		}
	}
}

VKRGraphicsPipeline::~VKRGraphicsPipeline() {
	// This is called from the callbacked queued in QueueForDeletion.
	// When we reach here, we should already be empty, so let's assert on that.
	for (size_t i = 0; i < (size_t)RenderPassType::TYPE_COUNT; i++) {
		_assert_(!pipeline[i]);
	}
	if (desc)
		desc->Release();
}

void VKRGraphicsPipeline::BlockUntilCompiled() {
	for (size_t i = 0; i < (size_t)RenderPassType::TYPE_COUNT; i++) {
		if (pipeline[i]) {
			pipeline[i]->BlockUntilReady();
		}
	}
}

void VKRGraphicsPipeline::QueueForDeletion(VulkanContext *vulkan) {
	// Can't destroy variants here, the pipeline still lives for a while.
	vulkan->Delete().QueueCallback([](VulkanContext *vulkan, void *p) {
		VKRGraphicsPipeline *pipeline = (VKRGraphicsPipeline *)p;
		pipeline->DestroyVariantsInstant(vulkan->GetDevice());
		delete pipeline;
	}, this);
}

u32 VKRGraphicsPipeline::GetVariantsBitmask() const {
	u32 bitmask = 0;
	for (size_t i = 0; i < (size_t)RenderPassType::TYPE_COUNT; i++) {
		if (pipeline[i]) {
			bitmask |= 1 << i;
		}
	}
	return bitmask;
}

void VKRGraphicsPipeline::LogCreationFailure() const {
	ERROR_LOG(Log::G3D, "vs: %s\n[END VS]", desc->vertexShaderSource.c_str());
	ERROR_LOG(Log::G3D, "fs: %s\n[END FS]", desc->fragmentShaderSource.c_str());
	if (desc->geometryShader) {
		ERROR_LOG(Log::G3D, "gs: %s\n[END GS]", desc->geometryShaderSource.c_str());
	}
	// TODO: Maybe log various other state?
	ERROR_LOG(Log::G3D, "======== END OF PIPELINE ==========");
}

struct SinglePipelineTask {
	VKRGraphicsPipeline *pipeline;
	VkRenderPass compatibleRenderPass;
	RenderPassType rpType;
	VkSampleCountFlagBits sampleCount;
	double scheduleTime;
	int countToCompile;
};

class CreateMultiPipelinesTask : public Task {
public:
	CreateMultiPipelinesTask(VulkanContext *vulkan, std::vector<SinglePipelineTask> tasks) : vulkan_(vulkan), tasks_(std::move(tasks)) {
		tasksInFlight_.fetch_add(1);
	}
	~CreateMultiPipelinesTask() = default;

	TaskType Type() const override {
		return TaskType::CPU_COMPUTE;
	}

	TaskPriority Priority() const override {
		return TaskPriority::HIGH;
	}

	void Run() override {
		for (auto &task : tasks_) {
			task.pipeline->Create(vulkan_, task.compatibleRenderPass, task.rpType, task.sampleCount, task.scheduleTime, task.countToCompile);
		}
		tasksInFlight_.fetch_sub(1);
	}

	VulkanContext *vulkan_;
	std::vector<SinglePipelineTask> tasks_;

	// Use during shutdown to make sure there aren't any leftover tasks sitting queued.
	// Could probably be done more elegantly. Like waiting for all tasks of a type, or saving pointers to them, or something...
	// Returns the maximum value of tasks in flight seen during the wait.
	static int WaitForAll();
	static std::atomic<int> tasksInFlight_;
};

int CreateMultiPipelinesTask::WaitForAll() {
	int inFlight = 0;
	int maxInFlight = 0;
	while ((inFlight = tasksInFlight_.load()) > 0) {
		if (inFlight > maxInFlight) {
			maxInFlight = inFlight;
		}
		sleep_ms(2, "create-multi-pipelines-wait");
	}
	return maxInFlight;
}

std::atomic<int> CreateMultiPipelinesTask::tasksInFlight_;

VulkanRenderManager::VulkanRenderManager(VulkanContext *vulkan, bool useThread, HistoryBuffer<FrameTimeData, FRAME_TIME_HISTORY_LENGTH> &frameTimeHistory)
	: vulkan_(vulkan), queueRunner_(vulkan),
	initTimeMs_("initTimeMs"),
	totalGPUTimeMs_("totalGPUTimeMs"),
	renderCPUTimeMs_("renderCPUTimeMs"),
	descUpdateTimeMs_("descUpdateCPUTimeMs"),
	useRenderThread_(useThread),
	frameTimeHistory_(frameTimeHistory)
{
	inflightFramesAtStart_ = vulkan_->GetInflightFrames();

	// For present timing experiments. Disabled for now.
	measurePresentTime_ = false;

	frameDataShared_.Init(vulkan, useThread, measurePresentTime_);

	for (int i = 0; i < inflightFramesAtStart_; i++) {
		frameData_[i].Init(vulkan, i);
	}

	queueRunner_.CreateDeviceObjects();
}

bool VulkanRenderManager::CreateBackbuffers() {
	if (!vulkan_->IsSwapchainInited()) {
		ERROR_LOG(Log::G3D, "No swapchain - can't create backbuffers");
		return false;
	}

	VkCommandBuffer cmdInit = GetInitCmd();

	if (vulkan_->HasRealSwapchain()) {
		if (!CreateSwapchainViewsAndDepth(cmdInit, &postInitBarrier_, frameDataShared_)) {
			return false;
		}
	}

	curWidthRaw_ = -1;
	curHeightRaw_ = -1;

	if (newInflightFrames_ != -1) {
		INFO_LOG(Log::G3D, "Updating inflight frames to %d", newInflightFrames_);
		vulkan_->UpdateInflightFrames(newInflightFrames_);
		newInflightFrames_ = -1;
	}

	outOfDateFrames_ = 0;

	for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
		auto &frameData = frameData_[i];
		frameData.readyForFence = true;  // Just in case.
	}

	// Start the thread(s).
	StartThreads();
	return true;
}

bool VulkanRenderManager::CreateSwapchainViewsAndDepth(VkCommandBuffer cmdInit, VulkanBarrierBatch *barriers, FrameDataShared &frameDataShared) {
	VkResult res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &frameDataShared.swapchainImageCount_, nullptr);
	_dbg_assert_(res == VK_SUCCESS);

	VkImage *swapchainImages = new VkImage[frameDataShared.swapchainImageCount_];
	res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &frameDataShared.swapchainImageCount_, swapchainImages);
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "vkGetSwapchainImagesKHR failed");
		delete[] swapchainImages;
		return false;
	}

	static const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	for (uint32_t i = 0; i < frameDataShared.swapchainImageCount_; i++) {
		SwapchainImageData sc_buffer{};
		sc_buffer.image = swapchainImages[i];
		res = vkCreateSemaphore(vulkan_->GetDevice(), &semaphoreCreateInfo, nullptr, &sc_buffer.renderingCompleteSemaphore);
		_dbg_assert_(res == VK_SUCCESS);

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
		color_image_view.subresourceRange.layerCount = 1;  // TODO: Investigate hw-assisted stereo.
		color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		color_image_view.flags = 0;
		color_image_view.image = sc_buffer.image;

		// We leave the images as UNDEFINED, there's no need to pre-transition them as
		// the backbuffer renderpass starts out with them being auto-transitioned from UNDEFINED anyway.
		// Also, turns out it's illegal to transition un-acquired images, thanks Hans-Kristian. See #11417.

		res = vkCreateImageView(vulkan_->GetDevice(), &color_image_view, nullptr, &sc_buffer.view);
		vulkan_->SetDebugName(sc_buffer.view, VK_OBJECT_TYPE_IMAGE_VIEW, "swapchain_view");
		frameDataShared.swapchainImages_.push_back(sc_buffer);
		_dbg_assert_(res == VK_SUCCESS);
	}
	delete[] swapchainImages;

	// Must be before InitBackbufferRenderPass.
	if (queueRunner_.InitDepthStencilBuffer(cmdInit, barriers)) {
		queueRunner_.InitBackbufferFramebuffers(vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight(), frameDataShared);
	}
	return true;
}

void VulkanRenderManager::StartThreads() {
	{
		std::unique_lock<std::mutex> lock(compileQueueMutex_);
		_assert_(compileQueue_.empty());
	}

	runCompileThread_ = true;  // For controlling the compiler thread's exit

	if (useRenderThread_) {
		INFO_LOG(Log::G3D, "Starting Vulkan submission thread");
		renderThread_ = std::thread(&VulkanRenderManager::RenderThreadFunc, this);
	}
	INFO_LOG(Log::G3D, "Starting Vulkan compiler thread");
	compileThread_ = std::thread(&VulkanRenderManager::CompileThreadFunc, this);

	if (measurePresentTime_ && vulkan_->Extensions().KHR_present_wait && vulkan_->GetPresentMode() == VK_PRESENT_MODE_FIFO_KHR) {
		INFO_LOG(Log::G3D, "Starting Vulkan present wait thread");
		presentWaitThread_ = std::thread(&VulkanRenderManager::PresentWaitThreadFunc, this);
	}
}

// MUST be called from emuthread!
void VulkanRenderManager::StopThreads() {
	INFO_LOG(Log::G3D, "VulkanRenderManager::StopThreads");
	// Make sure we don't have an open non-backbuffer render pass
	if (curRenderStep_ && curRenderStep_->render.framebuffer != nullptr) {
		EndCurRenderStep();
	}
	// Not sure this is a sensible check - should be ok even if not.
	// _dbg_assert_(steps_.empty());

	if (useRenderThread_) {
		_dbg_assert_(renderThread_.joinable());
		// Tell the render thread to quit when it's done.
		VKRRenderThreadTask *task = new VKRRenderThreadTask(VKRRunType::EXIT);
		task->frame = vulkan_->GetCurFrame();
		{
			std::unique_lock<std::mutex> lock(pushMutex_);
			renderThreadQueue_.push(task);
		}
		pushCondVar_.notify_one();
		// Once the render thread encounters the above exit task, it'll exit.
		renderThread_.join();
		INFO_LOG(Log::G3D, "Vulkan submission thread joined. Frame=%d", vulkan_->GetCurFrame());
	}

	for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
		auto &frameData = frameData_[i];
		// Zero the queries so we don't try to pull them later.
		frameData.profile.timestampDescriptions.clear();
	}

	{
		std::unique_lock<std::mutex> lock(compileQueueMutex_);
		runCompileThread_ = false;  // Compiler and present thread both look at this bool.
		_assert_(compileThread_.joinable());
		compileCond_.notify_one();
	}
	compileThread_.join();

	if (presentWaitThread_.joinable()) {
		presentWaitThread_.join();
	}

	INFO_LOG(Log::G3D, "Vulkan compiler thread joined. Now wait for any straggling compile tasks. runCompileThread_ = %d", (int)runCompileThread_);
	CreateMultiPipelinesTask::WaitForAll();

	{
		std::unique_lock<std::mutex> lock(compileQueueMutex_);
		_assert_(compileQueue_.empty());
	}
}

void VulkanRenderManager::DestroyBackbuffers() {
	StopThreads();
	vulkan_->WaitUntilQueueIdle();

	for (auto &image : frameDataShared_.swapchainImages_) {
		vulkan_->Delete().QueueDeleteImageView(image.view);
		vkDestroySemaphore(vulkan_->GetDevice(), image.renderingCompleteSemaphore, nullptr);
	}
	frameDataShared_.swapchainImages_.clear();
	frameDataShared_.swapchainImageCount_ = 0;

	queueRunner_.DestroyBackBuffers();
}

// Hm, I'm finding the occasional report of these asserts.
void VulkanRenderManager::CheckNothingPending() {
	_assert_(pipelinesToCheck_.empty());
	{
		std::unique_lock<std::mutex> lock(compileQueueMutex_);
		_assert_(compileQueue_.empty());
	}
}

VulkanRenderManager::~VulkanRenderManager() {
	INFO_LOG(Log::G3D, "VulkanRenderManager destructor");

	{
		std::unique_lock<std::mutex> lock(compileQueueMutex_);
		_assert_(compileQueue_.empty());
	}

	if (useRenderThread_) {
		_dbg_assert_(!renderThread_.joinable());
	}

	_dbg_assert_(!runCompileThread_);  // StopThread should already have been called from DestroyBackbuffers.

	vulkan_->WaitUntilQueueIdle();

	_dbg_assert_(pipelineLayouts_.empty());

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
		bool exitAfterCompile = false;
		std::vector<CompileQueueEntry> toCompile;
		{
			std::unique_lock<std::mutex> lock(compileQueueMutex_);
			while (compileQueue_.empty() && runCompileThread_) {
				compileCond_.wait(lock);
			}
			toCompile = std::move(compileQueue_);
			compileQueue_.clear();
			if (!runCompileThread_) {
				exitAfterCompile = true;
			}
		}

		int countToCompile = (int)toCompile.size();

		// Here we sort the pending pipelines by vertex and fragment shaders,
		std::map<std::pair<Promise<VkShaderModule> *, Promise<VkShaderModule> *>, std::vector<SinglePipelineTask>> map;

		double scheduleTime = time_now_d();

		// Here we sort pending graphics pipelines by vertex and fragment shaders, and split up further.
		// Those with the same pairs of shaders should be on the same thread, at least on NVIDIA.
		// I don't think PowerVR cares though, it doesn't seem to reuse information between the compiles,
		// so we might want a different splitting algorithm there.
		for (auto &entry : toCompile) {
			switch (entry.type) {
			case CompileQueueEntry::Type::GRAPHICS:
			{
				map[std::make_pair(entry.graphics->desc->vertexShader, entry.graphics->desc->fragmentShader)].push_back(
					SinglePipelineTask{
						entry.graphics,
						entry.compatibleRenderPass,
						entry.renderPassType,
						entry.sampleCount,
						scheduleTime,    // these two are for logging purposes.
						countToCompile,
					}
				);
				break;
			}
			}
		}

		for (const auto &iter : map) {
			auto &shaders = iter.first;
			auto &entries = iter.second;

			// NOTICE_LOG(Log::G3D, "For this shader pair, we have %d pipelines to create", (int)entries.size());

			Task *task = new CreateMultiPipelinesTask(vulkan_, entries);
			g_threadManager.EnqueueTask(task);
		}

		if (exitAfterCompile) {
			break;
		}

		// Hold off just a bit before we check again, to allow bunches of pipelines to collect.
		sleep_ms(1, "pipeline-collect");
	}

	std::unique_lock<std::mutex> lock(compileQueueMutex_);
	_assert_(compileQueue_.empty());
}

void VulkanRenderManager::RenderThreadFunc() {
	SetCurrentThreadName("VulkanRenderMan");
	while (true) {
		_dbg_assert_(useRenderThread_);

		// Pop a task of the queue and execute it.
		VKRRenderThreadTask *task = nullptr;
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
		if (task->runType == VKRRunType::EXIT) {
			// Oh, host wanted out. Let's leave.
			delete task;
			// In this case, there should be no more tasks.
			break;
		}

		Run(*task);
		delete task;
	}

	// Wait for the device to be done with everything, before tearing stuff down.
	// TODO: Do we really need this? It's probably a good idea, though.
	vkDeviceWaitIdle(vulkan_->GetDevice());
	VLOG("PULL: Quitting");
}

void VulkanRenderManager::PresentWaitThreadFunc() {
	SetCurrentThreadName("PresentWait");

#if !PPSSPP_PLATFORM(IOS_APP_STORE)
	_dbg_assert_(vkWaitForPresentKHR != nullptr);

	uint64_t waitedId = frameIdGen_;
	while (runCompileThread_) {
		const uint64_t timeout = 1000000000ULL;  // 1 sec
		if (VK_SUCCESS == vkWaitForPresentKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), waitedId, timeout)) {
			frameTimeHistory_[waitedId].actualPresent = time_now_d();
			frameTimeHistory_[waitedId].waitCount++;
			waitedId++;
		} else {
			// We caught up somehow, which is a bad sign (we should have blocked, right?). Maybe we should break out of the loop?
			sleep_ms(1, "present-wait-problem");
			frameTimeHistory_[waitedId].waitCount++;
		}
		_dbg_assert_(waitedId <= frameIdGen_);
	}
#endif

	INFO_LOG(Log::G3D, "Leaving PresentWaitThreadFunc()");
}

void VulkanRenderManager::PollPresentTiming() {
	// For VK_GOOGLE_display_timing, we need to poll.

	// Poll for information about completed frames.
	// NOTE: We seem to get the information pretty late! Like after 6 frames, which is quite weird.
	// Tested on POCO F4.
	// TODO: Getting validation errors that this should be called from the thread doing the presenting.
	// Probably a fair point. For now, we turn it off.
	if (measurePresentTime_ && vulkan_->Extensions().GOOGLE_display_timing) {
		uint32_t count = 0;
		vkGetPastPresentationTimingGOOGLE(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &count, nullptr);
		if (count > 0) {
			VkPastPresentationTimingGOOGLE *timings = new VkPastPresentationTimingGOOGLE[count];
			vkGetPastPresentationTimingGOOGLE(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &count, timings);
			for (uint32_t i = 0; i < count; i++) {
				uint64_t presentId = timings[i].presentID;
				frameTimeHistory_[presentId].actualPresent = from_time_raw(timings[i].actualPresentTime);
				frameTimeHistory_[presentId].desiredPresentTime = from_time_raw(timings[i].desiredPresentTime);
				frameTimeHistory_[presentId].earliestPresentTime = from_time_raw(timings[i].earliestPresentTime);
				double presentMargin = from_time_raw_relative(timings[i].presentMargin);
				frameTimeHistory_[presentId].presentMargin = presentMargin;
			}
			delete[] timings;
		}
	}
}

void VulkanRenderManager::BeginFrame(bool enableProfiling, bool enableLogProfiler) {
	double frameBeginTime = time_now_d()
	VLOG("BeginFrame");
	VkDevice device = vulkan_->GetDevice();

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];
	VLOG("PUSH: Fencing %d", curFrame);

	// Makes sure the submission from the previous time around has happened. Otherwise
	// we are not allowed to wait from another thread here..
	if (useRenderThread_) {
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

	uint64_t frameId = frameIdGen_++;

	PollPresentTiming();

	ResetDescriptorLists(curFrame);

	int validBits = vulkan_->GetQueueFamilyProperties(vulkan_->GetGraphicsQueueFamilyIndex()).timestampValidBits;

	FrameTimeData &frameTimeData = frameTimeHistory_.Add(frameId);
	frameTimeData.frameId = frameId;
	frameTimeData.frameBegin = frameBeginTime;
	frameTimeData.afterFenceWait = time_now_d();

	// Can't set this until after the fence.
	frameData.profile.enabled = enableProfiling;
	frameData.profile.timestampsEnabled = enableProfiling && validBits > 0;
	frameData.frameId = frameId;

	uint64_t queryResults[MAX_TIMESTAMP_QUERIES];

	if (enableProfiling) {
		// Pull the profiling results from last time and produce a summary!
		if (!frameData.profile.timestampDescriptions.empty() && frameData.profile.timestampsEnabled) {
			int numQueries = (int)frameData.profile.timestampDescriptions.size();
			VkResult res = vkGetQueryPoolResults(
				vulkan_->GetDevice(),
				frameData.profile.queryPool, 0, numQueries, sizeof(uint64_t) * numQueries, &queryResults[0], sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT);
			if (res == VK_SUCCESS) {
				double timestampConversionFactor = (double)vulkan_->GetPhysicalDeviceProperties().properties.limits.timestampPeriod * (1.0 / 1000000.0);
				uint64_t timestampDiffMask = validBits == 64 ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << validBits) - 1);
				std::stringstream str;

				char line[256];
				totalGPUTimeMs_.Update(((double)((queryResults[numQueries - 1] - queryResults[0]) & timestampDiffMask) * timestampConversionFactor));
				totalGPUTimeMs_.Format(line, sizeof(line));
				str << line;
				renderCPUTimeMs_.Update((frameData.profile.cpuEndTime - frameData.profile.cpuStartTime) * 1000.0);
				renderCPUTimeMs_.Format(line, sizeof(line));
				str << line;
				descUpdateTimeMs_.Update(frameData.profile.descWriteTime * 1000.0);
				descUpdateTimeMs_.Format(line, sizeof(line));
				str << line;
				snprintf(line, sizeof(line), "Descriptors written: %d (dedup: %d)\n", frameData.profile.descriptorsWritten, frameData.profile.descriptorsDeduped);
				str << line;
				snprintf(line, sizeof(line), "Resource deletions: %d\n", vulkan_->GetLastDeleteCount());
				str << line;
				for (int i = 0; i < numQueries - 1; i++) {
					uint64_t diff = (queryResults[i + 1] - queryResults[i]) & timestampDiffMask;
					double milliseconds = (double)diff * timestampConversionFactor;

					// Can't use SimpleStat for these very easily since these are dynamic per frame.
					// Only the first one is static, the initCmd.
					// Could try some hashtable tracking for the rest, later.
					if (i == 0) {
						initTimeMs_.Update(milliseconds);
						initTimeMs_.Format(line, sizeof(line));
					} else {
						snprintf(line, sizeof(line), "%s: %0.3f ms\n", frameData.profile.timestampDescriptions[i + 1].c_str(), milliseconds);
					}
					str << line;
				}
				frameData.profile.profileSummary = str.str();
			} else {
				frameData.profile.profileSummary = "(error getting GPU profile - not ready?)";
			}
		} else {
			std::stringstream str;
			char line[256];
			renderCPUTimeMs_.Update((frameData.profile.cpuEndTime - frameData.profile.cpuStartTime) * 1000.0);
			renderCPUTimeMs_.Format(line, sizeof(line));
			str << line;
			descUpdateTimeMs_.Update(frameData.profile.descWriteTime * 1000.0);
			descUpdateTimeMs_.Format(line, sizeof(line));
			str << line;
			snprintf(line, sizeof(line), "Descriptors written: %d\n", frameData.profile.descriptorsWritten);
			str << line;
			frameData.profile.profileSummary = str.str();
		}

#ifdef _DEBUG
		std::string cmdString;
		for (int i = 0; i < ARRAY_SIZE(frameData.profile.commandCounts); i++) {
			if (frameData.profile.commandCounts[i] > 0) {
				cmdString += StringFromFormat("%s: %d\n", VKRRenderCommandToString((VKRRenderCommand)i), frameData.profile.commandCounts[i]);
			}
		}
		memset(frameData.profile.commandCounts, 0, sizeof(frameData.profile.commandCounts));
		frameData.profile.profileSummary += cmdString;
#endif
	}

	frameData.profile.descriptorsWritten = 0;
	frameData.profile.descriptorsDeduped = 0;

	// Must be after the fence - this performs deletes.
	VLOG("PUSH: BeginFrame %d", curFrame);

	insideFrame_ = true;
	vulkan_->BeginFrame(enableLogProfiler ? GetInitCmd() : VK_NULL_HANDLE);

	frameData.profile.timestampDescriptions.clear();
	if (frameData.profile.timestampsEnabled) {
		// For various reasons, we need to always use an init cmd buffer in this case to perform the vkCmdResetQueryPool,
		// unless we want to limit ourselves to only measure the main cmd buffer.
		// Later versions of Vulkan have support for clearing queries on the CPU timeline, but we don't want to rely on that.
		// Reserve the first two queries for initCmd.
		frameData.profile.timestampDescriptions.emplace_back("initCmd Begin");
		frameData.profile.timestampDescriptions.emplace_back("initCmd");
		VkCommandBuffer initCmd = GetInitCmd();
	}
}

VkCommandBuffer VulkanRenderManager::GetInitCmd() {
	int curFrame = vulkan_->GetCurFrame();
	return frameData_[curFrame].GetInitCmd(vulkan_);
}

void VulkanRenderManager::ReportBadStateForDraw() {
	const char *cause1 = "";
	char cause2[256];
	cause2[0] = '\0';
	if (!curRenderStep_) {
		cause1 = "No current render step";
	}
	if (curRenderStep_ && curRenderStep_->stepType != VKRStepType::RENDER) {
		cause1 = "Not a render step: ";
		std::string str = VulkanQueueRunner::StepToString(vulkan_, *curRenderStep_);
		truncate_cpy(cause2, str);
	}
	ERROR_LOG_REPORT_ONCE(baddraw, Log::G3D, "Can't draw: %s%s. Step count: %d", cause1, cause2, (int)steps_.size());
}

int VulkanRenderManager::WaitForPipelines() {
	return CreateMultiPipelinesTask::WaitForAll();
}

VKRGraphicsPipeline *VulkanRenderManager::CreateGraphicsPipeline(VKRGraphicsPipelineDesc *desc, PipelineFlags pipelineFlags, uint32_t variantBitmask, VkSampleCountFlagBits sampleCount, bool cacheLoad, const char *tag) {
	if (!desc->vertexShader || !desc->fragmentShader) {
		ERROR_LOG(Log::G3D, "Can't create graphics pipeline with missing vs/ps: %p %p", desc->vertexShader, desc->fragmentShader);
		return nullptr;
	}

	VKRGraphicsPipeline *pipeline = new VKRGraphicsPipeline(pipelineFlags, tag);
	pipeline->desc = desc;
	pipeline->desc->AddRef();
	if (curRenderStep_ && !cacheLoad) {
		// The common case during gameplay.
		pipelinesToCheck_.push_back(pipeline);
	} else {
		if (!variantBitmask) {
			WARN_LOG(Log::G3D, "WARNING: Will not compile any variants of pipeline, not in renderpass and empty variantBitmask");
		}
		// Presumably we're in initialization, loading the shader cache.
		// Look at variantBitmask to see what variants we should queue up.
		RPKey key{
			VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR,
			VKRRenderPassStoreAction::STORE, VKRRenderPassStoreAction::DONT_CARE, VKRRenderPassStoreAction::DONT_CARE,
		};
		VKRRenderPass *compatibleRenderPass = queueRunner_.GetRenderPass(key);
		std::unique_lock<std::mutex> lock(compileQueueMutex_);
		_dbg_assert_(runCompileThread_);
		bool needsCompile = false;
		for (size_t i = 0; i < (size_t)RenderPassType::TYPE_COUNT; i++) {
			if (!(variantBitmask & (1 << i)))
				continue;
			RenderPassType rpType = (RenderPassType)i;

			// Sanity check - don't compile incompatible types (could be caused by corrupt caches, changes in data structures, etc).
			if ((pipelineFlags & PipelineFlags::USES_DEPTH_STENCIL) && !RenderPassTypeHasDepth(rpType)) {
				WARN_LOG(Log::G3D, "Not compiling pipeline that requires depth, for non depth renderpass type");
				continue;
			}
			// Shouldn't hit this, these should have been filtered elsewhere. However, still a good check to do.
			if (sampleCount == VK_SAMPLE_COUNT_1_BIT && RenderPassTypeHasMultisample(rpType)) {
				WARN_LOG(Log::G3D, "Not compiling single sample pipeline for a multisampled render pass type");
				continue;
			}

			if (rpType == RenderPassType::BACKBUFFER) {
				sampleCount = VK_SAMPLE_COUNT_1_BIT;
			}

			// Sanity check
			if (runCompileThread_) {
				pipeline->pipeline[i] = Promise<VkPipeline>::CreateEmpty();
				compileQueue_.emplace_back(pipeline, compatibleRenderPass->Get(vulkan_, rpType, sampleCount), rpType, sampleCount);
			}
			needsCompile = true;
		}
		if (needsCompile)
			compileCond_.notify_one();
	}
	return pipeline;
}

void VulkanRenderManager::EndCurRenderStep() {
	if (!curRenderStep_)
		return;

	_dbg_assert_(runCompileThread_);

	RPKey key{
		curRenderStep_->render.colorLoad, curRenderStep_->render.depthLoad, curRenderStep_->render.stencilLoad,
		curRenderStep_->render.colorStore, curRenderStep_->render.depthStore, curRenderStep_->render.stencilStore,
	};
	// Save the accumulated pipeline flags so we can use that to configure the render pass.
	// We'll often be able to avoid loading/saving the depth/stencil buffer.
	curRenderStep_->render.pipelineFlags = curPipelineFlags_;
	bool depthStencil = (curPipelineFlags_ & PipelineFlags::USES_DEPTH_STENCIL) != 0;
	RenderPassType rpType = depthStencil ? RenderPassType::HAS_DEPTH : RenderPassType::DEFAULT;

	if (curRenderStep_->render.framebuffer && (rpType & RenderPassType::HAS_DEPTH) && !curRenderStep_->render.framebuffer->HasDepth()) {
		WARN_LOG(Log::G3D, "Trying to render with a depth-writing pipeline to a framebuffer without depth: %s", curRenderStep_->render.framebuffer->Tag());
		rpType = RenderPassType::DEFAULT;
	}

	if (!curRenderStep_->render.framebuffer) {
		rpType = RenderPassType::BACKBUFFER;
	} else {
		// Framebuffers can be stereo, and if so, will control the render pass type to match.
		// Pipelines can be mono and render fine to stereo etc, so not checking them here.
		// Note that we don't support rendering to just one layer of a multilayer framebuffer!
		if (curRenderStep_->render.framebuffer->numLayers > 1) {
			rpType = (RenderPassType)(rpType | RenderPassType::MULTIVIEW);
		}

		if (curRenderStep_->render.framebuffer->sampleCount != VK_SAMPLE_COUNT_1_BIT) {
			rpType = (RenderPassType)(rpType | RenderPassType::MULTISAMPLE);
		}
	}

	VKRRenderPass *renderPass = queueRunner_.GetRenderPass(key);
	curRenderStep_->render.renderPassType = rpType;

	VkSampleCountFlagBits sampleCount = curRenderStep_->render.framebuffer ? curRenderStep_->render.framebuffer->sampleCount : VK_SAMPLE_COUNT_1_BIT;

	bool needsCompile = false;
	for (VKRGraphicsPipeline *pipeline : pipelinesToCheck_) {
		if (!pipeline) {
			// Not good, but let's try not to crash.
			continue;
		}
		std::unique_lock<std::mutex> lock(pipeline->mutex_);
		if (!pipeline->pipeline[(size_t)rpType]) {
			pipeline->pipeline[(size_t)rpType] = Promise<VkPipeline>::CreateEmpty();
			lock.unlock();

			_assert_(renderPass);
			compileQueueMutex_.lock();
			compileQueue_.emplace_back(pipeline, renderPass->Get(vulkan_, rpType, sampleCount), rpType, sampleCount);
			compileQueueMutex_.unlock();
			needsCompile = true;
		}
	}

	compileQueueMutex_.lock();
	if (needsCompile)
		compileCond_.notify_one();
	compileQueueMutex_.unlock();
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

void VulkanRenderManager::BindFramebufferAsRenderTarget(VKRFramebuffer *fb, VKRRenderPassLoadAction colorLoad, VKRRenderPassLoadAction depthLoad, VKRRenderPassLoadAction stencilLoad, uint32_t clearColor, float clearDepth, uint8_t clearStencil, const char *tag) {
	_dbg_assert_(insideFrame_);

	if (!fb) {
		// Backbuffer render passes have some requirements.
		_dbg_assert_(colorLoad != VKRRenderPassLoadAction::KEEP);
		_dbg_assert_(depthLoad != VKRRenderPassLoadAction::KEEP);
		_dbg_assert_(stencilLoad != VKRRenderPassLoadAction::KEEP);
	}

	// Eliminate dupes (bind of the framebuffer we already are rendering to), instantly convert to a clear if possible.
	if (!steps_.empty() && steps_.back()->stepType == VKRStepType::RENDER && steps_.back()->render.framebuffer == fb) {
		u32 clearMask = 0;
		if (colorLoad == VKRRenderPassLoadAction::CLEAR) {
			clearMask |= VK_IMAGE_ASPECT_COLOR_BIT;
		}
		if (depthLoad == VKRRenderPassLoadAction::CLEAR) {
			clearMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			curPipelineFlags_ |= PipelineFlags::USES_DEPTH_STENCIL;
		}
		if (stencilLoad == VKRRenderPassLoadAction::CLEAR) {
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

#ifdef _DEBUG
	SanityCheckPassesOnAdd();
#endif

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

	// Sanity check that we don't have binds to the backbuffer before binds to other buffers. It must always be bound last.
	if (steps_.size() >= 1 && steps_.back()->stepType == VKRStepType::RENDER && steps_.back()->render.framebuffer == nullptr && fb != nullptr) {
		_dbg_assert_(false);
	}

	// Older Mali drivers have issues with depth and stencil don't match load/clear/etc.
	// TODO: Determine which versions and do this only where necessary.
	u32 lateClearMask = 0;
	if (depthLoad != stencilLoad && vulkan_->GetPhysicalDeviceProperties().properties.vendorID == VULKAN_VENDOR_ARM) {
		if (stencilLoad == VKRRenderPassLoadAction::DONT_CARE) {
			stencilLoad = depthLoad;
		} else if (depthLoad == VKRRenderPassLoadAction::DONT_CARE) {
			depthLoad = stencilLoad;
		} else if (stencilLoad == VKRRenderPassLoadAction::CLEAR) {
			depthLoad = stencilLoad;
			lateClearMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		} else if (depthLoad == VKRRenderPassLoadAction::CLEAR) {
			stencilLoad = depthLoad;
			lateClearMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
	}

	VKRStep *step = new VKRStep{ VKRStepType::RENDER };
	step->render.framebuffer = fb;
	step->render.colorLoad = colorLoad;
	step->render.depthLoad = depthLoad;
	step->render.stencilLoad = stencilLoad;
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
	// pipelineFlags, renderArea and renderPassType get filled in when we finalize the step. Do not read from them before that.
	step->tag = tag;
	steps_.push_back(step);

	if (fb) {
		// If there's a KEEP, we naturally read from the framebuffer.
		if (colorLoad == VKRRenderPassLoadAction::KEEP || depthLoad == VKRRenderPassLoadAction::KEEP || stencilLoad == VKRRenderPassLoadAction::KEEP) {
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
		if (g_display.rotation == DisplayRotation::ROTATE_90 ||
			g_display.rotation == DisplayRotation::ROTATE_270) {
			curWidth_ = curHeightRaw_;
			curHeight_ = curWidthRaw_;
		} else {
			curWidth_ = curWidthRaw_;
			curHeight_ = curHeightRaw_;
		}
	}

	if (colorLoad == VKRRenderPassLoadAction::CLEAR || depthLoad == VKRRenderPassLoadAction::CLEAR || stencilLoad == VKRRenderPassLoadAction::CLEAR) {
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

	if (invalidationCallback_) {
		invalidationCallback_(InvalidationCallbackFlags::RENDER_PASS_STATE);
	}
}

bool VulkanRenderManager::CopyFramebufferToMemory(VKRFramebuffer *src, VkImageAspectFlags aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, Draw::ReadbackMode mode, const char *tag) {
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
	step->readback.delayed = mode == Draw::ReadbackMode::OLD_DATA_OK;
	step->dependencies.insert(src);
	step->tag = tag;
	steps_.push_back(step);

	if (mode == Draw::ReadbackMode::BLOCK) {
		FlushSync();
	}

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
				ERROR_LOG(Log::G3D, "Copying from backbuffer not supported, can't take screenshots");
				return false;
			}
			switch (vulkan_->GetSwapchainFormat()) {
			case VK_FORMAT_B8G8R8A8_UNORM: srcFormat = Draw::DataFormat::B8G8R8A8_UNORM; break;
			case VK_FORMAT_R8G8B8A8_UNORM: srcFormat = Draw::DataFormat::R8G8B8A8_UNORM; break;
			// NOTE: If you add supported formats here, make sure to also support them in VulkanQueueRunner::CopyReadbackBuffer.
			default:
				ERROR_LOG(Log::G3D, "Unsupported backbuffer format for screenshots");
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
	return queueRunner_.CopyReadbackBuffer(frameData_[vulkan_->GetCurFrame()],
		mode == Draw::ReadbackMode::OLD_DATA_OK ? src : nullptr, w, h, srcFormat, destFormat, pixelStride, pixels);
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
	queueRunner_.CopyReadbackBuffer(frameData_[vulkan_->GetCurFrame()], nullptr, w, h, destFormat, destFormat, pixelStride, pixels);

	_dbg_assert_(steps_.empty());
}

static void RemoveDrawCommands(FastVec<VkRenderData> *cmds) {
	// Here we remove any DRAW type commands when we hit a CLEAR.
	for (auto &c : *cmds) {
		if (c.cmd == VKRRenderCommand::DRAW || c.cmd == VKRRenderCommand::DRAW_INDEXED) {
			c.cmd = VKRRenderCommand::REMOVED;
		}
	}
}

static void CleanupRenderCommands(FastVec<VkRenderData> *cmds) {
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
			if (curRenderStep_->render.framebuffer && !curRenderStep_->render.framebuffer->HasDepth()) {
				WARN_LOG(Log::G3D, "Trying to clear depth/stencil on a non-depth framebuffer: %s", curRenderStep_->render.framebuffer->Tag());
			} else {
				curPipelineFlags_ |= PipelineFlags::USES_DEPTH_STENCIL;
			}
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
#ifdef _DEBUG
	SanityCheckPassesOnAdd();
#endif
	// _dbg_assert_msg_(src != dst, "Can't copy within the same buffer");
	if (src == dst) {
		// TODO: Check for rectangle self-overlap.
	}

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

	VkImageLayout finalSrcLayoutBeforeCopy = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	if (src == dst) {
		// We only use the first loop before, and transition to VK_IMAGE_LAYOUT_GENERAL.
		finalSrcLayoutBeforeCopy = VK_IMAGE_LAYOUT_GENERAL;
	}

	for (int i = (int)steps_.size() - 1; i >= 0; i--) {
		if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == src) {
			if (aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
				if (steps_[i]->render.finalColorLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalColorLayout = finalSrcLayoutBeforeCopy;
				}
			}
			if (aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
				if (steps_[i]->render.finalDepthStencilLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
					steps_[i]->render.finalDepthStencilLayout = finalSrcLayoutBeforeCopy;
				}
			}
			steps_[i]->render.numReads++;
			break;
		}
	}

	if (src != dst) {
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
#ifdef _DEBUG
	SanityCheckPassesOnAdd();
#endif

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

	// Sanity check. Added an assert to try to gather more info.
	// Got this assert in NPJH50443 FINAL FANTASY TYPE-0, but pretty rare. Moving back to debug assert.
	if (aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		_dbg_assert_msg_(src->depth.image != VK_NULL_HANDLE, "%s", src->Tag());
		_dbg_assert_msg_(dst->depth.image != VK_NULL_HANDLE, "%s", dst->Tag());

		if (!src->depth.image || !dst->depth.image) {
			// Something has gone wrong, but let's try to stumble along.
			return;
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

VkImageView VulkanRenderManager::BindFramebufferAsTexture(VKRFramebuffer *fb, int binding, VkImageAspectFlags aspectBit, int layer) {
	_dbg_assert_(curRenderStep_ != nullptr);
	_dbg_assert_(fb != nullptr);

	// We don't support texturing from stencil, neither do we support texturing from depth|stencil together (nonsensical).
	_dbg_assert_(aspectBit == VK_IMAGE_ASPECT_COLOR_BIT || aspectBit == VK_IMAGE_ASPECT_DEPTH_BIT);

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

	// Add this pretransition unless we already have it.
	TransitionRequest rq{ fb, aspectBit, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	curRenderStep_->preTransitions.insert(rq);  // Note that insert avoids inserting duplicates.

	if (layer == -1) {
		return aspectBit == VK_IMAGE_ASPECT_COLOR_BIT ? fb->color.texAllLayersView : fb->depth.texAllLayersView;
	} else {
		return aspectBit == VK_IMAGE_ASPECT_COLOR_BIT ? fb->color.texLayerViews[layer] : fb->depth.texLayerViews[layer];
	}
}

// Called on main thread.
// Sends the collected commands to the render thread. Submit-latency should be
// measured from here, probably.
void VulkanRenderManager::Finish() {
	EndCurRenderStep();

	// Let's do just a bit of cleanup on render commands now.
	// TODO: Should look into removing this.
	for (auto &step : steps_) {
		if (step->stepType == VKRStepType::RENDER) {
			CleanupRenderCommands(&step->commands);
		}
	}

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];

	if (!postInitBarrier_.empty()) {
		VkCommandBuffer buffer = frameData.GetInitCmd(vulkan_);
		postInitBarrier_.Flush(buffer);
	}

	VLOG("PUSH: Frame[%d]", curFrame);
	VKRRenderThreadTask *task = new VKRRenderThreadTask(VKRRunType::SUBMIT);
	task->frame = curFrame;
	if (useRenderThread_) {
		std::unique_lock<std::mutex> lock(pushMutex_);
		renderThreadQueue_.push(task);
		renderThreadQueue_.back()->steps = std::move(steps_);
		pushCondVar_.notify_one();
	} else {
		// Just do it!
		task->steps = std::move(steps_);
		Run(*task);
		delete task;
	}

	steps_.clear();
}

void VulkanRenderManager::Present() {
	int curFrame = vulkan_->GetCurFrame();

	VKRRenderThreadTask *task = new VKRRenderThreadTask(VKRRunType::PRESENT);
	task->frame = curFrame;
	if (useRenderThread_) {
		std::unique_lock<std::mutex> lock(pushMutex_);
		renderThreadQueue_.push(task);
		pushCondVar_.notify_one();
	} else {
		// Just do it!
		Run(*task);
		delete task;
	}

	vulkan_->EndFrame();
	insideFrame_ = false;
}

// Called on the render thread.
//
// Can be called again after a VKRRunType::SYNC on the same frame.
void VulkanRenderManager::Run(VKRRenderThreadTask &task) {
	FrameData &frameData = frameData_[task.frame];

	if (task.runType == VKRRunType::PRESENT) {
		if (!frameData.skipSwap) {
			VkResult res = frameData.QueuePresent(vulkan_, frameDataShared_);
			frameTimeHistory_[frameData.frameId].queuePresent = time_now_d();
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
			if (vulkan_->HasRealSwapchain()) {
				outOfDateFrames_++;
			}
			frameData.skipSwap = false;
		}
		return;
	}

	_dbg_assert_(!frameData.hasPresentCommands);

	if (!frameTimeHistory_[frameData.frameId].firstSubmit) {
		frameTimeHistory_[frameData.frameId].firstSubmit = time_now_d();
	}
	frameData.Submit(vulkan_, FrameSubmitType::Pending, frameDataShared_);

	// Flush descriptors.
	double descStart = time_now_d();
	FlushDescriptors(task.frame);
	frameData.profile.descWriteTime = time_now_d() - descStart;

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
	queueRunner_.RunSteps(task.steps, task.frame, frameData, frameDataShared_);

	switch (task.runType) {
	case VKRRunType::SUBMIT:
		frameData.Submit(vulkan_, FrameSubmitType::FinishFrame, frameDataShared_);
		break;

	case VKRRunType::SYNC:
		// The submit will trigger the readbackFence, and also do the wait for it.
		frameData.Submit(vulkan_, FrameSubmitType::Sync, frameDataShared_);

		if (useRenderThread_) {
			std::unique_lock<std::mutex> lock(syncMutex_);
			syncCondVar_.notify_one();
		}

		// At this point the GPU is idle, and we can resume filling the command buffers for the
		// current frame since and thus all previously enqueued command buffers have been
		// processed. No need to switch to the next frame number, would just be confusing.
		break;

	default:
		_dbg_assert_(false);
		break;
	}

	VLOG("PULL: Finished running frame %d", task.frame);
}

// Called from main thread.
void VulkanRenderManager::FlushSync() {
	_dbg_assert_(!curRenderStep_);

	if (invalidationCallback_) {
		invalidationCallback_(InvalidationCallbackFlags::COMMAND_BUFFER_STATE);
	}

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frameData = frameData_[curFrame];

	if (!postInitBarrier_.empty()) {
		VkCommandBuffer buffer = frameData.GetInitCmd(vulkan_);
		postInitBarrier_.Flush(buffer);
	}

	if (useRenderThread_) {
		{
			VLOG("PUSH: Frame[%d]", curFrame);
			VKRRenderThreadTask *task = new VKRRenderThreadTask(VKRRunType::SYNC);
			task->frame = curFrame;
			{
				std::unique_lock<std::mutex> lock(pushMutex_);
				renderThreadQueue_.push(task);
				renderThreadQueue_.back()->steps = std::move(steps_);
				pushCondVar_.notify_one();
			}
			steps_.clear();
		}

		{
			std::unique_lock<std::mutex> lock(syncMutex_);
			// Wait for the flush to be hit, since we're syncing.
			while (!frameData.syncDone) {
				VLOG("PUSH: Waiting for frame[%d].syncDone = 1 (sync)", curFrame);
				syncCondVar_.wait(lock);
			}
			frameData.syncDone = false;
		}
	} else {
		VKRRenderThreadTask task(VKRRunType::SYNC);
		task.frame = curFrame;
		task.steps = std::move(steps_);
		Run(task);
		steps_.clear();
	}
}

void VulkanRenderManager::ResetStats() {
	initTimeMs_.Reset();
	totalGPUTimeMs_.Reset();
	renderCPUTimeMs_.Reset();
}

VKRPipelineLayout *VulkanRenderManager::CreatePipelineLayout(BindingType *bindingTypes, size_t bindingTypesCount, bool geoShadersEnabled, const char *tag) {
	VKRPipelineLayout *layout = new VKRPipelineLayout();
	layout->SetTag(tag);
	layout->bindingTypesCount = (uint32_t)bindingTypesCount;

	_dbg_assert_(bindingTypesCount <= ARRAY_SIZE(layout->bindingTypes));
	memcpy(layout->bindingTypes, bindingTypes, sizeof(BindingType) * bindingTypesCount);

	VkDescriptorSetLayoutBinding bindings[VKRPipelineLayout::MAX_DESC_SET_BINDINGS];
	for (int i = 0; i < (int)bindingTypesCount; i++) {
		bindings[i].binding = i;
		bindings[i].descriptorCount = 1;
		bindings[i].pImmutableSamplers = nullptr;

		switch (bindingTypes[i]) {
		case BindingType::COMBINED_IMAGE_SAMPLER:
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			break;
		case BindingType::UNIFORM_BUFFER_DYNAMIC_VERTEX:
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			break;
		case BindingType::UNIFORM_BUFFER_DYNAMIC_ALL:
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			if (geoShadersEnabled) {
				bindings[i].stageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
			}
			break;
		case BindingType::STORAGE_BUFFER_VERTEX:
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			break;
		case BindingType::STORAGE_BUFFER_COMPUTE:
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			break;
		case BindingType::STORAGE_IMAGE_COMPUTE:
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			break;
		default:
			UNREACHABLE();
			break;
		}
	}

	VkDescriptorSetLayoutCreateInfo dsl = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dsl.bindingCount = (uint32_t)bindingTypesCount;
	dsl.pBindings = bindings;
	VkResult res = vkCreateDescriptorSetLayout(vulkan_->GetDevice(), &dsl, nullptr, &layout->descriptorSetLayout);
	_assert_(VK_SUCCESS == res && layout->descriptorSetLayout);

	VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkDescriptorSetLayout setLayouts[1] = { layout->descriptorSetLayout };
	pl.setLayoutCount = ARRAY_SIZE(setLayouts);
	pl.pSetLayouts = setLayouts;
	res = vkCreatePipelineLayout(vulkan_->GetDevice(), &pl, nullptr, &layout->pipelineLayout);
	_assert_(VK_SUCCESS == res && layout->pipelineLayout);

	vulkan_->SetDebugName(layout->descriptorSetLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, tag);
	vulkan_->SetDebugName(layout->pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, tag);

	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		// Some games go beyond 1024 and end up having to resize like GTA, but most stay below so we start there.
		layout->frameData[i].pool.Create(vulkan_, bindingTypes, (uint32_t)bindingTypesCount, 1024);
	}

	pipelineLayouts_.push_back(layout);
	return layout;
}

void VulkanRenderManager::DestroyPipelineLayout(VKRPipelineLayout *layout) {
	for (auto iter = pipelineLayouts_.begin(); iter != pipelineLayouts_.end(); iter++) {
		if (*iter == layout) {
			pipelineLayouts_.erase(iter);
			break;
		}
	}
	vulkan_->Delete().QueueCallback([](VulkanContext *vulkan, void *userdata) {
		VKRPipelineLayout *layout = (VKRPipelineLayout *)userdata;
		for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
			layout->frameData[i].pool.DestroyImmediately();
		}
		vkDestroyPipelineLayout(vulkan->GetDevice(), layout->pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(vulkan->GetDevice(), layout->descriptorSetLayout, nullptr);

		delete layout;
	}, layout);
}

void VulkanRenderManager::FlushDescriptors(int frame) {
	for (auto iter : pipelineLayouts_) {
		iter->FlushDescSets(vulkan_, frame, &frameData_[frame].profile);
	}
}

void VulkanRenderManager::ResetDescriptorLists(int frame) {
	for (auto iter : pipelineLayouts_) {
		VKRPipelineLayout::FrameData &data = iter->frameData[frame];

		data.flushedDescriptors_ = 0;
		data.descSets_.clear();
		data.descData_.clear();
	}
}

VKRPipelineLayout::~VKRPipelineLayout() {
	_assert_(frameData[0].pool.IsDestroyed());
}

void VKRPipelineLayout::FlushDescSets(VulkanContext *vulkan, int frame, QueueProfileContext *profile) {
	_dbg_assert_(frame < VulkanContext::MAX_INFLIGHT_FRAMES);

	FrameData &data = frameData[frame];

	VulkanDescSetPool &pool = data.pool;
	FastVec<PackedDescriptor> &descData = data.descData_;
	FastVec<PendingDescSet> &descSets = data.descSets_;

	pool.Reset();

	VkDescriptorSet setCache[8];
	VkDescriptorSetLayout layoutsForAlloc[ARRAY_SIZE(setCache)];
	for (int i = 0; i < ARRAY_SIZE(setCache); i++) {
		layoutsForAlloc[i] = descriptorSetLayout;
	}
	int setsUsed = ARRAY_SIZE(setCache);  // To allocate immediately.

	// This will write all descriptors.
	// Initially, we just do a simple look-back comparing to the previous descriptor to avoid sequential dupes.
	// In theory, we could multithread this. Gotta be a lot of descriptors for that to be worth it though.

	// Initially, let's do naive single desc set writes.
	VkWriteDescriptorSet writes[MAX_DESC_SET_BINDINGS];
	VkDescriptorImageInfo imageInfo[MAX_DESC_SET_BINDINGS];  // just picked a practical number
	VkDescriptorBufferInfo bufferInfo[MAX_DESC_SET_BINDINGS];

	// Preinitialize fields that won't change.
	for (size_t i = 0; i < ARRAY_SIZE(writes); i++) {
		writes[i].descriptorCount = 1;
		writes[i].dstArrayElement = 0;
		writes[i].pTexelBufferView = nullptr;
		writes[i].pNext = nullptr;
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	}

	size_t start = data.flushedDescriptors_;
	int writeCount = 0, dedupCount = 0;

	for (size_t index = start; index < descSets.size(); index++) {
		auto &d = descSets[index];

		// This is where we look up to see if we already have an identical descriptor previously in the array.
		// We could do a simple custom hash map here that doesn't handle collisions, since those won't matter.
		// Instead, for now we just check history one item backwards. Good enough, it seems.
		if (index > start + 1) {
			if (descSets[index - 1].count == d.count) {
				if (!memcmp(descData.data() + d.offset, descData.data() + descSets[index - 1].offset, d.count * sizeof(PackedDescriptor))) {
					d.set = descSets[index - 1].set;
					dedupCount++;
					continue;
				}
			}
		}

		if (setsUsed < ARRAY_SIZE(setCache)) {
			d.set = setCache[setsUsed++];
		} else {
			// Allocate in small batches.
			bool success = pool.Allocate(setCache, ARRAY_SIZE(setCache), layoutsForAlloc);
			_dbg_assert_(success);
			d.set = setCache[0];
			setsUsed = 1;
		}

		// TODO: Build up bigger batches of writes.
		const PackedDescriptor *data = descData.begin() + d.offset;
		int numWrites = 0;
		int numBuffers = 0;
		int numImages = 0;
		for (int i = 0; i < d.count; i++) {
			if (!data[i].image.view) {  // This automatically also checks for an null buffer due to the union.
				continue;
			}
			switch (this->bindingTypes[i]) {
			case BindingType::COMBINED_IMAGE_SAMPLER:
				_dbg_assert_(data[i].image.sampler != VK_NULL_HANDLE);
				_dbg_assert_(data[i].image.view != VK_NULL_HANDLE);
				imageInfo[numImages].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfo[numImages].imageView = data[i].image.view;
				imageInfo[numImages].sampler = data[i].image.sampler;
				writes[numWrites].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[numWrites].pImageInfo = &imageInfo[numImages];
				writes[numWrites].pBufferInfo = nullptr;
				numImages++;
				break;
			case BindingType::STORAGE_IMAGE_COMPUTE:
				_dbg_assert_(data[i].image.view != VK_NULL_HANDLE);
				imageInfo[numImages].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				imageInfo[numImages].imageView = data[i].image.view;
				imageInfo[numImages].sampler = VK_NULL_HANDLE;
				writes[numWrites].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				writes[numWrites].pImageInfo = &imageInfo[numImages];
				writes[numWrites].pBufferInfo = nullptr;
				numImages++;
				break;
			case BindingType::STORAGE_BUFFER_VERTEX:
			case BindingType::STORAGE_BUFFER_COMPUTE:
				_dbg_assert_(data[i].buffer.buffer != VK_NULL_HANDLE);
				bufferInfo[numBuffers].buffer = data[i].buffer.buffer;
				bufferInfo[numBuffers].range = data[i].buffer.range;
				bufferInfo[numBuffers].offset = data[i].buffer.offset;
				writes[numWrites].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writes[numWrites].pBufferInfo = &bufferInfo[numBuffers];
				writes[numWrites].pImageInfo = nullptr;
				numBuffers++;
				break;
			case BindingType::UNIFORM_BUFFER_DYNAMIC_ALL:
			case BindingType::UNIFORM_BUFFER_DYNAMIC_VERTEX:
				_dbg_assert_(data[i].buffer.buffer != VK_NULL_HANDLE);
				bufferInfo[numBuffers].buffer = data[i].buffer.buffer;
				bufferInfo[numBuffers].range = data[i].buffer.range;
				bufferInfo[numBuffers].offset = 0;
				writes[numWrites].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				writes[numWrites].pBufferInfo = &bufferInfo[numBuffers];
				writes[numWrites].pImageInfo = nullptr;
				numBuffers++;
				break;
			}
			writes[numWrites].dstBinding = i;
			writes[numWrites].dstSet = d.set;
			numWrites++;
		}

		vkUpdateDescriptorSets(vulkan->GetDevice(), numWrites, writes, 0, nullptr);

		writeCount++;
	}

	data.flushedDescriptors_ = (int)descSets.size();
	profile->descriptorsWritten += writeCount;
	profile->descriptorsDeduped += dedupCount;
}

void VulkanRenderManager::SanityCheckPassesOnAdd() {
#if _DEBUG
	// Check that we don't have any previous passes that write to the backbuffer, that must ALWAYS be the last one.
	for (int i = 0; i < (int)steps_.size(); i++) {
		if (steps_[i]->stepType == VKRStepType::RENDER) {
			_dbg_assert_msg_(steps_[i]->render.framebuffer != nullptr, "Adding second backbuffer pass? Not good!");
		}
	}
#endif
}
