#pragma once

#include <cstdint>
#include <thread>

#include "Common/Vulkan/VulkanContext.h"
#include "math/dataconv.h"
#include "thin3d/thin3d.h"

// Takes the role that a GL driver does of sequencing and optimizing render passes.
// Only draws and binds are handled here, resource creation and allocations are handled as normal -
// that's the nice thing with Vulkan.

// The cool thing is that you can Flush on a different thread than you record the commands on!

enum VkRenderCmd : uint8_t {
	VKR_STENCIL,
	VKR_BLEND,
	VKR_VIEWPORT,
	VKR_SCISSOR,
	VKR_CLEAR,
	VKR_DRAW,
	VKR_DRAW_INDEXED,
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

struct VKRRenderPass {
	VkFramebuffer framebuffer;
	uint32_t clearColor;
	float clearZ;
	int clearStencil;
	int clearMask = 0;   // VK_IMAGE_ASPECT_COLOR_BIT etc
	int dontCareMask = 0;
	int numDraws;
	std::vector<VkRenderData> commands;
};

class VulkanRenderManager {
public:
	VulkanRenderManager(VulkanContext *vulkan) : vulkan_(vulkan) {}

	// Makes sure that the GPU has caught up enough that we can start writing buffers of this frame again.
	void BeginFrameWrites();
	void EndFrame();

	void SetViewport(const VkViewport &vp) {
		VkRenderData data{ VKR_VIEWPORT };
		data.viewport.vp = vp;
		curRp_->commands.push_back(data);
	}

	void SetScissor(const VkRect2D &rc) {
		VkRenderData data{ VKR_SCISSOR };
		data.scissor.scissor = rc;
		curRp_->commands.push_back(data);
	}

	void SetBlendFactor(float color[4]) {
		VkRenderData data{ VKR_BLEND };
		CopyFloat4(data.blendColor.color, color);
		curRp_->commands.push_back(data);
	}

	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask);

	void Draw(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, int count) {
		VkRenderData data{ VKR_DRAW };
		data.draw.count = count;
		data.draw.pipeline = pipeline;
		data.draw.pipelineLayout = layout;
		data.draw.ds = descSet;
		data.draw.vbuffer = vbuffer;
		data.draw.voffset = voffset;
		data.draw.numUboOffsets = numUboOffsets;
		for (int i = 0; i < numUboOffsets; i++)
			data.draw.uboOffsets[i] = uboOffsets[i];
		curRp_->commands.push_back(data);
		curRp_->numDraws++;
	}

	void DrawIndexed(VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet descSet, int numUboOffsets, uint32_t *uboOffsets, VkBuffer vbuffer, int voffset, VkBuffer ibuffer, int ioffset, int count, VkIndexType indexType) {
		VkRenderData data{ VKR_DRAW };
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
		curRp_->commands.push_back(data);
		curRp_->numDraws++;
	}

	// Can run on a different thread! Just make sure to use BeginFrameWrites.
	void Flush(VkCommandBuffer cmd);

	// Bad for performance but sometimes necessary for synchronous CPU readbacks (screenshots and whatnot).
	void Sync(VkCommandBuffer cmd);

	std::vector<VKRRenderPass *> renderPasses_;
	VKRRenderPass *curRp_;

private:
	VulkanContext *vulkan_;
	int curWidth_;
	int curHeight_;
};