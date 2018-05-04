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

enum CheckAlphaResult {
	// These are intended to line up with TexCacheEntry::STATUS_ALPHA_UNKNOWN, etc.
	CHECKALPHA_FULL = 0,
	CHECKALPHA_ANY = 4,
};

#include "ppsspp_config.h"
#include "Common/Common.h"
#include "Common/Swap.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoderNEON.h"
#include "GPU/GPUState.h"

void SetupTextureDecoder();

// Pitch must be aligned to 16 bits (as is the case on a PSP)
void DoSwizzleTex16(const u32 *ysrcp, u8 *texptr, int bxc, int byc, u32 pitch);

// For SSE, we statically link the SSE2 algorithms.
#if defined(_M_SSE)
u32 QuickTexHashSSE2(const void *checkp, u32 size);
#define DoQuickTexHash QuickTexHashSSE2
#define StableQuickTexHash QuickTexHashSSE2

// Pitch must be aligned to 16 bytes (as is the case on a PSP)
void DoUnswizzleTex16Basic(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch);
#define DoUnswizzleTex16 DoUnswizzleTex16Basic

// For ARM64, NEON is mandatory, so we also statically link.
#elif PPSSPP_ARCH(ARM64)
#define DoQuickTexHash QuickTexHashNEON
#define StableQuickTexHash QuickTexHashNEON
#define DoUnswizzleTex16 DoUnswizzleTex16NEON
#else
typedef u32 (*QuickTexHashFunc)(const void *checkp, u32 size);
extern QuickTexHashFunc DoQuickTexHash;
extern QuickTexHashFunc StableQuickTexHash;

typedef void (*UnswizzleTex16Func)(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch);
extern UnswizzleTex16Func DoUnswizzleTex16;
#endif

CheckAlphaResult CheckAlphaRGBA8888Basic(const u32_le *pixelData, int stride, int w, int h);
CheckAlphaResult CheckAlphaABGR4444Basic(const u32_le *pixelData, int stride, int w, int h);
CheckAlphaResult CheckAlphaRGBA4444Basic(const u32_le *pixelData, int stride, int w, int h);
CheckAlphaResult CheckAlphaABGR1555Basic(const u32_le *pixelData, int stride, int w, int h);
CheckAlphaResult CheckAlphaRGBA5551Basic(const u32_le *pixelData, int stride, int w, int h);

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

void DecodeDXT1Block(u32_le *dst, const DXT1Block *src, int pitch, int height, bool ignore1bitAlpha);
void DecodeDXT3Block(u32_le *dst, const DXT3Block *src, int pitch, int height);
void DecodeDXT5Block(u32_le *dst, const DXT5Block *src, int pitch, int height);

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

u32 GetTextureBufw(int level, u32 texaddr, GETextureFormat format);

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

inline void DeIndexTexture4Optimal(u16_le *dest, const u8 *indexed, int length, u16 color) {
	const u16_le *indexed16 = (const u16_le *)indexed;
	const u64 color64 = ((u64)color << 48) | ((u64)color << 32) | ((u64)color << 16) | (u64)color;
	u64_le *dest64 = (u64_le *)dest;
	for (int i = 0; i < length / 4; i ++) {
		u64 index = *indexed16++;
		dest64[i] = color64 | ((index & 0xf000) <<  36) | ((index & 0x0f00) << 24) | ((index & 0x00f0) << 12) | ((index & 0x000f) << 0);
	}
}

inline void DeIndexTexture4OptimalRev(u16_le *dest, const u8 *indexed, int length, u16 color) {
	const u16_le *indexed16 = (const u16_le *)indexed;
	const u64 color64 = ((u64)color << 48) | ((u64)color << 32) | ((u64)color << 16) | (u64)color;
	u64_le *dest64 = (u64_le *)dest;
	for (int i = 0; i < length / 4; i ++) {
		u64 index = *indexed16++;
		dest64[i] = color64 | ((index & 0xf000) << 48) | ((index & 0x0f00) <<  36) | ((index & 0x00f0) << 24) | ((index & 0x000f) << 12);
	}
}

template <typename ClutT>
inline void DeIndexTexture4(ClutT *dest, const u32 texaddr, int length, const ClutT *clut) {
	const u8 *indexed = (const u8 *) Memory::GetPointer(texaddr);
	DeIndexTexture4(dest, indexed, length, clut);
}

inline void DeIndexTexture4Optimal(u16_le *dest, const u32 texaddr, int length, u16 color) {
	const u8 *indexed = (const u8 *) Memory::GetPointer(texaddr);
	DeIndexTexture4Optimal(dest, indexed, length, color);
}
