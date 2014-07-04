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

#include "ext/xxhash.h"
#include "Common/CPUDetect.h"
#include "GPU/Common/TextureDecoder.h"
// NEON is in a separate file so that it can be compiled with a runtime check.
#include "GPU/Common/TextureDecoderNEON.h"

// TODO: Move some common things into here.

#ifdef _M_SSE
#include <xmmintrin.h>
#if _M_SSE >= 0x401
#include <smmintrin.h>
#endif

u32 QuickTexHashSSE2(const void *checkp, u32 size) {
	u32 check = 0;

	if (((intptr_t)checkp & 0xf) == 0 && (size & 0x3f) == 0) {
		__m128i cursor = _mm_set1_epi32(0);
		__m128i cursor2 = _mm_set_epi16(0x0001U, 0x0083U, 0x4309U, 0x4d9bU, 0xb651U, 0x4b73U, 0x9bd9U, 0xc00bU);
		__m128i update = _mm_set1_epi16(0x2455U);
		const __m128i *p = (const __m128i *)checkp;
		for (u32 i = 0; i < size / 16; i += 4) {
			__m128i chunk = _mm_mullo_epi16(_mm_load_si128(&p[i]), cursor2);
			cursor = _mm_add_epi32(cursor, chunk);
			cursor = _mm_xor_si128(cursor, _mm_load_si128(&p[i + 1]));
			cursor = _mm_add_epi32(cursor, _mm_load_si128(&p[i + 2]));
			chunk = _mm_mullo_epi16(_mm_load_si128(&p[i + 3]), cursor2);
			cursor = _mm_xor_si128(cursor, chunk);
			cursor2 = _mm_add_epi16(cursor2, update);
		}
		cursor = _mm_add_epi32(cursor, cursor2);
		// Add the four parts into the low i32.
		cursor = _mm_add_epi32(cursor, _mm_srli_si128(cursor, 8));
		cursor = _mm_add_epi32(cursor, _mm_srli_si128(cursor, 4));
		check = _mm_cvtsi128_si32(cursor);
	} else {
		const u32 *p = (const u32 *)checkp;
		for (u32 i = 0; i < size / 8; ++i) {
			check += *p++;
			check ^= *p++;
		}
	}

	return check;
}
#endif

static u32 QuickTexHashBasic(const void *checkp, u32 size) {
#if defined(ARM) && defined(__GNUC__)
	__builtin_prefetch(checkp, 0, 0);

	u32 check;
	asm volatile (
		// Let's change size to the end address.
		"add %1, %1, %2\n"
		"mov r6, #0\n"

		".align 2\n"

		// If we have zero sized input, we'll return garbage.  Oh well, shouldn't happen.
		"QuickTexHashBasic_next:\n"
		"ldmia %2!, {r2-r5}\n"
		"add r6, r6, r2\n"
		"eor r6, r6, r3\n"
		"cmp %2, %1\n"
		"add r6, r6, r4\n"
		"eor r6, r6, r5\n"
		"blo QuickTexHashBasic_next\n"

		".align 2\n"

		"QuickTexHashBasic_done:\n"
		"mov %0, r6\n"

		: "=r"(check)
		: "r"(size), "r"(checkp)
		: "r2", "r3", "r4", "r5", "r6"
	);
#else
	u32 check = 0;
	const u32 size_u32 = size / 4;
	const u32 *p = (const u32 *)checkp;
	for (u32 i = 0; i < size_u32; i += 4) {
		check += p[i + 0];
		check ^= p[i + 1];
		check += p[i + 2];
		check ^= p[i + 3];
	}
#endif

	return check;
}

void DoUnswizzleTex16Basic(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch, u32 rowWidth) {
#ifdef _M_SSE
	const __m128i *src = (const __m128i *)texptr;
	for (int by = 0; by < byc; by++) {
		__m128i *xdest = (__m128i *)ydestp;
		for (int bx = 0; bx < bxc; bx++) {
			__m128i *dest = xdest;
			for (int n = 0; n < 2; n++) {
				// Textures are always 16-byte aligned so this is fine.
				__m128i temp1 = _mm_load_si128(src);
				__m128i temp2 = _mm_load_si128(src + 1);
				__m128i temp3 = _mm_load_si128(src + 2);
				__m128i temp4 = _mm_load_si128(src + 3);
				_mm_store_si128(dest, temp1);
				dest += pitch >> 2;
				_mm_store_si128(dest, temp2);
				dest += pitch >> 2;
				_mm_store_si128(dest, temp3);
				dest += pitch >> 2;
				_mm_store_si128(dest, temp4);
				dest += pitch >> 2;
				src += 4;
			}
			xdest ++;
		}
		ydestp += (rowWidth * 8) / 4;
	}
#else
	const u32 *src = (const u32 *)texptr;
	for (int by = 0; by < byc; by++) {
		u32 *xdest = ydestp;
		for (int bx = 0; bx < bxc; bx++) {
			u32 *dest = xdest;
			for (int n = 0; n < 8; n++) {
				memcpy(dest, src, 16);
				dest += pitch;
				src += 4;
			}
			xdest += 4;
		}
		ydestp += (rowWidth * 8) / 4;
	}
#endif
}

#ifndef _M_SSE
QuickTexHashFunc DoQuickTexHash = &QuickTexHashBasic;
UnswizzleTex16Func DoUnswizzleTex16 = &DoUnswizzleTex16Basic;
ReliableHashFunc DoReliableHash = &XXH32;
#endif

// This has to be done after CPUDetect has done its magic.
void SetupTextureDecoder() {
#ifdef HAVE_ARMV7
	if (cpu_info.bNEON) {
		DoQuickTexHash = &QuickTexHashNEON;
		DoUnswizzleTex16 = &DoUnswizzleTex16NEON;
#ifndef IOS
		// Not sure if this is safe on iOS, it's had issues with xxhash.
		DoReliableHash = &ReliableHashNEON;
#endif
	}
#endif
}

static inline u32 makecol(int r, int g, int b, int a) {
	return (a << 24) | (r << 16) | (g << 8) | b;
}

// This could probably be done faster by decoding two or four blocks at a time with SSE/NEON.
void DecodeDXT1Block(u32 *dst, const DXT1Block *src, int pitch, bool ignore1bitAlpha) {
	// S3TC Decoder
	// Needs more speed and debugging.
	u16 c1 = (src->color1);
	u16 c2 = (src->color2);
	int red1 = Convert5To8(c1 & 0x1F);
	int red2 = Convert5To8(c2 & 0x1F);
	int green1 = Convert6To8((c1 >> 5) & 0x3F);
	int green2 = Convert6To8((c2 >> 5) & 0x3F);
	int blue1 = Convert5To8((c1 >> 11) & 0x1F);
	int blue2 = Convert5To8((c2 >> 11) & 0x1F);

	u32 colors[4];
	colors[0] = makecol(red1, green1, blue1, 255);
	colors[1] = makecol(red2, green2, blue2, 255);
	if (c1 > c2 || ignore1bitAlpha) {
		int blue3 = ((blue2 - blue1) >> 1) - ((blue2 - blue1) >> 3);
		int green3 = ((green2 - green1) >> 1) - ((green2 - green1) >> 3);
		int red3 = ((red2 - red1) >> 1) - ((red2 - red1) >> 3);				
		colors[2] = makecol(red1 + red3, green1 + green3, blue1 + blue3, 255);
		colors[3] = makecol(red2 - red3, green2 - green3, blue2 - blue3, 255);
	} else {
		colors[2] = makecol((red1 + red2 + 1) / 2, // Average
			(green1 + green2 + 1) / 2,
			(blue1 + blue2 + 1) / 2, 255);
		colors[3] = makecol(red2, green2, blue2, 0);	// Color2 but transparent
	}

	for (int y = 0; y < 4; y++) {
		int val = src->lines[y];
		for (int x = 0; x < 4; x++) {
			dst[x] = colors[val & 3];
			val >>= 2;
		}
		dst += pitch;
	}
}

void DecodeDXT3Block(u32 *dst, const DXT3Block *src, int pitch)
{
	DecodeDXT1Block(dst, &src->color, pitch, true);

	for (int y = 0; y < 4; y++) {
		u32 line = src->alphaLines[y];
		for (int x = 0; x < 4; x++) {
			const u8 a4 = line & 0xF;
			dst[x] = (dst[x] & 0xFFFFFF) | (a4 << 24) | (a4 << 28);
			line >>= 4;
		}
		dst += pitch;
	}
}

static inline u8 lerp8(const DXT5Block *src, int n) {
	float d = n / 7.0f;
	return (u8)(src->alpha1 + (src->alpha2 - src->alpha1) * d);
}

static inline u8 lerp6(const DXT5Block *src, int n) {
	float d = n / 5.0f;
	return (u8)(src->alpha1 + (src->alpha2 - src->alpha1) * d);
}

// The alpha channel is not 100% correct 
void DecodeDXT5Block(u32 *dst, const DXT5Block *src, int pitch) {
	DecodeDXT1Block(dst, &src->color, pitch, true);
	u8 alpha[8];

	alpha[0] = src->alpha1;
	alpha[1] = src->alpha2;
	if (alpha[0] > alpha[1]) {
		alpha[2] = lerp8(src, 1);
		alpha[3] = lerp8(src, 2);
		alpha[4] = lerp8(src, 3);
		alpha[5] = lerp8(src, 4);
		alpha[6] = lerp8(src, 5);
		alpha[7] = lerp8(src, 6);
	} else {
		alpha[2] = lerp6(src, 1);
		alpha[3] = lerp6(src, 2);
		alpha[4] = lerp6(src, 3);
		alpha[5] = lerp6(src, 4);
		alpha[6] = 0;
		alpha[7] = 255;
	}

	u64 data = ((u64)(u16)src->alphadata1 << 32) | (u32)src->alphadata2;

	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			dst[x] = (dst[x] & 0xFFFFFF) | (alpha[data & 7] << 24);
			data >>= 3;
		}
		dst += pitch;
	}
}

void ConvertBGRA8888ToRGBA8888(u32 *dst, const u32 *src, const u32 numPixels) {
#ifdef _M_SSE
	const __m128i maskGA = _mm_set1_epi32(0xFF00FF00);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = numPixels / 4;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		__m128i c = _mm_load_si128(&srcp[i]);
		__m128i rb = _mm_andnot_si128(maskGA, c);
		c = _mm_and_si128(c, maskGA);

		__m128i b = _mm_srli_epi32(rb, 16);
		__m128i r = _mm_slli_epi32(rb, 16);
		c = _mm_or_si128(_mm_or_si128(c, r), b);
		_mm_store_si128(&dstp[i], c);
	}
	// The remainder starts right after those done via SSE.
	u32 i = sseChunks * 4;
#else
	u32 i = 0;
#endif
	for (; i < numPixels; i++) {
		const u32 c = src[i];
		dst[i] = ((c >> 16) & 0x000000FF) |
		         ((c >> 0)  & 0xFF00FF00) |
		         ((c << 16) & 0x00FF0000);
	}
}

inline u16 RGBA8888toRGBA5551(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 6) & 0x03E0) | ((px >> 9) & 0x7C00) | ((px >> 16) & 0x8000);
}

void ConvertRGBA8888ToRGBA5551(u16 *dst, const u32 *src, const u32 numPixels) {
#if _M_SSE >= 0x401
	const __m128i maskAG = _mm_set1_epi32(0x8000F800);
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i mask = _mm_set1_epi32(0x0000FFFF);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = (numPixels / 4) & ~1;
	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF) || !cpu_info.bSSE4_1) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; i += 2) {
		__m128i c1 = _mm_load_si128(&srcp[i + 0]);
		__m128i c2 = _mm_load_si128(&srcp[i + 1]);
		__m128i ag, rb;

		ag = _mm_and_si128(c1, maskAG);
		ag = _mm_or_si128(_mm_srli_epi32(ag, 16), _mm_srli_epi32(ag, 6));
		rb = _mm_and_si128(c1, maskRB);
		rb = _mm_or_si128(_mm_srli_epi32(rb, 3), _mm_srli_epi32(rb, 9));
		c1 = _mm_and_si128(_mm_or_si128(ag, rb), mask);

		ag = _mm_and_si128(c2, maskAG);
		ag = _mm_or_si128(_mm_srli_epi32(ag, 16), _mm_srli_epi32(ag, 6));
		rb = _mm_and_si128(c2, maskRB);
		rb = _mm_or_si128(_mm_srli_epi32(rb, 3), _mm_srli_epi32(rb, 9));
		c2 = _mm_and_si128(_mm_or_si128(ag, rb), mask);

		_mm_store_si128(&dstp[i / 2], _mm_packus_epi32(c1, c2));
	}
	// The remainder starts right after those done via SSE.
	u32 i = sseChunks * 4;
#else
	u32 i = 0;
#endif
	for (; i < numPixels; i++) {
		dst[i] = RGBA8888toRGBA5551(src[i]);
	}
}

inline u16 BGRA8888toRGBA5551(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 6) & 0x03E0) | ((px << 7) & 0x7C00) | ((px >> 16) & 0x8000);
}

void ConvertBGRA8888ToRGBA5551(u16 *dst, const u32 *src, const u32 numPixels) {
#if _M_SSE >= 0x401
	const __m128i maskAG = _mm_set1_epi32(0x8000F800);
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i mask = _mm_set1_epi32(0x0000FFFF);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = (numPixels / 4) & ~1;
	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF) || !cpu_info.bSSE4_1) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; i += 2) {
		__m128i c1 = _mm_load_si128(&srcp[i + 0]);
		__m128i c2 = _mm_load_si128(&srcp[i + 1]);
		__m128i ag, rb;

		ag = _mm_and_si128(c1, maskAG);
		ag = _mm_or_si128(_mm_srli_epi32(ag, 16), _mm_srli_epi32(ag, 6));
		rb = _mm_and_si128(c1, maskRB);
		rb = _mm_or_si128(_mm_srli_epi32(rb, 19), _mm_slli_epi32(rb, 7));
		c1 = _mm_and_si128(_mm_or_si128(ag, rb), mask);

		ag = _mm_and_si128(c2, maskAG);
		ag = _mm_or_si128(_mm_srli_epi32(ag, 16), _mm_srli_epi32(ag, 6));
		rb = _mm_and_si128(c2, maskRB);
		rb = _mm_or_si128(_mm_srli_epi32(rb, 19), _mm_slli_epi32(rb, 7));
		c2 = _mm_and_si128(_mm_or_si128(ag, rb), mask);

		_mm_store_si128(&dstp[i / 2], _mm_packus_epi32(c1, c2));
	}
	// The remainder starts right after those done via SSE.
	u32 i = sseChunks * 4;
#else
	u32 i = 0;
#endif
	for (; i < numPixels; i++) {
		dst[i] = BGRA8888toRGBA5551(src[i]);
	}
}
