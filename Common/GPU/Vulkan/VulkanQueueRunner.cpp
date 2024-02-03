#include <unordered_map>

#include "Common/GPU/DataFormat.h"
#include "Common/GPU/Vulkan/VulkanQueueRunner.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"

using namespace PPSSPP_VK;

// Debug help: adb logcat -s DEBUG AndroidRuntime PPSSPPNativeActivity PPSSPP NativeGLView NativeRenderer NativeSurfaceView PowerSaveModeReceiver InputDeviceState PpssppActivity CameraHelper

static void MergeRenderAreaRectInto(VkRect2D *dest, const VkRect2D &src) {
	if (dest->offset.x > src.offset.x) {
		dest->extent.width += (dest->offset.x - src.offset.x);
		dest->offset.x = src.offset.x;
	}
	if (dest->offset.y > src.offset.y) {
		dest->extent.height += (dest->offset.y - src.offset.y);
		dest->offset.y = src.offset.y;
	}
	if (dest->extent.width < src.extent.width) {
		dest->extent.width = src.extent.width;
	}
	if (dest->extent.height < src.extent.height) {
		dest->extent.height = src.extent.height;
	}
}

// We need to take the "max" of the features used in the two render passes.
RenderPassType MergeRPTypes(RenderPassType a, RenderPassType b) {
	// Either both are backbuffer type, or neither are.
	// These can't merge with other renderpasses
	if (a == RenderPassType::BACKBUFFER || b == RenderPassType::BACKBUFFER) {
		_dbg_assert_(a == b);
		return a;
	}

	_dbg_assert_((a & RenderPassType::MULTIVIEW) == (b & RenderPassType::MULTIVIEW));

	// The rest we can just OR together to get the maximum feature set.
	return (RenderPassType)((u32)a | (u32)b);
}

void VulkanQueueRunner::CreateDeviceObjects() {
	INFO_LOG(G3D, "VulkanQueueRunner::CreateDeviceObjects");

	RPKey key{
		VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR,
		VKRRenderPassStoreAction::STORE, VKRRenderPassStoreAction::DONT_CARE, VKRRenderPassStoreAction::DONT_CARE,
	};
	compatibleRenderPass_ = GetRenderPass(key);

#if 0
	// Just to check whether it makes sense to split some of these. drawidx is way bigger than the others...
	// We should probably just move to variable-size data in a raw buffer anyway...
	VkRenderData rd;
	INFO_LOG(G3D, "sizeof(pipeline): %d", (int)sizeof(rd.pipeline));
	INFO_LOG(G3D, "sizeof(draw): %d", (int)sizeof(rd.draw));
	INFO_LOG(G3D, "sizeof(drawidx): %d", (int)sizeof(rd.drawIndexed));
	INFO_LOG(G3D, "sizeof(clear): %d", (int)sizeof(rd.clear));
	INFO_LOG(G3D, "sizeof(viewport): %d", (int)sizeof(rd.viewport));
	INFO_LOG(G3D, "sizeof(scissor): %d", (int)sizeof(rd.scissor));
	INFO_LOG(G3D, "sizeof(blendColor): %d", (int)sizeof(rd.blendColor));
	INFO_LOG(G3D, "sizeof(push): %d", (int)sizeof(rd.push));
#endif
}

void VulkanQueueRunner::DestroyDeviceObjects() {
	INFO_LOG(G3D, "VulkanQueueRunner::DestroyDeviceObjects");

	syncReadback_.Destroy(vulkan_);

	renderPasses_.IterateMut([&](const RPKey &rpkey, VKRRenderPass *rp) {
		_assert_(rp);
		rp->Destroy(vulkan_);
		delete rp;
	});
	renderPasses_.Clear();
}

bool VulkanQueueRunner::CreateSwapchain(VkCommandBuffer cmdInit, VulkanBarrierBatch *barriers) {
	VkResult res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &swapchainImageCount_, nullptr);
	_dbg_assert_(res == VK_SUCCESS);

	VkImage *swapchainImages = new VkImage[swapchainImageCount_];
	res = vkGetSwapchainImagesKHR(vulkan_->GetDevice(), vulkan_->GetSwapchain(), &swapchainImageCount_, swapchainImages);
	if (res != VK_SUCCESS) {
		ERROR_LOG(G3D, "vkGetSwapchainImagesKHR failed");
		delete[] swapchainImages;
		return false;
	}

	for (uint32_t i = 0; i < swapchainImageCount_; i++) {
		SwapchainImageData sc_buffer{};
		sc_buffer.image = swapchainImages[i];

		VkImageViewCreateInfo color_image_view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		color_image_view.format = vulkan_->GetSwapchainFormat();
		color_image_view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		color_image_view.subresourceRange.baseMipLevel = 0;
		color_image_view.subresourceRange.levelCount = 1;
		color_image_view.subresourceRange.baseArrayLayer = 0;
		color_image_view.subresourceRange.layerCount = 1;  // TODO: Investigate hw-assisted stereo.
		color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		color_image_view.flags = 0;
		color_image_view.image = sc_buffer.image;

		// We leave the images as UNDEFINED, there's no need to pre-transition them as
		// the backbuffer renderpass starts out with them being auto-transitioned from UNDEFINED anyway.
		// Also, turns out it's illegal to transition un-acquired images, thanks Hans-Kristian. See #11417.

		res = vkCreateImageView(vulkan_->GetDevice(), &color_image_view, nullptr, &sc_buffer.view);
		vulkan_->SetDebugName(sc_buffer.view, VK_OBJECT_TYPE_IMAGE_VIEW, "swapchain_view");
		swapchainImages_.push_back(sc_buffer);
		_dbg_assert_(res == VK_SUCCESS);
	}
	delete[] swapchainImages;

	// Must be before InitBackbufferRenderPass.
	if (InitDepthStencilBuffer(cmdInit, barriers)) {
		InitBackbufferFramebuffers(vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	}
	return true;
}

bool VulkanQueueRunner::InitBackbufferFramebuffers(int width, int height) {
	VkResult res;
	// We share the same depth buffer but have multiple color buffers, see the loop below.
	VkImageView attachments[2] = { VK_NULL_HANDLE, depth_.view };

	VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb_info.renderPass = GetCompatibleRenderPass()->Get(vulkan_, RenderPassType::BACKBUFFER, VK_SAMPLE_COUNT_1_BIT);
	fb_info.attachmentCount = 2;
	fb_info.pAttachments = attachments;
	fb_info.width = width;
	fb_info.height = height;
	fb_info.layers = 1;

	framebuffers_.resize(swapchainImageCount_);

	for (uint32_t i = 0; i < swapchainImageCount_; i++) {
		attachments[0] = swapchainImages_[i].view;
		res = vkCreateFramebuffer(vulkan_->GetDevice(), &fb_info, nullptr, &framebuffers_[i]);
		_dbg_assert_(res == VK_SUCCESS);
		if (res != VK_SUCCESS) {
			framebuffers_.clear();
			return false;
		}
	}

	return true;
}

bool VulkanQueueRunner::InitDepthStencilBuffer(VkCommandBuffer cmd, VulkanBarrierBatch *barriers) {
	const VkFormat depth_format = vulkan_->GetDeviceInfo().preferredDepthStencilFormat;
	int aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = depth_format;
	image_info.extent.width = vulkan_->GetBackbufferWidth();
	image_info.extent.height = vulkan_->GetBackbufferHeight();
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.queueFamilyIndexCount = 0;
	image_info.pQueueFamilyIndices = nullptr;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	image_info.flags = 0;

	depth_.format = depth_format;

	VmaAllocationCreateInfo allocCreateInfo{};
	VmaAllocationInfo allocInfo{};

	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkResult res = vmaCreateImage(vulkan_->Allocator(), &image_info, &allocCreateInfo, &depth_.image, &depth_.alloc, &allocInfo);
	_dbg_assert_(res == VK_SUCCESS);
	if (res != VK_SUCCESS)
		return false;

	vulkan_->SetDebugName(depth_.image, VK_OBJECT_TYPE_IMAGE, "BackbufferDepth");

	VkImageMemoryBarrier *barrier = barriers->Add(depth_.image,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0);
	barrier->subresourceRange.aspectMask = aspectMask;
	barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier->newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	barrier->srcAccessMask = 0;
	barrier->dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkImageViewCreateInfo depth_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	depth_view_info.image = depth_.image;
	depth_view_info.format = depth_format;
	depth_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	depth_view_info.subresourceRange.aspectMask = aspectMask;
	depth_view_info.subresourceRange.baseMipLevel = 0;
	depth_view_info.subresourceRange.levelCount = 1;
	depth_view_info.subresourceRange.baseArrayLayer = 0;
	depth_view_info.subresourceRange.layerCount = 1;
	depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depth_view_info.flags = 0;

	VkDevice device = vulkan_->GetDevice();

	res = vkCreateImageView(device, &depth_view_info, NULL, &depth_.view);
	vulkan_->SetDebugName(depth_.view, VK_OBJECT_TYPE_IMAGE_VIEW, "depth_stencil_backbuffer");
	_dbg_assert_(res == VK_SUCCESS);
	if (res != VK_SUCCESS)
		return false;

	return true;
}


void VulkanQueueRunner::DestroyBackBuffers() {
	for (auto &image : swapchainImages_) {
		vulkan_->Delete().QueueDeleteImageView(image.view);
	}
	swapchainImages_.clear();

	if (depth_.view) {
		vulkan_->Delete().QueueDeleteImageView(depth_.view);
	}
	if (depth_.image) {
		_dbg_assert_(depth_.alloc);
		vulkan_->Delete().QueueDeleteImageAllocation(depth_.image, depth_.alloc);
	}
	depth_ = {};
	for (uint32_t i = 0; i < framebuffers_.size(); i++) {
		_dbg_assert_(framebuffers_[i] != VK_NULL_HANDLE);
		vulkan_->Delete().QueueDeleteFramebuffer(framebuffers_[i]);
	}
	framebuffers_.clear();

	INFO_LOG(G3D, "Backbuffers destroyed");
}


// Self-dependency: https://github.com/gpuweb/gpuweb/issues/442#issuecomment-547604827
// Also see https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#synchronization-pipeline-barriers-subpass-self-dependencies
VKRRenderPass *VulkanQueueRunner::GetRenderPass(const RPKey &key) {
	VKRRenderPass *foundPass;
	if (renderPasses_.Get(key, &foundPass)) {
		return foundPass;
	}

	VKRRenderPass *pass = new VKRRenderPass(key);
	renderPasses_.Insert(key, pass);
	return pass;
}

void VulkanQueueRunner::PreprocessSteps(std::vector<VKRStep *> &steps) {
	// Optimizes renderpasses, then sequences them.
	// Planned optimizations: 
	//  * Create copies of render target that are rendered to multiple times and textured from in sequence, and push those render passes
	//    as early as possible in the frame (Wipeout billboards). This will require taking over more of descriptor management so we can
	//    substitute descriptors, alternatively using texture array layers creatively.

	for (int j = 0; j < (int)steps.size(); j++) {
		if (steps[j]->stepType == VKRStepType::RENDER &&
			steps[j]->render.framebuffer) {
			if (steps[j]->render.finalColorLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
				steps[j]->render.finalColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
			if (steps[j]->render.finalDepthStencilLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
				steps[j]->render.finalDepthStencilLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}
		}
	}

	for (int j = 0; j < (int)steps.size() - 1; j++) {
		// Push down empty "Clear/Store" renderpasses, and merge them with the first "Load/Store" to the same framebuffer.
		if (steps.size() > 1 && steps[j]->stepType == VKRStepType::RENDER &&
			steps[j]->render.numDraws == 0 &&
			steps[j]->render.numReads == 0 &&
			steps[j]->render.colorLoad == VKRRenderPassLoadAction::CLEAR &&
			steps[j]->render.stencilLoad == VKRRenderPassLoadAction::CLEAR &&
			steps[j]->render.depthLoad == VKRRenderPassLoadAction::CLEAR) {

			// Drop the clear step, and merge it into the next step that touches the same framebuffer.
			for (int i = j + 1; i < (int)steps.size(); i++) {
				if (steps[i]->stepType == VKRStepType::RENDER &&
					steps[i]->render.framebuffer == steps[j]->render.framebuffer) {
					if (steps[i]->render.colorLoad != VKRRenderPassLoadAction::CLEAR) {
						steps[i]->render.colorLoad = VKRRenderPassLoadAction::CLEAR;
						steps[i]->render.clearColor = steps[j]->render.clearColor;
					}
					if (steps[i]->render.depthLoad != VKRRenderPassLoadAction::CLEAR) {
						steps[i]->render.depthLoad = VKRRenderPassLoadAction::CLEAR;
						steps[i]->render.clearDepth = steps[j]->render.clearDepth;
					}
					if (steps[i]->render.stencilLoad != VKRRenderPassLoadAction::CLEAR) {
						steps[i]->render.stencilLoad = VKRRenderPassLoadAction::CLEAR;
						steps[i]->render.clearStencil = steps[j]->render.clearStencil;
					}
					MergeRenderAreaRectInto(&steps[i]->render.renderArea, steps[j]->render.renderArea);
					steps[i]->render.renderPassType = MergeRPTypes(steps[i]->render.renderPassType, steps[j]->render.renderPassType);
					steps[i]->render.numDraws += steps[j]->render.numDraws;
					steps[i]->render.numReads += steps[j]->render.numReads;
					// Cheaply skip the first step.
					steps[j]->stepType = VKRStepType::RENDER_SKIP;
					break;
				} else if (steps[i]->stepType == VKRStepType::COPY &&
					steps[i]->copy.src == steps[j]->render.framebuffer) {
					// Can't eliminate the clear if a game copies from it before it's
					// rendered to. However this should be rare.
					// TODO: This should never happen when we check numReads now.
					break;
				}
			}
		}
	}

	// Queue hacks.
	if (hacksEnabled_) {
		if (hacksEnabled_ & QUEUE_HACK_MGS2_ACID) {
			// Massive speedup.
			ApplyMGSHack(steps);
		}
		if (hacksEnabled_ & QUEUE_HACK_SONIC) {
			ApplySonicHack(steps);
		}
		if (hacksEnabled_ & QUEUE_HACK_RENDERPASS_MERGE) {
			ApplyRenderPassMerge(steps);
		}
	}
}

void VulkanQueueRunner::RunSteps(std::vector<VKRStep *> &steps, int curFrame, FrameData &frameData, FrameDataShared &frameDataShared, bool keepSteps) {
	QueueProfileContext *profile = frameData.profile.enabled ? &frameData.profile : nullptr;

	if (profile)
		profile->cpuStartTime = time_now_d();

	bool emitLabels = vulkan_->Extensions().EXT_debug_utils;

	VkCommandBuffer cmd = frameData.hasPresentCommands ? frameData.presentCmd : frameData.mainCmd;

	for (size_t i = 0; i < steps.size(); i++) {
		const VKRStep &step = *steps[i];
		if (emitLabels) {
			VkDebugUtilsLabelEXT labelInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			char temp[128];
			if (step.stepType == VKRStepType::RENDER && step.render.framebuffer) {
				snprintf(temp, sizeof(temp), "%s: %s", step.tag, step.render.framebuffer->Tag());
				labelInfo.pLabelName = temp;
			} else {
				labelInfo.pLabelName = step.tag;
			}
			vkCmdBeginDebugUtilsLabelEXT(cmd, &labelInfo);
		}

		switch (step.stepType) {
		case VKRStepType::RENDER:
			if (!step.render.framebuffer) {
				if (emitLabels) {
					vkCmdEndDebugUtilsLabelEXT(cmd);
				}
				frameData.Submit(vulkan_, FrameSubmitType::Pending, frameDataShared);

				// When stepping in the GE debugger, we can end up here multiple times in a "frame".
				// So only acquire once.
				if (!frameData.hasAcquired) {
					frameData.AcquireNextImage(vulkan_);
					SetBackbuffer(framebuffers_[frameData.curSwapchainImage], swapchainImages_[frameData.curSwapchainImage].image);
				}

				if (!frameData.hasPresentCommands) {
					// A RENDER step rendering to the backbuffer is normally the last step that happens in a frame,
					// unless taking a screenshot, in which case there might be a READBACK_IMAGE after it.
					// This is why we have to switch cmd to presentCmd, in this case.
					VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
					begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
					vkBeginCommandBuffer(frameData.presentCmd, &begin);
					frameData.hasPresentCommands = true;
				}
				cmd = frameData.presentCmd;
				if (emitLabels) {
					VkDebugUtilsLabelEXT labelInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
					labelInfo.pLabelName = "present";
					vkCmdBeginDebugUtilsLabelEXT(cmd, &labelInfo);
				}
			}
			PerformRenderPass(step, cmd, curFrame, frameData.profile);
			break;
		case VKRStepType::COPY:
			PerformCopy(step, cmd);
			break;
		case VKRStepType::BLIT:
			PerformBlit(step, cmd);
			break;
		case VKRStepType::READBACK:
			PerformReadback(step, cmd, frameData);
			break;
		case VKRStepType::READBACK_IMAGE:
			PerformReadbackImage(step, cmd);
			break;
		case VKRStepType::RENDER_SKIP:
			break;
		}

		if (profile && profile->timestampsEnabled && profile->timestampDescriptions.size() + 1 < MAX_TIMESTAMP_QUERIES) {
			vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, profile->queryPool, (uint32_t)profile->timestampDescriptions.size());
			profile->timestampDescriptions.push_back(StepToString(vulkan_, step));
		}

		if (emitLabels) {
			vkCmdEndDebugUtilsLabelEXT(cmd);
		}
	}

	// Deleting all in one go should be easier on the instruction cache than deleting
	// them as we go - and easier to debug because we can look backwards in the frame.
	if (!keepSteps) {
		for (auto step : steps) {
			delete step;
		}
		steps.clear();
	}

	if (profile)
		profile->cpuEndTime = time_now_d();
}

void VulkanQueueRunner::ApplyMGSHack(std::vector<VKRStep *> &steps) {
	// Really need a sane way to express transforms of steps.

	// We want to turn a sequence of copy,render(1),copy,render(1),copy,render(1) to copy,copy,copy,render(n).

	for (int i = 0; i < (int)steps.size() - 3; i++) {
		int last = -1;
		if (!(steps[i]->stepType == VKRStepType::COPY &&
			steps[i + 1]->stepType == VKRStepType::RENDER &&
			steps[i + 2]->stepType == VKRStepType::COPY &&
			steps[i + 1]->render.numDraws == 1 &&
			steps[i]->copy.dst == steps[i + 2]->copy.dst))
			continue;
		// Looks promising! Let's start by finding the last one.
		for (int j = i; j < (int)steps.size(); j++) {
			switch (steps[j]->stepType) {
			case VKRStepType::RENDER:
				if (steps[j]->render.numDraws > 1)
					last = j - 1;
				// should really also check descriptor sets...
				if (steps[j]->commands.size()) {
					const VkRenderData &cmd = steps[j]->commands.back();
					if (cmd.cmd == VKRRenderCommand::DRAW_INDEXED && cmd.draw.count != 6)
						last = j - 1;
				}
				break;
			case VKRStepType::COPY:
				if (steps[j]->copy.dst != steps[i]->copy.dst)
					last = j - 1;
				break;
			default:
				break;
			}
			if (last != -1)
				break;
		}

		if (last != -1) {
			// We've got a sequence from i to last that needs reordering.
			// First, let's sort it, keeping the same length.
			std::vector<VKRStep *> copies;
			std::vector<VKRStep *> renders;
			copies.reserve((last - i) / 2);
			renders.reserve((last - i) / 2);
			for (int n = i; n <= last; n++) {
				if (steps[n]->stepType == VKRStepType::COPY)
					copies.push_back(steps[n]);
				else if (steps[n]->stepType == VKRStepType::RENDER)
					renders.push_back(steps[n]);
			}
			// Write the copies back. TODO: Combine them too.
			for (int j = 0; j < (int)copies.size(); j++) {
				steps[i + j] = copies[j];
			}
			// Write the renders back (so they will be deleted properly).
			for (int j = 0; j < (int)renders.size(); j++) {
				steps[i + j + copies.size()] = renders[j];
			}
			_assert_(steps[i + copies.size()]->stepType == VKRStepType::RENDER);
			// Combine the renders.
			for (int j = 1; j < (int)renders.size(); j++) {
				steps[i + copies.size()]->commands.reserve(renders[j]->commands.size());
				for (int k = 0; k < (int)renders[j]->commands.size(); k++) {
					steps[i + copies.size()]->commands.push_back(renders[j]->commands[k]);
				}
				steps[i + copies.size() + j]->stepType = VKRStepType::RENDER_SKIP;
			}
			// We're done.
			break;
		}
	}

	// There's also a post processing effect using depals that's just brutal in some parts
	// of the game.
	for (int i = 0; i < (int)steps.size() - 3; i++) {
		int last = -1;
		if (!(steps[i]->stepType == VKRStepType::RENDER &&
			steps[i + 1]->stepType == VKRStepType::RENDER &&
			steps[i + 2]->stepType == VKRStepType::RENDER &&
			steps[i]->render.numDraws == 1 &&
			steps[i + 1]->render.numDraws == 1 &&
			steps[i + 2]->render.numDraws == 1 &&
			steps[i]->render.colorLoad == VKRRenderPassLoadAction::DONT_CARE &&
			steps[i + 1]->render.colorLoad == VKRRenderPassLoadAction::KEEP &&
			steps[i + 2]->render.colorLoad == VKRRenderPassLoadAction::DONT_CARE))
			continue;
		VKRFramebuffer *depalFramebuffer = steps[i]->render.framebuffer;
		VKRFramebuffer *targetFramebuffer = steps[i + 1]->render.framebuffer;
		// OK, found the start of a post-process sequence. Let's scan until we find the end.
		for (int j = i; j < (int)steps.size() - 3; j++) {
			if (((j - i) & 1) == 0) {
				// This should be a depal draw.
				if (steps[j]->render.numDraws != 1)
					break;
				if (steps[j]->render.colorLoad != VKRRenderPassLoadAction::DONT_CARE)
					break;
				if (steps[j]->render.framebuffer != depalFramebuffer)
					break;
				last = j;
			} else {
				// This should be a target draw.
				if (steps[j]->render.numDraws != 1)
					break;
				if (steps[j]->render.colorLoad != VKRRenderPassLoadAction::KEEP)
					break;
				if (steps[j]->render.framebuffer != targetFramebuffer)
					break;
				last = j;
			}
		}

		if (last == -1)
			continue;

		// Combine the depal renders.
		for (int j = i + 2; j <= last + 1; j += 2) {
			for (int k = 0; k < (int)steps[j]->commands.size(); k++) {
				switch (steps[j]->commands[k].cmd) {
				case VKRRenderCommand::DRAW:
				case VKRRenderCommand::DRAW_INDEXED:
					steps[i]->commands.push_back(steps[j]->commands[k]);
					break;
				default:
					break;
				}
			}
			steps[j]->stepType = VKRStepType::RENDER_SKIP;
		}

		// Combine the target renders.
		for (int j = i + 3; j <= last; j += 2) {
			for (int k = 0; k < (int)steps[j]->commands.size(); k++) {
				switch (steps[j]->commands[k].cmd) {
				case VKRRenderCommand::DRAW:
				case VKRRenderCommand::DRAW_INDEXED:
					steps[i + 1]->commands.push_back(steps[j]->commands[k]);
					break;
				default:
					break;
				}
			}
			steps[j]->stepType = VKRStepType::RENDER_SKIP;
		}

		// We're done - we only expect one of these sequences per frame.
		break;
	}
}

void VulkanQueueRunner::ApplySonicHack(std::vector<VKRStep *> &steps) {
	// We want to turn a sequence of render(3),render(1),render(6),render(1),render(6),render(1),render(3) to
	// render(1), render(1), render(1), render(6), render(6), render(6)

	for (int i = 0; i < (int)steps.size() - 4; i++) {
		int last = -1;
		if (!(steps[i]->stepType == VKRStepType::RENDER &&
			steps[i + 1]->stepType == VKRStepType::RENDER &&
			steps[i + 2]->stepType == VKRStepType::RENDER &&
			steps[i + 3]->stepType == VKRStepType::RENDER &&
			steps[i]->render.numDraws == 3 &&
			steps[i + 1]->render.numDraws == 1 &&
			steps[i + 2]->render.numDraws == 6 &&
			steps[i + 3]->render.numDraws == 1 &&
			steps[i]->render.framebuffer == steps[i + 2]->render.framebuffer &&
			steps[i + 1]->render.framebuffer == steps[i + 3]->render.framebuffer))
			continue;
		// Looks promising! Let's start by finding the last one.
		for (int j = i; j < (int)steps.size(); j++) {
			switch (steps[j]->stepType) {
			case VKRStepType::RENDER:
				if ((j - i) & 1) {
					if (steps[j]->render.framebuffer != steps[i + 1]->render.framebuffer)
						last = j - 1;
					if (steps[j]->render.numDraws != 1)
						last = j - 1;
				} else {
					if (steps[j]->render.framebuffer != steps[i]->render.framebuffer)
						last = j - 1;
					if (steps[j]->render.numDraws != 3 && steps[j]->render.numDraws != 6)
						last = j - 1;
				}
				break;
			default:
				break;
			}
			if (last != -1)
				break;
		}

		if (last != -1) {
			// We've got a sequence from i to last that needs reordering.
			// First, let's sort it, keeping the same length.
			std::vector<VKRStep *> type1;
			std::vector<VKRStep *> type2;
			type1.reserve((last - i) / 2);
			type2.reserve((last - i) / 2);
			for (int n = i; n <= last; n++) {
				if (steps[n]->render.framebuffer == steps[i]->render.framebuffer)
					type1.push_back(steps[n]);
				else
					type2.push_back(steps[n]);
			}

			// Write the renders back in order. Same amount, so deletion will work fine.
			for (int j = 0; j < (int)type1.size(); j++) {
				steps[i + j] = type1[j];
			}
			for (int j = 0; j < (int)type2.size(); j++) {
				steps[i + j + type1.size()] = type2[j];
			}

			// Combine the renders.
			for (int j = 1; j < (int)type1.size(); j++) {
				for (int k = 0; k < (int)type1[j]->commands.size(); k++) {
					steps[i]->commands.push_back(type1[j]->commands[k]);
				}
				steps[i + j]->stepType = VKRStepType::RENDER_SKIP;
			}
			for (int j = 1; j < (int)type2.size(); j++) {
				for (int k = 0; k < (int)type2[j]->commands.size(); k++) {
					steps[i + type1.size()]->commands.push_back(type2[j]->commands[k]);
				}
				steps[i + j + type1.size()]->stepType = VKRStepType::RENDER_SKIP;
			}
			// We're done.
			break;
		}
	}
}

const char *AspectToString(VkImageAspectFlags aspect) {
	switch (aspect) {
	case VK_IMAGE_ASPECT_COLOR_BIT: return "COLOR";
	case VK_IMAGE_ASPECT_DEPTH_BIT: return "DEPTH";
	case VK_IMAGE_ASPECT_STENCIL_BIT: return "STENCIL";
	case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT: return "DEPTHSTENCIL";
	default: return "UNUSUAL";
	}
}

std::string VulkanQueueRunner::StepToString(VulkanContext *vulkan, const VKRStep &step) {
	char buffer[256];
	switch (step.stepType) {
	case VKRStepType::RENDER:
	{
		int w = step.render.framebuffer ? step.render.framebuffer->width : vulkan->GetBackbufferWidth();
		int h = step.render.framebuffer ? step.render.framebuffer->height : vulkan->GetBackbufferHeight();
		int actual_w = step.render.renderArea.extent.width;
		int actual_h = step.render.renderArea.extent.height;
		const char *renderCmd = GetRPTypeName(step.render.renderPassType);
		snprintf(buffer, sizeof(buffer), "%s %s %s (draws: %d, %dx%d/%dx%d)", renderCmd, step.tag, step.render.framebuffer ? step.render.framebuffer->Tag() : "", step.render.numDraws, actual_w, actual_h, w, h);
		break;
	}
	case VKRStepType::COPY:
		snprintf(buffer, sizeof(buffer), "COPY '%s' %s -> %s (%dx%d, %s)", step.tag, step.copy.src->Tag(), step.copy.dst->Tag(), step.copy.srcRect.extent.width, step.copy.srcRect.extent.height, AspectToString(step.copy.aspectMask));
		break;
	case VKRStepType::BLIT:
		snprintf(buffer, sizeof(buffer), "BLIT '%s' %s -> %s (%dx%d->%dx%d, %s)", step.tag, step.copy.src->Tag(), step.copy.dst->Tag(), step.blit.srcRect.extent.width, step.blit.srcRect.extent.height, step.blit.dstRect.extent.width, step.blit.dstRect.extent.height, AspectToString(step.blit.aspectMask));
		break;
	case VKRStepType::READBACK:
		snprintf(buffer, sizeof(buffer), "READBACK '%s' %s (%dx%d, %s)", step.tag, step.readback.src ? step.readback.src->Tag() : "(backbuffer)", step.readback.srcRect.extent.width, step.readback.srcRect.extent.height, AspectToString(step.readback.aspectMask));
		break;
	case VKRStepType::READBACK_IMAGE:
		snprintf(buffer, sizeof(buffer), "READBACK_IMAGE '%s' (%dx%d)", step.tag, step.readback_image.srcRect.extent.width, step.readback_image.srcRect.extent.height);
		break;
	case VKRStepType::RENDER_SKIP:
		snprintf(buffer, sizeof(buffer), "(RENDER_SKIP) %s", step.tag);
		break;
	default:
		buffer[0] = 0;
		break;
	}
	return std::string(buffer);
}

// Ideally, this should be cheap enough to be applied to all games. At least on mobile, it's pretty
// much a guaranteed neutral or win in terms of GPU power. However, dependency calculation really
// must be perfect!
void VulkanQueueRunner::ApplyRenderPassMerge(std::vector<VKRStep *> &steps) {
	// First let's count how many times each framebuffer is rendered to.
	// If it's more than one, let's do our best to merge them. This can help God of War quite a bit.
	std::unordered_map<VKRFramebuffer *, int> counts;
	for (int i = 0; i < (int)steps.size(); i++) {
		if (steps[i]->stepType == VKRStepType::RENDER) {
			counts[steps[i]->render.framebuffer]++;
		}
	}

	auto mergeRenderSteps = [](VKRStep *dst, VKRStep *src) {
		// OK. Now, if it's a render, slurp up all the commands and kill the step.
		// Also slurp up any pretransitions.
		dst->preTransitions.append(src->preTransitions);
		dst->commands.insert(dst->commands.end(), src->commands.begin(), src->commands.end());
		MergeRenderAreaRectInto(&dst->render.renderArea, src->render.renderArea);
		// So we don't consider it for other things, maybe doesn't matter.
		src->dependencies.clear();
		src->stepType = VKRStepType::RENDER_SKIP;
		dst->render.numDraws += src->render.numDraws;
		dst->render.numReads += src->render.numReads;
		dst->render.pipelineFlags |= src->render.pipelineFlags;
		dst->render.renderPassType = MergeRPTypes(dst->render.renderPassType, src->render.renderPassType);
	};
	auto renderHasClear = [](const VKRStep *step) {
		const auto &r = step->render;
		return r.colorLoad == VKRRenderPassLoadAction::CLEAR || r.depthLoad == VKRRenderPassLoadAction::CLEAR || r.stencilLoad == VKRRenderPassLoadAction::CLEAR;
	};

	// Now, let's go through the steps. If we find one that is rendered to more than once,
	// we'll scan forward and slurp up any rendering that can be merged across.
	for (int i = 0; i < (int)steps.size(); i++) {
		if (steps[i]->stepType == VKRStepType::RENDER && counts[steps[i]->render.framebuffer] > 1) {
			auto fb = steps[i]->render.framebuffer;
			TinySet<VKRFramebuffer *, 8> touchedFramebuffers;  // must be the same fast-size as the dependencies TinySet for annoying reasons.
			for (int j = i + 1; j < (int)steps.size(); j++) {
				// If any other passes are reading from this framebuffer as-is, we cancel the scan.
				if (steps[j]->dependencies.contains(fb)) {
					// Reading from itself means a KEEP, which is okay.
					if (steps[j]->stepType != VKRStepType::RENDER || steps[j]->render.framebuffer != fb)
						break;
				}
				switch (steps[j]->stepType) {
				case VKRStepType::RENDER:
					if (steps[j]->render.framebuffer == fb) {
						// Prevent Unknown's example case from https://github.com/hrydgard/ppsspp/pull/12242
						if (renderHasClear(steps[j]) || steps[j]->dependencies.contains(touchedFramebuffers)) {
							goto done_fb;
						} else {
							// Safe to merge, great.
							mergeRenderSteps(steps[i], steps[j]);
						}
					} else {
						// Remember the framebuffer this wrote to. We can't merge with later passes that depend on these.
						touchedFramebuffers.insert(steps[j]->render.framebuffer);
					}
					break;
				case VKRStepType::COPY:
					if (steps[j]->copy.dst == fb) {
						// Without framebuffer "renaming", we can't merge past a clobbered fb.
						goto done_fb;
					}
					touchedFramebuffers.insert(steps[j]->copy.dst);
					break;
				case VKRStepType::BLIT:
					if (steps[j]->blit.dst == fb) {
						// Without framebuffer "renaming", we can't merge past a clobbered fb.
						goto done_fb;
					}
					touchedFramebuffers.insert(steps[j]->blit.dst);
					break;
				case VKRStepType::READBACK:
					// Not sure this has much effect, when executed READBACK is always the last step
					// since we stall the GPU and wait immediately after.
					break;
				case VKRStepType::RENDER_SKIP:
				case VKRStepType::READBACK_IMAGE:
					break;
				default:
					// We added a new step?  Might be unsafe.
					goto done_fb;
				}
			}
			done_fb:
				;
		}
	}
}

void VulkanQueueRunner::LogSteps(const std::vector<VKRStep *> &steps, bool verbose) {
	INFO_LOG(G3D, "===================  FRAME  ====================");
	for (size_t i = 0; i < steps.size(); i++) {
		const VKRStep &step = *steps[i];
		switch (step.stepType) {
		case VKRStepType::RENDER:
			LogRenderPass(step, verbose);
			break;
		case VKRStepType::COPY:
			LogCopy(step);
			break;
		case VKRStepType::BLIT:
			LogBlit(step);
			break;
		case VKRStepType::READBACK:
			LogReadback(step);
			break;
		case VKRStepType::READBACK_IMAGE:
			LogReadbackImage(step);
			break;
		case VKRStepType::RENDER_SKIP:
			INFO_LOG(G3D, "(skipped render pass)");
			break;
		}
	}
	INFO_LOG(G3D, "-------------------  SUBMIT  ------------------");
}

const char *RenderPassActionName(VKRRenderPassLoadAction a) {
	switch (a) {
	case VKRRenderPassLoadAction::CLEAR:
		return "CLEAR";
	case VKRRenderPassLoadAction::DONT_CARE:
		return "DONT_CARE";
	case VKRRenderPassLoadAction::KEEP:
		return "KEEP";
	}
	return "?";
}

const char *ImageLayoutToString(VkImageLayout layout) {
	switch (layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT";
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT";
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY";
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC";
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST";
	case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC_KHR";
	case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
	default: return "(unknown)";
	}
}

void VulkanQueueRunner::LogRenderPass(const VKRStep &pass, bool verbose) {
	const auto &r = pass.render;
	const char *framebuf = r.framebuffer ? r.framebuffer->Tag() : "backbuffer";
	int w = r.framebuffer ? r.framebuffer->width : vulkan_->GetBackbufferWidth();
	int h = r.framebuffer ? r.framebuffer->height : vulkan_->GetBackbufferHeight();

	INFO_LOG(G3D, "RENDER %s Begin(%s, draws: %d, %dx%d, %s, %s, %s)", pass.tag, framebuf, r.numDraws, w, h, RenderPassActionName(r.colorLoad), RenderPassActionName(r.depthLoad), RenderPassActionName(r.stencilLoad));
	// TODO: Log these in detail.
	for (int i = 0; i < (int)pass.preTransitions.size(); i++) {
		INFO_LOG(G3D, "  PRETRANSITION: %s %s -> %s", pass.preTransitions[i].fb->Tag(), AspectToString(pass.preTransitions[i].aspect), ImageLayoutToString(pass.preTransitions[i].targetLayout));
	}

	if (verbose) {
		for (auto &cmd : pass.commands) {
			switch (cmd.cmd) {
			case VKRRenderCommand::REMOVED:
				INFO_LOG(G3D, "  (Removed)");
				break;
			case VKRRenderCommand::BIND_GRAPHICS_PIPELINE:
				INFO_LOG(G3D, "  BindGraphicsPipeline(%x)", (int)(intptr_t)cmd.graphics_pipeline.pipeline);
				break;
			case VKRRenderCommand::BLEND:
				INFO_LOG(G3D, "  BlendColor(%08x)", cmd.blendColor.color);
				break;
			case VKRRenderCommand::CLEAR:
				INFO_LOG(G3D, "  Clear");
				break;
			case VKRRenderCommand::DRAW:
				INFO_LOG(G3D, "  Draw(%d)", cmd.draw.count);
				break;
			case VKRRenderCommand::DRAW_INDEXED:
				INFO_LOG(G3D, "  DrawIndexed(%d)", cmd.drawIndexed.count);
				break;
			case VKRRenderCommand::SCISSOR:
				INFO_LOG(G3D, "  Scissor(%d, %d, %d, %d)", (int)cmd.scissor.scissor.offset.x, (int)cmd.scissor.scissor.offset.y, (int)cmd.scissor.scissor.extent.width, (int)cmd.scissor.scissor.extent.height);
				break;
			case VKRRenderCommand::STENCIL:
				INFO_LOG(G3D, "  Stencil(ref=%d, compare=%d, write=%d)", cmd.stencil.stencilRef, cmd.stencil.stencilCompareMask, cmd.stencil.stencilWriteMask);
				break;
			case VKRRenderCommand::VIEWPORT:
				INFO_LOG(G3D, "  Viewport(%f, %f, %f, %f, %f, %f)", cmd.viewport.vp.x, cmd.viewport.vp.y, cmd.viewport.vp.width, cmd.viewport.vp.height, cmd.viewport.vp.minDepth, cmd.viewport.vp.maxDepth);
				break;
			case VKRRenderCommand::PUSH_CONSTANTS:
				INFO_LOG(G3D, "  PushConstants(%d)", cmd.push.size);
				break;
			case VKRRenderCommand::DEBUG_ANNOTATION:
				INFO_LOG(G3D, "  DebugAnnotation(%s)", cmd.debugAnnotation.annotation);
				break;

			case VKRRenderCommand::NUM_RENDER_COMMANDS:
				break;
			}
		}
	}

	INFO_LOG(G3D, "  Final: %s %s", ImageLayoutToString(pass.render.finalColorLayout), ImageLayoutToString(pass.render.finalDepthStencilLayout));
	INFO_LOG(G3D, "RENDER End(%s) - %d commands executed", framebuf, (int)pass.commands.size());
}

void VulkanQueueRunner::LogCopy(const VKRStep &step) {
	INFO_LOG(G3D, "%s", StepToString(vulkan_, step).c_str());
}

void VulkanQueueRunner::LogBlit(const VKRStep &step) {
	INFO_LOG(G3D, "%s", StepToString(vulkan_, step).c_str());
}

void VulkanQueueRunner::LogReadback(const VKRStep &step) {
	INFO_LOG(G3D, "%s", StepToString(vulkan_, step).c_str());
}

void VulkanQueueRunner::LogReadbackImage(const VKRStep &step) {
	INFO_LOG(G3D, "%s", StepToString(vulkan_, step).c_str());
}

void TransitionToOptimal(VkCommandBuffer cmd, VkImage colorImage, VkImageLayout colorLayout, VkImage depthStencilImage, VkImageLayout depthStencilLayout, int numLayers, VulkanBarrier *recordBarrier) {
	if (colorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		VkPipelineStageFlags srcStageMask = 0;
		VkAccessFlags srcAccessMask = 0;
		switch (colorLayout) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			// No need to specify stage or access.
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			// Already the right color layout. Unclear that we need to do a lot here..
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
			// We came from the Mali workaround, and are transitioning back to COLOR_ATTACHMENT_OPTIMAL.
			srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		default:
			_dbg_assert_msg_(false, "TransitionToOptimal: Unexpected color layout %d", (int)colorLayout);
			break;
		}
		recordBarrier->TransitionImage(
			colorImage, 0, 1, numLayers, VK_IMAGE_ASPECT_COLOR_BIT,
			colorLayout,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			srcAccessMask,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			srcStageMask,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	}

	if (depthStencilImage != VK_NULL_HANDLE && depthStencilLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		VkPipelineStageFlags srcStageMask = 0;
		VkAccessFlags srcAccessMask = 0;
		switch (depthStencilLayout) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			// No need to specify stage or access.
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			// Already the right depth layout. Unclear that we need to do a lot here..
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		default:
			_dbg_assert_msg_(false, "TransitionToOptimal: Unexpected depth layout %d", (int)depthStencilLayout);
			break;
		}
		recordBarrier->TransitionImage(
			depthStencilImage, 0, 1, numLayers, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
			depthStencilLayout,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			srcAccessMask,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			srcStageMask,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
	}
}

void TransitionFromOptimal(VkCommandBuffer cmd, VkImage colorImage, VkImageLayout colorLayout, VkImage depthStencilImage, int numLayers, VkImageLayout depthStencilLayout) {
	VkPipelineStageFlags srcStageMask = 0;
	VkPipelineStageFlags dstStageMask = 0;

	// If layouts aren't optimal, transition them.
	VkImageMemoryBarrier barrier[2]{};

	int barrierCount = 0;
	if (colorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier[0].pNext = nullptr;
		srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		// And the final transition.
		// Don't need to transition it if VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL.
		switch (colorLayout) {
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			dstStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			dstStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_UNDEFINED:
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			// Nothing to do.
			break;
		default:
			_dbg_assert_msg_(false, "TransitionFromOptimal: Unexpected final color layout %d", (int)colorLayout);
			break;
		}
		barrier[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier[0].newLayout = colorLayout;
		barrier[0].image = colorImage;
		barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier[0].subresourceRange.baseMipLevel = 0;
		barrier[0].subresourceRange.levelCount = 1;
		barrier[0].subresourceRange.layerCount = numLayers;
		barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCount++;
	}

	if (depthStencilImage && depthStencilLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier[barrierCount].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier[barrierCount].pNext = nullptr;

		srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		barrier[barrierCount].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		switch (depthStencilLayout) {
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier[barrierCount].dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
			dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			barrier[barrierCount].dstAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
			dstStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier[barrierCount].dstAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
			dstStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_UNDEFINED:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			// Nothing to do.
			break;
		default:
			_dbg_assert_msg_(false, "TransitionFromOptimal: Unexpected final depth layout %d", (int)depthStencilLayout);
			break;
		}
		barrier[barrierCount].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		barrier[barrierCount].newLayout = depthStencilLayout;
		barrier[barrierCount].image = depthStencilImage;
		barrier[barrierCount].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		barrier[barrierCount].subresourceRange.baseMipLevel = 0;
		barrier[barrierCount].subresourceRange.levelCount = 1;
		barrier[barrierCount].subresourceRange.layerCount = numLayers;
		barrier[barrierCount].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier[barrierCount].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrierCount++;
	}
	if (barrierCount) {
		vkCmdPipelineBarrier(cmd, srcStageMask, dstStageMask, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, barrierCount, barrier);
	}
}

void VulkanQueueRunner::PerformRenderPass(const VKRStep &step, VkCommandBuffer cmd, int curFrame, QueueProfileContext &profile) {
	for (size_t i = 0; i < step.preTransitions.size(); i++) {
		const TransitionRequest &iter = step.preTransitions[i];
		if (iter.aspect == VK_IMAGE_ASPECT_COLOR_BIT && iter.fb->color.layout != iter.targetLayout) {
			recordBarrier_.TransitionImageAuto(
				iter.fb->color.image,
				0,
				1,
				iter.fb->numLayers,
				VK_IMAGE_ASPECT_COLOR_BIT,
				iter.fb->color.layout,
				iter.targetLayout
			);
			iter.fb->color.layout = iter.targetLayout;
		} else if (iter.fb->depth.image != VK_NULL_HANDLE && (iter.aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) && iter.fb->depth.layout != iter.targetLayout) {
			recordBarrier_.TransitionImageAuto(
				iter.fb->depth.image,
				0,
				1,
				iter.fb->numLayers,
				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				iter.fb->depth.layout,
				iter.targetLayout
			);
			iter.fb->depth.layout = iter.targetLayout;
		}
	}

	// Don't execute empty renderpasses that keep the contents.
	if (step.commands.empty() && step.render.colorLoad == VKRRenderPassLoadAction::KEEP && step.render.depthLoad == VKRRenderPassLoadAction::KEEP && step.render.stencilLoad == VKRRenderPassLoadAction::KEEP) {
		// Flush the pending barrier
		recordBarrier_.Flush(cmd);
		// Nothing to do.
		// TODO: Though - a later step might have used this step's finalColorLayout etc to get things in a layout it expects.
		// Should we just do a barrier? Or just let the later step deal with not having things in its preferred layout, like now?
		return;
	}

	// Write-after-write hazards. Fixed flicker in God of War on ARM (before we added another fix that removed these).
	// These aren't so common so not bothering to combine the barrier with the pretransition one.
	if (step.render.framebuffer) {
		int n = 0;
		int stage = 0;

		if (step.render.framebuffer->color.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			recordBarrier_.TransitionImage(
				step.render.framebuffer->color.image,
				0,
				1,
				step.render.framebuffer->numLayers,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				);
		}
		if (step.render.framebuffer->depth.image != VK_NULL_HANDLE && step.render.framebuffer->depth.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			recordBarrier_.TransitionImage(
				step.render.framebuffer->depth.image,
				0,
				1,
				step.render.framebuffer->numLayers,
				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
			);
		}
	}

	// This reads the layout of the color and depth images, and chooses a render pass using them that
	// will transition to the desired final layout.
	//
	// NOTE: Flushes recordBarrier_.
	VKRRenderPass *renderPass = PerformBindFramebufferAsRenderTarget(step, cmd);

	int curWidth = step.render.framebuffer ? step.render.framebuffer->width : vulkan_->GetBackbufferWidth();
	int curHeight = step.render.framebuffer ? step.render.framebuffer->height : vulkan_->GetBackbufferHeight();

	VKRFramebuffer *fb = step.render.framebuffer;

	VKRGraphicsPipeline *lastGraphicsPipeline = nullptr;
	VKRComputePipeline *lastComputePipeline = nullptr;

	const auto &commands = step.commands;

	// We can do a little bit of state tracking here to eliminate some calls into the driver.
	// The stencil ones are very commonly mostly redundant so let's eliminate them where possible.
	// Might also want to consider scissor and viewport.
	VkPipeline lastPipeline = VK_NULL_HANDLE;
	FastVec<PendingDescSet> *descSets = nullptr;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

	bool pipelineOK = false;

	int lastStencilWriteMask = -1;
	int lastStencilCompareMask = -1;
	int lastStencilReference = -1;

	const RenderPassType rpType = step.render.renderPassType;

	for (size_t i = 0; i < commands.size(); i++) {
		const VkRenderData &c = commands[i];
#ifdef _DEBUG
		if (profile.enabled) {
			if ((size_t)step.stepType < ARRAY_SIZE(profile.commandCounts)) {
				profile.commandCounts[(size_t)c.cmd]++;
			}
		}
#endif
		switch (c.cmd) {
		case VKRRenderCommand::REMOVED:
			break;

		case VKRRenderCommand::BIND_GRAPHICS_PIPELINE:
		{
			VKRGraphicsPipeline *graphicsPipeline = c.graphics_pipeline.pipeline;
			if (graphicsPipeline != lastGraphicsPipeline) {
				VkSampleCountFlagBits fbSampleCount = fb ? fb->sampleCount : VK_SAMPLE_COUNT_1_BIT;

				if (RenderPassTypeHasMultisample(rpType) && fbSampleCount != graphicsPipeline->SampleCount()) {
					// should have been invalidated.
					_assert_msg_(graphicsPipeline->SampleCount() == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM,
						"expected %d sample count, got %d", fbSampleCount, graphicsPipeline->SampleCount());
				}

				if (!graphicsPipeline->pipeline[(size_t)rpType]) {
					// NOTE: If render steps got merged, it can happen that, as they ended during recording,
					// they didn't know their final render pass type so they created the wrong pipelines in EndCurRenderStep().
					// Unfortunately I don't know if we can fix it in any more sensible place than here.
					// Maybe a middle pass. But let's try to just block and compile here for now, this doesn't
					// happen all that much.
					graphicsPipeline->pipeline[(size_t)rpType] = Promise<VkPipeline>::CreateEmpty();
					graphicsPipeline->Create(vulkan_, renderPass->Get(vulkan_, rpType, fbSampleCount), rpType, fbSampleCount, time_now_d(), -1);
				}

				VkPipeline pipeline = graphicsPipeline->pipeline[(size_t)rpType]->BlockUntilReady();

				if (pipeline != VK_NULL_HANDLE) {
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					descSets = &c.graphics_pipeline.pipelineLayout->frameData[curFrame].descSets_;
					pipelineLayout = c.graphics_pipeline.pipelineLayout->pipelineLayout;
					_dbg_assert_(pipelineLayout != VK_NULL_HANDLE);
					lastGraphicsPipeline = graphicsPipeline;
					pipelineOK = true;
				} else {
					pipelineOK = false;
				}

				// Reset dynamic state so it gets refreshed with the new pipeline.
				lastStencilWriteMask = -1;
				lastStencilCompareMask = -1;
				lastStencilReference = -1;
			}
			break;
		}

		case VKRRenderCommand::VIEWPORT:
			if (fb != nullptr) {
				vkCmdSetViewport(cmd, 0, 1, &c.viewport.vp);
			} else {
				const VkViewport &vp = c.viewport.vp;
				DisplayRect<float> rc{ vp.x, vp.y, vp.width, vp.height };
				RotateRectToDisplay(rc, (float)vulkan_->GetBackbufferWidth(), (float)vulkan_->GetBackbufferHeight());
				VkViewport final_vp;
				final_vp.x = rc.x;
				final_vp.y = rc.y;
				final_vp.width = rc.w;
				final_vp.height = rc.h;
				final_vp.maxDepth = vp.maxDepth;
				final_vp.minDepth = vp.minDepth;
				vkCmdSetViewport(cmd, 0, 1, &final_vp);
			}
			break;

		case VKRRenderCommand::SCISSOR:
		{
			if (fb != nullptr) {
				vkCmdSetScissor(cmd, 0, 1, &c.scissor.scissor);
			} else {
				// Rendering to backbuffer. Might need to rotate.
				const VkRect2D &rc = c.scissor.scissor;
				DisplayRect<int> rotated_rc{ rc.offset.x, rc.offset.y, (int)rc.extent.width, (int)rc.extent.height };
				RotateRectToDisplay(rotated_rc, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
				_dbg_assert_(rotated_rc.x >= 0);
				_dbg_assert_(rotated_rc.y >= 0);
				VkRect2D finalRect = VkRect2D{ { rotated_rc.x, rotated_rc.y }, { (uint32_t)rotated_rc.w, (uint32_t)rotated_rc.h} };
				vkCmdSetScissor(cmd, 0, 1, &finalRect);
			}
			break;
		}

		case VKRRenderCommand::BLEND:
		{
			float bc[4];
			Uint8x4ToFloat4(bc, c.blendColor.color);
			vkCmdSetBlendConstants(cmd, bc);
			break;
		}

		case VKRRenderCommand::PUSH_CONSTANTS:
			if (pipelineOK) {
				vkCmdPushConstants(cmd, pipelineLayout, c.push.stages, c.push.offset, c.push.size, c.push.data);
			}
			break;

		case VKRRenderCommand::STENCIL:
			if (lastStencilWriteMask != c.stencil.stencilWriteMask) {
				lastStencilWriteMask = (int)c.stencil.stencilWriteMask;
				vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilWriteMask);
			}
			if (lastStencilCompareMask != c.stencil.stencilCompareMask) {
				lastStencilCompareMask = c.stencil.stencilCompareMask;
				vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilCompareMask);
			}
			if (lastStencilReference != c.stencil.stencilRef) {
				lastStencilReference = c.stencil.stencilRef;
				vkCmdSetStencilReference(cmd, VK_STENCIL_FRONT_AND_BACK, c.stencil.stencilRef);
			}
			break;

		case VKRRenderCommand::DRAW_INDEXED:
			if (pipelineOK) {
				VkDescriptorSet set = (*descSets)[c.drawIndexed.descSetIndex].set;
				_dbg_assert_(set != VK_NULL_HANDLE);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, c.drawIndexed.numUboOffsets, c.drawIndexed.uboOffsets);
				vkCmdBindIndexBuffer(cmd, c.drawIndexed.ibuffer, c.drawIndexed.ioffset, VK_INDEX_TYPE_UINT16);
				VkDeviceSize voffset = c.drawIndexed.voffset;
				vkCmdBindVertexBuffers(cmd, 0, 1, &c.drawIndexed.vbuffer, &voffset);
				vkCmdDrawIndexed(cmd, c.drawIndexed.count, c.drawIndexed.instances, 0, 0, 0);
			}
			break;

		case VKRRenderCommand::DRAW:
			if (pipelineOK) {
				VkDescriptorSet set = (*descSets)[c.drawIndexed.descSetIndex].set;
				_dbg_assert_(set != VK_NULL_HANDLE);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, c.draw.numUboOffsets, c.draw.uboOffsets);
				if (c.draw.vbuffer) {
					vkCmdBindVertexBuffers(cmd, 0, 1, &c.draw.vbuffer, &c.draw.voffset);
				}
				vkCmdDraw(cmd, c.draw.count, 1, c.draw.offset, 0);
			}
			break;

		case VKRRenderCommand::CLEAR:
		{
			// If we get here, we failed to merge a clear into a render pass load op. This is bad for perf.
			int numAttachments = 0;
			VkClearRect rc{};
			rc.baseArrayLayer = 0;
			rc.layerCount = 1;  // In multiview mode, 1 means to replicate to all the active layers.
			rc.rect.extent.width = (uint32_t)curWidth;
			rc.rect.extent.height = (uint32_t)curHeight;
			VkClearAttachment attachments[2]{};
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
					attachment.clearValue.depthStencil.stencil = (uint32_t)c.clear.clearStencil;
					attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			if (numAttachments) {
				vkCmdClearAttachments(cmd, numAttachments, attachments, 1, &rc);
			}
			break;
		}

		case VKRRenderCommand::DEBUG_ANNOTATION:
			if (vulkan_->Extensions().EXT_debug_utils) {
				VkDebugUtilsLabelEXT labelInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
				labelInfo.pLabelName = c.debugAnnotation.annotation;
				vkCmdInsertDebugUtilsLabelEXT(cmd, &labelInfo);
			}
			break;

		default:
			ERROR_LOG(G3D, "Unimpl queue command");
			break;
		}
	}
	vkCmdEndRenderPass(cmd);

	if (fb) {
		// If the desired final layout aren't the optimal layout for rendering, transition.
		TransitionFromOptimal(cmd, fb->color.image, step.render.finalColorLayout, fb->depth.image, fb->numLayers, step.render.finalDepthStencilLayout);

		fb->color.layout = step.render.finalColorLayout;
		fb->depth.layout = step.render.finalDepthStencilLayout;
	}
}

VKRRenderPass *VulkanQueueRunner::PerformBindFramebufferAsRenderTarget(const VKRStep &step, VkCommandBuffer cmd) {
	VKRRenderPass *renderPass;
	int numClearVals = 0;
	VkClearValue clearVal[4]{};
	VkFramebuffer framebuf;
	int w;
	int h;

	bool hasDepth = RenderPassTypeHasDepth(step.render.renderPassType);

	VkSampleCountFlagBits sampleCount;

	if (step.render.framebuffer) {
		_dbg_assert_(step.render.finalColorLayout != VK_IMAGE_LAYOUT_UNDEFINED);
		_dbg_assert_(step.render.finalDepthStencilLayout != VK_IMAGE_LAYOUT_UNDEFINED);

		RPKey key{
			step.render.colorLoad, step.render.depthLoad, step.render.stencilLoad,
			step.render.colorStore, step.render.depthStore, step.render.stencilStore,
		};
		renderPass = GetRenderPass(key);

		VKRFramebuffer *fb = step.render.framebuffer;
		framebuf = fb->Get(renderPass, step.render.renderPassType);
		sampleCount = fb->sampleCount;
		_dbg_assert_(framebuf != VK_NULL_HANDLE);
		w = fb->width;
		h = fb->height;

		// Mali driver on S8 (Android O) and S9 mishandles renderpasses that do just a clear
		// and then no draw calls. Memory transaction elimination gets mis-flagged or something.
		// To avoid this, we transition to GENERAL and back in this case (ARM-approved workaround).
		// See pull request #10723.
		bool maliBugWorkaround = step.render.numDraws == 0 &&
			step.render.colorLoad == VKRRenderPassLoadAction::CLEAR &&
			vulkan_->GetPhysicalDeviceProperties().properties.driverVersion == 0xaa9c4b29;
		if (maliBugWorkaround) {
			recordBarrier_.TransitionImage(fb->color.image, 0, 1, fb->numLayers, VK_IMAGE_ASPECT_COLOR_BIT,
				fb->color.layout, VK_IMAGE_LAYOUT_GENERAL,
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			fb->color.layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		TransitionToOptimal(cmd, fb->color.image, fb->color.layout, fb->depth.image, fb->depth.layout, fb->numLayers, &recordBarrier_);

		// The transition from the optimal format happens after EndRenderPass, now that we don't
		// do it as part of the renderpass itself anymore.

		if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
			// We don't initialize values for these.
			numClearVals = hasDepth ? 2 : 1; // Skip the resolve buffers, don't need to clear those.
		}
		if (step.render.colorLoad == VKRRenderPassLoadAction::CLEAR) {
			Uint8x4ToFloat4(clearVal[numClearVals].color.float32, step.render.clearColor);
		}
		numClearVals++;
		if (hasDepth) {
			if (step.render.depthLoad == VKRRenderPassLoadAction::CLEAR || step.render.stencilLoad == VKRRenderPassLoadAction::CLEAR) {
				clearVal[numClearVals].depthStencil.depth = step.render.clearDepth;
				clearVal[numClearVals].depthStencil.stencil = step.render.clearStencil;
			}
			numClearVals++;
		}
		_dbg_assert_(numClearVals != 3);
	} else {
		RPKey key{
			VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR, VKRRenderPassLoadAction::CLEAR,
			VKRRenderPassStoreAction::STORE, VKRRenderPassStoreAction::DONT_CARE, VKRRenderPassStoreAction::DONT_CARE,
		};
		renderPass = GetRenderPass(key);

		if (IsVREnabled()) {
			framebuf = (VkFramebuffer)BindVRFramebuffer();
		} else {
			framebuf = backbuffer_;
		}

		// Raw, rotated backbuffer size.
		w = vulkan_->GetBackbufferWidth();
		h = vulkan_->GetBackbufferHeight();

		Uint8x4ToFloat4(clearVal[0].color.float32, step.render.clearColor);
		numClearVals = hasDepth ? 2 : 1;  // We might do depth-less backbuffer in the future, though doubtful of the value.
		clearVal[1].depthStencil.depth = 0.0f;
		clearVal[1].depthStencil.stencil = 0;
		sampleCount = VK_SAMPLE_COUNT_1_BIT;
	}

	VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rp_begin.renderPass = renderPass->Get(vulkan_, step.render.renderPassType, sampleCount);
	rp_begin.framebuffer = framebuf;

	VkRect2D rc = step.render.renderArea;
	if (!step.render.framebuffer) {
		// Rendering to backbuffer, must rotate, just like scissors.
		DisplayRect<int> rotated_rc{ rc.offset.x, rc.offset.y, (int)rc.extent.width, (int)rc.extent.height };
		RotateRectToDisplay(rotated_rc, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());

		rc.offset.x = rotated_rc.x;
		rc.offset.y = rotated_rc.y;
		rc.extent.width = rotated_rc.w;
		rc.extent.height = rotated_rc.h;
	}

	recordBarrier_.Flush(cmd);

	rp_begin.renderArea = rc;
	rp_begin.clearValueCount = numClearVals;
	rp_begin.pClearValues = numClearVals ? clearVal : nullptr;
	vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

	return renderPass;
}

void VulkanQueueRunner::PerformCopy(const VKRStep &step, VkCommandBuffer cmd) {
	// The barrier code doesn't handle this case. We'd need to transition to GENERAL to do an intra-image copy.
	_dbg_assert_(step.copy.src != step.copy.dst);

	VKRFramebuffer *src = step.copy.src;
	VKRFramebuffer *dst = step.copy.dst;

	int layerCount = std::min(step.copy.src->numLayers, step.copy.dst->numLayers);
	_dbg_assert_(step.copy.src->numLayers >= step.copy.dst->numLayers);

	// TODO: If dst covers exactly the whole destination, we can set up a UNDEFINED->TRANSFER_DST_OPTIMAL transition,
	// which can potentially be more efficient.

	// First source barriers.
	if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		SetupTransitionToTransferSrc(src->color, VK_IMAGE_ASPECT_COLOR_BIT, &recordBarrier_);
		SetupTransitionToTransferDst(dst->color, VK_IMAGE_ASPECT_COLOR_BIT, &recordBarrier_);
	}

	// We can't copy only depth or only stencil unfortunately - or can we?.
	if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		_dbg_assert_(src->depth.image != VK_NULL_HANDLE);

		SetupTransitionToTransferSrc(src->depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &recordBarrier_);
		if (dst->depth.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			SetupTransitionToTransferDst(dst->depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &recordBarrier_);
			_dbg_assert_(dst->depth.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		} else {
			// Kingdom Hearts: Subsequent copies twice to the same depth buffer without any other use.
			// Not super sure how that happens, but we need a barrier to pass sync validation.
			SetupTransferDstWriteAfterWrite(dst->depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &recordBarrier_);
		}
	}
	recordBarrier_.Flush(cmd);

	bool multisampled = src->sampleCount != VK_SAMPLE_COUNT_1_BIT && dst->sampleCount != VK_SAMPLE_COUNT_1_BIT;
	if (multisampled) {
		// If both the targets are multisampled, copy the msaa targets too.
		// For that, we need to transition them from their normally permanent VK_*_ATTACHMENT_OPTIMAL layouts, and then back.
		if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
			SetupTransitionToTransferSrc(src->msaaColor, VK_IMAGE_ASPECT_COLOR_BIT, &recordBarrier_);
			recordBarrier_.Flush(cmd);
			SetupTransitionToTransferDst(dst->msaaColor, VK_IMAGE_ASPECT_COLOR_BIT, &recordBarrier_);
			recordBarrier_.Flush(cmd);
		}
		if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
			// Kingdom Hearts: Subsequent copies to the same depth buffer without any other use.
			// Not super sure how that happens, but we need a barrier to pass sync validation.
			SetupTransitionToTransferSrc(src->msaaDepth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &recordBarrier_);
			recordBarrier_.Flush(cmd);
			SetupTransitionToTransferDst(dst->msaaDepth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &recordBarrier_);
			recordBarrier_.Flush(cmd);
		}
	}

	recordBarrier_.Flush(cmd);

	VkImageCopy copy{};
	copy.srcOffset.x = step.copy.srcRect.offset.x;
	copy.srcOffset.y = step.copy.srcRect.offset.y;
	copy.srcOffset.z = 0;
	copy.srcSubresource.mipLevel = 0;
	copy.srcSubresource.layerCount = layerCount;
	copy.dstOffset.x = step.copy.dstPos.x;
	copy.dstOffset.y = step.copy.dstPos.y;
	copy.dstOffset.z = 0;
	copy.dstSubresource.mipLevel = 0;
	copy.dstSubresource.layerCount = layerCount;
	copy.extent.width = step.copy.srcRect.extent.width;
	copy.extent.height = step.copy.srcRect.extent.height;
	copy.extent.depth = 1;

	if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdCopyImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &copy);

		if (multisampled) {
			vkCmdCopyImage(cmd, src->msaaColor.image, src->msaaColor.layout, dst->msaaColor.image, dst->msaaColor.layout, 1, &copy);
		}
	}
	if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		_dbg_assert_(src->depth.image != VK_NULL_HANDLE);
		_dbg_assert_(dst->depth.image != VK_NULL_HANDLE);
		copy.srcSubresource.aspectMask = step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		copy.dstSubresource.aspectMask = step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
		vkCmdCopyImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &copy);

		if (multisampled) {
			vkCmdCopyImage(cmd, src->msaaDepth.image, src->msaaDepth.layout, dst->msaaDepth.image, dst->msaaDepth.layout, 1, &copy);
		}
	}

	if (multisampled) {
		// Transition the MSAA surfaces back to optimal.
		if (step.copy.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
			recordBarrier_.TransitionImage(
				src->msaaColor.image,
				0,
				1,
				src->msaaColor.numLayers,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			);
			recordBarrier_.TransitionImage(
				dst->msaaColor.image,
				0,
				1,
				dst->msaaColor.numLayers,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			);
			src->msaaColor.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			dst->msaaColor.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (step.copy.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
			recordBarrier_.TransitionImage(
				src->msaaDepth.image,
				0,
				1,
				src->msaaDepth.numLayers,
				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
			);
			recordBarrier_.TransitionImage(
				dst->msaaDepth.image,
				0,
				1,
				dst->msaaDepth.numLayers,
				VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
			);
			src->msaaDepth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			dst->msaaDepth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		recordBarrier_.Flush(cmd);
	}
}

void VulkanQueueRunner::PerformBlit(const VKRStep &step, VkCommandBuffer cmd) {
	// The barrier code doesn't handle this case. We'd need to transition to GENERAL to do an intra-image copy.
	_dbg_assert_(step.blit.src != step.blit.dst);

	int layerCount = std::min(step.blit.src->numLayers, step.blit.dst->numLayers);
	_dbg_assert_(step.blit.src->numLayers >= step.blit.dst->numLayers);

	VKRFramebuffer *src = step.blit.src;
	VKRFramebuffer *dst = step.blit.dst;

	// First source barriers.
	if (step.blit.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		SetupTransitionToTransferSrc(src->color, VK_IMAGE_ASPECT_COLOR_BIT, &recordBarrier_);
		SetupTransitionToTransferDst(dst->color, VK_IMAGE_ASPECT_COLOR_BIT, &recordBarrier_);
	}

	// We can't copy only depth or only stencil unfortunately.
	if (step.blit.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		_assert_(src->depth.image != VK_NULL_HANDLE);
		_assert_(dst->depth.image != VK_NULL_HANDLE);
		SetupTransitionToTransferSrc(src->depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &recordBarrier_);
		SetupTransitionToTransferDst(dst->depth, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, &recordBarrier_);
	}

	recordBarrier_.Flush(cmd);

	// If any validation needs to be performed here, it should probably have been done
	// already when the blit was queued. So don't validate here.
	VkImageBlit blit{};
	blit.srcOffsets[0].x = step.blit.srcRect.offset.x;
	blit.srcOffsets[0].y = step.blit.srcRect.offset.y;
	blit.srcOffsets[0].z = 0;
	blit.srcOffsets[1].x = step.blit.srcRect.offset.x + step.blit.srcRect.extent.width;
	blit.srcOffsets[1].y = step.blit.srcRect.offset.y + step.blit.srcRect.extent.height;
	blit.srcOffsets[1].z = 1;
	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.layerCount = layerCount;
	blit.dstOffsets[0].x = step.blit.dstRect.offset.x;
	blit.dstOffsets[0].y = step.blit.dstRect.offset.y;
	blit.dstOffsets[0].z = 0;
	blit.dstOffsets[1].x = step.blit.dstRect.offset.x + step.blit.dstRect.extent.width;
	blit.dstOffsets[1].y = step.blit.dstRect.offset.y + step.blit.dstRect.extent.height;
	blit.dstOffsets[1].z = 1;
	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.layerCount = layerCount;

	if (step.blit.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdBlitImage(cmd, src->color.image, src->color.layout, dst->color.image, dst->color.layout, 1, &blit, step.blit.filter);
	}

	// TODO: Need to check if the depth format is blittable.
	// Actually, we should probably almost always use copies rather than blits for depth buffers.
	if (step.blit.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
		blit.srcSubresource.aspectMask = 0;
		blit.dstSubresource.aspectMask = 0;
		if (step.blit.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (step.blit.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
			blit.srcSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			blit.dstSubresource.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		vkCmdBlitImage(cmd, src->depth.image, src->depth.layout, dst->depth.image, dst->depth.layout, 1, &blit, step.blit.filter);
	}
}

void VulkanQueueRunner::SetupTransitionToTransferSrc(VKRImage &img, VkImageAspectFlags aspect, VulkanBarrier *recordBarrier) {
	if (img.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		return;
	}
	VkImageAspectFlags imageAspect = aspect;
	VkAccessFlags srcAccessMask = 0;
	VkPipelineStageFlags srcStageMask = 0;
	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		break;
	default:
		_dbg_assert_msg_(false, "Transition from this layout to transfer src not supported (%d)", (int)img.layout);
		break;
	}

	if (img.format == VK_FORMAT_D16_UNORM_S8_UINT || img.format == VK_FORMAT_D24_UNORM_S8_UINT || img.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
		// Barrier must specify both for combined depth/stencil buffers.
		imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	} else {
		imageAspect = aspect;
	}

	recordBarrier->TransitionImage(
		img.image,
		0,
		1,
		img.numLayers,
		imageAspect,
		img.layout,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		srcAccessMask,
		VK_ACCESS_TRANSFER_READ_BIT,
		srcStageMask,
		VK_PIPELINE_STAGE_TRANSFER_BIT
	);
	img.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

void VulkanQueueRunner::SetupTransitionToTransferDst(VKRImage &img, VkImageAspectFlags aspect, VulkanBarrier *recordBarrier) {
	if (img.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		return;
	}
	VkImageAspectFlags imageAspect = aspect;
	VkAccessFlags srcAccessMask = 0;
	VkPipelineStageFlags srcStageMask = 0;
	if (img.format == VK_FORMAT_D16_UNORM_S8_UINT || img.format == VK_FORMAT_D24_UNORM_S8_UINT || img.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
		// Barrier must specify both for combined depth/stencil buffers.
		imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	} else {
		imageAspect = aspect;
	}

	switch (img.layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		break;
	default:
		_dbg_assert_msg_(false, "Transition from this layout to transfer dst not supported (%d)", (int)img.layout);
		break;
	}

	recordBarrier->TransitionImage(
		img.image,
		0,
		1,
		img.numLayers,
		aspect,
		img.layout,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		srcAccessMask,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		srcStageMask,
		VK_PIPELINE_STAGE_TRANSFER_BIT
	);

	img.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

void VulkanQueueRunner::SetupTransferDstWriteAfterWrite(VKRImage &img, VkImageAspectFlags aspect, VulkanBarrier *recordBarrier) {
	VkImageAspectFlags imageAspect = aspect;
	VkAccessFlags srcAccessMask = 0;
	VkPipelineStageFlags srcStageMask = 0;
	if (img.format == VK_FORMAT_D16_UNORM_S8_UINT || img.format == VK_FORMAT_D24_UNORM_S8_UINT || img.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
		// Barrier must specify both for combined depth/stencil buffers.
		imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	} else {
		imageAspect = aspect;
	}
	_dbg_assert_(img.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	recordBarrier->TransitionImage(
		img.image,
		0,
		1,
		img.numLayers,
		aspect,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT
	);
}

void VulkanQueueRunner::ResizeReadbackBuffer(CachedReadback *readback, VkDeviceSize requiredSize) {
	if (readback->buffer && requiredSize <= readback->bufferSize) {
		return;
	}

	if (readback->buffer) {
		vulkan_->Delete().QueueDeleteBufferAllocation(readback->buffer, readback->allocation);
	}

	readback->bufferSize = requiredSize;

	VkDevice device = vulkan_->GetDevice();

	VkBufferCreateInfo buf{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	buf.size = readback->bufferSize;
	buf.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
	VmaAllocationInfo allocInfo{};

	VkResult res = vmaCreateBuffer(vulkan_->Allocator(), &buf, &allocCreateInfo, &readback->buffer, &readback->allocation, &allocInfo);
	_assert_(res == VK_SUCCESS);

	const VkMemoryType &memoryType = vulkan_->GetMemoryProperties().memoryTypes[allocInfo.memoryType];
	readback->isCoherent = (memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}

void VulkanQueueRunner::PerformReadback(const VKRStep &step, VkCommandBuffer cmd, FrameData &frameData) {
	VkImage image;
	VkImageLayout copyLayout;
	// Special case for backbuffer readbacks.
	if (step.readback.src == nullptr) {
		// We only take screenshots after the main render pass (anything else would be stupid) so we need to transition out of PRESENT,
		// and then back into it.
		// Regarding layers, backbuffer currently only has one layer.
		TransitionImageLayout2(cmd, backbufferImage_, 0, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_READ_BIT);
		copyLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		image = backbufferImage_;
	} else {
		VKRImage *srcImage;
		if (step.readback.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
			srcImage = &step.readback.src->color;
		} else if (step.readback.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
			srcImage = &step.readback.src->depth;
			_dbg_assert_(srcImage->image != VK_NULL_HANDLE);
		} else {
			_dbg_assert_msg_(false, "No image aspect to readback?");
			return;
		}

		if (srcImage->layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			SetupTransitionToTransferSrc(*srcImage, step.readback.aspectMask, &recordBarrier_);
			recordBarrier_.Flush(cmd);
		}
		image = srcImage->image;
		copyLayout = srcImage->layout;
	}

	// TODO: Handle different readback formats!
	u32 readbackSizeInBytes = sizeof(uint32_t) * step.readback.srcRect.extent.width * step.readback.srcRect.extent.height;

	CachedReadback *cached = nullptr;

	if (step.readback.delayed) {
		ReadbackKey key;
		key.framebuf = step.readback.src;
		key.width = step.readback.srcRect.extent.width;
		key.height = step.readback.srcRect.extent.height;

		// See if there's already a buffer we can reuse
		if (!frameData.readbacks_.Get(key, &cached)) {
			cached = new CachedReadback();
			cached->bufferSize = 0;
			frameData.readbacks_.Insert(key, cached);
		}
	} else {
		cached = &syncReadback_;
	}

	ResizeReadbackBuffer(cached, readbackSizeInBytes);

	VkBufferImageCopy region{};
	region.imageOffset = { step.readback.srcRect.offset.x, step.readback.srcRect.offset.y, 0 };
	region.imageExtent = { step.readback.srcRect.extent.width, step.readback.srcRect.extent.height, 1 };
	region.imageSubresource.aspectMask = step.readback.aspectMask;
	region.imageSubresource.layerCount = 1;
	region.bufferOffset = 0;
	region.bufferRowLength = step.readback.srcRect.extent.width;
	region.bufferImageHeight = step.readback.srcRect.extent.height;

	vkCmdCopyImageToBuffer(cmd, image, copyLayout, cached->buffer, 1, &region);

	// NOTE: Can't read the buffer using the CPU here - need to sync first.

	// If we copied from the backbuffer, transition it back.
	if (step.readback.src == nullptr) {
		// We only take screenshots after the main render pass (anything else would be stupid) so we need to transition out of PRESENT,
		// and then back into it.
		// Regarding layers, backbuffer currently only has one layer.
		TransitionImageLayout2(cmd, backbufferImage_, 0, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, 0);
		copyLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	}
}

void VulkanQueueRunner::PerformReadbackImage(const VKRStep &step, VkCommandBuffer cmd) {
	// TODO: Clean this up - just reusing `SetupTransitionToTransferSrc`.
	VKRImage srcImage{};
	srcImage.image = step.readback_image.image;
	srcImage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	srcImage.numLayers = 1;

	SetupTransitionToTransferSrc(srcImage, VK_IMAGE_ASPECT_COLOR_BIT, &recordBarrier_);
	recordBarrier_.Flush(cmd);

	ResizeReadbackBuffer(&syncReadback_, sizeof(uint32_t) * step.readback_image.srcRect.extent.width * step.readback_image.srcRect.extent.height);

	VkBufferImageCopy region{};
	region.imageOffset = { step.readback_image.srcRect.offset.x, step.readback_image.srcRect.offset.y, 0 };
	region.imageExtent = { step.readback_image.srcRect.extent.width, step.readback_image.srcRect.extent.height, 1 };
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = step.readback_image.mipLevel;
	region.bufferOffset = 0;
	region.bufferRowLength = step.readback_image.srcRect.extent.width;
	region.bufferImageHeight = step.readback_image.srcRect.extent.height;
	vkCmdCopyImageToBuffer(cmd, step.readback_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, syncReadback_.buffer, 1, &region);

	// Now transfer it back to a texture.
	TransitionImageLayout2(cmd, step.readback_image.image, 0, 1, 1,  // I don't think we have any multilayer cases for regular textures. Above in PerformReadback, though..
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

	// NOTE: Can't read the buffer using the CPU here - need to sync first.
	// Doing that will also act like a heavyweight barrier ensuring that device writes are visible on the host.
}

bool VulkanQueueRunner::CopyReadbackBuffer(FrameData &frameData, VKRFramebuffer *src, int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels) {
	CachedReadback *readback = &syncReadback_;

	// Look up in readback cache.
	if (src) {
		ReadbackKey key;
		key.framebuf = src;
		key.width = width;
		key.height = height;
		CachedReadback *cached;
		if (frameData.readbacks_.Get(key, &cached)) {
			readback = cached;
		} else {
			// Didn't have a cached image ready yet
			return false;
		}
	}

	if (!readback->buffer)
		return false;  // Didn't find anything in cache, or something has gone really wrong.

	// Read back to the requested address in ram from buffer.
	void *mappedData;
	const size_t srcPixelSize = DataFormatSizeInBytes(srcFormat);
	VkResult res = vmaMapMemory(vulkan_->Allocator(), readback->allocation, &mappedData);

	if (res != VK_SUCCESS) {
		ERROR_LOG(G3D, "CopyReadbackBuffer: vkMapMemory failed! result=%d", (int)res);
		return false;
	}

	if (!readback->isCoherent) {
		vmaInvalidateAllocation(vulkan_->Allocator(), readback->allocation, 0, width * height * srcPixelSize);
	}

	// TODO: Perform these conversions in a compute shader on the GPU.
	if (srcFormat == Draw::DataFormat::R8G8B8A8_UNORM) {
		ConvertFromRGBA8888(pixels, (const uint8_t *)mappedData, pixelStride, width, width, height, destFormat);
	} else if (srcFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
		ConvertFromBGRA8888(pixels, (const uint8_t *)mappedData, pixelStride, width, width, height, destFormat);
	} else if (srcFormat == destFormat) {
		// Can just memcpy when it matches no matter the format!
		uint8_t *dst = pixels;
		const uint8_t *src = (const uint8_t *)mappedData;
		for (int y = 0; y < height; ++y) {
			memcpy(dst, src, width * srcPixelSize);
			src += width * srcPixelSize;
			dst += pixelStride * srcPixelSize;
		}
	} else if (destFormat == Draw::DataFormat::D32F) {
		ConvertToD32F(pixels, (const uint8_t *)mappedData, pixelStride, width, width, height, srcFormat);
	} else if (destFormat == Draw::DataFormat::D16) {
		ConvertToD16(pixels, (const uint8_t *)mappedData, pixelStride, width, width, height, srcFormat);
	} else {
		// TODO: Maybe a depth conversion or something?
		ERROR_LOG(G3D, "CopyReadbackBuffer: Unknown format");
		_assert_msg_(false, "CopyReadbackBuffer: Unknown src format %d", (int)srcFormat);
	}

	vmaUnmapMemory(vulkan_->Allocator(), readback->allocation);
	return true;
}

const char *VKRRenderCommandToString(VKRRenderCommand cmd) {
	const char * const str[] = {
		"REMOVED",
		"BIND_GRAPHICS_PIPELINE",  // async
		"STENCIL",
		"BLEND",
		"VIEWPORT",
		"SCISSOR",
		"CLEAR",
		"DRAW",
		"DRAW_INDEXED",
		"PUSH_CONSTANTS",
		"DEBUG_ANNOTATION",
	};
	if ((int)cmd < ARRAY_SIZE(str)) {
		return str[(int)cmd];
	} else {
		return "N/A";
	}
}
