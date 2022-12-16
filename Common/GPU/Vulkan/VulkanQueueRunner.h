#pragma once

#include <cstdint>
#include <mutex>
#include <condition_variable>

#include "Common/Thread/Promise.h"
#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanBarrier.h"
#include "Common/GPU/Vulkan/VulkanFrameData.h"
#include "Common/GPU/Vulkan/VulkanFramebuffer.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Collections/TinySet.h"
#include "Common/GPU/DataFormat.h"

class VKRFramebuffer;
struct VKRGraphicsPipeline;
struct VKRComputePipeline;
struct VKRImage;
struct FrameData;

enum {
	QUEUE_HACK_MGS2_ACID = 1,
	QUEUE_HACK_SONIC = 2,
	QUEUE_HACK_RENDERPASS_MERGE = 8,
};

enum class VKRRenderCommand : uint8_t {
	REMOVED,
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
	SELF_DEPENDENCY_BARRIER,
	DEBUG_ANNOTATION,
	NUM_RENDER_COMMANDS,
};

enum class PipelineFlags : u8 {
	NONE = 0,
	USES_BLEND_CONSTANT = (1 << 1),
	USES_DEPTH_STENCIL = (1 << 2),  // Reads or writes the depth or stencil buffers.
	USES_INPUT_ATTACHMENT = (1 << 3),
	USES_GEOMETRY_SHADER = (1 << 4),
	USES_MULTIVIEW = (1 << 5),  // Inherited from the render pass it was created with.
	USES_DISCARD = (1 << 6),
};
ENUM_CLASS_BITOPS(PipelineFlags);

struct VkRenderData {
	VKRRenderCommand cmd;
	union {
		struct {
			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
		} pipeline;
		struct {
			VKRGraphicsPipeline *pipeline;
			VkPipelineLayout pipelineLayout;
		} graphics_pipeline;
		struct {
			VKRComputePipeline *pipeline;
			VkPipelineLayout pipelineLayout;
		} compute_pipeline;
		struct {
			VkDescriptorSet ds;
			int numUboOffsets;
			uint32_t uboOffsets[3];
			VkBuffer vbuffer;
			VkDeviceSize voffset;
			uint32_t count;
			uint32_t offset;
		} draw;
		struct {
			VkDescriptorSet ds;
			int numUboOffsets;
			uint32_t uboOffsets[3];
			VkBuffer vbuffer;
			VkBuffer ibuffer;
			uint32_t voffset;
			uint32_t ioffset;
			uint32_t count;
			int16_t instances;
			int16_t indexType;
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
			VkShaderStageFlags stages;
			uint8_t offset;
			uint8_t size;
			uint8_t data[40];  // Should be enough for now.
		} push;
		struct {
			const char *annotation;
		} debugAnnotation;
		struct {
			int setNumber;
			VkDescriptorSet set;
			VkPipelineLayout pipelineLayout;
		} bindDescSet;
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

struct TransitionRequest {
	VKRFramebuffer *fb;
	VkImageAspectFlags aspect;  // COLOR or DEPTH
	VkImageLayout targetLayout;

	bool operator == (const TransitionRequest &other) const {
		return fb == other.fb && aspect == other.aspect && targetLayout == other.targetLayout;
	}
};

class VKRRenderPass;

struct VKRStep {
	VKRStep(VKRStepType _type) : stepType(_type) {}
	~VKRStep() {}

	VKRStepType stepType;
	std::vector<VkRenderData> commands;
	TinySet<TransitionRequest, 4> preTransitions;
	TinySet<VKRFramebuffer *, 8> dependencies;
	const char *tag;
	union {
		struct {
			VKRFramebuffer *framebuffer;
			VKRRenderPassLoadAction colorLoad;
			VKRRenderPassLoadAction depthLoad;
			VKRRenderPassLoadAction stencilLoad;
			VKRRenderPassStoreAction colorStore;
			VKRRenderPassStoreAction depthStore;
			VKRRenderPassStoreAction stencilStore;
			u8 clearStencil;
			uint32_t clearColor;
			float clearDepth;
			int numDraws;
			// Downloads and textures from this pass.
			int numReads;
			VkImageLayout finalColorLayout;
			VkImageLayout finalDepthStencilLayout;
			PipelineFlags pipelineFlags;  // contains the self dependency flag, in the form of USES_INPUT_ATTACHMENT
			VkRect2D renderArea;
			// Render pass type. Deduced after finishing recording the pass, from the used pipelines.
			// NOTE: Storing the render pass here doesn't do much good, we change the compatible parameters (load/store ops) during step optimization.
			RenderPassType renderPassType;
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

// These are enqueued from the main thread,
// and the render thread pops them off
struct VKRRenderThreadTask {
	std::vector<VKRStep *> steps;
	int frame;
	VKRRunType runType;
};

class VulkanQueueRunner {
public:
	VulkanQueueRunner(VulkanContext *vulkan) : vulkan_(vulkan), renderPasses_(16) {}

	void SetBackbuffer(VkFramebuffer fb, VkImage img) {
		backbuffer_ = fb;
		backbufferImage_ = img;
	}

	void PreprocessSteps(std::vector<VKRStep *> &steps);
	void RunSteps(std::vector<VKRStep *> &steps, FrameData &frameData, FrameDataShared &frameDataShared, bool keepSteps = false);
	void LogSteps(const std::vector<VKRStep *> &steps, bool verbose);

	std::string StepToString(const VKRStep &step) const;

	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	// Swapchain
	void DestroyBackBuffers();
	bool CreateSwapchain(VkCommandBuffer cmdInit);

	bool HasBackbuffers() const {
		return !framebuffers_.empty();
	}

	// Get a render pass that's compatible with all our framebuffers.
	// Note that it's precached, cannot look up in the map as this might be on another thread.
	VKRRenderPass *GetCompatibleRenderPass() const {
		return compatibleRenderPass_;
	}

	inline int RPIndex(VKRRenderPassLoadAction color, VKRRenderPassLoadAction depth) {
		return (int)depth * 3 + (int)color;
	}

	void CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels);

	VKRRenderPass *GetRenderPass(const RPKey &key);

	bool GetRenderPassKey(VKRRenderPass *passToFind, RPKey *outKey) const {
		bool found = false;
		renderPasses_.Iterate([passToFind, &found, outKey](const RPKey &rpkey, const VKRRenderPass *pass) {
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
	bool InitBackbufferFramebuffers(int width, int height);
	bool InitDepthStencilBuffer(VkCommandBuffer cmd);  // Used for non-buffered rendering.

	VKRRenderPass *PerformBindFramebufferAsRenderTarget(const VKRStep &pass, VkCommandBuffer cmd);
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
	static void SetupTransferDstWriteAfterWrite(VKRImage &img, VkImageAspectFlags aspect, VulkanBarrier *recordBarrier);

	static void SelfDependencyBarrier(VKRImage &img, VkImageAspectFlags aspect, VulkanBarrier *recordBarrier);

	VulkanContext *vulkan_;

	VkFramebuffer backbuffer_ = VK_NULL_HANDLE;
	VkImage backbufferImage_ = VK_NULL_HANDLE;

	// The "Compatible" render pass. Should be able to get rid of this soon.
	VKRRenderPass *compatibleRenderPass_ = nullptr;

	// Renderpasses, all combinations of preserving or clearing or dont-care-ing fb contents.
	// Each VKRRenderPass contains all compatibility classes (which attachments they have, etc).
	DenseHashMap<RPKey, VKRRenderPass *, nullptr> renderPasses_;

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

	// Swap chain management
	struct SwapchainImageData {
		VkImage image;
		VkImageView view;
	};
	std::vector<VkFramebuffer> framebuffers_;
	std::vector<SwapchainImageData> swapchainImages_;
	uint32_t swapchainImageCount_ = 0;
	struct DepthBufferInfo {
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation alloc = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
	};
	DepthBufferInfo depth_;
};
