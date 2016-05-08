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

// On Visual Studio 2012, include this before anything else so
// _VARIADIC_MAX gets set to 10, to avoid std::bind compile errors.
// See header file for reasons why.
#if defined(_WIN32) && _MSC_VER == 1700
#include "../native/base/basictypes.h"
#endif

#include <algorithm>

#include "Common/ColorConv.h"
#include "Common/ThreadPools.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/Directx9/TextureScalerDX9.h"
#include "GPU/Directx9/GPU_DX9.h"

namespace DX9 {

int TextureScalerDX9::BytesPerPixel(u32 format) {
	return format == D3DFMT_A8R8G8B8 ? 4 : 2;
}

u32 TextureScalerDX9::Get8888Format() {
	return D3DFMT_A8R8G8B8;
}

void TextureScalerDX9::ConvertTo8888(u32 format, u32* source, u32* &dest, int width, int height) {
	switch(format) {
	case D3DFMT_A8R8G8B8:
		dest = source; // already fine
		break;

	case D3DFMT_A4R4G4B4:
		GlobalThreadPool::Loop(std::bind(&convert4444_dx9, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	case D3DFMT_R5G6B5:
		GlobalThreadPool::Loop(std::bind(&convert565_dx9, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	case D3DFMT_A1R5G5B5:
		GlobalThreadPool::Loop(std::bind(&convert5551_dx9, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}

}  // namespace