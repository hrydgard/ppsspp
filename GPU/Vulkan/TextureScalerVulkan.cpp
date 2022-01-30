// Copyright (c) 2012- PPSSPP Project.

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

#include "Common/CommonTypes.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/Log.h"
#include "Common/Thread/ParallelLoop.h"
#include "Core/ThreadPools.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/Vulkan/TextureScalerVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

int TextureScalerVulkan::BytesPerPixel(u32 format) {
	return (format == VULKAN_8888_FORMAT) ? 4 : 2;
}

u32 TextureScalerVulkan::Get8888Format() {
	return VULKAN_8888_FORMAT;
}

void TextureScalerVulkan::ConvertTo8888(u32 format, u32* source, u32* &dest, int width, int height) {
	switch (format) {
	case VULKAN_8888_FORMAT:
		dest = source; // already fine
		break;

	case VULKAN_4444_FORMAT:
		ParallelRangeLoop(&g_threadManager, std::bind(&convert4444_dx9, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_TEXSCALE_LINES_PER_THREAD);
		break;

	case VULKAN_565_FORMAT:
		ParallelRangeLoop(&g_threadManager, std::bind(&convert565_dx9, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_TEXSCALE_LINES_PER_THREAD);
		break;

	case VULKAN_1555_FORMAT:
		ParallelRangeLoop(&g_threadManager, std::bind(&convert5551_dx9, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_TEXSCALE_LINES_PER_THREAD);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}
