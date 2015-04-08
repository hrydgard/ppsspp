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
#include "gfx/gl_common.h"

#include "GPU/Common/TextureScalerCommon.h"
#include "GPU/GLES/TextureScaler.h"
#include "Common/ColorConv.h"
#include "Common/Log.h"
#include "Common/ThreadPools.h"

int TextureScalerGL::BytesPerPixel(u32 format) {
	return (format == GL_UNSIGNED_BYTE) ? 4 : 2;
}

u32 TextureScalerGL::Get8888Format() {
	return GL_UNSIGNED_BYTE;
}

void TextureScalerGL::ConvertTo8888(u32 format, u32* source, u32* &dest, int width, int height) {
	switch(format) {
	case GL_UNSIGNED_BYTE:
		dest = source; // already fine
		break;

	case GL_UNSIGNED_SHORT_4_4_4_4:
		GlobalThreadPool::Loop(std::bind(&convert4444_gl, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	case GL_UNSIGNED_SHORT_5_6_5:
		GlobalThreadPool::Loop(std::bind(&convert565_gl, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	case GL_UNSIGNED_SHORT_5_5_5_1:
		GlobalThreadPool::Loop(std::bind(&convert5551_gl, (u16*)source, dest, width, placeholder::_1, placeholder::_2), 0, height);
		break;

	default:
		dest = source;
		ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
	}
}
