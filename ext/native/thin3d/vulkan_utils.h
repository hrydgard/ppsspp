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

#pragma once

#include <vector>

#define VK_PROTOTYPES
#include "ext/vulkan/vulkan.h"
#include "VulkanContext.h"

class VulkanContext;
// Utility class to handle images without going insane.
// Allocates its own memory. 
class VulkanImage {
public:
	VulkanImage() : image_(nullptr), memory_(nullptr) {}

	bool IsValid() const { return image_ != nullptr; }
	// This can be done completely unsynchronized.
	void Create2D(VulkanContext *vulkan, VkFormat format, VkFlags required_props, VkImageTiling tiling, VkImageUsageFlags usage, int width, int height);

	// This can only be used if you pass in VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT in required_props in Create2D.
	void SetImageData2D(VkDevice device, const uint8_t *data, int width, int height, int pitch);
	void ChangeLayout(VkCommandBuffer cmd, VkImageAspectFlags aspectMask, VkImageLayout old_image_layout, VkImageLayout new_image_layout);
	VkImage GetImage() const {
		return image_;
	}

private:
	VkMemoryAllocateInfo mem_alloc_;
	VkImage image_;
	VkDeviceMemory memory_;
	int width_;
	int height_;
};


class Thin3DPipelineCache {

};

