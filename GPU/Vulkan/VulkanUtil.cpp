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

#include "GPU/Vulkan/VulkanUtil.h"

VulkanFBO::VulkanFBO() : color_(nullptr), depthStencil_(nullptr) {}

VulkanFBO::~VulkanFBO() {
	delete color_;
	delete depthStencil_;
}

void VulkanFBO::Create(VulkanContext *vulkan, VkRenderPass rp_compatible, int width, int height, VkFormat color_Format) {
	color_ = new VulkanTexture(vulkan);
	VkImageCreateFlags flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	color_->CreateDirect(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, flags | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	depthStencil_->CreateDirect(width, height, 1, VK_FORMAT_D24_UNORM_S8_UINT, flags | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

	VkImageView views[2] = { color_->GetImageView(), depthStencil_->GetImageView() };

	VkFramebufferCreateInfo fb = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb.pAttachments = views;
	fb.attachmentCount = 2;
	fb.flags = 0;
	fb.renderPass = rp_compatible;
	fb.width = width;
	fb.height = height;
	fb.layers = 1;

	vkCreateFramebuffer(vulkan->GetDevice(), &fb, nullptr, &framebuffer_);
}
