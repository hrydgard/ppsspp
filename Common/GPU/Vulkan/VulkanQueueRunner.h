#pragma once

#include <cstdint>
#include <mutex>
#include <condition_variable>


#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanBarrier.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Collections/TinySet.h"
#include "Common/GPU/DataFormat.h"

class VKRFramebuffer;
struct VKRGraphicsPipeline;
struct VKRComputePipeline;
struct VKRImage;

enum {
	QUEUE_HACK_MGS2_ACID = 1,
	QUEUE_HACK_SONIC = 2,
	// Killzone PR = 4.
	QUEUE_HACK_RENDERPASS_MERGE = 8,
};

enum class VKRRenderCommand : uint8_t {
	REMOVED,
	BIND_PIPELINE,  // raw pipeline
	BIND_GRAPHICS_PIPELINE,  // async
	BIND_COMPUTE_PIPELINE,  // async
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

enum PipelineFlags {
	PIPELINE_FLAG_NONE = 0,
	PIPELINE_FLAG_USES_LINES = (1 << 2),
	PIPELINE_FLAG_USES_BLEND_CONSTANT = (1 << 3),
	PIPELINE_FLAG_USES_DEPTH_STENCIL = (1 << 4),  // Reads or writes the depth buffer.
};

struct VkRenderData {
	VKRRenderCommand cmd;
	union {
		struct {
			VkPipeline pipeline;
		} pipeline;
		struct {
			VKRGraphicsPipeline *pipeline;
		} graphics_pipeline;
		struct {
			VKRComputePipeline *pipeline;
		} compute_pipeline;
		struct {
			VkPipelineLayout pipelineLayout;
			VkDescriptorSet ds;
			int numUboOffsets;
			uint32_t uboOffsets[3];
			VkBuffer vbuffer;
			VkDeviceSize voffset;
			uint32_t count;
			uint32_t offset;
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
			uint32_t color;
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

enum class VKRRenderPassLoadAction : uint8_t {
	DONT_CARE,
	CLEAR,
	KEEP,
};

struct TransitionRequest {
	VkImageAspectFlags aspect;  // COLOR or DEPTH
	VKRFramebuffer *fb;
	VkImageLayout targetLayout;
};

struct QueueProfileContext {
	VkQueryPool queryPool;
	std::vector<std::string> timestampDescriptions;
	std::string profileSummary;
	double cpuStartTime;
	double cpuEndTime;
};

struct VKRStep {
	VKRStep(VKRStepType _type) : stepType(_type) {}
	~VKRStep() {}

	VKRStepType stepType;
	std::vector<VkRenderData> commands;
	std::vector<TransitionRequest> preTransitions;
	TinySet<VKRFramebuffer *, 8> dependencies;
	const char *tag;
	union {
		struct {
			VKRFramebuffer *framebuffer;
			VKRRenderPassLoadAction colorLoad;
			VKRRenderPassLoadAction depthLoad;
			VKRRenderPassLoadAction stencilLoad;
			u8 clearStencil;
			uint32_t clearColor;
			float clearDepth;
			int numDraws;
			// Downloads and textures from this pass.
			int numReads;
			VkImageLayout finalColorLayout;
			VkImageLayout finalDepthStencilLayout;
			u32 pipelineFlags;
			VkRect2D renderArea;
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

	void PreprocessSteps(std::vector<VKRStep *> &steps);
	void RunSteps(VkCommandBuffer cmd, std::vector<VKRStep *> &steps, QueueProfileContext *profile);
	void LogSteps(const std::vector<VKRStep *> &steps, bool verbose);

	std::string StepToString(const VKRStep &step) const;

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

	inline int RPIndex(VKRRenderPassLoadAction color, VKRRenderPassLoadAction depth) {
		return (int)depth * 3 + (int)color;
	}

	void CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels);

	struct RPKey {
		VKRRenderPassLoadAction colorLoadAction;
		VKRRenderPassLoadAction depthLoadAction;
		VKRRenderPassLoadAction stencilLoadAction;
	};

	// Only call this from the render thread! Also ok during initialization (LoadCache).
	VkRenderPass GetRenderPass(
		VKRRenderPassLoadAction colorLoadAction, VKRRenderPassLoadAction depthLoadAction, VKRRenderPassLoadAction stencilLoadAction) {
		RPKey key{ colorLoadAction, depthLoadAction, stencilLoadAction };
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

	void NotifyCompileDone() {
		compileDone_.notify_all();
	}

	void WaitForCompileNotification() {
		std::unique_lock<std::mutex> lock(compileDoneMutex_);
		compileDone_.wait(lock);
	}

private:
	void InitBackbufferRenderPass();

	void PerformBindFramebufferAsRenderTarget(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformRenderPass(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformCopy(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformBlit(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformReadback(const VKRStep &pass, VkCommandBuffer cmd);
	void PerformReadbackImage(const VKRStep &pass, VkCommandBuffer cmd);

	void LogRenderPass(const VKRStep &pass, bool verbose);
	void LogCopy(const VKRStep &pass);
	void LogBlit(const VKRStep &pass);
	void LogReadback(const VKRStep &pass);
	void LogReadbackImage(const VKRStep &pass);

	void ResizeReadbackBuffer(VkDeviceSize requiredSize);

	void ApplyMGSHack(std::vector<VKRStep *> &steps);
	void ApplySonicHack(std::vector<VKRStep *> &steps);
	void ApplyRenderPassMerge(std::vector<VKRStep *> &steps);

	static void SetupTransitionToTransferSrc(VKRImage &img, VkImageAspectFlags aspect, VulkanBarrier *recordBarrier);
	static void SetupTransitionToTransferDst(VKRImage &img, VkImageAspectFlags aspect, VulkanBarrier *recordBarrier);

	VulkanContext *vulkan_;

	VkFramebuffer backbuffer_ = VK_NULL_HANDLE;
	VkImage backbufferImage_ = VK_NULL_HANDLE;

	VkRenderPass backbufferRenderPass_ = VK_NULL_HANDLE;

	// The "Compatible" render pass. Used when creating pipelines that render to "normal" framebuffers.
	VkRenderPass framebufferRenderPass_ = VK_NULL_HANDLE;

	// Renderpasses, all combinations of preserving or clearing or dont-care-ing fb contents.
	// TODO: Create these on demand.
	DenseHashMap<RPKey, VkRenderPass, (VkRenderPass)VK_NULL_HANDLE> renderPasses_;

	// Readback buffer. Currently we only support synchronous readback, so we only really need one.
	// We size it generously.
	VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
	VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
	VkDeviceSize readbackBufferSize_ = 0;
	bool readbackBufferIsCoherent_ = false;

	// TODO: Enable based on compat.ini.
	uint32_t hacksEnabled_ = 0;

	// Compile done notifications.
	std::mutex compileDoneMutex_;
	std::condition_variable compileDone_;

	// Image barrier helper used during command buffer record (PerformRenderPass etc).
	// Stored here to help reuse the allocation.

	VulkanBarrier recordBarrier_;
};
