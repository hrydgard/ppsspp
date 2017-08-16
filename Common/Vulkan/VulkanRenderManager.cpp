#include "VulkanRenderManager.h"

void VulkanRenderManager::Clear(uint32_t clearColor, float clearZ, int clearStencil, int clearMask) {
	// If this is the first drawing command, merge it into the pass.
	if (curRp_->numDraws == 0) {
		curRp_->clearColor = clearColor;
		curRp_->clearZ = clearZ;
		curRp_->clearStencil = clearStencil;
		curRp_->clearMask = clearMask;
	} else {
		VkRenderData data{ VKR_CLEAR };
		data.clear.clearColor = clearColor;
		data.clear.clearZ = clearZ;
		data.clear.clearStencil = clearStencil;
		data.clear.clearMask = clearMask;
		curRp_->commands.push_back(data);
	}
}

void VulkanRenderManager::Flush(VkCommandBuffer cmdbuf) {
	// Optimizes renderpasses, then sequences them.
	for (int i = 0; i < renderPasses_.size(); i++) {
		auto &commands = renderPasses_[i]->commands;
		for (const auto &c : commands) {
			switch (c.cmd) {
			case VKR_VIEWPORT:
				vkCmdSetViewport(cmdbuf, 0, 1, &c.viewport.vp);
				break;

			case VKR_SCISSOR:
				vkCmdSetScissor(cmdbuf, 0, 1, &c.scissor.scissor);
				break;

			case VKR_DRAW_INDEXED:
				vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, c.drawIndexed.pipelineLayout, 0, 1, &c.drawIndexed.ds, c.drawIndexed.numUboOffsets, c.drawIndexed.uboOffsets);
				vkCmdBindIndexBuffer(cmdbuf, c.drawIndexed.ibuffer, c.drawIndexed.ioffset, VK_INDEX_TYPE_UINT16);
				vkCmdBindVertexBuffers(cmdbuf, 0, 1, &c.drawIndexed.vbuffer, &c.drawIndexed.voffset);
				vkCmdDrawIndexed(cmdbuf, c.drawIndexed.count, c.drawIndexed.instances, 0, 0, 0);
				break;

			case VKR_DRAW:
				vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, c.drawIndexed.pipelineLayout, 0, 1, &c.draw.ds, c.draw.numUboOffsets, c.draw.uboOffsets);
				vkCmdBindVertexBuffers(cmdbuf, 0, 1, &c.drawIndexed.vbuffer, &c.drawIndexed.voffset);
				vkCmdDraw(cmdbuf, c.drawIndexed.count, c.drawIndexed.instances, 0, 0);
				break;

			case VKR_STENCIL:
				vkCmdSetStencilWriteMask(cmdbuf, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilWriteMask);
				vkCmdSetStencilCompareMask(cmdbuf, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilCompareMask);
				vkCmdSetStencilReference(cmdbuf, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilRef);
				break;

			case VKR_BLEND:
				vkCmdSetBlendConstants(cmdbuf, c.blendColor.color);
				break;

			case VKR_CLEAR:
				 // vkCmdClearAttachments
				break;
			}
		}
	}
}

void VulkanRenderManager::Sync(VkCommandBuffer cmd) {

}
