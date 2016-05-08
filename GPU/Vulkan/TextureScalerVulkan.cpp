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

#if _MSC_VER == 1700
// Has to be included before TextureScaler.h, else we get those std::bind errors in VS2012.. 
#include "../native/base/basictypes.h"
#endif

#include <algorithm>

#include "Common/Vulkan/VulkanContext.h"
#include "Common/ColorConv.h"
#include "Common/Log.h"
#include "Common/ThreadPools.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/Vulkan/TextureScalerVulkan.h"

// TODO: Share in TextureCacheVulkan.h?
// Note: some drivers prefer B4G4R4A4_UNORM_PACK16 over R4G4B4A4_UNORM_PACK16.
#define VULKAN_4444_FORMAT VK_FORMAT_B4G4R4A4_UNORM_PACK16
#define VULKAN_1555_FORMAT VK_FORMAT_A1R5G5B5_UNORM_PACK16
#define VULKAN_565_FORMAT  VK_FORMAT_B5G6R5_UNORM_PACK16
#define VULKAN_8888_FORMAT VK_FORMAT_R8G8B8A8_UNORM

int TextureScalerVulkan::BytesPerPixel(u32 format) {
	return (format == VULKAN_8888_FORMAT) ? 4 : 2;
}

u32 TextureScalerVulkan::Get8888Format() {
	return VULKAN_8888_FORMAT;
}

void TextureScalerVulkan::ScaleAlways(u32 *&data, u32 &dstFmt, int &width, int &height, int factor) {
	if (!Scale(data, dstFmt, width, height, factor)) {
		// This means it was a flat texture.  Vulkan wants the size up front, so we need to make it happen.
		assert(IsEmptyOrFlat(data, width * height, dstFmt));

		u32 pixel;
		// Since it's flat, one pixel is enough.  It might end up pointing to data, though.
		u32 *pixelPointer = &pixel;
		ConvertTo8888(dstFmt, data, pixelPointer, 1, 1);
		if (pixelPointer != &pixel) {
			pixel = *pixelPointer;
		}

		bufOutput.resize(width * height * factor * factor);
		dstFmt = Get8888Format();
		data = bufOutput.data();
		width *= factor;
		height *= factor;

		// ABCD.  If A = D, and AB = CD, then they must all be equal (B = C, etc.)
		if ((pixel & 0x000000FF) == (pixel >> 24) && (pixel & 0x0000FFFF) == (pixel >> 16)) {
			memset(data, pixel & 0xFF, width * height * sizeof(u32));
		} else {
			// TODO: Optimize.
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					data[y * width + x] = pixel;
				}
			}
		}
	}
}

void TextureScalerVulkan::ConvertTo8888(u32 format, u32* source, u32* &dest, int width, int height) {
	switch (format) {
	case VULKAN_8888_FORMAT:
		dest = source; // already fine
		break;

	case VULKAN_4444_FORMAT:
		GlobalThreadPool::Loop(std::bind(&convert4444_dx9, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	case VULKAN_565_FORMAT:
		GlobalThreadPool::Loop(std::bind(&convert565_dx9, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	case VULKAN_1555_FORMAT:
		GlobalThreadPool::Loop(std::bind(&convert5551_dx9, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}
