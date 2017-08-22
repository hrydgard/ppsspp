#pragma once

#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "Common/Vulkan/VulkanContext.h"
#include "math/dataconv.h"
#include "thin3d/thin3d.h"

// Takes the role that a GL driver does of sequencing and optimizing render passes.
// Only draws and binds are handled here, resource creation and allocations are handled as normal -
// that's the nice thing with Vulkan.

// The cool thing is that you can Flush on a different thread than you record the commands on!

enum class VKRRenderCommand : uint8_t {
	STENCIL,
	BLEND,
	VIEWPORT,
	SCISSOR,
	CLEAR,
	DRAW,
	DRAW_INDEXED,
};

struct VkRenderData {
	VKRRenderCommand cmd;
	union {
		struct {
			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSet ds;
			int numUboOffsets;
			uint32_t uboOffsets[3];
			VkBuffer vbuffer;
			VkDeviceSize voffset;
			int count;
		} draw;
		struct {
			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkDescriptorSet ds;
			int numUboOffsets;
			uint32_t uboOffsets[3];
			VkBuffer vbuffer;  // might need to increase at some point
			VkDeviceSize voffset;
			VkBuffer ibuffer;
			VkDeviceSize ioffset;
			int16_t count;
			int16_t instances;
			VkIndexType indexType;
		} drawIndexed;
		struct {
			uint32_t clearColor;
			float clearZ;
			int clearStencil;
			int clearMask;   // VK_IMAGE_ASPECT_COLOR_BIT etc
		} clear;

		struct {
			VkViewport vp;
		} viewport;
		struct {
			VkRect2D scissor;
		} scissor;
		struct {
			uint8_t stencilWriteMask;
			uint8_t stencilCompareMask;
			uint8_t stencilRef;
		} stencil;
		struct {
			float color[4];
		} blendColor;
		struct {

		} beginRp;
		struct {

		} endRp;
	};
};

enum class VKRStepType : uint8_t {
	RENDER,
	COPY,
	BLIT,
	READBACK,
};

class VKRFramebuffer;

enum class VKRRenderPassAction {
	DONT_CARE,
	CLEAR,
	KEEP,
};

struct TransitionRequest {
	VKRFramebuffer *fb;
	VkImageLayout targetLayout;
};

struct VKRStep {
	VKRStep(VKRStepType _type) : stepType(_type) {}
	VKRStepType stepType;
	std::vector<VkRenderData> commands;
	std::vector<TransitionRequest> preTransitions;
	union {
		struct {
			VKRFramebuffer *framebuffer;
			VKRRenderPassAction color;
			VKRRenderPassAction depthStencil;
			uint32_t clearColor;
			float clearDepth;
			int clearStencil;
			int numDraws;
			VkImageLayout finalColorLayout;
		} render;
		struct {
			VKRFramebuffer *src;
			VKRFramebuffer *dst;
			VkRect2D srcRect;
			VkOffset2D dstPos;
			int aspectMask;
		} copy;
		struct {
			VKRFramebuffer *src;
			VKRFramebuffer *dst;
			VkRect2D srcRect;
			VkRect2D dstRect;
			int aspectMask;
			VkFilter filter;
		} blit;
		struct {
			VKRFramebuffer *src;
			void *destPtr;
			VkRect2D srcRect;
		} readback;
	};
};

// Simple independent framebuffer image. Gets its own allocation, we don't have that many framebuffers so it's fine
// to let them have individual non-pooled allocations. Until it's not fine. We'll see.
struct VKRImage {
	VkImage image;
	VkImageView imageView;
	VkDeviceMemory memory;
	VkImageLayout layout;
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
		vulkan_->Delete().QueueDeleteImage(color.image);
		vulkan_->Delete().QueueDeleteImage(depth.image);
		vulkan_->Delete().QueueDeleteImageView(color.imageView);
		vulkan_->Delete().QueueDeleteImageView(depth.imageView);
		vulkan_->Delete().QueueDeleteDeviceMemory(color.memory);
		vulkan_->Delete().QueueDeleteDeviceMemory(depth.memory);
		vulkan_->Delete().QueueDeleteFramebuffer(framebuf);
	}

	int numShadows = 1;  // TODO: Support this.

	VkFramebuffer framebuf = VK_NULL_HANDLE;
	VKRImage color{};
	VKRImage depth{};
	int width = 0;
	int height = 0;

private:
	VulkanContext *vulkan_;
};

class VulkanRenderManager {
public:
	VulkanRenderManager(VulkanContext *vulkan);
	~VulkanRenderManager();

	void ThreadFunc();

	// Makes sure that the GPU has caught up enough that we can start writing buffers of this frame again.
	void BeginFrame();
	void EndFrame(int frame);

	void BindFramebufferAsRenderTarget(VKRFramebuffer *fb, VKRRenderPassAction color, VKRRenderPassAction depth, uint32_t clearColor, float clearDepth, uint8_t clearStencil);
	VkImageView BindFramebufferAsTexture(VKRFramebuffer *fb, int binding, int aspectBit, int attachment);

	void CopyFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkOffset2D dstPos, int aspectMask);
	void BlitFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkRect2D dstRect, int aspectMask, VkFilter filter);

	void SetViewport(const VkViewport &vp) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::VIEWPORT };
		data.viewport.vp = vp;
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

	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask);

	void Draw(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, const uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, int count) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::DRAW };
		data.draw.count = count;
		data.draw.pipeline = pipeline;
		data.draw.pipelineLayout = layout;
		data.draw.ds = descSet;
		data.draw.vbuffer = vbuffer;
		data.draw.voffset = voffset;
		data.draw.numUboOffsets = numUboOffsets;
		for (int i = 0; i < numUboOffsets; i++)
			data.draw.uboOffsets[i] = uboOffsets[i];
		curRenderStep_->commands.push_back(data);
		curRenderStep_->render.numDraws++;
	}

	void DrawIndexed(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, const uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, VkBuffer ibuffer, int ioffset, int count, int numInstances, VkIndexType indexType) {
		_dbg_assert_(G3D, curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData data{ VKRRenderCommand::DRAW_INDEXED };
		data.drawIndexed.count = count;
		data.drawIndexed.instances = numInstances;
		data.drawIndexed.pipeline = pipeline;
		data.drawIndexed.pipelineLayout = layout;
		data.drawIndexed.ds = descSet;
		data.drawIndexed.vbuffer = vbuffer;
		data.drawIndexed.voffset = voffset;
		data.drawIndexed.ibuffer = ibuffer;
		data.drawIndexed.ioffset = ioffset;
		data.drawIndexed.numUboOffsets = numUboOffsets;
		for (int i = 0; i < numUboOffsets; i++)
			data.drawIndexed.uboOffsets[i] = uboOffsets[i];
		data.drawIndexed.indexType = indexType;
		curRenderStep_->commands.push_back(data);
		curRenderStep_->render.numDraws++;
	}

	// Can run on a different thread! Just make sure to use BeginFrameWrites.
	void Flush();
	void Run(int frame);

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void Sync();

	VkCommandBuffer GetInitCmd();
	VkRenderPass GetBackbufferRenderpass() const {
		return backbufferRenderPass_;
	}
	VkRenderPass GetRenderPass(int i) const {
		return renderPasses_[i];
	}
	VkRenderPass GetCompatibleRenderpass() const {
		if (curRenderStep_ && curRenderStep_->render.framebuffer != nullptr) {
			return GetRenderPass(0);
		} else {
			return backbufferRenderPass_;
		}
	}

	void CreateBackbuffers();
	void DestroyBackbuffers();

private:
	void InitBackbufferFramebuffers(int width, int height);
	void InitBackbufferRenderPass();
	void InitRenderpasses();
	void InitDepthStencilBuffer(VkCommandBuffer cmd);  // Used for non-buffered rendering.

	void PerformBindFramebufferAsRenderTarget(const VKRStep &pass, VkCommandBuffer cmd);

	void PerformRenderPass(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformCopy(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformBlit(const VKRStep &pass, VkCommandBuffer cmd);

	inline int RPIndex(VKRRenderPassAction color, VKRRenderPassAction depth) {
		return (int)depth * 3 + (int)color;
	}

	static void SetupTransitionToTransferSrc(VKRImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect);
	static void SetupTransitionToTransferDst(VKRImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect);

	// Permanent objects
	VkSemaphore acquireSemaphore_;
	VkSemaphore renderingCompleteSemaphore;
	VkRenderPass backbufferRenderPass_ = VK_NULL_HANDLE;
	// Renderpasses, all combinations of preserving or clearing or dont-care-ing fb contents.
	// TODO: Create these on demand.
	VkRenderPass renderPasses_[9];

	// Per-frame data, round-robin so we can overlap submission with execution of the previous frame.
	struct FrameData {
		bool readyForFence = true;
		VkFence fence;
		// These are on different threads so need separate pools.
		VkCommandPool cmdPoolInit;
		VkCommandPool cmdPoolMain;
		VkCommandBuffer initCmd;
		VkCommandBuffer mainCmd;
		bool hasInitCommands = false;
	};
	FrameData frameData_[VulkanContext::MAX_INFLIGHT_FRAMES];

	// Submission time state
	int curWidth_;
	int curHeight_;
	bool insideFrame_ = false;
	VKRStep *curRenderStep_;
	VKRFramebuffer *boundFramebuffer_;
	std::vector<VKRStep *> steps_;

	// Execution time state
	int threadFrame_;
	volatile bool frameAvailable_ = false;
	bool run_ = true;
	VulkanContext *vulkan_;
	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable condVar_;
	std::vector<VKRStep *> stepsOnThread_;
	VkFramebuffer curFramebuffer_ = VK_NULL_HANDLE;

	// Swap chain management
	struct SwapchainImageData {
		VkImage image;
		VkImageView view;
	};
	std::vector<VkFramebuffer> framebuffers_;
	std::vector<SwapchainImageData> swapchainImages_;
	uint32_t swapchainImageCount_;
	uint32_t curSwapchainImage_ = 0;
	struct DepthBufferInfo {
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
	};
	DepthBufferInfo depth_;
};
