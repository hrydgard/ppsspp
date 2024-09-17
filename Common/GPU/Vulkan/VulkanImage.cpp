#include <algorithm>

#include "Common/Log.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanAlloc.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/GPU/Vulkan/VulkanBarrier.h"
#include "Common/StringUtils.h"

using namespace PPSSPP_VK;

VulkanTexture::VulkanTexture(VulkanContext *vulkan, const char *tag)
	: vulkan_(vulkan) {
	truncate_cpy(tag_, tag);
}

void VulkanTexture::Wipe() {
	if (view_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteImageView(view_);
	}
	if (image_ != VK_NULL_HANDLE) {
		_dbg_assert_(allocation_ != VK_NULL_HANDLE);
		vulkan_->Delete().QueueDeleteImageAllocation(image_, allocation_);
	}
}

static bool IsDepthStencilFormat(VkFormat format) {
	switch (format) {
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return true;
	default:
		return false;
	}
}

bool VulkanTexture::CreateDirect(int w, int h, int depth, int numMips, VkFormat format, VkImageLayout initialLayout, VkImageUsageFlags usage, VulkanBarrierBatch *barrierBatch, const VkComponentMapping *mapping) {
	if (w == 0 || h == 0 || numMips == 0) {
		ERROR_LOG(Log::G3D, "Can't create a zero-size VulkanTexture");
		return false;
	}
	int maxDim = vulkan_->GetPhysicalDeviceProperties(0).properties.limits.maxImageDimension2D;
	if (w > maxDim || h > maxDim) {
		ERROR_LOG(Log::G3D, "Can't create a texture this large");
		return false;
	}

	Wipe();

	width_ = w;
	height_ = h;
	depth_ = depth;
	numMips_ = numMips;
	format_ = format;

	VkImageAspectFlags aspect = IsDepthStencilFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageCreateInfo image_create_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_create_info.imageType = depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
	image_create_info.format = format_;
	image_create_info.extent.width = width_;
	image_create_info.extent.height = height_;
	image_create_info.extent.depth = depth;
	image_create_info.mipLevels = numMips;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.flags = 0;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = usage;
	if (initialLayout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	} else {
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	// The graphics debugger always "needs" TRANSFER_SRC but in practice doesn't matter - 
	// unless validation is on. So let's only force it on when being validated, for now.
	if (vulkan_->GetFlags() & VULKAN_FLAG_VALIDATE) {
		image_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	VmaAllocationCreateInfo allocCreateInfo{};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	VmaAllocationInfo allocInfo{};
	VkResult res = vmaCreateImage(vulkan_->Allocator(), &image_create_info, &allocCreateInfo, &image_, &allocation_, &allocInfo);

	// Apply the tag
	vulkan_->SetDebugName(image_, VK_OBJECT_TYPE_IMAGE, tag_);

	// Write a command to transition the image to the requested layout, if it's not already that layout.
	// TODO: We may generate mipmaps right after, so can't add to the end of frame batch. Well actually depending
	// on the amount of mips we probably sometimes can..

	if (initialLayout != VK_IMAGE_LAYOUT_UNDEFINED && initialLayout != VK_IMAGE_LAYOUT_PREINITIALIZED) {
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkAccessFlagBits dstAccessFlags;
		switch (initialLayout) {
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstAccessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
			// We use this initial layout when we're about to write to the image using a compute shader, only.
			dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			dstAccessFlags = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			// If you planned to use UploadMip, you want VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL. After the
			// upload, you can transition using EndCreate.
			_assert_(false);
			break;
		}
		barrierBatch->TransitionImage(image_, 0, numMips, 1, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, initialLayout,
			0, dstAccessFlags,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dstStage);
	}

	// Create the view while we're at it.
	VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = image_;
	view_info.viewType = depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format_;
	if (mapping) {
		view_info.components = *mapping;
	} else {
		view_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
	}
	view_info.subresourceRange.aspectMask = aspect;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = numMips;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &view_);
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "vkCreateImageView failed: %s. Destroying image.", VulkanResultToString(res));
		_assert_msg_(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS, "%d", (int)res);
		vmaDestroyImage(vulkan_->Allocator(), image_, allocation_);
		view_ = VK_NULL_HANDLE;
		image_ = VK_NULL_HANDLE;
		allocation_ = VK_NULL_HANDLE;
		return false;
	}
	vulkan_->SetDebugName(view_, VK_OBJECT_TYPE_IMAGE_VIEW, tag_);

	// Additionally, create an array view, but only if it's a 2D texture.
	if (view_info.viewType == VK_IMAGE_VIEW_TYPE_2D) {
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &arrayView_);
		// Assume that if the above view creation succeeded, so will this.
		_assert_msg_(res == VK_SUCCESS, "View creation failed: %d", (int)res);
		vulkan_->SetDebugName(arrayView_, VK_OBJECT_TYPE_IMAGE_VIEW, tag_);
	}

	return true;
}

void VulkanTexture::CopyBufferToMipLevel(VkCommandBuffer cmd, TextureCopyBatch *copyBatch, int mip, int mipWidth, int mipHeight, int depthLayer, VkBuffer buffer, uint32_t offset, size_t rowLength) {
	VkBufferImageCopy copy_region{};
	copy_region.bufferOffset = offset;
	copy_region.bufferRowLength = (uint32_t)rowLength;
	copy_region.bufferImageHeight = 0;  // 2D
	copy_region.imageOffset.z = depthLayer;
	copy_region.imageExtent.width = mipWidth;
	copy_region.imageExtent.height = mipHeight;
	copy_region.imageExtent.depth = 1;
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.mipLevel = mip;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;

	_dbg_assert_(mip < numMips_);

	if (!copyBatch->buffer) {
		copyBatch->buffer = buffer;
	} else if (copyBatch->buffer != buffer) {
		// Need to flush the batch if this image isn't from the same buffer as the previous ones.
		FinishCopyBatch(cmd, copyBatch);
		copyBatch->buffer = buffer;
	}
	copyBatch->copies.push_back(copy_region);
}

void VulkanTexture::FinishCopyBatch(VkCommandBuffer cmd, TextureCopyBatch *copyBatch) {
	if (!copyBatch->copies.empty()) {
		vkCmdCopyBufferToImage(cmd, copyBatch->buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)copyBatch->copies.size(), copyBatch->copies.data());
		copyBatch->copies.clear();
	}
}

void VulkanTexture::ClearMip(VkCommandBuffer cmd, int mip, uint32_t value) {
	// Must be in TRANSFER_DST mode.
	VkClearColorValue clearVal;
	for (int i = 0; i < 4; i++) {
		clearVal.float32[i] = ((value >> (i * 8)) & 0xFF) / 255.0f;
	}
	VkImageSubresourceRange range{};
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.layerCount = 1;
	range.baseMipLevel = mip;
	range.levelCount = 1;
	vkCmdClearColorImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &range);
}

// Low-quality mipmap generation by bilinear blit, but works okay.
void VulkanTexture::GenerateMips(VkCommandBuffer cmd, int firstMipToGenerate, bool fromCompute) {
	_assert_msg_(firstMipToGenerate > 0, "Cannot generate the first level");
	_assert_msg_(firstMipToGenerate < numMips_, "Can't generate levels beyond storage");

	VulkanBarrierBatch batch;
	// Transition the pre-set levels to GENERAL.

	VkImageMemoryBarrier *barrier = batch.Add(image_,
		fromCompute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	barrier->oldLayout = fromCompute ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier->srcAccessMask = fromCompute ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier->dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier->subresourceRange.levelCount = firstMipToGenerate;

	barrier = batch.Add(image_,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	barrier->subresourceRange.baseMipLevel = firstMipToGenerate;
	barrier->subresourceRange.levelCount = numMips_ - firstMipToGenerate;
	barrier->oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier->srcAccessMask = 0;
	barrier->dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	batch.Flush(cmd);

	// Now we can blit and barrier the whole pipeline.
	for (int mip = firstMipToGenerate; mip < numMips_; mip++) {
		VkImageBlit blit{};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcSubresource.mipLevel = mip - 1;
		blit.srcOffsets[1].x = std::max(width_ >> (mip - 1), 1);
		blit.srcOffsets[1].y = std::max(height_ >> (mip - 1), 1);
		blit.srcOffsets[1].z = 1;

		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.layerCount = 1;
		blit.dstSubresource.mipLevel = mip;
		blit.dstOffsets[1].x = std::max(width_ >> mip, 1);
		blit.dstOffsets[1].y = std::max(height_ >> mip, 1);
		blit.dstOffsets[1].z = 1;

		// TODO: We could do better with the image transitions - would be enough with one per level
		// for the memory barrier, then one final one for the whole stack when done. This function
		// currently doesn't have a global enough view, though.
		// We should also coalesce barriers across multiple texture uploads in a frame and all kinds of other stuff, but...

		vkCmdBlitImage(cmd, image_, VK_IMAGE_LAYOUT_GENERAL, image_, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);

		barrier = batch.Add(image_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
		barrier->subresourceRange.baseMipLevel = mip;
		barrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier->srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier->dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		batch.Flush(cmd);
	}
}

void VulkanTexture::EndCreate(VkCommandBuffer cmd, bool vertexTexture, VkPipelineStageFlags prevStage, VkImageLayout layout) {
	VulkanBarrierBatch batch;
	VkImageMemoryBarrier *barrier = batch.Add(image_, prevStage, vertexTexture ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0);
	barrier->subresourceRange.levelCount = numMips_;
	barrier->oldLayout = layout;
	barrier->newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier->srcAccessMask = prevStage == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier->dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	batch.Flush(cmd);
}

void VulkanTexture::PrepareForTransferDst(VkCommandBuffer cmd, int levels) {
	VulkanBarrierBatch batch;
	VkImageMemoryBarrier *barrier = batch.Add(image_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
	barrier->subresourceRange.levelCount = levels;
	barrier->srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier->dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier->oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier->newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	batch.Flush(cmd);
}

void VulkanTexture::RestoreAfterTransferDst(int levels, VulkanBarrierBatch *barriers) {
	VkImageMemoryBarrier *barrier = barriers->Add(image_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0);
	barrier->subresourceRange.levelCount = levels;
	barrier->srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier->dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier->oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier->newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

VkImageView VulkanTexture::CreateViewForMip(int mip) {
	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = image_;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format_;
	view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = mip;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	VkImageView view;
	VkResult res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &view);
	vulkan_->SetDebugName(view, VK_OBJECT_TYPE_IMAGE_VIEW, "mipview");
	_assert_(res == VK_SUCCESS);
	return view;
}

void VulkanTexture::Destroy() {
	if (view_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteImageView(view_);
	}
	if (arrayView_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteImageView(arrayView_);
	}
	if (image_ != VK_NULL_HANDLE) {
		_dbg_assert_(allocation_ != VK_NULL_HANDLE);
		vulkan_->Delete().QueueDeleteImageAllocation(image_, allocation_);
	}
}
