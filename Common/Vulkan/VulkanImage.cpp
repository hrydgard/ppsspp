#include "Common/Vulkan/VulkanImage.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "Common/Log.h"

VkResult VulkanTexture::Create(int w, int h, VkFormat format) {
	tex_width = w;
	tex_height = h;
	format_ = format;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties(vulkan_->GetPhysicalDevice(), format, &formatProps);
	return VK_SUCCESS;
}

void VulkanTexture::CreateMappableImage() {
	// If we already have a mappableImage, forget it.
	if (mappableImage) {
		vulkan_->Delete().QueueDeleteImage(mappableImage);
	}
	if (mappableMemory) {
		vulkan_->Delete().QueueDeleteDeviceMemory(mappableMemory);
	}

	VkImageCreateInfo image_create_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = format_;
	image_create_info.extent.width = tex_width;
	image_create_info.extent.height = tex_height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_LINEAR;
	image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_create_info.queueFamilyIndexCount = 0;
	image_create_info.pQueueFamilyIndices = NULL;
	image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_create_info.flags = 0;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

	VkMemoryAllocateInfo mem_alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	mem_alloc.allocationSize = 0;
	mem_alloc.memoryTypeIndex = 0;

	// Create a mappable image.  It will be the texture if linear images are ok to be textures
	// or it will be the staging image if they are not.
	VkResult res = vkCreateImage(vulkan_->GetDevice(), &image_create_info, NULL, &mappableImage);
	assert(res == VK_SUCCESS);

	vkGetImageMemoryRequirements(vulkan_->GetDevice(), mappableImage, &mem_reqs);
	assert(res == VK_SUCCESS);

	mem_alloc.allocationSize = mem_reqs.size;

	// Find the memory type that is host mappable.
	bool pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mem_alloc.memoryTypeIndex);
	assert(pass);

	res = vkAllocateMemory(vulkan_->GetDevice(), &mem_alloc, NULL, &mappableMemory);
	assert(res == VK_SUCCESS);

	res = vkBindImageMemory(vulkan_->GetDevice(), mappableImage, mappableMemory, 0);
	assert(res == VK_SUCCESS);
}

uint8_t *VulkanTexture::Lock(int level, int *rowPitch) {
	CreateMappableImage();

	VkImageSubresource subres = {};
	subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subres.mipLevel = 0;
	subres.arrayLayer = 0;

	VkSubresourceLayout layout;
	void *data;

	// Get the subresource layout so we know what the row pitch is
	vkGetImageSubresourceLayout(vulkan_->GetDevice(), mappableImage, &subres, &layout);
	VkResult res = vkMapMemory(vulkan_->GetDevice(), mappableMemory, layout.offset, layout.size, 0, &data);
	assert(res == VK_SUCCESS);

	*rowPitch = (int)layout.rowPitch;
	return (uint8_t *)data;
}

void VulkanTexture::Unlock(VkCommandBuffer cmd) {
	vkUnmapMemory(vulkan_->GetDevice(), mappableMemory);

	// if we already have an image, queue it for destruction and forget it.
	Wipe();

	{  // Shrink the diff by not unindenting. If you make major changes, remove this.
		VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = format_;
		image_create_info.extent.width = tex_width;
		image_create_info.extent.height = tex_height;
		image_create_info.extent.depth = 1;
		image_create_info.mipLevels = 1;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.queueFamilyIndexCount = 0;
		image_create_info.pQueueFamilyIndices = NULL;
		image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_create_info.flags = 0;
		// The mappable image cannot be our texture, so create an optimally tiled image and blit to it
		image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkResult res = vkCreateImage(vulkan_->GetDevice(), &image_create_info, NULL, &image);
		assert(res == VK_SUCCESS);

		vkGetImageMemoryRequirements(vulkan_->GetDevice(), image, &mem_reqs);

		VkMemoryAllocateInfo mem_alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		mem_alloc.memoryTypeIndex = 0;
		mem_alloc.allocationSize = mem_reqs.size;

		if (allocator_) {
			offset_ = allocator_->Allocate(mem_reqs, &mem);
		} else {
			offset_ = 0;
			// Find memory type - don't specify any mapping requirements
			bool pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
			assert(pass);
			res = vkAllocateMemory(vulkan_->GetDevice(), &mem_alloc, NULL, &mem);
			assert(res == VK_SUCCESS);
		}

		res = vkBindImageMemory(vulkan_->GetDevice(), image, mem, offset_);
		assert(res == VK_SUCCESS);

		// Since we're going to blit from the mappable image, set its layout to SOURCE_OPTIMAL
		TransitionImageLayout2(cmd, mappableImage, 0, 1,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

		TransitionImageLayout2(cmd, image, 0, 1,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_WRITE_BIT);

		VkImageCopy copy_region;
		copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.srcSubresource.mipLevel = 0;
		copy_region.srcSubresource.baseArrayLayer = 0;
		copy_region.srcSubresource.layerCount = 1;
		copy_region.srcOffset.x = 0;
		copy_region.srcOffset.y = 0;
		copy_region.srcOffset.z = 0;
		copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.dstSubresource.mipLevel = 0;
		copy_region.dstSubresource.baseArrayLayer = 0;
		copy_region.dstSubresource.layerCount = 1;
		copy_region.dstOffset.x = 0;
		copy_region.dstOffset.y = 0;
		copy_region.dstOffset.z = 0;
		copy_region.extent.width = tex_width;
		copy_region.extent.height = tex_height;
		copy_region.extent.depth = 1;

		// Put the copy command into the command buffer
		vkCmdCopyImage(cmd,
			mappableImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &copy_region);

		assert(res == VK_SUCCESS);

		// Set the layout for the texture image from DESTINATION_OPTIMAL to SHADER_READ_ONLY
		TransitionImageLayout2(cmd, image, 0, 1,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

		// Then drop the temporary mappable image - although should not be necessary...
		vulkan_->Delete().QueueDeleteImage(mappableImage);
		vulkan_->Delete().QueueDeleteDeviceMemory(mappableMemory);
	}

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format_;
	view_info.components.r = VK_COMPONENT_SWIZZLE_R;
	view_info.components.g = VK_COMPONENT_SWIZZLE_G;
	view_info.components.b = VK_COMPONENT_SWIZZLE_B;
	view_info.components.a = VK_COMPONENT_SWIZZLE_A;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	VkResult res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &view);
	assert(res == VK_SUCCESS);
}

void VulkanTexture::Wipe() {
	if (image) {
		vulkan_->Delete().QueueDeleteImage(image);
	}
	if (view) {
		vulkan_->Delete().QueueDeleteImageView(view);
	}
	if (mem && !allocator_) {
		vulkan_->Delete().QueueDeleteDeviceMemory(mem);
	} else if (mem) {
		allocator_->Free(mem, offset_);
		mem = VK_NULL_HANDLE;
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

	tex_width = w;
	tex_height = h;
	numMips_ = numMips;
	format_ = format;

	VkImageAspectFlags aspect = IsDepthStencilFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageCreateInfo image_create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = format_;
	image_create_info.extent.width = tex_width;
	image_create_info.extent.height = tex_height;
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

	VkResult res = vkCreateImage(vulkan_->GetDevice(), &image_create_info, NULL, &image);
	if (res != VK_SUCCESS) {
		assert(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
		return false;
	}

	vkGetImageMemoryRequirements(vulkan_->GetDevice(), image, &mem_reqs);

	if (allocator_) {
		offset_ = allocator_->Allocate(mem_reqs, &mem);
		if (offset_ == VulkanDeviceAllocator::ALLOCATE_FAILED) {
			return false;
		}
	} else {
		VkMemoryAllocateInfo mem_alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		mem_alloc.memoryTypeIndex = 0;
		mem_alloc.allocationSize = mem_reqs.size;

		// Find memory type - don't specify any mapping requirements
		bool pass = vulkan_->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_alloc.memoryTypeIndex);
		assert(pass);

		res = vkAllocateMemory(vulkan_->GetDevice(), &mem_alloc, NULL, &mem);
		if (res != VK_SUCCESS) {
			_assert_msg_(G3D, res != VK_ERROR_TOO_MANY_OBJECTS, "Too many Vulkan memory objects!");
			assert(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
			return false;
		}

		offset_ = 0;
	}

	res = vkBindImageMemory(vulkan_->GetDevice(), image, mem, offset_);
	if (res != VK_SUCCESS) {
		assert(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
		return false;
	}

	// Write a command to transition the image to the requested layout, if it's not already that layout.
	if (initialLayout != VK_IMAGE_LAYOUT_UNDEFINED && initialLayout != VK_IMAGE_LAYOUT_PREINITIALIZED) {
		switch (initialLayout) {
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			TransitionImageLayout2(cmd, image, 0, numMips, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, VK_ACCESS_TRANSFER_WRITE_BIT);
			break;
		default:
			// If you planned to use UploadMip, you want VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL. After the
			// upload, you can transition.
			assert(false);
			break;
		}
	}

	// Create the view while we're at it.
	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	view_info.image = image;
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

	res = vkCreateImageView(vulkan_->GetDevice(), &view_info, NULL, &view);
	if (res != VK_SUCCESS) {
		assert(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY || res == VK_ERROR_TOO_MANY_OBJECTS);
		return false;
	}
	return true;
}

void VulkanTexture::UploadMip(VkCommandBuffer cmd, int mip, int mipWidth, int mipHeight, VkBuffer buffer, uint32_t offset, size_t rowLength) {
	VkBufferImageCopy copy_region = {};
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

	vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
}

void VulkanTexture::GenerateMip(VkCommandBuffer cmd, int mip) {
	_assert_msg_(G3D, mip != 0, "Cannot generate the first level");
	_assert_msg_(G3D, mip < numMips_, "Cannot generate mipmaps past the maximum created (%d vs %d)", mip, numMips_);
	VkImageBlit blit{};
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.mipLevel = mip - 1;
	blit.srcOffsets[1].x = tex_width >> (mip - 1);
	blit.srcOffsets[1].y = tex_height >> (mip - 1);
	blit.srcOffsets[1].z = 1;

	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.mipLevel = mip;
	blit.dstOffsets[1].x = tex_width >> mip;
	blit.dstOffsets[1].y = tex_height >> mip;
	blit.dstOffsets[1].z = 1;

	TransitionImageLayout2(cmd, image, mip - 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

	// Low-quality mipmap generation, but works okay.
	vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

	TransitionImageLayout2(cmd, image, mip - 1, 1, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
}

void VulkanTexture::EndCreate(VkCommandBuffer cmd, bool vertexTexture) {
	TransitionImageLayout2(cmd, image, 0, 1,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, vertexTexture ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void VulkanTexture::Destroy() {
	if (view != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteImageView(view);
	}
	if (image != VK_NULL_HANDLE) {
		if (mappableImage == image) {
			mappableImage = VK_NULL_HANDLE;
		}
		vulkan_->Delete().QueueDeleteImage(image);
	}
	if (mem != VK_NULL_HANDLE && !allocator_) {
		if (mappableMemory == mem) {
			mappableMemory = VK_NULL_HANDLE;
		}
		vulkan_->Delete().QueueDeleteDeviceMemory(mem);
	} else if (mem != VK_NULL_HANDLE) {
		allocator_->Free(mem, offset_);
		mem = VK_NULL_HANDLE;
	}
}
