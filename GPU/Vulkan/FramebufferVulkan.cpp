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

#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"



VulkanFramebuffer *FramebufferManagerVulkan::GetTempFBO(int width, int height, VulkanFBOColorDepth colorDepth) {
	return nullptr;
}

void FramebufferManagerVulkan::DestroyAllFBOs() {

}

void FramebufferManagerVulkan::Resized() {

}

void FramebufferManagerVulkan::DeviceLost() {

}

void FramebufferManagerVulkan::CopyDisplayToOutput() {

}

void FramebufferManagerVulkan::DecimateFBOs() {

}

void FramebufferManagerVulkan::EndFrame() {

}

void FramebufferManagerVulkan::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	// So: Allocate a temporary texture from a (very small) pool, upload content directly into it, schedule a transition
	// into the init command buffer, alloc and create an appropriate descriptor set, then bind and draw. no need for uniforms.
}


void FramebufferManagerVulkan::FlushBeforeCopy() {
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.

	// TODO: It's really bad that we are calling SetRenderFramebuffer here with
	// all the irrelevant state checking it'll use to decide what to do. Should
	// do something more focused here.
	SetRenderFrameBuffer(gstate_c.framebufChanged, gstate_c.skipDrawReason);
	drawEngine_->Flush(nullptr);
}

std::vector<FramebufferInfo> FramebufferManagerVulkan::GetFramebufferList() {
	return std::vector<FramebufferInfo>();
}
