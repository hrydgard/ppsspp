#pragma once

// VulkanRenderManager takes the role that a GL driver does of sequencing and optimizing render passes.
// Only draws and binds are handled here, resource creation and allocations are handled as normal -
// that's the nice thing with Vulkan.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <queue>

#include "Common/Math/Statistics.h"
#include "Common/Thread/Promise.h"
#include "Common/System/Display.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanBarrier.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Collections/FastVec.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/DataFormat.h"
#include "Common/GPU/MiscTypes.h"
#include "Common/GPU/Vulkan/VulkanQueueRunner.h"
#include "Common/GPU/Vulkan/VulkanFramebuffer.h"
#include "Common/GPU/Vulkan/VulkanDescSet.h"
#include "Common/GPU/thin3d.h"

// Forward declaration
VK_DEFINE_HANDLE(VmaAllocation);

struct BoundingRect {
	int x1;
	int y1;
	int x2;
	int y2;

	BoundingRect() {
		Reset();
	}

	void Reset() {
		x1 = 65535;
		y1 = 65535;
		x2 = -65535;
		y2 = -65535;
	}

	bool Empty() const {
		return x2 < 0;
	}

	void SetRect(int x, int y, int width, int height) {
		x1 = x;
		y1 = y;
		x2 = width;
		y2 = height;
	}

	void Apply(const VkRect2D &rect) {
		if (rect.offset.x < x1) x1 = rect.offset.x;
		if (rect.offset.y < y1) y1 = rect.offset.y;
		int rect_x2 = rect.offset.x + rect.extent.width;
		int rect_y2 = rect.offset.y + rect.extent.height;
		if (rect_x2 > x2) x2 = rect_x2;
		if (rect_y2 > y2) y2 = rect_y2;
	}

	VkRect2D ToVkRect2D() const {
		VkRect2D rect;
		rect.offset.x = x1;
		rect.offset.y = y1;
		rect.extent.width = x2 - x1;
		rect.extent.height = y2 - y1;
		return rect;
	}
};

// All the data needed to create a graphics pipeline.
// TODO: Compress this down greatly.
class VKRGraphicsPipelineDesc : public Draw::RefCountedObject {
public:
	VKRGraphicsPipelineDesc() : Draw::RefCountedObject("VKRGraphicsPipelineDesc") {}

	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkPipelineColorBlendStateCreateInfo cbs{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	VkPipelineColorBlendAttachmentState blend0{};
	VkPipelineDepthStencilStateCreateInfo dss{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkDynamicState dynamicStates[6]{};
	VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	VkPipelineRasterizationProvokingVertexStateCreateInfoEXT rs_provoking{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT };

	// Replaced the ShaderStageInfo with promises here so we can wait for compiles to finish.
	Promise<VkShaderModule> *vertexShader = nullptr;
	Promise<VkShaderModule> *fragmentShader = nullptr;
	Promise<VkShaderModule> *geometryShader = nullptr;

	// These are for pipeline creation failure logging.
	// TODO: Store pointers to the string instead? Feels iffy but will probably work.
	std::string vertexShaderSource;
	std::string fragmentShaderSource;
	std::string geometryShaderSource;

	VkPrimitiveTopology topology;
	VkVertexInputAttributeDescription attrs[8]{};
	VkVertexInputBindingDescription ibd{};
	VkPipelineVertexInputStateCreateInfo vis{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineViewportStateCreateInfo views{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };

	VKRPipelineLayout *pipelineLayout = nullptr;

	// Does not include the render pass type, it's passed in separately since the
	// desc is persistent.
	RPKey rpKey{};
};

// Wrapped pipeline. Does own desc!
struct VKRGraphicsPipeline {
	VKRGraphicsPipeline(PipelineFlags flags, const char *tag) : flags_(flags), tag_(tag) {}
	~VKRGraphicsPipeline();

	bool Create(VulkanContext *vulkan, VkRenderPass compatibleRenderPass, RenderPassType rpType, VkSampleCountFlagBits sampleCount, double scheduleTime, int countToCompile);
	void DestroyVariants(VulkanContext *vulkan, bool msaaOnly);

	// This deletes the whole VKRGraphicsPipeline, you must remove your last pointer to it when doing this.
	void QueueForDeletion(VulkanContext *vulkan);

	// This blocks until any background compiles are finished.
	// Used during game shutdown before we clear out shaders that these compiles depend on.
	void BlockUntilCompiled();

	u32 GetVariantsBitmask() const;

	void LogCreationFailure() const;

	VKRGraphicsPipelineDesc *desc = nullptr;
	Promise<VkPipeline> *pipeline[(size_t)RenderPassType::TYPE_COUNT]{};

	VkSampleCountFlagBits SampleCount() const { return sampleCount_; }

	const char *Tag() const { return tag_.c_str(); }

private:
	void DestroyVariantsInstant(VkDevice device);

	std::string tag_;
	PipelineFlags flags_;
	VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
};

struct CompileQueueEntry {
	CompileQueueEntry(VKRGraphicsPipeline *p, VkRenderPass _compatibleRenderPass, RenderPassType _renderPassType, VkSampleCountFlagBits _sampleCount)
		: type(Type::GRAPHICS), graphics(p), compatibleRenderPass(_compatibleRenderPass), renderPassType(_renderPassType), sampleCount(_sampleCount) {}
	enum class Type {
		GRAPHICS,
	};
	Type type;
	VkRenderPass compatibleRenderPass;
	RenderPassType renderPassType;
	VKRGraphicsPipeline* graphics = nullptr;
	VkSampleCountFlagBits sampleCount;
};

// Pending descriptor sets.
// TODO: Sort these by VKRPipelineLayout to avoid storing it for each element.
struct PendingDescSet {
	int offset;  // probably enough with a u16.
	u8 count;
	VkDescriptorSet set;
};

struct PackedDescriptor {
	union {
		struct {
			VkImageView view;
			VkSampler sampler;
		} image;
		struct {
			VkBuffer buffer;
			uint32_t range;
			uint32_t offset;
		} buffer;
#if false
		struct {
			VkBuffer buffer;
			uint64_t range;  // write range and a zero offset in one operation with this.
		} buffer_zero_offset;
#endif
	};
};

// Note that we only support a single descriptor set due to compatibility with some ancient devices.
// We should probably eventually give that up eventually.
struct VKRPipelineLayout {
	~VKRPipelineLayout();

	enum { MAX_DESC_SET_BINDINGS = 10 };
	BindingType bindingTypes[MAX_DESC_SET_BINDINGS];

	uint32_t bindingTypesCount = 0;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;  // only support 1 for now.
	int pushConstSize = 0;
	const char *tag = nullptr;

	struct FrameData {
		FrameData() : pool("N/A", true) {}

		VulkanDescSetPool pool;
		FastVec<PackedDescriptor> descData_;
		FastVec<PendingDescSet> descSets_;
		// TODO: We should be able to get away with a single descData_/descSets_ and then send it along,
		// but it's easier to just segregate by frame id.
		int flushedDescriptors_ = 0;
	};

	FrameData frameData[VulkanContext::MAX_INFLIGHT_FRAMES];

	void FlushDescSets(VulkanContext *vulkan, int frame, QueueProfileContext *profile);
	void SetTag(const char *tag) {
		this->tag = tag;
		for (int i = 0; i < ARRAY_SIZE(frameData); i++) {
			frameData[i].pool.SetTag(tag);
		}
	}
};

class VulkanRenderManager {
public:
	VulkanRenderManager(VulkanContext *vulkan, bool useThread, HistoryBuffer<FrameTimeData, FRAME_TIME_HISTORY_LENGTH> &frameTimeHistory);
	~VulkanRenderManager();

	// Makes sure that the GPU has caught up enough that we can start writing buffers of this frame again.
	void BeginFrame(bool enableProfiling, bool enableLogProfiler);
	// These can run on a different thread!
	void Finish();
	void Present();
	void CheckNothingPending();

	void SetInvalidationCallback(InvalidationCallback callback) {
		invalidationCallback_ = callback;
	}

	// This starts a new step containing a render pass (unless it can be trivially merged into the previous one, which is pretty common).
	//
	// After a "CopyFramebuffer" or the other functions that start "steps", you need to call this beforce
	// making any new render state changes or draw calls.
	//
	// The following dynamic state needs to be reset by the caller after calling this (and will thus not safely carry over from
	// the previous one):
	//   * Viewport/Scissor
	//   * Stencil parameters
	//   * Blend color
	//
	// (Most other state is directly decided by your choice of pipeline and descriptor set, so not handled here).
	//
	// It can be useful to use GetCurrentStepId() to figure out when you need to send all this state again, if you're
	// not keeping track of your calls to this function on your own.
	void BindFramebufferAsRenderTarget(VKRFramebuffer *fb, VKRRenderPassLoadAction color, VKRRenderPassLoadAction depth, VKRRenderPassLoadAction stencil, uint32_t clearColor, float clearDepth, uint8_t clearStencil, const char *tag);

	// Returns an ImageView corresponding to a framebuffer. Is called BindFramebufferAsTexture to maintain a similar interface
	// as the other backends, even though there's no actual binding happening here.
	// For layer, we use the same convention as thin3d, where layer = -1 means all layers together. For texturing, that means that you
	// get an array texture view.
	VkImageView BindFramebufferAsTexture(VKRFramebuffer *fb, int binding, VkImageAspectFlags aspectBits, int layer);

	bool CopyFramebufferToMemory(VKRFramebuffer *src, VkImageAspectFlags aspectBits, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, Draw::ReadbackMode mode, const char *tag);
	void CopyImageToMemorySync(VkImage image, int mipLevel, int x, int y, int w, int h, Draw::DataFormat destFormat, uint8_t *pixels, int pixelStride, const char *tag);

	void CopyFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkOffset2D dstPos, VkImageAspectFlags aspectMask, const char *tag);
	void BlitFramebuffer(VKRFramebuffer *src, VkRect2D srcRect, VKRFramebuffer *dst, VkRect2D dstRect, VkImageAspectFlags aspectMask, VkFilter filter, const char *tag);

	// Deferred creation, like in GL. Unlike GL though, the purpose is to allow background creation and avoiding
	// stalling the emulation thread as much as possible.
	// We delay creating pipelines until the end of the current render pass, so we can create the right type immediately.
	// Unless a variantBitmask is passed in, in which case we can just go ahead.
	// WARNING: desc must stick around during the lifetime of the pipeline! It's not enough to build it on the stack and drop it.
	VKRGraphicsPipeline *CreateGraphicsPipeline(VKRGraphicsPipelineDesc *desc, PipelineFlags pipelineFlags, uint32_t variantBitmask, VkSampleCountFlagBits sampleCount, bool cacheLoad, const char *tag);

	VKRPipelineLayout *CreatePipelineLayout(BindingType *bindingTypes, size_t bindingCount, bool geoShadersEnabled, const char *tag);
	void DestroyPipelineLayout(VKRPipelineLayout *pipelineLayout);

	void ReportBadStateForDraw();

	void NudgeCompilerThread() {
		compileMutex_.lock();
		compileCond_.notify_one();
		compileMutex_.unlock();
	}

	// This is the first call in a draw operation. Instead of asserting like we used to, you can now check the
	// return value and skip the draw if we're in a bad state. In that case, call ReportBadState.
	// The old assert wasn't very helpful in figuring out what caused it anyway...
	bool BindPipeline(VKRGraphicsPipeline *pipeline, PipelineFlags flags, VKRPipelineLayout *pipelineLayout) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER && pipeline != nullptr);
		if (!curRenderStep_ || curRenderStep_->stepType != VKRStepType::RENDER) {
			return false;
		}
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::BIND_GRAPHICS_PIPELINE;
		pipelinesToCheck_.push_back(pipeline);
		data.graphics_pipeline.pipeline = pipeline;
		data.graphics_pipeline.pipelineLayout = pipelineLayout;
		// This can be used to debug cases where depth/stencil rendering is used on color-only framebuffers.
		// if ((flags & PipelineFlags::USES_DEPTH_STENCIL) && curRenderStep_->render.framebuffer && !curRenderStep_->render.framebuffer->HasDepth()) {
		//     DebugBreak();
		// }
		curPipelineFlags_ |= flags;
		curPipelineLayout_ = pipelineLayout;
		return true;
	}

	void SetViewport(const VkViewport &vp) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		_dbg_assert_((int)vp.width >= 0);
		_dbg_assert_((int)vp.height >= 0);
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::VIEWPORT;
		data.viewport.vp.x = vp.x;
		data.viewport.vp.y = vp.y;
		data.viewport.vp.width = vp.width;
		data.viewport.vp.height = vp.height;
		// We can't allow values outside this range unless we use VK_EXT_depth_range_unrestricted.
		// Sometimes state mapping produces 65536/65535 which is slightly outside.
		// TODO: This should be fixed at the source.
		data.viewport.vp.minDepth = clamp_value(vp.minDepth, 0.0f, 1.0f);
		data.viewport.vp.maxDepth = clamp_value(vp.maxDepth, 0.0f, 1.0f);
		curStepHasViewport_ = true;
	}

	// It's OK to set scissor outside the valid range - the function will automatically clip.
	void SetScissor(int x, int y, int width, int height) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);

		if (x < 0) {
			width += x;  // since x is negative, this shrinks width.
			x = 0;
		}
		if (y < 0) {
			height += y;
			y = 0;
		}

		if (x + width > curWidth_) {
			width = curWidth_ - x;
		}
		if (y + height > curHeight_) {
			height = curHeight_ - y;
		}

		// Check validity.
		if (width < 0 || height < 0 || x >= curWidth_ || y >= curHeight_) {
			// TODO: If any of the dimensions are now zero or negative, we should flip a flag and not do draws, probably.
			// Instead, if we detect an invalid scissor rectangle, we just put a 1x1 rectangle in the upper left corner.
			x = 0;
			y = 0;
			width = 1;
			height = 1;
		}

		VkRect2D rc;
		rc.offset.x = x;
		rc.offset.y = y;
		rc.extent.width = width;
		rc.extent.height = height;

		curRenderArea_.Apply(rc);

		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::SCISSOR;
		data.scissor.scissor = rc;
		curStepHasScissor_ = true;
	}

	void SetStencilParams(uint8_t writeMask, uint8_t compareMask, uint8_t refValue) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::STENCIL;
		data.stencil.stencilWriteMask = writeMask;
		data.stencil.stencilCompareMask = compareMask;
		data.stencil.stencilRef = refValue;
	}

	void SetBlendFactor(uint32_t color) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::BLEND;
		data.blendColor.color = color;
	}

	void PushConstants(VkPipelineLayout pipelineLayout, VkShaderStageFlags stages, int offset, int size, void *constants) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		_dbg_assert_(size + offset < 40);
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::PUSH_CONSTANTS;
		data.push.stages = stages;
		data.push.offset = offset;
		data.push.size = size;
		memcpy(data.push.data, constants, size);
	}

	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask);

	// Cheaply set that we don't care about the contents of a surface at the start of the current render pass.
	// This set the corresponding load-op of the current render pass to DONT_CARE.
	// Useful when we don't know at bind-time whether we will overwrite the surface or not.
	void SetLoadDontCare(VkImageAspectFlags aspects) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
			curRenderStep_->render.colorLoad = VKRRenderPassLoadAction::DONT_CARE;
		if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
			curRenderStep_->render.depthLoad = VKRRenderPassLoadAction::DONT_CARE;
		if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
			curRenderStep_->render.stencilLoad = VKRRenderPassLoadAction::DONT_CARE;
	}

	// Cheaply set that we don't care about the contents of a surface at the end of the current render pass.
	// This set the corresponding store-op of the current render pass to DONT_CARE.
	void SetStoreDontCare(VkImageAspectFlags aspects) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);
		if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
			curRenderStep_->render.colorStore = VKRRenderPassStoreAction::DONT_CARE;
		if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
			curRenderStep_->render.depthStore = VKRRenderPassStoreAction::DONT_CARE;
		if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
			curRenderStep_->render.stencilStore = VKRRenderPassStoreAction::DONT_CARE;
	}

	// Descriptors will match the current pipeline layout, set by the last call to BindPipeline.
	// Count is the count of void*s. Two are needed for COMBINED_IMAGE_SAMPLER, everything else is a single one.
	// The goal is to keep this function very small and fast, and do the expensive work on the render thread or
	// another thread.
	PackedDescriptor *PushDescriptorSet(int count, int *descSetIndex) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER);

		int curFrame = vulkan_->GetCurFrame();

		VKRPipelineLayout::FrameData &data = curPipelineLayout_->frameData[curFrame];

		size_t offset = data.descData_.size();
		PackedDescriptor *retval = data.descData_.extend_uninitialized(count);

		int setIndex = (int)data.descSets_.size();
		PendingDescSet &descSet = data.descSets_.push_uninitialized();
		descSet.offset = (uint32_t)offset;
		descSet.count = count;
		// descSet.set = VK_NULL_HANDLE;  // to be filled in
		*descSetIndex = setIndex;
		return retval;
	}

	void Draw(int descSetIndex, int numUboOffsets, const uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, int count, int offset = 0) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER && curStepHasViewport_ && curStepHasScissor_);
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::DRAW;
		data.draw.count = count;
		data.draw.offset = offset;
		data.draw.descSetIndex = descSetIndex;
		data.draw.vbuffer = vbuffer;
		data.draw.voffset = voffset;
		data.draw.numUboOffsets = numUboOffsets;
		_dbg_assert_(numUboOffsets <= ARRAY_SIZE(data.draw.uboOffsets));
		for (int i = 0; i < numUboOffsets; i++)
			data.draw.uboOffsets[i] = uboOffsets[i];
		curRenderStep_->render.numDraws++;
	}

	void DrawIndexed(int descSetIndex, int numUboOffsets, const uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, VkBuffer ibuffer, int ioffset, int count, int numInstances) {
		_dbg_assert_(curRenderStep_ && curRenderStep_->stepType == VKRStepType::RENDER && curStepHasViewport_ && curStepHasScissor_);
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::DRAW_INDEXED;
		data.drawIndexed.count = count;
		data.drawIndexed.instances = numInstances;
		data.drawIndexed.descSetIndex = descSetIndex;
		data.drawIndexed.vbuffer = vbuffer;
		data.drawIndexed.voffset = voffset;
		data.drawIndexed.ibuffer = ibuffer;
		data.drawIndexed.ioffset = ioffset;
		data.drawIndexed.numUboOffsets = numUboOffsets;
		_dbg_assert_(numUboOffsets <= ARRAY_SIZE(data.drawIndexed.uboOffsets));
		for (int i = 0; i < numUboOffsets; i++)
			data.drawIndexed.uboOffsets[i] = uboOffsets[i];
		curRenderStep_->render.numDraws++;
	}

	// These can be useful both when inspecting in RenderDoc, and when manually inspecting recorded commands
	// in the debugger.
	void DebugAnnotate(const char *annotation) {
		_dbg_assert_(curRenderStep_);
		VkRenderData &data = curRenderStep_->commands.push_uninitialized();
		data.cmd = VKRRenderCommand::DEBUG_ANNOTATION;
		data.debugAnnotation.annotation = annotation;
	}

	VkCommandBuffer GetInitCmd();

	bool CreateBackbuffers();
	void DestroyBackbuffers();

	bool HasBackbuffers() {
		return queueRunner_.HasBackbuffers();
	}

	void SetInflightFrames(int f) {
		newInflightFrames_ = f < 1 || f > VulkanContext::MAX_INFLIGHT_FRAMES ? VulkanContext::MAX_INFLIGHT_FRAMES : f;
	}

	VulkanContext *GetVulkanContext() {
		return vulkan_;
	}

	// Be careful with this. Only meant to be used for fetching render passes for shader cache initialization.
	VulkanQueueRunner *GetQueueRunner() {
		return &queueRunner_;
	}

	std::string GetGpuProfileString() const {
		return frameData_[vulkan_->GetCurFrame()].profile.profileSummary;
	}

	bool NeedsSwapchainRecreate() const {
		// Accepting a few of these makes shutdown simpler.
		return outOfDateFrames_ > VulkanContext::MAX_INFLIGHT_FRAMES;
	}

	VulkanBarrierBatch &PostInitBarrier() {
		return postInitBarrier_;
	}

	void ResetStats();

	void StartThreads();
	void StopThreads();

	size_t GetNumSteps() const {
		return steps_.size();
	}

private:
	void EndCurRenderStep();

	void RenderThreadFunc();
	void CompileThreadFunc();

	void Run(VKRRenderThreadTask &task);

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void FlushSync();

	void PresentWaitThreadFunc();
	void PollPresentTiming();

	void ResetDescriptorLists(int frame);
	void FlushDescriptors(int frame);

	void SanityCheckPassesOnAdd();

	FrameDataShared frameDataShared_;

	FrameData frameData_[VulkanContext::MAX_INFLIGHT_FRAMES];
	int newInflightFrames_ = -1;
	int inflightFramesAtStart_ = 0;

	int outOfDateFrames_ = 0;

	// Submission time state

	// Note: These are raw backbuffer-sized. Rotated.
	int curWidthRaw_ = -1;
	int curHeightRaw_ = -1;

	// Pre-rotation (as you'd expect).
	int curWidth_ = -1;
	int curHeight_ = -1;

	bool insideFrame_ = false;
	// probably doesn't need to be atomic.
	std::atomic<bool> runCompileThread_;

	bool useRenderThread_ = true;
	bool measurePresentTime_ = false;

	// This is the offset within this frame, in case of a mid-frame sync.
	VKRStep *curRenderStep_ = nullptr;
	bool curStepHasViewport_ = false;
	bool curStepHasScissor_ = false;
	PipelineFlags curPipelineFlags_{};
	BoundingRect curRenderArea_;

	std::vector<VKRStep *> steps_;

	// Execution time state
	VulkanContext *vulkan_;
	std::thread renderThread_;
	VulkanQueueRunner queueRunner_;

	// For pushing data on the queue.
	std::mutex pushMutex_;
	std::condition_variable pushCondVar_;

	std::queue<VKRRenderThreadTask *> renderThreadQueue_;

	// For readbacks and other reasons we need to sync with the render thread.
	std::mutex syncMutex_;
	std::condition_variable syncCondVar_;

	// Shader compilation thread to compile while emulating the rest of the frame.
	// Only one right now but we could use more.
	std::thread compileThread_;
	// Sync
	std::condition_variable compileCond_;
	std::mutex compileMutex_;
	std::vector<CompileQueueEntry> compileQueue_;

	// Thread for measuring presentation delay.
	std::thread presentWaitThread_;

	// pipelines to check and possibly create at the end of the current render pass.
	std::vector<VKRGraphicsPipeline *> pipelinesToCheck_;

	// For nicer output in the little internal GPU profiler.
	SimpleStat initTimeMs_;
	SimpleStat totalGPUTimeMs_;
	SimpleStat renderCPUTimeMs_;
	SimpleStat descUpdateTimeMs_;

	VulkanBarrierBatch postInitBarrier_;

	std::function<void(InvalidationCallbackFlags)> invalidationCallback_;

	uint64_t frameIdGen_ = FRAME_TIME_HISTORY_LENGTH;
	HistoryBuffer<FrameTimeData, FRAME_TIME_HISTORY_LENGTH> &frameTimeHistory_;

	VKRPipelineLayout *curPipelineLayout_ = nullptr;
	std::vector<VKRPipelineLayout *> pipelineLayouts_;
};
