// Copyright (c) 2015- PPSSPP Project.

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

#include "ColorConv.h"
#include "Common.h"
#include "CPUDetect.h"

#ifdef _M_SSE
#include <xmmintrin.h>
#endif

inline u16 RGBA8888toRGB565(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 5) & 0x07E0) | ((px >> 8) & 0xF800);
}

inline u16 RGBA8888toRGBA4444(u32 px) {
	return ((px >> 4) & 0x000F) | ((px >> 8) & 0x00F0) | ((px >> 12) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u16 BGRA8888toRGB565(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 5) & 0x07E0) | ((px << 8) & 0xF800);
}

inline u16 BGRA8888toRGBA4444(u32 px) {
	return ((px >> 20) & 0x000F) | ((px >> 8) & 0x00F0) | ((px << 4) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u16 BGRA8888toRGBA5551(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 6) & 0x03E0) | ((px << 7) & 0x7C00) | ((px >> 16) & 0x8000);
}

inline u16 RGBA8888toRGBA5551(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 6) & 0x03E0) | ((px >> 9) & 0x7C00) | ((px >> 16) & 0x8000);
}

// convert 4444 image to 8888, parallelizable
void convert4444_gl(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < width; ++x) {
			u32 val = data[y*width + x];
			u32 r = ((val >> 12) & 0xF) * 17;
			u32 g = ((val >> 8) & 0xF) * 17;
			u32 b = ((val >> 4) & 0xF) * 17;
			u32 a = ((val >> 0) & 0xF) * 17;
			out[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

// convert 565 image to 8888, parallelizable
void convert565_gl(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < width; ++x) {
			u32 val = data[y*width + x];
			u32 r = Convert5To8((val >> 11) & 0x1F);
			u32 g = Convert6To8((val >> 5) & 0x3F);
			u32 b = Convert5To8((val)& 0x1F);
			out[y*width + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

// convert 5551 image to 8888, parallelizable
void convert5551_gl(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < width; ++x) {
			u32 val = data[y*width + x];
			u32 r = Convert5To8((val >> 11) & 0x1F);
			u32 g = Convert5To8((val >> 6) & 0x1F);
			u32 b = Convert5To8((val >> 1) & 0x1F);
			u32 a = (val & 0x1) * 255;
			out[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

// convert 4444 image to 8888, parallelizable
void convert4444_dx9(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < width; ++x) {
			u32 val = data[y*width + x];
			u32 r = ((val >> 0) & 0xF) * 17;
			u32 g = ((val >> 4) & 0xF) * 17;
			u32 b = ((val >> 8) & 0xF) * 17;
			u32 a = ((val >> 12) & 0xF) * 17;
			out[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

// convert 565 image to 8888, parallelizable
void convert565_dx9(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < width; ++x) {
			u32 val = data[y*width + x];
			u32 r = Convert5To8((val)& 0x1F);
			u32 g = Convert6To8((val >> 5) & 0x3F);
			u32 b = Convert5To8((val >> 11) & 0x1F);
			out[y*width + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
		}
	}
}

// convert 5551 image to 8888, parallelizable
void convert5551_dx9(u16* data, u32* out, int width, int l, int u) {
	for (int y = l; y < u; ++y) {
		for (int x = 0; x < width; ++x) {
			u32 val = data[y*width + x];
			u32 r = Convert5To8((val >> 0) & 0x1F);
			u32 g = Convert5To8((val >> 5) & 0x1F);
			u32 b = Convert5To8((val >> 10) & 0x1F);
			u32 a = ((val >> 15) & 0x1) * 255;
			out[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
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
			(c & 0xFF00FF00) |
			((c << 16) & 0x00FF0000);
	}
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

void ConvertBGRA8888ToRGB565(u16 *dst, const u32 *src, const u32 numPixels) {
	for (u32 i = 0; i < numPixels; i++) {
		dst[i] = BGRA8888toRGB565(src[i]);
	}
}

void ConvertBGRA8888ToRGBA4444(u16 *dst, const u32 *src, const u32 numPixels) {
	for (u32 i = 0; i < numPixels; i++) {
		dst[i] = BGRA8888toRGBA4444(src[i]);
	}
}

void ConvertRGBA8888ToRGB565(u16 *dst, const u32 *src, const u32 numPixels) {
	for (u32 x = 0; x < numPixels; ++x) {
		dst[x] = RGBA8888toRGB565(src[x]);
	}
}

void ConvertRGBA8888ToRGBA4444(u16 *dst, const u32 *src, const u32 numPixels) {
	for (u32 x = 0; x < numPixels; ++x) {
		dst[x] = RGBA8888toRGBA4444(src[x]);
	}
}

void ConvertRGBA565ToRGBA8888(u32 *dst32, const u16 *src, const u32 numPixels) {
	u8 *dst = (u8 *)dst32;
	for (u32 x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col) & 0x1f);
		dst[x * 4 + 1] = Convert6To8((col >> 5) & 0x3f);
		dst[x * 4 + 2] = Convert5To8((col >> 11) & 0x1f);
		dst[x * 4 + 3] = 255;
	}
}

void ConvertRGBA5551ToRGBA8888(u32 *dst32, const u16 *src, const u32 numPixels) {
	u8 *dst = (u8 *)dst32;
	for (u32 x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col) & 0x1f);
		dst[x * 4 + 1] = Convert5To8((col >> 5) & 0x1f);
		dst[x * 4 + 2] = Convert5To8((col >> 10) & 0x1f);
		dst[x * 4 + 3] = (col >> 15) ? 255 : 0;
	}
}

void ConvertRGBA4444ToRGBA8888(u32 *dst32, const u16 *src, const u32 numPixels) {
	u8 *dst = (u8 *)dst32;
	for (u32 x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert4To8((col >> 8) & 0xf);
		dst[x * 4 + 1] = Convert4To8((col >> 4) & 0xf);
		dst[x * 4 + 2] = Convert4To8(col & 0xf);
		dst[x * 4 + 3] = Convert4To8(col >> 12);
	}
}

void ConvertBGRA4444ToRGBA8888(u32 *dst32, const u16 *src, const u32 numPixels) {
	u8 *dst = (u8 *)dst32;
	for (u32 x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4 + 0] = (col >> 12) << 4;
		dst[x * 4 + 1] = ((col >> 8) & 0xf) << 4;
		dst[x * 4 + 2] = ((col >> 4) & 0xf) << 4;
		dst[x * 4 + 3] = (col & 0xf) << 4;
	}
}

inline void ARGB8From565(u16 c, u32 * dst) {
	*dst = ((c & 0x001f) << 19) | (((c >> 5) & 0x003f) << 11) | ((((c >> 10) & 0x001f) << 3)) | 0xFF000000;
}

inline void ARGB8From5551(u16 c, u32 * dst) {
	*dst = ((c & 0x001f) << 19) | (((c >> 5) & 0x001f) << 11) | ((((c >> 10) & 0x001f) << 3)) | 0xFF000000;
}

void ConvertBGRA5551ToRGBA8888(u32 *dst, const u16 *src, const u32 numPixels) {
	for (u32 x = 0; x < numPixels; x++) {
		u16 col0 = src[x];
		ARGB8From5551(col0, &dst[x]);
	}
}

void ConvertBGR565ToRGBA8888(u32 *dst, const u16 *src, const u32 numPixels) {
	for (u32 x = 0; x < numPixels; x++) {
		u16 col0 = src[x];
		ARGB8From565(col0, &dst[x]);
	}
}

void ConvertRGBA4444ToABGR4444(u16 *dst, const u16 *src, const u32 numPixels) {
#ifdef _M_SSE
	const __m128i maskB = _mm_set1_epi16(0x00F0);
	const __m128i maskG = _mm_set1_epi16(0x0F00);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = numPixels / 8;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		const __m128i c = _mm_load_si128(&srcp[i]);
		__m128i v = _mm_srli_epi16(c, 12);
		v = _mm_or_si128(v, _mm_and_si128(_mm_srli_epi16(c, 4), maskB));
		v = _mm_or_si128(v, _mm_and_si128(_mm_slli_epi16(c, 4), maskG));
		v = _mm_or_si128(v, _mm_slli_epi16(c, 12));
		_mm_store_si128(&dstp[i], v);
	}
	// The remainder is done in chunks of 2, SSE was chunks of 8.
	u32 i = sseChunks * 8 / 2;
#else
	u32 i = 0;
#endif

	const u32 *src32 = (const u32 *)src;
	u32 *dst32 = (u32 *)dst;
	for (; i < numPixels / 2; i++) {
		const u32 c = src32[i];
		dst32[i] = ((c >> 12) & 0x000F000F) |
		           ((c >> 4)  & 0x00F000F0) |
		           ((c << 4)  & 0x0F000F00) |
		           ((c << 12) & 0xF000F000);
	}

	if (numPixels & 1) {
		const u32 i = numPixels - 1;
		const u16 c = src[i];
		dst[i] = ((c >> 12) & 0x000F) |
		         ((c >> 4)  & 0x00F0) |
		         ((c << 4)  & 0x0F00) |
		         ((c << 12) & 0xF000);
	}
}

void ConvertRGBA5551ToABGR1555(u16 *dst, const u16 *src, const u32 numPixels) {
#ifdef _M_SSE
	const __m128i maskB = _mm_set1_epi16(0x003E);
	const __m128i maskG = _mm_set1_epi16(0x07C0);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = numPixels / 8;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		const __m128i c = _mm_load_si128(&srcp[i]);
		__m128i v = _mm_srli_epi16(c, 15);
		v = _mm_or_si128(v, _mm_and_si128(_mm_srli_epi16(c, 9), maskB));
		v = _mm_or_si128(v, _mm_and_si128(_mm_slli_epi16(c, 1), maskG));
		v = _mm_or_si128(v, _mm_slli_epi16(c, 11));
		_mm_store_si128(&dstp[i], v);
	}
	// The remainder is done in chunks of 2, SSE was chunks of 8.
	u32 i = sseChunks * 8 / 2;
#else
	u32 i = 0;
#endif

	const u32 *src32 = (const u32 *)src;
	u32 *dst32 = (u32 *)dst;
	for (; i < numPixels / 2; i++) {
		const u32 c = src32[i];
		dst32[i] = ((c >> 15) & 0x00010001) |
		           ((c >> 9)  & 0x003E003E) |
		           ((c << 1)  & 0x07C007C0) |
		           ((c << 11) & 0xF800F800);
	}

	if (numPixels & 1) {
		const u32 i = numPixels - 1;
		const u16 c = src[i];
		dst[i] = ((c >> 15) & 0x0001) |
		         ((c >> 9)  & 0x003E) |
		         ((c << 1)  & 0x07C0) |
		         ((c << 11) & 0xF800);
	}
}

void ConvertRGB565ToBGR565(u16 *dst, const u16 *src, const u32 numPixels) {
#ifdef _M_SSE
	const __m128i maskG = _mm_set1_epi16(0x07E0);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = numPixels / 8;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		const __m128i c = _mm_load_si128(&srcp[i]);
		__m128i v = _mm_srli_epi16(c, 11);
		v = _mm_or_si128(v, _mm_and_si128(c, maskG));
		v = _mm_or_si128(v, _mm_slli_epi16(c, 11));
		_mm_store_si128(&dstp[i], v);
	}
	// The remainder is done in chunks of 2, SSE was chunks of 8.
	u32 i = sseChunks * 8 / 2;
#else
	u32 i = 0;
#endif

	const u32 *src32 = (const u32 *)src;
	u32 *dst32 = (u32 *)dst;
	for (; i < numPixels / 2; i++) {
		const u32 c = src32[i];
		dst32[i] = ((c >> 11) & 0x001F001F) |
		           ((c >> 0)  & 0x07E007E0) |
		           ((c << 11) & 0xF800F800);
	}

	if (numPixels & 1) {
		const u32 i = numPixels - 1;
		const u16 c = src[i];
		dst[i] = ((c >> 11) & 0x001F) |
		         ((c >> 0)  & 0x07E0) |
		         ((c << 11) & 0xF800);
	}
}
