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

#include <algorithm>

#include "Common/Profiler/Profiler.h"

#include "Common/System/Display.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/GPU/thin3d.h"

#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

using namespace PPSSPP_VK;

FramebufferManagerVulkan::FramebufferManagerVulkan(Draw::DrawContext *draw) :
	FramebufferManagerCommon(draw) {
	presentation_->SetLanguage(GLSL_VULKAN);
}

FramebufferManagerVulkan::~FramebufferManagerVulkan() {
	DeviceLost();
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
