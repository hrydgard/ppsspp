// Copyright (C) 2015 PPSSPP Project.

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


// TODO: Make SSE2 and NEON versions of many of these.

#include "Common.h"
#include "CPUDetect.h"
#include "ColorConv.h"
#include "CommonTypes.h"

#ifdef _M_SSE
#include <xmmintrin.h>
#endif

inline u16 RGBA8888toRGB565(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 5) & 0x07E0) | ((px >> 8) & 0xF800);
}

inline u16 RGBA8888toRGBA4444(u32 px) {
	return ((px >> 4) & 0x000F) | ((px >> 8) & 0x00F0) | ((px >> 12) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u16 RGBA8888toRGBA5551(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 6) & 0x03E0) | ((px >> 9) & 0x7C00) | ((px >> 16) & 0x8000);
}

inline u16 BGRA8888toRGB565(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 5) & 0x07E0) | ((px << 8) & 0xF800);
}

inline u16 BGRA8888toRGBA4444(u32 px) {
	return ((px >> 20) & 0x000F) | ((px >> 8) & 0x00F0) | ((px << 4) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u32 RGBA2BGRA(u32 src) {
	const u32 r = (src & 0x000000FF) << 16;
	const u32 ga = src & 0xFF00FF00;
	const u32 b = (src & 0x00FF0000) >> 16;
	return r | ga | b;
}

void convert4444(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		ConvertRGBA4444ToRGBA8888(out + y * width, data + y * width, width);
	}
}

void convert565(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		ConvertRGB565ToRGBA888F(out + y * width, data + y * width, width);
	}
}

void convert5551(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		ConvertRGBA5551ToRGBA8888(out + y * width, data + y * width, width);
	}
}

// Used heavily in Test Drive Unlimited
void ConvertBGRA8888ToRGB565(u16 *dst, const u32 *src, int numPixels) {
#if _M_SSE >= 0x401
	const __m128i maskG = _mm_set1_epi32(0x8000FC00);
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i mask = _mm_set1_epi32(0x0000FFFF);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	int sseChunks = (numPixels / 4) & ~1;

	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF) || !cpu_info.bSSE4_1) {
		sseChunks = 0;
	}
	for (int i = 0; i < sseChunks; i += 2) {
		__m128i c1 = _mm_load_si128(&srcp[i + 0]);
		__m128i c2 = _mm_load_si128(&srcp[i + 1]);
		__m128i g, rb;

		g = _mm_and_si128(c1, maskG);
		g = _mm_srli_epi32(g, 5);
		rb = _mm_and_si128(c1, maskRB);
		rb = _mm_or_si128(_mm_srli_epi32(rb, 3), _mm_srli_epi32(rb, 8));
		c1 = _mm_and_si128(_mm_or_si128(g, rb), mask);

		g = _mm_and_si128(c2, maskG);
		g = _mm_srli_epi32(g, 5);
		rb = _mm_and_si128(c2, maskRB);
		rb = _mm_or_si128(_mm_srli_epi32(rb, 3), _mm_srli_epi32(rb, 8));
		c2 = _mm_and_si128(_mm_or_si128(g, rb), mask);

		_mm_store_si128(&dstp[i / 2], _mm_packus_epi32(c1, c2));
	}
	// The remainder starts right after those done via SSE.
	u32 i = sseChunks * 4;
#else
	u32 i = 0;
#endif
	for (int x = 0; x < numPixels; ++x) {
		dst[x] = BGRA8888toRGB565(src[x]);
	}
}

void ConvertRGBA8888ToRGB565(u16 *dst, const u32 *src, int numPixels) {
	for (int x = 0; x < numPixels; x++) {
		dst[x] = RGBA8888toRGB565(src[x]);
	}
}

void ConvertBGRA8888ToRGBA4444(u16 *dst, const u32 *src, int numPixels) {
	for (int x = 0; x < numPixels; ++x) {
		dst[x] = BGRA8888toRGBA4444(src[x]);
	}
}

void ConvertRGBA8888ToRGBA4444(u16 *dst, const u32 *src, int numPixels) {
	for (int x = 0; x < numPixels; ++x) {
		dst[x] = RGBA8888toRGBA4444(src[x]);
	}
}

void ConvertBGRA8888ToRGBA8888(u32 *dst, const u32 *src, int numPixels) {
#ifdef _M_SSE
	const __m128i maskGA = _mm_set1_epi32(0xFF00FF00);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	int sseChunks = numPixels / 4;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	}
	for (int i = 0; i < sseChunks; ++i) {
		__m128i c = _mm_load_si128(&srcp[i]);
		__m128i rb = _mm_andnot_si128(maskGA, c);
		c = _mm_and_si128(c, maskGA);

		__m128i b = _mm_srli_epi32(rb, 16);
		__m128i r = _mm_slli_epi32(rb, 16);
		c = _mm_or_si128(_mm_or_si128(c, r), b);
		_mm_store_si128(&dstp[i], c);
	}
	// The remainder starts right after those done via SSE.
	int i = sseChunks * 4;
#else
	int i = 0;
#endif
	for (; i < numPixels; i++) {
		const u32 c = src[i];
		dst[i] = ((c >> 16) & 0x000000FF) |
			((c >> 0) & 0xFF00FF00) |
			((c << 16) & 0x00FF0000);
	}
}

void ConvertRGBA8888ToRGBA5551(u16 *dst, const u32 *src, int numPixels) {
#if _M_SSE >= 0x401
	const __m128i maskAG = _mm_set1_epi32(0x8000F800);
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i mask = _mm_set1_epi32(0x0000FFFF);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	int sseChunks = (numPixels / 4) & ~1;
	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF) || !cpu_info.bSSE4_1) {
		sseChunks = 0;
	}
	for (int i = 0; i < sseChunks; i += 2) {
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
	int i = sseChunks * 4;
#else
	int i = 0;
#endif
	for (; i < numPixels; i++) {
		dst[i] = RGBA8888toRGBA5551(src[i]);
	}
}

inline u16 BGRA8888toRGBA5551(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 6) & 0x03E0) | ((px << 7) & 0x7C00) | ((px >> 16) & 0x8000);
}

void ConvertBGRA8888ToRGBA5551(u16 *dst, const u32 *src, int numPixels) {
#if _M_SSE >= 0x401
	const __m128i maskAG = _mm_set1_epi32(0x8000F800);
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i mask = _mm_set1_epi32(0x0000FFFF);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	int sseChunks = (numPixels / 4) & ~1;
	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF) || !cpu_info.bSSE4_1) {
		sseChunks = 0;
	}
	for (int i = 0; i < sseChunks; i += 2) {
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
	int i = sseChunks * 4;
#else
	int i = 0;
#endif
	for (; i < numPixels; i++) {
		dst[i] = BGRA8888toRGBA5551(src[i]);
	}
}

void ConvertRGB565ToRGBA888F(u32 *dst32, const u16 *src, int numPixels) {
	u8 *dst = (u8 *)dst32;
	for (int x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col)& 0x1f);
		dst[x * 4 + 1] = Convert6To8((col >> 5) & 0x3f);
		dst[x * 4 + 2] = Convert5To8((col >> 11) & 0x1f);
		dst[x * 4 + 3] = 255;
	}
}

void ConvertRGBA5551ToRGBA8888(u32 *dst32, const u16 *src, int numPixels) {
	u8 *dst = (u8 *)dst32;
	for (int x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col)& 0x1f);
		dst[x * 4 + 1] = Convert5To8((col >> 5) & 0x1f);
		dst[x * 4 + 2] = Convert5To8((col >> 10) & 0x1f);
		dst[x * 4 + 3] = (col >> 15) ? 255 : 0;
	}
}

void ConvertRGBA4444ToRGBA8888(u32 *dst32, const u16 *src, int numPixels) {
	u8 *dst = (u8 *)dst32;
	for (int x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert4To8((col >> 8) & 0xf);
		dst[x * 4 + 1] = Convert4To8((col >> 4) & 0xf);
		dst[x * 4 + 2] = Convert4To8(col & 0xf);
		dst[x * 4 + 3] = Convert4To8(col >> 12);
	}
}
