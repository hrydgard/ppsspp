// Copyright (c) 2016- PPSSPP Project.

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

#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanImage.h"

// Vulkan doesn't really have the concept of an FBO that owns the images,
// but it does have the concept of a framebuffer as a set of attachments.
// VulkanFBO is an approximation of the FBO concept the other backends use
// to make things as similar as possible without being suboptimal.
//
// An FBO can be rendered to and used as a texture multiple times in a frame.
// Even at multiple sizes, while keeping the same contents.
// With GL or D3D we'd just rely on the driver managing duplicates for us, but in
// Vulkan we will want to be able to batch up the whole frame and reorder passes
// so that all textures are ready before the main scene, instead of switching back and
// forth. This comes at a memory cost but will be worth it.
//
// When we render to a scene, then render to a texture, then go back to the scene and
// use that texture, we will register that as a dependency. Then we will walk the DAG
// to find the final order of command buffers, and execute it.
//
// Each FBO will get its own command buffer for each pass. 

// 
struct VulkanFBOPass {
	VkCommandBuffer cmd;
};

class VulkanFBO {
public:
	VulkanFBO();
	~VulkanFBO();

	// Depth-format is chosen automatically depending on hardware support.
	// Color format will be 32-bit RGBA.
	void Create(VulkanContext *vulkan, VkRenderPass rp_compatible, int width, int height, VkFormat colorFormat);

	VulkanTexture *GetColor() { return color_; }
	VulkanTexture *GetDepthStencil() { return depthStencil_; }

	VkFramebuffer GetFramebuffer() { return framebuffer_; }

private:
	VulkanTexture *color_;
	VulkanTexture *depthStencil_;

	// This point specifically to color and depth.
	VkFramebuffer framebuffer_;
};