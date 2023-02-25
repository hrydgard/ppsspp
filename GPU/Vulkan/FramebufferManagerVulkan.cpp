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

#include "GPU/GPUState.h"

#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

using namespace PPSSPP_VK;

FramebufferManagerVulkan::FramebufferManagerVulkan(Draw::DrawContext *draw) :
	FramebufferManagerCommon(draw) {
	presentation_->SetLanguage(GLSL_VULKAN);
}

void FramebufferManagerVulkan::NotifyClear(bool clearColor, bool clearAlpha, bool clearDepth, uint32_t color, float depth) {
	int mask = 0;
	// The Clear detection takes care of doing a regular draw instead if separate masking
	// of color and alpha is needed, so we can just treat them as the same.
	if (clearColor || clearAlpha)
		mask |= Draw::FBChannel::FB_COLOR_BIT;
	if (clearDepth)
		mask |= Draw::FBChannel::FB_DEPTH_BIT;
	if (clearAlpha)
		mask |= Draw::FBChannel::FB_STENCIL_BIT;

	// Note that since the alpha channel and the stencil channel are shared on the PSP,
	// when we clear alpha, we also clear stencil to the same value.
	draw_->Clear(mask, color, depth, color >> 24);
	if (clearColor || clearAlpha) {
		SetColorUpdated(gstate_c.skipDrawReason);
	}
}
