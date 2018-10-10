#include "Common/Vulkan/VulkanImage.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "Common/Log.h"

void VulkanTexture::Wipe() {
	if (image_) {
		vulkan_->Delete().QueueDeleteImage(image_);
	}
	if (view_) {
		vulkan_->Delete().QueueDeleteImageView(view_);
	}
	if (mem_ && !allocator_) {
		vulkan_->Delete().QueueDeleteDeviceMemory(mem_);
	} else if (mem_) {
		allocator_->Free(mem_, offset_);
		mem_ = VK_NULL_HANDLE;
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

bool VulkanTexture::CreateDirect(VkCommandBuffer cmd, int w, int h, int numMips, VkFormat format, VkImageLayout initialLayout, VkImageUsageFlags usage, const VkComponentMapping *mapping) {
	Wipe();

	width_ = w;
	height_ = h;
	numMips_ = numMips;
	format_ = format;

	VkImageAspectFlags aspect = IsDepthStencilFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageCreateInfo image_create_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = format_;
	image_create_info.extent.width = width_;
	image_create_info.extent.height = height_;
	image_create_info.extent.depth = 1;
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

	VkResult res = vkCreateImage(vulkan_->GetDevice(), &image_create_info, NULL, &image_);
	if (res != VK_SUCCESS) {
		_assert_(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
		return false;
	}

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(vulkan_->GetDevice(), image_, &mem_reqs);

	if (allocator_) {
		offset_ = allocator_->Allocate(mem_reqs, &mem_, Tag());
		if (offset_ == VulkanDeviceAllocator::ALLOCATE_FAILED) {
			return false;
		}
	} else {
		VkMemoryAllocateInfo mem_alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		mem_alloc.memoryTypeIndex = 0;
		mem_alloc.allocationSize = mem_reqs.size;

		// Find memory type - don't specify any mapping requirements
		bool pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
		_assert_(pass);

		res = vkAllocateMemory(vulkan_->GetDevice(), &mem_alloc, NULL, &mem_);
		if (res != VK_SUCCESS) {
			_assert_msg_(G3D, res != VK_ERROR_TOO_MANY_OBJECTS, "Too many Vulkan memory objects!");
			_assert_(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
			vkDestroyImage(vulkan_->GetDevice(), image_, nullptr);
			image_ = VK_NULL_HANDLE;
			return false;
		}

		offset_ = 0;
	}

	res = vkBindImageMemory(vulkan_->GetDevice(), image_, mem_, offset_);
	if (res != VK_SUCCESS) {
		// This leaks the image and memory. Should not really happen though...
		_assert_(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
		return false;
	}

	// Write a command to transition the image to the requested layout, if it's not already that layout.
	if (initialLayout != VK_IMAGE_LAYOUT_UNDEFINED && initialLayout != VK_IMAGE_LAYOUT_PREINITIALIZED) {
		switch (initialLayout) {
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			TransitionImageLayout2(cmd, image_, 0, numMips, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, VK_ACCESS_TRANSFER_WRITE_BIT);
			break;
		default:
			// If you planned to use UploadMip, you want VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL. After the
			// upload, you can transition using EndCreate.
			_assert_(false);
			break;
		}
	}

	// Create the view while we're at it.
	VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = image_;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format_;
	if (mapping) {
		view_info.components = *mapping;
	} else {
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		view_info.components.a = VK_COMPONENT_SWIZZLE_A;
	}
	view_info.subresourceRange.aspectMask = aspect;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = numMips;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &view_);
	if (res != VK_SUCCESS) {
		// This leaks the image.
		_assert_(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
		return false;
	}
	return true;
}

void VulkanTexture::UploadMip(VkCommandBuffer cmd, int mip, int mipWidth, int mipHeight, VkBuffer buffer, uint32_t offset, size_t rowLength) {
	VkBufferImageCopy copy_region{};
	copy_region.bufferOffset = offset;
	copy_region.bufferRowLength = (uint32_t)rowLength;
	copy_region.bufferImageHeight = 0;  // 2D
	copy_region.imageExtent.width = mipWidth;
	copy_region.imageExtent.height = mipHeight;
	copy_region.imageExtent.depth = 1;
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.mipLevel = mip;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;

	vkCmdCopyBufferToImage(cmd, buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
}

void VulkanTexture::GenerateMip(VkCommandBuffer cmd, int mip) {
	_assert_msg_(G3D, mip != 0, "Cannot generate the first level");
	_assert_msg_(G3D, mip < numMips_, "Cannot generate mipmaps past the maximum created (%d vs %d)", mip, numMips_);
	VkImageBlit blit{};
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.mipLevel = mip - 1;
	blit.srcOffsets[1].x = width_ >> (mip - 1);
	blit.srcOffsets[1].y = height_ >> (mip - 1);
	blit.srcOffsets[1].z = 1;

	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.mipLevel = mip;
	blit.dstOffsets[1].x = width_ >> mip;
	blit.dstOffsets[1].y = height_ >> mip;
	blit.dstOffsets[1].z = 1;

	TransitionImageLayout2(cmd, image_, mip - 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	// Low-quality mipmap generation, but works okay.
	vkCmdBlitImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

	TransitionImageLayout2(cmd, image_, mip - 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
}

void VulkanTexture::EndCreate(VkCommandBuffer cmd, bool vertexTexture) {
	TransitionImageLayout2(cmd, image_, 0, numMips_,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, vertexTexture ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void VulkanTexture::Touch() {
	if (allocator_ && mem_ != VK_NULL_HANDLE) {
		allocator_->Touch(mem_, offset_);
	}
}

void VulkanTexture::Destroy() {
	if (view_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteImageView(view_);
	}
	if (image_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteImage(image_);
	}
	if (mem_ != VK_NULL_HANDLE && !allocator_) {
		vulkan_->Delete().QueueDeleteDeviceMemory(mem_);
	} else if (mem_ != VK_NULL_HANDLE) {
		allocator_->Free(mem_, offset_);
		mem_ = VK_NULL_HANDLE;
	}
}
