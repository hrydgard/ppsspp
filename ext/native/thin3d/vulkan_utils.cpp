// Copyright (c) 2015- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef min
#undef max

#endif

#include <vector>
#include <sstream>
#include <assert.h>

#ifdef USE_CRT_DBG
#undef new
#endif

#include "ext/glslang/SPIRV/GlslangToSpv.h"
#include "ext/glslang/SPIRV/disassemble.h"

#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

#include "thin3d/vulkan_utils.h"

void VulkanImage::Create2D(VulkanContext *vulkan, VkFormat format, VkFlags required_props, VkImageTiling tiling, VkImageUsageFlags usage, int width, int height) {
	VkDevice device = vulkan->GetDevice();
	width_ = width;
	height_ = height;

	VkImageCreateInfo i = {};
	i.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	i.pNext = NULL;
	i.imageType = VK_IMAGE_TYPE_2D;
	i.format = format;
	i.extent = { (uint32_t)width, (uint32_t)height, 1 };
	i.mipLevels = 1;
	i.arrayLayers = 1;
	i.samples = VK_SAMPLE_COUNT_1_BIT;
	i.tiling = tiling;
	i.usage = usage;
	i.flags = 0;
	i.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	i.queueFamilyIndexCount = 0;
	i.pQueueFamilyIndices = nullptr;
	i.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkMemoryRequirements mem_reqs;

	VkResult err = vkCreateImage(device, &i, nullptr, &image_);
	assert(!err);

	vkGetImageMemoryRequirements(device, image_, &mem_reqs);

	mem_alloc_.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_alloc_.pNext = NULL;
	mem_alloc_.allocationSize = mem_reqs.size;
	mem_alloc_.memoryTypeIndex = 0;

	bool res = vulkan->MemoryTypeFromProperties(mem_reqs.memoryTypeBits, required_props, &mem_alloc_.memoryTypeIndex);
	assert(res);

	err = vkAllocateMemory(device, &mem_alloc_, nullptr, &memory_);
	assert(!err);

	err = vkBindImageMemory(device, image_, memory_, 0);  // at offset 0.
	assert(!err);
}

void VulkanImage::SetImageData2D(VkDevice device, const uint8_t *data, int width, int height, int pitch) {
	VkImageSubresource subres;
	subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subres.mipLevel = 0;
	subres.arrayLayer = 0;

	VkSubresourceLayout layout;
	void *destData;
	vkGetImageSubresourceLayout(device, image_, &subres, &layout);

	VkResult err = vkMapMemory(device, memory_, 0, mem_alloc_.allocationSize, 0, &destData);
	assert(!err);
	
	uint8_t *writePtr = (uint8_t *)destData + layout.offset;
	int bpp = 4;  // TODO
	for (int y = 0; y < height; y++) {
		memcpy(writePtr + y * layout.rowPitch, data + y * pitch, bpp * width);
	}

	vkUnmapMemory(device, memory_);
}


void VulkanImage::ChangeLayout(VkCommandBuffer cmd, VkImageAspectFlags aspectMask, VkImageLayout old_image_layout, VkImageLayout new_image_layout) {
	VkImageMemoryBarrier image_memory_barrier;
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.pNext = NULL;
	image_memory_barrier.dstAccessMask = 0;
	image_memory_barrier.srcAccessMask = 0;
	image_memory_barrier.oldLayout = old_image_layout;
	image_memory_barrier.newLayout = new_image_layout;
	image_memory_barrier.image = image_;
	image_memory_barrier.subresourceRange.aspectMask = aspectMask;
	image_memory_barrier.subresourceRange.layerCount = 1;
	image_memory_barrier.subresourceRange.baseArrayLayer = 0;
	image_memory_barrier.subresourceRange.baseMipLevel = 0;
	image_memory_barrier.subresourceRange.levelCount = 1;

	if (old_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		// Make sure anything that was copying from this image has completed
		image_memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	}

	if (new_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		// Make sure any Copy or CPU writes to image are flushed
		image_memory_barrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
	}

	VkPipelineStageFlags src_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dest_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	vkCmdPipelineBarrier(cmd, src_stages, dest_stages, false, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
}
