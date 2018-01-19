#pragma once

#include <cstdint>

#include "Common/Hashmaps.h"
#include "Common/Vulkan/VulkanContext.h"
#include "math/dataconv.h"
#include "thin3d/DataFormat.h"

class VKRFramebuffer;
struct VKRImage;

enum class VKRRenderCommand : uint8_t {
	BIND_PIPELINE,
	STENCIL,
	BLEND,
	VIEWPORT,
	SCISSOR,
	CLEAR,
	DRAW,
	DRAW_INDEXED,
	PUSH_CONSTANTS,
};

struct VkRenderData {
	VKRRenderCommand cmd;
	union {
		struct {
			VkPipeline pipeline;
		} pipeline;
		struct {
			VkPipelineLayout pipelineLayout;
			VkDescriptorSet ds;
			int numUboOffsets;
			uint32_t uboOffsets[3];
			VkBuffer vbuffer;
			VkDeviceSize voffset;
			uint32_t count;
		} draw;
		struct {
			VkPipelineLayout pipelineLayout;
			VkDescriptorSet ds;
			int numUboOffsets;
			uint32_t uboOffsets[3];
			VkBuffer vbuffer;  // might need to increase at some point
			VkDeviceSize voffset;
			VkBuffer ibuffer;
			VkDeviceSize ioffset;
			uint32_t count;
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
			VkPipelineLayout pipelineLayout;
			VkShaderStageFlags stages;
			uint8_t offset;
			uint8_t size;
			uint8_t data[40];  // Should be enough for now.
		} push;
	};
};

enum class VKRStepType : uint8_t {
	RENDER,
	COPY,
	BLIT,
	READBACK,
	READBACK_IMAGE,
};

enum class VKRRenderPassAction : uint8_t {
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
			VKRRenderPassAction depth;
			VKRRenderPassAction stencil;
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
			int aspectMask;
			VKRFramebuffer *src;
			VkRect2D srcRect;
		} readback;
		struct {
			VkImage image;
			VkRect2D srcRect;
			int mipLevel;
		} readback_image;
	};
};

class VulkanQueueRunner {
public:
	VulkanQueueRunner(VulkanContext *vulkan) : vulkan_(vulkan), renderPasses_(16) {}
	void SetBackbuffer(VkFramebuffer fb, VkImage img) {
		backbuffer_ = fb;
		backbufferImage_ = img;
	}
	void RunSteps(VkCommandBuffer cmd, const std::vector<VKRStep *> &steps);
	void LogSteps(const std::vector<VKRStep *> &steps);

	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	VkRenderPass GetBackbufferRenderPass() const {
		return backbufferRenderPass_;
	}
	VkRenderPass GetRenderPass(VKRRenderPassAction colorLoadAction, VKRRenderPassAction depthLoadAction, VKRRenderPassAction stencilLoadAction);

	inline int RPIndex(VKRRenderPassAction color, VKRRenderPassAction depth) {
		return (int)depth * 3 + (int)color;
	}

	void CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels);

private:
	void InitBackbufferRenderPass();

	void PerformBindFramebufferAsRenderTarget(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformRenderPass(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformCopy(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformBlit(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformReadback(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformReadbackImage(const VKRStep &pass, VkCommandBuffer cmd);

	void LogRenderPass(const VKRStep &pass);
	void LogCopy(const VKRStep &pass);
	void LogBlit(const VKRStep &pass);
	void LogReadback(const VKRStep &pass);
	void LogReadbackImage(const VKRStep &pass);

	void ResizeReadbackBuffer(VkDeviceSize requiredSize);

	static void SetupTransitionToTransferSrc(VKRImage &img, VkImageMemoryBarrier &barrier, VkPipelineStageFlags &stage, VkImageAspectFlags aspect);
	static void SetupTransitionToTransferDst(VKRImage &img, VkImageMemoryBarrier &barrier, VkPipelineStageFlags &stage, VkImageAspectFlags aspect);

	VulkanContext *vulkan_;

	VkFramebuffer backbuffer_;
	VkImage backbufferImage_;
	VkFramebuffer curFramebuffer_ = VK_NULL_HANDLE;

	VkRenderPass backbufferRenderPass_ = VK_NULL_HANDLE;

	struct RPKey {
		VKRRenderPassAction colorAction;
		VKRRenderPassAction depthAction;
		VKRRenderPassAction stencilAction;
	};

	// Renderpasses, all combinations of preserving or clearing or dont-care-ing fb contents.
	// TODO: Create these on demand.
	DenseHashMap<RPKey, VkRenderPass, (VkRenderPass)VK_NULL_HANDLE> renderPasses_;

	// Readback buffer. Currently we only support synchronous readback, so we only really need one.
	// We size it generously.
	VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
	VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
	VkDeviceSize readbackBufferSize_ = 0;
};
