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

enum class VkRenderCmd : uint8_t {
	STENCIL,
	BLEND,
	VIEWPORT,
	SCISSOR,
	CLEAR,
	DRAW,
	DRAW_INDEXED,
};

struct VkRenderData {
	VkRenderCmd cmd;
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

enum class VKStepType : uint8_t {
	RENDER,
	COPY,
	BLIT,
	READBACK,
};

class VKRFramebuffer;

enum class RenderPassAction {
	DONT_CARE,
	CLEAR,
	KEEP,
};

struct VKRStep {
	VKRStep(VKStepType _type) : stepType(_type) {}
	VKStepType stepType;
	std::vector<VkRenderData> commands;
	union {
		struct {
			VKRFramebuffer *framebuffer;
			RenderPassAction color;
			RenderPassAction depthStencil;
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
struct VKImage {
	VkImage image;
	VkImageView imageView;
	VkDeviceMemory memory;
	VkImageLayout layout;
};
void CreateImage(VulkanContext *vulkan, VkCommandBuffer cmd, VKImage &img, int width, int height, VkFormat format, VkImageLayout initialLayout, bool color);

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
	VKImage color{};
	VKImage depth{};
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
	void BeginFrameWrites();
	void EndFrame();

	void BindFramebufferAsRenderTarget(VKRFramebuffer *fb);
	VkImageView BindFramebufferAsTexture(VKRFramebuffer *fb, int binding, int aspectBit, int attachment);

	void CopyFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkOffset2D dstPoint);
	void BlitFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkRect2D dstRect, VkFilter filter);

	void BeginRenderPass();

	void SetViewport(const VkViewport &vp) {
		_dbg_assert_(G3D, curStep_ && curStep_->stepType == VKStepType::RENDER);
		VkRenderData data{ VkRenderCmd::VIEWPORT };
		data.viewport.vp = vp;
		curStep_->commands.push_back(data);
	}

	void SetScissor(const VkRect2D &rc) {
		_dbg_assert_(G3D, curStep_ && curStep_->stepType == VKStepType::RENDER);
		VkRenderData data{ VkRenderCmd::SCISSOR };
		data.scissor.scissor = rc;
		curStep_->commands.push_back(data);
	}

	void SetBlendFactor(float color[4]) {
		_dbg_assert_(G3D, curStep_ && curStep_->stepType == VKStepType::RENDER);
		VkRenderData data{ VkRenderCmd::BLEND };
		CopyFloat4(data.blendColor.color, color);
		curStep_->commands.push_back(data);
	}

	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask);

	void Draw(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, int count) {
		_dbg_assert_(G3D, curStep_ && curStep_->stepType == VKStepType::RENDER);
		VkRenderData data{ VkRenderCmd::DRAW };
		data.draw.count = count;
		data.draw.pipeline = pipeline;
		data.draw.pipelineLayout = layout;
		data.draw.ds = descSet;
		data.draw.vbuffer = vbuffer;
		data.draw.voffset = voffset;
		data.draw.numUboOffsets = numUboOffsets;
		for (int i = 0; i < numUboOffsets; i++)
			data.draw.uboOffsets[i] = uboOffsets[i];
		curStep_->commands.push_back(data);
		curStep_->render.numDraws++;
	}

	void DrawIndexed(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, VkBuffer ibuffer, int ioffset, int count, VkIndexType indexType) {
		_dbg_assert_(G3D, curStep_ && curStep_->stepType == VKStepType::RENDER);
		VkRenderData data{ VkRenderCmd::DRAW_INDEXED };
		data.drawIndexed.count = count;
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
		curStep_->commands.push_back(data);
		curStep_->render.numDraws++;
	}

	// Can run on a different thread! Just make sure to use BeginFrameWrites.
	void Flush();

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void Sync();

	std::vector<VKRStep *> steps_;
	std::vector<VKRStep *> stepsOnThread_;
	std::mutex rpLock_;

	VKRStep *curStep_;

	VkCommandBuffer GetInitCmd();
	VkCommandBuffer GetSurfaceCommandBuffer() {
		return frameData_[vulkan_->GetCurFrame()].mainCmd;
	}
	VkRenderPass GetSurfaceRenderPass() const {
		return backbufferRenderPass_;
	}
	VkRenderPass GetRenderPass(int i) const {
		return renderPasses_[i];
	}

private:
	void InitFramebuffers();
	void InitSurfaceRenderPass();
	void InitRenderpasses();
	void InitDepthStencilBuffer(VkCommandBuffer cmd);  // Used for non-buffered rendering.

	// The surface render pass is special because it has to acquire the backbuffer, and may thus "block".
	// Use the returned command buffer to enqueue commands that render to the backbuffer.
	// To render to other buffers first, you can submit additional commandbuffers using QueueBeforeSurfaceRender(cmd).
	void BeginSurfaceRenderPass(VkCommandBuffer cmd, VkClearValue clear_value);
	// May eventually need the ability to break and resume the backbuffer render pass in a few rare cases.
	void EndSurfaceRenderPass(VkCommandBuffer cmd);
	
	void EndCurrentRenderpass(VkCommandBuffer cmd);

	void PerformBindFramebufferAsRenderTarget(const VKRStep &pass, VkCommandBuffer cmd);

	void PerformRenderPass(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformCopy(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformBlit(const VKRStep &pass, VkCommandBuffer cmd);

	inline int RPIndex(RenderPassAction color, RenderPassAction depth) {
		return (int)depth * 3 + (int)color;
	}

	static void SetupTransitionToTransferSrc(VKImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect);
	static void SetupTransitionToTransferDst(VKImage &img, VkImageMemoryBarrier &barrier, VkImageAspectFlags aspect);

	VkSemaphore acquireSemaphore_;
	VkSemaphore renderingCompleteSemaphore;

	// Renderpasses, all combinations of preserving or clearing or dont-care-ing fb contents.
	// TODO: Create these on demand.
	VkRenderPass renderPasses_[9];

	struct FrameData {
		VkFence fence;
		VkCommandPool cmdPool;
		VkCommandBuffer initCmd;
		VkCommandBuffer mainCmd;
		bool hasInitCommands;
	};
	FrameData frameData_[VulkanContext::MAX_INFLIGHT_FRAMES];

	VulkanContext *vulkan_;
	int curWidth_;
	int curHeight_;

	std::thread submissionThread;
	std::mutex mutex_;
	std::condition_variable condVar_;

	struct SwapchainImageData {
		VkImage image;
		VkImageView view;
	};
	std::vector<VkFramebuffer> framebuffers_;
	std::vector<SwapchainImageData> swapchainImages_;
	uint32_t swapchainImageCount;
	uint32_t current_buffer = 0;
	VkRenderPass backbufferRenderPass_ = VK_NULL_HANDLE;
	struct DepthBufferInfo {
		VkFormat format;
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
	};
	DepthBufferInfo depth_;
	// Interpreter state
	VkFramebuffer curFramebuffer_;
	VkRenderPass curRenderPass_;

};
