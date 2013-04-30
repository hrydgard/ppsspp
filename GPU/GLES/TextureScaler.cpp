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

#include "TextureScaler.h"

#include "Core/Config.h"
#include "ext/xbrz/xbrz.h"

void TextureScaler::Scale(u32* &data, GLenum &dstFmt, int &width, int &height) {
	if(g_Config.iXBRZTexScalingLevel > 1) {
		int factor = g_Config.iXBRZTexScalingLevel;

		// depending on the factor and texture sizes, these can be pretty large (25 MB for a 512 by 512 texture with scaling factor 5)
		bufInput.resize(width*height); // used to store the input image image if it needs to be reformatted
		bufOutput.resize(width*height*factor*factor); // used to store the upscaled image
		u32 *xbrzInputBuf = bufInput.data();
		u32 *xbrzBuf = bufOutput.data();

		// convert texture to correct format for xBRZ
		switch(dstFmt) {
		case GL_UNSIGNED_BYTE:
			xbrzInputBuf = data; // already fine
			break;

		case GL_UNSIGNED_SHORT_4_4_4_4:
			for(int y = 0; y < height; ++y) {
				for(int x = 0; x < width; ++x) {
					u32 val = ((u16*)data)[y*width + x];
					u32 r = ((val>>12) & 0xF) * 17;
					u32 g = ((val>> 8) & 0xF) * 17;
					u32 b = ((val>> 4) & 0xF) * 17;
					u32 a = ((val>> 0) & 0xF) * 17;
					xbrzInputBuf[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
				}
			}
			break;

		case GL_UNSIGNED_SHORT_5_6_5:
			for(int y = 0; y < height; ++y) {
				for(int x = 0; x < width; ++x) {
					u32 val = ((u16*)data)[y*width + x];
					u32 r = ((val>>11) & 0x1F) * 8;
					u32 g = ((val>> 5) & 0x3F) * 4;
					u32 b = ((val    ) & 0x1F) * 8;
					xbrzInputBuf[y*width + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
				}
			}
			break;

		case GL_UNSIGNED_SHORT_5_5_5_1:
			for(int y = 0; y < height; ++y) {
				for(int x = 0; x < width; ++x) {
					u32 val = ((u16*)data)[y*width + x];
					u32 r = ((val>>11) & 0x1F) * 8;
					u32 g = ((val>> 6) & 0x1F) * 8;
					u32 b = ((val>> 1) & 0x1F) * 8;
					u32 a = (val & 0x1) * 255;
					xbrzInputBuf[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
				}
			}
			break;

		default:
			ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
		}

		// scale and update values accordingly
		xbrz::scale(factor, xbrzInputBuf, xbrzBuf, width, height);
		data = xbrzBuf;
		dstFmt = GL_UNSIGNED_BYTE;
		width *= factor;
		height *= factor;
	}
}
