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

#include <wiiu/gx2.h>
#include "Common/ColorConv.h"
#include "Core/ThreadPools.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/GX2/TextureScalerGX2.h"
#include "GPU/GX2/GPU_GX2.h"

#undef _1

int TextureScalerGX2::BytesPerPixel(u32 format) {
	return format == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8 ? 4 : 2;
}

u32 TextureScalerGX2::Get8888Format() {
	return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
}

void TextureScalerGX2::ConvertTo8888(u32 format, u32* source, u32* &dest, int width, int height) {
	switch (format) {
	case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8:
		dest = source; // already fine
		break;

	case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4:
		GlobalThreadPool::Loop(std::bind(&convert4444_dx9, (u16_le*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height);
		break;

	case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5:
		GlobalThreadPool::Loop(std::bind(&convert565_dx9, (u16_le*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height);
		break;

	case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1:
		GlobalThreadPool::Loop(std::bind(&convert5551_dx9, (u16_le*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}
