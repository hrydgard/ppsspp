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

#pragma once

#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

static const u8 textureBitsPerPixel[16] = {
	16,  //GE_TFMT_5650,
	16,  //GE_TFMT_5551,
	16,  //GE_TFMT_4444,
	32,  //GE_TFMT_8888,
	4,   //GE_TFMT_CLUT4,
	8,   //GE_TFMT_CLUT8,
	16,  //GE_TFMT_CLUT16,
	32,  //GE_TFMT_CLUT32,
	4,   //GE_TFMT_DXT1,
	8,   //GE_TFMT_DXT3,
	8,   //GE_TFMT_DXT5,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
};

// Masks to downalign bufw to 16 bytes, and wrap at 2048.
static const u32 textureAlignMask16[16] = {
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_5650,
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_5551,
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_4444,
	0x7FF & ~(((8 * 16) / 32) - 1),  //GE_TFMT_8888,
	0x7FF & ~(((8 * 16) / 4) - 1),   //GE_TFMT_CLUT4,
	0x7FF & ~(((8 * 16) / 8) - 1),   //GE_TFMT_CLUT8,
	0x7FF & ~(((8 * 16) / 16) - 1),  //GE_TFMT_CLUT16,
	0x7FF & ~(((8 * 16) / 32) - 1),  //GE_TFMT_CLUT32,
	0x7FF & ~(((8 * 16) / 4) - 1),   //GE_TFMT_DXT1,
	0x7FF & ~(((8 * 16) / 8) - 1),   //GE_TFMT_DXT3,
	0x7FF & ~(((8 * 16) / 8) - 1),   //GE_TFMT_DXT5,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
};

static inline u32 GetTextureBufw(int level, u32 texaddr, GETextureFormat format) {
	// This is a hack to allow for us to draw the huge PPGe texture, which is always in kernel ram.
	if (texaddr < PSP_GetUserMemoryBase())
		return gstate.texbufwidth[level] & 0x1FFF;

	u32 bufw = gstate.texbufwidth[level] & textureAlignMask16[format];
	if (bufw == 0) {
		// If it's less than 16 bytes, use 16 bytes.
		bufw = (8 * 16) / textureBitsPerPixel[format];
	}
	return bufw;
}