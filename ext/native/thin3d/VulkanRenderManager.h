#pragma once

// VulkanRenderManager takes the role that a GL driver does of sequencing and optimizing render passes.
// Only draws and binds are handled here, resource creation and allocations are handled as normal -
// that's the nice thing with Vulkan.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "Common/Vulkan/VulkanContext.h"
#include "math/dataconv.h"
#include "math/math_util.h"
#include "thin3d/DataFormat.h"
#include "thin3d/VulkanQueueRunner.h"

// Simple independent framebuffer image. Gets its own allocation, we don't have that many framebuffers so it's fine
// to let them have individual non-pooled allocations. Until it's not fine. We'll see.
struct VKRImage {
	VkImage image;
	VkImageView imageView;
	VkDeviceMemory memory;
	VkImageLayout layout;
	VkFormat format;
};
void CreateImage(VulkanContext *vulkan, VkCommandBuffer cmd, VKRImage &img, int width, int height, VkFormat format, VkImageLayout initialLayout, bool color);

class VKRFramebuffer {
public:
	VKRFramebuffer(VulkanContext *vk, VkCommandBuffer initCmd, VkRenderPass renderPass, int _width, int _height) : vulkan_(vk) {
		width = _width;
		height = _height;

		CreateImage(vulkan_, initCmd, color, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
		CreateImage(vulkan_, initCmd, depth, width, height, vulkan_->GetDeviceInfo().preferredDepthStencilFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);

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

		vkCreateFramebuffer(vulkan_->GetDevice(), &fbci, nullptr, &framebuf);
	}

	~VKRFramebuffer() {
		if (color.image)
			vulkan_->Delete().QueueDeleteImage(color.image);
		if (depth.image)
			vulkan_->Delete().QueueDeleteImage(depth.image);
		if (color.imageView)
			vulkan_->Delete().QueueDeleteImageView(color.imageView);
		if (depth.imageView)
			vulkan_->Delete().QueueDeleteImageView(depth.imageView);
		if (color.memory)
			vulkan_->Delete().QueueDeleteDeviceMemory(color.memory);
		if (depth.memory)
			vulkan_->Delete().QueueDeleteDeviceMemory(depth.memory);
		if (framebuf)
			vulkan_->Delete().QueueDeleteFramebuffer(framebuf);
	}

	int numShadows = 1;  // TODO: Support this.

	VkFramebuffer framebuf = VK_NULL_HANDLE;
	VKRImage color{};
	VKRImage depth{};
	int width = 0;
	int height = 0;

	VulkanContext *vulkan_;
};

enum class VKRRunType {
	END,
	SYNC,
};

class VulkanRenderManager {
public:
	VulkanRenderManager(VulkanContext *vulkan);
	~VulkanRenderManager();

	void ThreadFunc();

	// Makes sure that the GPU has caught up enough that we can start writing buffers of this frame again.
	void BeginFrame();
	// Can run on a different thread!
	void Finish();
	void Run(int frame);

	// Zaps queued up commands. Use if you know there's a risk you've queued up stuff that has already been deleted. Can happen during in-game shutdown.
	void Wipe();

	void BindFramebufferAsRenderTarget(VKRFramebuffer *fb, VKRRenderPassAction color, VKRRenderPassAction depth, VKRRenderPassAction stencil, uint32_t clearColor, float clearDepth, uint8_t clearStencil);
	VkImageView BindFramebufferAsTexture(VKRFramebuffer *fb, int binding, int aspectBit, int attachment);
	bool CopyFramebufferToMemorySync(VKRFramebuffer *src, int aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride);
	void CopyImageToMemorySync(VkImage image, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride);

	void CopyFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkOffset2D dstPos, int aspectMask);
	void BlitFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkRect2D dstRect, int aspectMask, VkFilter filter);

	void BindPipeline(VkPipeline pipeline) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		_dbg_assert_(G3D, pipeline != VK_NULL_HANDLE);
		VkRenderData data{ VKRRenderCommand::BIND_PIPELINE };
		data.pipeline.pipeline = pipeline;
		curRenderStep_->commands.push_back(data);
	}

	void SetViewport(const VkViewport &vp) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::VIEWPORT };
		data.viewport.vp.x = vp.x;
		data.viewport.vp.y = vp.y;
		data.viewport.vp.width = vp.width;
		data.viewport.vp.height = vp.height;
		// We can't allow values outside this range unless we use VK_EXT_depth_range_unrestricted.
		// Sometimes state mapping produces 65536/65535 which is slightly outside.
		data.viewport.vp.maxDepth = clamp_value(vp.maxDepth, 0.0f, 1.0f);
		data.viewport.vp.minDepth = clamp_value(vp.minDepth, 0.0f, 1.0f);
		curRenderStep_->commands.push_back(data);
	}

	void SetScissor(const VkRect2D &rc) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::SCISSOR };
		data.scissor.scissor = rc;
		curRenderStep_->commands.push_back(data);
	}

	void SetStencilParams(uint8_t writeMask, uint8_t compareMask, uint8_t refValue) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::STENCIL };
		data.stencil.stencilWriteMask = writeMask;
		data.stencil.stencilCompareMask = compareMask;
		data.stencil.stencilRef = refValue;
		curRenderStep_->commands.push_back(data);
	}

	void SetBlendFactor(float color[4]) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::BLEND };
		CopyFloat4(data.blendColor.color, color);
		curRenderStep_->commands.push_back(data);
	}

	void PushConstants(VkPipelineLayout pipelineLayout, VkShaderStageFlags stages, int offset, int size, void *constants) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		assert(size + offset < 40);
		VkRenderData data{ VKRRenderCommand::PUSH_CONSTANTS };
		data.push.pipelineLayout = pipelineLayout;
		data.push.stages = stages;
		data.push.offset = offset;
		data.push.size = size;
		memcpy(data.push.data, constants, size);
		curRenderStep_->commands.push_back(data);
	}

	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask);

	void Draw(VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, const uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, int count) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::DRAW };
		data.draw.count = count;
		data.draw.pipelineLayout = layout;
		data.draw.ds = descSet;
		data.draw.vbuffer = vbuffer;
		data.draw.voffset = voffset;
		data.draw.numUboOffsets = numUboOffsets;
		assert(numUboOffsets <= ARRAY_SIZE(data.draw.uboOffsets));
		for (int i = 0; i < numUboOffsets; i++)
			data.draw.uboOffsets[i] = uboOffsets[i];
		curRenderStep_->commands.push_back(data);
		curRenderStep_->render.numDraws++;
	}

	void DrawIndexed(VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, const uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, VkBuffer ibuffer, int ioffset, int count, int numInstances, VkIndexType indexType) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::DRAW_INDEXED };
		data.drawIndexed.count = count;
		data.drawIndexed.instances = numInstances;
		data.drawIndexed.pipelineLayout = layout;
		data.drawIndexed.ds = descSet;
		data.drawIndexed.vbuffer = vbuffer;
		data.drawIndexed.voffset = voffset;
		data.drawIndexed.ibuffer = ibuffer;
		data.drawIndexed.ioffset = ioffset;
		data.drawIndexed.numUboOffsets = numUboOffsets;
		assert(numUboOffsets <= ARRAY_SIZE(data.drawIndexed.uboOffsets));
		for (int i = 0; i < numUboOffsets; i++)
			data.drawIndexed.uboOffsets[i] = uboOffsets[i];
		data.drawIndexed.indexType = indexType;
		curRenderStep_->commands.push_back(data);
		curRenderStep_->render.numDraws++;
	}

	VkCommandBuffer GetInitCmd();

	VkRenderPass GetBackbufferRenderPass() {
		return queueRunner_.GetBackbufferRenderPass();
	}
	VkRenderPass GetFramebufferRenderPass() {
		return queueRunner_.GetFramebufferRenderPass();
	}
	VkRenderPass GetCompatibleRenderPass() {
		if (curRenderStep_ && curRenderStep_->render.framebuffer != nullptr) {
			return queueRunner_.GetFramebufferRenderPass();
		} else {
			return queueRunner_.GetBackbufferRenderPass();
		}
	}

	void CreateBackbuffers();
	void DestroyBackbuffers();

	bool HasBackbuffers() {
		return !framebuffers_.empty();
	}

	void SetSplitSubmit(bool split) {
		splitSubmit_ = split;
	}

	VulkanContext *GetVulkanContext() {
		return vulkan_;
	}

	// Be careful with this. Only meant to be used for fetching render passes for shader cache initialization.
	VulkanQueueRunner *GetQueueRunner() {
		return &queueRunner_;
	}

private:
	bool InitBackbufferFramebuffers(int width, int height);
	bool InitDepthStencilBuffer(VkCommandBuffer cmd);  // Used for non-buffered rendering.
	void BeginSubmitFrame(int frame);
	void EndSubmitFrame(int frame);
	void Submit(int frame, bool triggerFence);

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void FlushSync();
	void EndSyncFrame(int frame);

	void StopThread();

	// Permanent objects
	VkSemaphore acquireSemaphore_;
	VkSemaphore renderingCompleteSemaphore_;

	// Per-frame data, round-robin so we can overlap submission with execution of the previous frame.
	struct FrameData {
		std::mutex push_mutex;
		std::condition_variable push_condVar;

		std::mutex pull_mutex;
		std::condition_variable pull_condVar;

		bool readyForFence = true;
		bool readyForRun = false;
		bool skipSwap = false;
		VKRRunType type = VKRRunType::END;

		VkFence fence;
		// These are on different threads so need separate pools.
		VkCommandPool cmdPoolInit;
		VkCommandPool cmdPoolMain;
		VkCommandBuffer initCmd;
		VkCommandBuffer mainCmd;
		bool hasInitCommands = false;
		std::vector<VKRStep *> steps;

		// Swapchain.
		bool hasBegun = false;
		uint32_t curSwapchainImage = -1;
	};
	FrameData frameData_[VulkanContext::MAX_INFLIGHT_FRAMES];

	// Submission time state
	int curWidth_;
	int curHeight_;
	bool insideFrame_ = false;
	VKRStep *curRenderStep_ = nullptr;
	std::vector<VKRStep *> steps_;
	bool splitSubmit_ = false;

	// Execution time state
	bool run_ = true;
	VulkanContext *vulkan_;
	std::thread thread_;
	std::mutex mutex_;
	int threadInitFrame_ = 0;
	VulkanQueueRunner queueRunner_;

	// Swap chain management
	struct SwapchainImageData {
		VkImage image;
		VkImageView view;
	};
	std::vector<VkFramebuffer> framebuffers_;
	std::vector<SwapchainImageData> swapchainImages_;
	uint32_t swapchainImageCount_;
	struct DepthBufferInfo {
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
	};
	DepthBufferInfo depth_;

	// This works great - except see issue #10097. WTF?
	bool useThread_ = true;
};
