#pragma once

#include <cstdint>

#include "Common/Vulkan/VulkanContext.h"
#include "thin3d/thin3d.h"

// Takes the role that a GL driver does of sequencing and optimizing render passes.
// Only draws and binds are handled here, resource creation and allocations are handled as normal.


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
			int offset;
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
	void SetViewport(VkViewport vp) {
		VkRenderData data{ VKR_VIEWPORT };
		data.viewport.vp = vp;
		curRp_->commands.push_back(data);
	}

	void SetScissor(VkRect2D rc) {
		VkRenderData data{ VKR_SCISSOR };
		data.scissor.scissor = rc;
		curRp_->commands.push_back(data);
	}

	void Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask);

	void Draw(VkPipeline pipeline, VkBuffer vbuffer, int offset, int count) {
		VkRenderData data{ VKR_DRAW };
		data.draw.vbuffer = vbuffer;
		data.draw.offset = offset;
		curRp_->commands.push_back(data);
		curRp_->numDraws++;
	}

	void DrawIndexed(VkPipeline pipeline, VkPipelineLayout layout, VkBuffer vbuffer, int voffset, VkBuffer ibuffer, int ioffset, int count) {
		VkRenderData data{ VKR_DRAW };
		data.drawIndexed.pipeline = pipeline;
		data.drawIndexed.vbuffer = vbuffer;
		data.drawIndexed.voffset = voffset;
		data.drawIndexed.ibuffer = ibuffer;
		data.drawIndexed.ioffset = ioffset;
		curRp_->commands.push_back(data);
		curRp_->numDraws++;
	}

	void Flush(VkCommandBuffer cmd);

	// Bad for performance but sometimes necessary for synchonous CPU readbacks (screenshots and whatnot).
	void Sync(VkCommandBuffer cmd);

	std::vector<VKRRenderPass *> renderPasses_;
	VKRRenderPass *curRp_;
};