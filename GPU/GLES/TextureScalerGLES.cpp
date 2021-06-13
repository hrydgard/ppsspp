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

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Log.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/DataFormat.h"

#include "Core/ThreadPools.h"
#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/GLES/TextureScalerGLES.h"

int TextureScalerGLES::BytesPerPixel(u32 format) {
	return ((Draw::DataFormat)format == Draw::DataFormat::R8G8B8A8_UNORM) ? 4 : 2;
}

u32 TextureScalerGLES::Get8888Format() {
	return (u32)Draw::DataFormat::R8G8B8A8_UNORM;
}

void TextureScalerGLES::ConvertTo8888(u32 format, u32* source, u32* &dest, int width, int height) {
	Draw::DataFormat fmt = (Draw::DataFormat)format;
	switch (fmt) {
	case Draw::DataFormat::R8G8B8A8_UNORM:
		dest = source; // already fine
		break;

	case Draw::DataFormat::R4G4B4A4_UNORM_PACK16:
		ParallelRangeLoop(&g_threadManager, std::bind(&convert4444_gl, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_TEXSCALE_LINES_PER_THREAD);
		break;

	case Draw::DataFormat::R5G6B5_UNORM_PACK16:
		ParallelRangeLoop(&g_threadManager, std::bind(&convert565_gl, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_TEXSCALE_LINES_PER_THREAD);
		break;

	case Draw::DataFormat::R5G5B5A1_UNORM_PACK16:
		ParallelRangeLoop(&g_threadManager, std::bind(&convert5551_gl, (u16*)source, dest, width, std::placeholders::_1, std::placeholders::_2), 0, height, MIN_TEXSCALE_LINES_PER_THREAD);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}
