#pragma once

#include <cstdint>

#include "Common/Hashmaps.h"
#include "Common/Vulkan/VulkanContext.h"
#include "math/dataconv.h"
#include "thin3d/DataFormat.h"

class VKRFramebuffer;
struct VKRImage;

enum {
	QUEUE_HACK_MGS2_ACID = 1,
	QUEUE_HACK_SONIC = 2,
};

enum class VKRRenderCommand : uint8_t {
	REMOVED,
	BIND_PIPELINE,
	STENCIL,
	BLEND,
	VIEWPORT,
	SCISSOR,
	CLEAR,
	DRAW,
	DRAW_INDEXED,
	PUSH_CONSTANTS,
	NUM_RENDER_COMMANDS,
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
			uint32_t uboOffsets[2];
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
	RENDER_SKIP,
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
			// Downloads and textures from this pass.
			int numReads;
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

	// RunSteps can modify steps but will leave it in a valid state.
	void RunSteps(VkCommandBuffer cmd, std::vector<VKRStep *> &steps);
	void LogSteps(const std::vector<VKRStep *> &steps);

	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	VkRenderPass GetBackbufferRenderPass() const {
		return backbufferRenderPass_;
	}

	// Get a render pass that's compatible with all our framebuffers.
	// Note that it's precached, cannot look up in the map as this might be on another thread.
	VkRenderPass GetFramebufferRenderPass() const {
		return framebufferRenderPass_;
	}

	inline int RPIndex(VKRRenderPassAction color, VKRRenderPassAction depth) {
		return (int)depth * 3 + (int)color;
	}

	void CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels);

	struct RPKey {
		VKRRenderPassAction colorLoadAction;
		VKRRenderPassAction depthLoadAction;
		VKRRenderPassAction stencilLoadAction;
		VkImageLayout prevColorLayout;
		VkImageLayout prevDepthLayout;
		VkImageLayout finalColorLayout;
		// TODO: Also pre-transition depth, for copies etc.
	};

	// Only call this from the render thread! Also ok during initialization (LoadCache).
	VkRenderPass GetRenderPass(
		VKRRenderPassAction colorLoadAction, VKRRenderPassAction depthLoadAction, VKRRenderPassAction stencilLoadAction,
		VkImageLayout prevColorLayout, VkImageLayout prevDepthLayout, VkImageLayout finalColorLayout) {
		RPKey key{ colorLoadAction, depthLoadAction, stencilLoadAction, prevColorLayout, prevDepthLayout, finalColorLayout };
		return GetRenderPass(key);
	}

	VkRenderPass GetRenderPass(const RPKey &key);

	bool GetRenderPassKey(VkRenderPass passToFind, RPKey *outKey) const {
		bool found = false;
		renderPasses_.Iterate([passToFind, &found, outKey](const RPKey &rpkey, VkRenderPass pass) {
			if (pass == passToFind) {
				found = true;
				*outKey = rpkey;
			}
		});
		return found;
	}

	void EnableHacks(uint32_t hacks) {
		hacksEnabled_ = hacks;
	}

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

	void ApplyMGSHack(std::vector<VKRStep *> &steps);
	void ApplySonicHack(std::vector<VKRStep *> &steps);

	static void SetupTransitionToTransferSrc(VKRImage &img, VkImageMemoryBarrier &barrier, VkPipelineStageFlags &stage, VkImageAspectFlags aspect);
	static void SetupTransitionToTransferDst(VKRImage &img, VkImageMemoryBarrier &barrier, VkPipelineStageFlags &stage, VkImageAspectFlags aspect);

	VulkanContext *vulkan_;

	VkFramebuffer backbuffer_;
	VkImage backbufferImage_;
	VkFramebuffer curFramebuffer_ = VK_NULL_HANDLE;

	VkRenderPass backbufferRenderPass_ = VK_NULL_HANDLE;
	VkRenderPass framebufferRenderPass_ = VK_NULL_HANDLE;

	// Renderpasses, all combinations of preserving or clearing or dont-care-ing fb contents.
	// TODO: Create these on demand.
	DenseHashMap<RPKey, VkRenderPass, (VkRenderPass)VK_NULL_HANDLE> renderPasses_;

	// Readback buffer. Currently we only support synchronous readback, so we only really need one.
	// We size it generously.
	VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
	VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
	VkDeviceSize readbackBufferSize_ = 0;

	// TODO: Enable based on compat.ini.
	uint32_t hacksEnabled_ = 0;
};
