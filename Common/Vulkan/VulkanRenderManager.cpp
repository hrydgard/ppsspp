#include "base/logging.h"

#include "VulkanRenderManager.h"

void VulkanRenderManager::BeginFrameWrites() {
	vulkan_->BeginFrame();
	renderPasses_.push_back(new VKRRenderPass);
	curRp_ = renderPasses_.back();
}

void VulkanRenderManager::EndFrame() {
	vulkan_->EndFrame();
}


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
				vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, c.drawIndexed.pipeline);
				vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, c.drawIndexed.pipelineLayout, 0, 1, &c.drawIndexed.ds, c.drawIndexed.numUboOffsets, c.drawIndexed.uboOffsets);
				vkCmdBindIndexBuffer(cmdbuf, c.drawIndexed.ibuffer, c.drawIndexed.ioffset, VK_INDEX_TYPE_UINT16);
				vkCmdBindVertexBuffers(cmdbuf, 0, 1, &c.drawIndexed.vbuffer, &c.drawIndexed.voffset);
				vkCmdDrawIndexed(cmdbuf, c.drawIndexed.count, c.drawIndexed.instances, 0, 0, 0);
				break;

			case VKR_DRAW:
				vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, c.draw.pipeline);
				vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, c.draw.pipelineLayout, 0, 1, &c.draw.ds, c.draw.numUboOffsets, c.draw.uboOffsets);
				vkCmdBindVertexBuffers(cmdbuf, 0, 1, &c.draw.vbuffer, &c.draw.voffset);
				vkCmdDraw(cmdbuf, c.draw.count, 1, 0, 0);
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
			{
				int numAttachments = 0;
				VkClearRect rc{};
				rc.baseArrayLayer = 0;
				rc.layerCount = 1;
				rc.rect.extent.width = curWidth_;
				rc.rect.extent.height = curHeight_;
				VkClearAttachment attachments[2];
				if (c.clear.clearMask & VK_IMAGE_ASPECT_COLOR_BIT) {
					VkClearAttachment &attachment = attachments[numAttachments++];
					attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					attachment.colorAttachment = 0;
					Uint8x4ToFloat4(attachment.clearValue.color.float32, c.clear.clearColor);
				}
				if (c.clear.clearMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
					VkClearAttachment &attachment = attachments[numAttachments++];
					attachment.aspectMask = 0;
					if (c.clear.clearMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
						attachment.clearValue.depthStencil.depth = c.clear.clearZ;
						attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
					}
					if (c.clear.clearMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
						attachment.clearValue.depthStencil.stencil = c.clear.clearStencil;
						attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
					}
				}
				if (numAttachments) {
					vkCmdClearAttachments(cmdbuf, numAttachments, attachments, 1, &rc);
				}
				break;
			}
			default:
				ELOG("Unimpl queue command");
				;
			}
		}
		delete renderPasses_[i];
	}
	renderPasses_.clear();
}

void VulkanRenderManager::Sync(VkCommandBuffer cmd) {

}
