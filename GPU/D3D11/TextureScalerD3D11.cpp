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

#include <d3d11.h>
#include "Common/ColorConv.h"
#include "Common/ThreadPools.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/D3D11/TextureScalerD3D11.h"
#include "GPU/D3D11/GPU_D3D11.h"

int TextureScalerD3D11::BytesPerPixel(u32 format) {
	return format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4 : 2;
}

u32 TextureScalerD3D11::Get8888Format() {
	return DXGI_FORMAT_B8G8R8A8_UNORM;
}

void TextureScalerD3D11::ConvertTo8888(u32 format, u32* source, u32* &dest, int width, int height) {
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		dest = source; // already fine
		break;

	case DXGI_FORMAT_B4G4R4A4_UNORM:
		GlobalThreadPool::Loop(std::bind(&convert4444_dx9, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height);
		break;

	case DXGI_FORMAT_B5G6R5_UNORM:
		GlobalThreadPool::Loop(std::bind(&convert565_dx9, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height);
		break;

	case DXGI_FORMAT_B5G5R5A1_UNORM:
		GlobalThreadPool::Loop(std::bind(&convert5551_dx9, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}