#include "Common/StringUtils.h"
#include "Common/GPU/Vulkan/VulkanFramebuffer.h"
#include "Common/GPU/Vulkan/VulkanQueueRunner.h"

static const char * const rpTypeDebugNames[] = {
	"RENDER",
	"RENDER_DEPTH",
	"MV_RENDER",
	"MV_RENDER_DEPTH",
	"MS_RENDER",
	"MS_RENDER_DEPTH",
	"MS_MV_RENDER",
	"MS_MV_RENDER_DEPTH",
	"BACKBUF",
};

const char *GetRPTypeName(RenderPassType rpType) {
	uint32_t index = (uint32_t)rpType;
	if (index < ARRAY_SIZE(rpTypeDebugNames)) {
		return rpTypeDebugNames[index];
	} else {
		return "N/A";
	}
}

VkSampleCountFlagBits MultiSampleLevelToFlagBits(int count) {
	// TODO: Check hardware support here, or elsewhere?
	// Some hardware only supports 4x.
	switch (count) {
	case 0: return VK_SAMPLE_COUNT_1_BIT;
	case 1: return VK_SAMPLE_COUNT_2_BIT;
	case 2: return VK_SAMPLE_COUNT_4_BIT;  // The only non-1 level supported on some mobile chips.
	case 3: return VK_SAMPLE_COUNT_8_BIT;
	case 4: return VK_SAMPLE_COUNT_16_BIT; // rare but exists, on Intel for example
	default:
		_assert_(false);
		return VK_SAMPLE_COUNT_1_BIT;
	}
}

void VKRImage::Delete(VulkanContext *vulkan) {
	// Get rid of the views first, feels cleaner (but in reality doesn't matter).
	if (rtView)
		vulkan->Delete().QueueDeleteImageView(rtView);
	if (texAllLayersView)
		vulkan->Delete().QueueDeleteImageView(texAllLayersView);
	for (int i = 0; i < 2; i++) {
		if (texLayerViews[i]) {
			vulkan->Delete().QueueDeleteImageView(texLayerViews[i]);
		}
	}

	if (image) {
		_dbg_assert_(alloc);
		vulkan->Delete().QueueDeleteImageAllocation(image, alloc);
	}
}

VKRFramebuffer::VKRFramebuffer(VulkanContext *vk, VulkanBarrierBatch *barriers, VkCommandBuffer initCmd, VKRRenderPass *compatibleRenderPass, int _width, int _height, int _numLayers, int _multiSampleLevel, bool createDepthStencilBuffer, const char *tag)
	: vulkan_(vk), tag_(tag), width(_width), height(_height), numLayers(_numLayers) {

	_dbg_assert_(tag);

	CreateImage(vulkan_, barriers, initCmd, color, width, height, numLayers, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, tag);
	if (createDepthStencilBuffer) {
		CreateImage(vulkan_, barriers, initCmd, depth, width, height, numLayers, VK_SAMPLE_COUNT_1_BIT, vulkan_->GetDeviceInfo().preferredDepthStencilFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false, tag);
	}

	if (_multiSampleLevel > 0) {
		sampleCount = MultiSampleLevelToFlagBits(_multiSampleLevel);

		// TODO: Create a different tag for these?
		CreateImage(vulkan_, barriers, initCmd, msaaColor, width, height, numLayers, sampleCount, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, tag);
		if (createDepthStencilBuffer) {
			CreateImage(vulkan_, barriers, initCmd, msaaDepth, width, height, numLayers, sampleCount, vulkan_->GetDeviceInfo().preferredDepthStencilFormat, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false, tag);
		}
	} else {
		sampleCount = VK_SAMPLE_COUNT_1_BIT;
	}

	UpdateTag(tag);

	// We create the actual framebuffer objects on demand, because some combinations might not make sense.
	// Framebuffer objects are just pointers to a set of images, so no biggie.
}

void VKRFramebuffer::UpdateTag(const char *newTag) {
	char name[128];
	snprintf(name, sizeof(name), "fb_color_%s", tag_.c_str());
	vulkan_->SetDebugName(color.image, VK_OBJECT_TYPE_IMAGE, name);
	vulkan_->SetDebugName(color.rtView, VK_OBJECT_TYPE_IMAGE_VIEW, name);
	if (depth.image) {
		snprintf(name, sizeof(name), "fb_depth_%s", tag_.c_str());
		vulkan_->SetDebugName(depth.image, VK_OBJECT_TYPE_IMAGE, name);
		vulkan_->SetDebugName(depth.rtView, VK_OBJECT_TYPE_IMAGE_VIEW, name);
	}
	for (size_t rpType = 0; rpType < (size_t)RenderPassType::TYPE_COUNT; rpType++) {
		if (framebuf[rpType]) {
			snprintf(name, sizeof(name), "fb_%s", tag_.c_str());
			vulkan_->SetDebugName(framebuf[(int)rpType], VK_OBJECT_TYPE_FRAMEBUFFER, name);
		}
	}
}

VkFramebuffer VKRFramebuffer::Get(VKRRenderPass *compatibleRenderPass, RenderPassType rpType) {
	bool multiview = RenderPassTypeHasMultiView(rpType);

	if (framebuf[(int)rpType]) {
		return framebuf[(int)rpType];
	}

	VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	VkImageView views[4]{};

	bool hasDepth = RenderPassTypeHasDepth(rpType);
	int attachmentCount = 0;
	views[attachmentCount++] = color.rtView;  // 2D array texture if multilayered.
	if (hasDepth) {
		if (!depth.rtView) {
			WARN_LOG(Log::G3D, "depth render type to non-depth fb: %p %p fmt=%d (%s %dx%d)", (void *)depth.image, (void *)depth.texAllLayersView, depth.format, tag_.c_str(), width, height);
			// Will probably crash, depending on driver.
		}
		views[attachmentCount++] = depth.rtView;
	}
	if (rpType & RenderPassType::MULTISAMPLE) {
		views[attachmentCount++] = msaaColor.rtView;
		if (hasDepth) {
			views[attachmentCount++] = msaaDepth.rtView;
		}
	}

	fbci.renderPass = compatibleRenderPass->Get(vulkan_, rpType, sampleCount);
	fbci.attachmentCount = attachmentCount;
	fbci.pAttachments = views;
	fbci.width = width;
	fbci.height = height;
	fbci.layers = 1;  // With multiview, this should be set as 1.

	VkResult res = vkCreateFramebuffer(vulkan_->GetDevice(), &fbci, nullptr, &framebuf[(int)rpType]);
	_assert_(res == VK_SUCCESS);

	if (!tag_.empty() && vulkan_->Extensions().EXT_debug_utils) {
		vulkan_->SetDebugName(framebuf[(int)rpType], VK_OBJECT_TYPE_FRAMEBUFFER, StringFromFormat("fb_%s", tag_.c_str()).c_str());
	}

	return framebuf[(int)rpType];
}

VKRFramebuffer::~VKRFramebuffer() {
	color.Delete(vulkan_);
	depth.Delete(vulkan_);
	msaaColor.Delete(vulkan_);
	msaaDepth.Delete(vulkan_);

	for (auto &fb : framebuf) {
		if (fb) {
			vulkan_->Delete().QueueDeleteFramebuffer(fb);
		}
	}
}

// NOTE: If numLayers > 1, it will create an array texture, rather than a normal 2D texture.
// This requires a different sampling path!
void VKRFramebuffer::CreateImage(VulkanContext *vulkan, VulkanBarrierBatch *barriers, VkCommandBuffer cmd, VKRImage &img, int width, int height, int numLayers, VkSampleCountFlagBits sampleCount, VkFormat format, VkImageLayout initialLayout, bool color, const char *tag) {
	// We don't support more exotic layer setups for now. Mono or stereo.
	_dbg_assert_(numLayers == 1 || numLayers == 2);

	VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.arrayLayers = numLayers;
	ici.mipLevels = 1;
	ici.extent.width = width;
	ici.extent.height = height;
	ici.extent.depth = 1;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.samples = sampleCount;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.format = format;
	ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (sampleCount == VK_SAMPLE_COUNT_1_BIT) {
		ici.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if (color) {
		ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	} else {
		ici.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	VmaAllocationInfo allocInfo{};

	VkResult res = vmaCreateImage(vulkan->Allocator(), &ici, &allocCreateInfo, &img.image, &img.alloc, &allocInfo);
	_dbg_assert_(res == VK_SUCCESS);

	vulkan->SetDebugName(img.image, VK_OBJECT_TYPE_IMAGE, tag);

	VkImageAspectFlags aspects = color ? VK_IMAGE_ASPECT_COLOR_BIT : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

	VkImageViewCreateInfo ivci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	ivci.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
	ivci.format = ici.format;
	ivci.image = img.image;
	ivci.viewType = numLayers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	ivci.subresourceRange.aspectMask = aspects;
	ivci.subresourceRange.layerCount = numLayers;
	ivci.subresourceRange.levelCount = 1;
	res = vkCreateImageView(vulkan->GetDevice(), &ivci, nullptr, &img.rtView);
	vulkan->SetDebugName(img.rtView, VK_OBJECT_TYPE_IMAGE_VIEW, tag);
	_dbg_assert_(res == VK_SUCCESS);

	// Separate view for texture sampling all layers together.
	if (!color) {
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	ivci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;  // layered for consistency, even if single image.
	res = vkCreateImageView(vulkan->GetDevice(), &ivci, nullptr, &img.texAllLayersView);
	vulkan->SetDebugName(img.texAllLayersView, VK_OBJECT_TYPE_IMAGE_VIEW, tag);

	// Create 2D views for both layers.
	// Useful when multipassing shaders that don't yet exist in a single-pass-stereo version.
	for (int i = 0; i < numLayers; i++) {
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.subresourceRange.layerCount = 1;
		ivci.subresourceRange.baseArrayLayer = i;
		res = vkCreateImageView(vulkan->GetDevice(), &ivci, nullptr, &img.texLayerViews[i]);
		if (vulkan->DebugLayerEnabled()) {
			char temp[128];
			snprintf(temp, sizeof(temp), "%s_layer%d", tag, i);
			vulkan->SetDebugName(img.texLayerViews[i], VK_OBJECT_TYPE_IMAGE_VIEW, temp);
		}
		_dbg_assert_(res == VK_SUCCESS);
	}

	VkPipelineStageFlags dstStage;
	VkAccessFlagBits dstAccessMask;
	switch (initialLayout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	default:
		Crash();
		return;
	}

	VkImageMemoryBarrier *barrier = barriers->Add(img.image, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dstStage, 0);
	barrier->subresourceRange.layerCount = numLayers;
	barrier->subresourceRange.aspectMask = aspects;
	barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier->newLayout = initialLayout;
	barrier->srcAccessMask = 0;
	barrier->dstAccessMask = dstAccessMask;

	img.layout = initialLayout;
	img.format = format;
	img.sampleCount = sampleCount;
	img.tag = tag ? tag : "N/A";
	img.numLayers = numLayers;
}

static VkAttachmentLoadOp ConvertLoadAction(VKRRenderPassLoadAction action) {
	switch (action) {
	case VKRRenderPassLoadAction::CLEAR:     return VK_ATTACHMENT_LOAD_OP_CLEAR;
	case VKRRenderPassLoadAction::KEEP:      return VK_ATTACHMENT_LOAD_OP_LOAD;
	case VKRRenderPassLoadAction::DONT_CARE: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}
	return VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // avoid compiler warning
}

static VkAttachmentStoreOp ConvertStoreAction(VKRRenderPassStoreAction action) {
	switch (action) {
	case VKRRenderPassStoreAction::STORE:     return VK_ATTACHMENT_STORE_OP_STORE;
	case VKRRenderPassStoreAction::DONT_CARE: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	return VK_ATTACHMENT_STORE_OP_DONT_CARE;  // avoid compiler warning
}

// Self-dependency: https://github.com/gpuweb/gpuweb/issues/442#issuecomment-547604827
// Also see https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#synchronization-pipeline-barriers-subpass-self-dependencies

VkRenderPass CreateRenderPass(VulkanContext *vulkan, const RPKey &key, RenderPassType rpType, VkSampleCountFlagBits sampleCount) {
	bool isBackbuffer = rpType == RenderPassType::BACKBUFFER;
	bool hasDepth = RenderPassTypeHasDepth(rpType);
	bool multiview = RenderPassTypeHasMultiView(rpType);
	bool multisample = RenderPassTypeHasMultisample(rpType);

	_dbg_assert_(!(isBackbuffer && multisample));

	if (isBackbuffer) {
		_dbg_assert_(key.depthLoadAction != VKRRenderPassLoadAction::KEEP);
	}

	if (multiview) {
		// TODO: Assert that the device has multiview support enabled.
	}

	int colorAttachmentIndex = 0;
	int depthAttachmentIndex = 1;

	int attachmentCount = 0;
	VkAttachmentDescription attachments[4]{};
	attachments[attachmentCount].format = isBackbuffer ? vulkan->GetSwapchainFormat() : VK_FORMAT_R8G8B8A8_UNORM;
	attachments[attachmentCount].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[attachmentCount].loadOp = multisample ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : ConvertLoadAction(key.colorLoadAction);
	attachments[attachmentCount].storeOp = ConvertStoreAction(key.colorStoreAction);
	attachments[attachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[attachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[attachmentCount].initialLayout = isBackbuffer ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[attachmentCount].finalLayout = isBackbuffer ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachmentCount++;

	if (hasDepth) {
		attachments[attachmentCount].format = vulkan->GetDeviceInfo().preferredDepthStencilFormat;
		attachments[attachmentCount].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[attachmentCount].loadOp = multisample ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : ConvertLoadAction(key.depthLoadAction);
		attachments[attachmentCount].storeOp = ConvertStoreAction(key.depthStoreAction);
		attachments[attachmentCount].stencilLoadOp = multisample ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : ConvertLoadAction(key.stencilLoadAction);
		attachments[attachmentCount].stencilStoreOp = ConvertStoreAction(key.stencilStoreAction);
		attachments[attachmentCount].initialLayout = isBackbuffer ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[attachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachmentCount++;
	}

	if (multisample) {
		colorAttachmentIndex = attachmentCount;
		attachments[attachmentCount].format = isBackbuffer ? vulkan->GetSwapchainFormat() : VK_FORMAT_R8G8B8A8_UNORM;
		attachments[attachmentCount].samples = sampleCount;
		attachments[attachmentCount].loadOp = ConvertLoadAction(key.colorLoadAction);
		attachments[attachmentCount].storeOp = ConvertStoreAction(key.colorStoreAction);
		attachments[attachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[attachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[attachmentCount].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments[attachmentCount].finalLayout = isBackbuffer ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachmentCount++;

		if (hasDepth) {
			depthAttachmentIndex = attachmentCount;
			attachments[attachmentCount].format = vulkan->GetDeviceInfo().preferredDepthStencilFormat;
			attachments[attachmentCount].samples = sampleCount;
			attachments[attachmentCount].loadOp = ConvertLoadAction(key.depthLoadAction);
			attachments[attachmentCount].storeOp = ConvertStoreAction(key.depthStoreAction);
			attachments[attachmentCount].stencilLoadOp = ConvertLoadAction(key.stencilLoadAction);
			attachments[attachmentCount].stencilStoreOp = ConvertStoreAction(key.stencilStoreAction);
			attachments[attachmentCount].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachments[attachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachmentCount++;
		}
	}

	VkAttachmentReference colorReference{};
	colorReference.attachment = colorAttachmentIndex;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference{};
	depthReference.attachment = depthAttachmentIndex;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorReference;

	VkAttachmentReference colorResolveReference;
	if (multisample) {
		colorResolveReference.attachment = 0;  // the non-msaa color buffer.
		colorResolveReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		subpass.pResolveAttachments = &colorResolveReference;
	} else {
		subpass.pResolveAttachments = nullptr;
	}
	if (hasDepth) {
		subpass.pDepthStencilAttachment = &depthReference;
	}
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = nullptr;

	// Not sure if this is really necessary.
	VkSubpassDependency deps[2]{};
	size_t numDeps = 0;

	VkRenderPassCreateInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp.attachmentCount = attachmentCount;
	rp.pAttachments = attachments;
	rp.subpassCount = 1;
	rp.pSubpasses = &subpass;

	VkRenderPassMultiviewCreateInfoKHR mv{ VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO_KHR };
	uint32_t viewMask = 0x3;  // Must be outside the 'if (multiview)' scope!
	int viewOffset = 0;
	if (multiview) {
		rp.pNext = &mv;
		mv.subpassCount = 1;
		mv.pViewMasks = &viewMask;
		mv.dependencyCount = 0;
		mv.pCorrelationMasks = &viewMask; // same masks
		mv.correlationMaskCount = 1;
		mv.pViewOffsets = &viewOffset;
	}

	if (isBackbuffer) {
		// We don't specify any explicit transitions for these, so let's use subpass dependencies.
		// This makes sure that writes to the depth image are done before we try to write to it again.
		// From Sascha's examples.
		deps[numDeps].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[numDeps].dstSubpass = 0;
		deps[numDeps].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		deps[numDeps].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		deps[numDeps].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[numDeps].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[numDeps].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		numDeps++;
		// Dependencies for the color image.
		deps[numDeps].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[numDeps].dstSubpass = 0;
		deps[numDeps].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		deps[numDeps].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[numDeps].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		deps[numDeps].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[numDeps].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		numDeps++;
	}

	if (numDeps > 0) {
		rp.dependencyCount = (u32)numDeps;
		rp.pDependencies = deps;
	}

	VkRenderPass pass;
	VkResult res;

	// We could always use renderpass2, but I think it'll get both paths better tested if we
	// only use it with multisample enabled.
	// if (vulkan->Extensions().KHR_create_renderpass2) {
	if (multisample) {
		// It's a bit unfortunate that we can't rely on vkCreateRenderPass2, because here we now have
		// to do a bunch of struct conversion, just to not have to repeat the logic from above.
		VkAttachmentDescription2KHR attachments2[4]{};
		for (int i = 0; i < attachmentCount; i++) {
			attachments2[i].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2_KHR;
			attachments2[i].format = attachments[i].format;
			attachments2[i].samples = attachments[i].samples;
			attachments2[i].loadOp = attachments[i].loadOp;
			attachments2[i].storeOp = attachments[i].storeOp;
			attachments2[i].stencilLoadOp = attachments[i].stencilLoadOp;
			attachments2[i].stencilStoreOp = attachments[i].stencilStoreOp;
			attachments2[i].initialLayout = attachments[i].initialLayout;
			attachments2[i].finalLayout = attachments[i].finalLayout;
		}

		VkAttachmentReference2KHR colorReference2{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2_KHR };
		colorReference2.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorReference2.attachment = colorReference.attachment;
		colorReference2.layout = colorReference.layout;

		VkAttachmentReference2KHR depthReference2{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2_KHR };
		depthReference2.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		depthReference2.attachment = depthReference.attachment;
		depthReference2.layout = depthReference.layout;

		VkSubpassDependency2KHR deps2[2]{};
		for (int i = 0; i < numDeps; i++) {
			deps2[i].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2_KHR;
			deps2[i].dependencyFlags = deps[i].dependencyFlags;
			deps2[i].srcAccessMask = deps[i].srcAccessMask;
			deps2[i].dstAccessMask = deps[i].dstAccessMask;
			deps2[i].srcStageMask = deps[i].srcStageMask;
			deps2[i].dstStageMask = deps[i].dstStageMask;
			deps2[i].srcSubpass = deps[i].srcSubpass;
			deps2[i].dstSubpass = deps[i].dstSubpass;
			deps2[i].dependencyFlags = deps[i].dependencyFlags;
			deps2[i].viewOffset = 0;
		}

		VkAttachmentReference2KHR colorResolveReference2{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2_KHR };

		VkSubpassDescription2KHR subpass2{ VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2_KHR };
		subpass2.colorAttachmentCount = subpass.colorAttachmentCount;
		subpass2.flags = subpass.flags;
		subpass2.pColorAttachments = &colorReference2;
		if (hasDepth) {
			subpass2.pDepthStencilAttachment = &depthReference2;
		}
		subpass2.pipelineBindPoint = subpass.pipelineBindPoint;
		subpass2.viewMask = multiview ? viewMask : 0;
		if (multisample) {
			colorResolveReference2.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colorResolveReference2.attachment = colorResolveReference.attachment;  // the non-msaa color buffer.
			colorResolveReference2.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			subpass2.pResolveAttachments = &colorResolveReference2;
		} else {
			subpass2.pResolveAttachments = nullptr;
		}

		VkAttachmentReference2KHR depthResolveReference2{ VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2_KHR };
		VkSubpassDescriptionDepthStencilResolveKHR depthStencilResolve{ VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE_KHR };
		if (hasDepth && multisample) {
			ChainStruct(subpass2, &depthStencilResolve);
			depthResolveReference2.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			depthResolveReference2.attachment = 1;
			depthResolveReference2.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			// TODO: Some games might benefit from the other depth resolve modes when depth texturing.
			depthStencilResolve.depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR;
			depthStencilResolve.stencilResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR;
			depthStencilResolve.pDepthStencilResolveAttachment = &depthResolveReference2;
		}

		VkRenderPassCreateInfo2KHR rp2{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2_KHR };
		rp2.pAttachments = attachments2;
		rp2.pDependencies = deps2;
		rp2.attachmentCount = rp.attachmentCount;
		rp2.dependencyCount = rp.dependencyCount;
		rp2.correlatedViewMaskCount = multiview ? 1 : 0;
		rp2.pCorrelatedViewMasks = multiview ? &viewMask : nullptr;
		rp2.pSubpasses = &subpass2;
		rp2.subpassCount = 1;
		res = vkCreateRenderPass2(vulkan->GetDevice(), &rp2, nullptr, &pass);
	} else {
		res = vkCreateRenderPass(vulkan->GetDevice(), &rp, nullptr, &pass);
	}

	if (pass) {
		vulkan->SetDebugName(pass, VK_OBJECT_TYPE_RENDER_PASS, GetRPTypeName(rpType));
	}

	_assert_(res == VK_SUCCESS);
	_assert_(pass != VK_NULL_HANDLE);
	return pass;
}

VkRenderPass VKRRenderPass::Get(VulkanContext *vulkan, RenderPassType rpType, VkSampleCountFlagBits sampleCount) {
	// When we create a render pass, we create all "types" of it immediately,
	// practical later when referring to it. Could change to on-demand if it feels motivated
	// but I think the render pass objects are cheap.

	// WARNING: We don't include sampleCount in the key, there's only the distinction multisampled or not
	// which comes from the rpType.
	// So you CAN NOT mix and match different non-one sample counts.

	_dbg_assert_(!((rpType & RenderPassType::MULTISAMPLE) && sampleCount == VK_SAMPLE_COUNT_1_BIT));

	if (!pass[(int)rpType] || sampleCounts[(int)rpType] != sampleCount) {
		if (pass[(int)rpType]) {
			vulkan->Delete().QueueDeleteRenderPass(pass[(int)rpType]);
		}
		pass[(int)rpType] = CreateRenderPass(vulkan, key_, (RenderPassType)rpType, sampleCount);
		sampleCounts[(int)rpType] = sampleCount;
	}
	return pass[(int)rpType];
}
