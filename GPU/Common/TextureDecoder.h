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

#include "Common/Common.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

void SetupTextureDecoder();

#ifdef _M_SSE
u32 QuickTexHashSSE2(const void *checkp, u32 size);
#define DoQuickTexHash QuickTexHashSSE2

void DoUnswizzleTex16Basic(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch, u32 rowWidth);
#define DoUnswizzleTex16 DoUnswizzleTex16Basic

#include "ext/xxhash.h"
#define DoReliableHash XXH32
#else
typedef u32 (*QuickTexHashFunc)(const void *checkp, u32 size);
extern QuickTexHashFunc DoQuickTexHash;

typedef void (*UnswizzleTex16Func)(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch, u32 rowWidth);
extern UnswizzleTex16Func DoUnswizzleTex16;

typedef u32 (*ReliableHashFunc)(const void *input, int len, u32 seed);
extern ReliableHashFunc DoReliableHash;
#endif

// All these DXT structs are in the reverse order, as compared to PC.
// On PC, alpha comes before color, and interpolants are before the tile data.

struct DXT1Block {
	u8 lines[4];
	u16_le color1;
	u16_le color2;
};

struct DXT3Block {
	DXT1Block color;
	u16_le alphaLines[4];
};

struct DXT5Block {
	DXT1Block color;
	u32_le alphadata2;
	u16_le alphadata1;
	u8 alpha1; u8 alpha2;
};

void DecodeDXT1Block(u32 *dst, const DXT1Block *src, int pitch, bool ignore1bitAlpha = false);
void DecodeDXT3Block(u32 *dst, const DXT3Block *src, int pitch);
void DecodeDXT5Block(u32 *dst, const DXT5Block *src, int pitch);

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
	0x7FF, //GE_TFMT_DXT1,
	0x7FF, //GE_TFMT_DXT3,
	0x7FF, //GE_TFMT_DXT5,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
	0,   // INVALID,
};

static inline u32 GetTextureBufw(int level, u32 texaddr, GETextureFormat format) {
	// This is a hack to allow for us to draw the huge PPGe texture, which is always in kernel ram.
	if (texaddr < PSP_GetKernelMemoryEnd())
		return gstate.texbufwidth[level] & 0x1FFF;

	u32 bufw = gstate.texbufwidth[level] & textureAlignMask16[format];
	if (bufw == 0) {
		// If it's less than 16 bytes, use 16 bytes.
		bufw = (8 * 16) / textureBitsPerPixel[format];
	}
	return bufw;
}

template <typename IndexT, typename ClutT>
inline void DeIndexTexture(ClutT *dest, const IndexT *indexed, int length, const ClutT *clut) {
	// Usually, there is no special offset, mask, or shift.
	const bool nakedIndex = gstate.isClutIndexSimple();

	if (nakedIndex) {
		if (sizeof(IndexT) == 1) {
			for (int i = 0; i < length; ++i) {
				*dest++ = clut[*indexed++];
			}
		} else {
			for (int i = 0; i < length; ++i) {
				*dest++ = clut[(*indexed++) & 0xFF];
			}
		}
	} else {
		for (int i = 0; i < length; ++i) {
			*dest++ = clut[gstate.transformClutIndex(*indexed++)];
		}
	}
}

template <typename IndexT, typename ClutT>
inline void DeIndexTexture(ClutT *dest, const u32 texaddr, int length, const ClutT *clut) {
	const IndexT *indexed = (const IndexT *) Memory::GetPointer(texaddr);
	DeIndexTexture(dest, indexed, length, clut);
}

template <typename ClutT>
inline void DeIndexTexture4(ClutT *dest, const u8 *indexed, int length, const ClutT *clut) {
	// Usually, there is no special offset, mask, or shift.
	const bool nakedIndex = gstate.isClutIndexSimple();

	if (nakedIndex) {
		for (int i = 0; i < length; i += 2) {
			u8 index = *indexed++;
			dest[i + 0] = clut[(index >> 0) & 0xf];
			dest[i + 1] = clut[(index >> 4) & 0xf];
		}
	} else {
		for (int i = 0; i < length; i += 2) {
			u8 index = *indexed++;
			dest[i + 0] = clut[gstate.transformClutIndex((index >> 0) & 0xf)];
			dest[i + 1] = clut[gstate.transformClutIndex((index >> 4) & 0xf)];
		}
	}
}

template <typename ClutT>
inline void DeIndexTexture4Optimal(ClutT *dest, const u8 *indexed, int length, ClutT color) {
	for (int i = 0; i < length; i += 2) {
		u8 index = *indexed++;
		dest[i + 0] = color | ((index >> 0) & 0xf);
		dest[i + 1] = color | ((index >> 4) & 0xf);
	}
}

template <>
inline void DeIndexTexture4Optimal<u16>(u16 *dest, const u8 *indexed, int length, u16 color) {
	const u16_le *indexed16 = (const u16_le *)indexed;
	const u32 color32 = (color << 16) | color;
	u32 *dest32 = (u32 *)dest;
	for (int i = 0; i < length / 2; i += 2) {
		u16 index = *indexed16++;
		dest32[i + 0] = color32 | ((index & 0x00f0) << 12) | ((index & 0x000f) >> 0);
		dest32[i + 1] = color32 | ((index & 0xf000) <<  4) | ((index & 0x0f00) >> 8);
	}
}

template <typename ClutT>
inline void DeIndexTexture4(ClutT *dest, const u32 texaddr, int length, const ClutT *clut) {
	const u8 *indexed = (const u8 *) Memory::GetPointer(texaddr);
	DeIndexTexture4(dest, indexed, length, clut);
}

template <typename ClutT>
inline void DeIndexTexture4Optimal(ClutT *dest, const u32 texaddr, int length, ClutT color) {
	const u8 *indexed = (const u8 *) Memory::GetPointer(texaddr);
	DeIndexTexture4Optimal(dest, indexed, length, color);
}

void ConvertBGRA8888ToRGBA8888(u32 *dst, const u32 *src, const u32 numPixels);
void ConvertRGBA8888ToRGBA5551(u16 *dst, const u32 *src, const u32 numPixels);
void ConvertBGRA8888ToRGBA5551(u16 *dst, const u32 *src, const u32 numPixels);
