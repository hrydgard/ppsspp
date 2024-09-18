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

#include "ppsspp_config.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Common.h"
#include "Common/CPUDetect.h"

#ifdef _M_SSE
#include <emmintrin.h>
#include <smmintrin.h>
#endif

#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

void ConvertBGRA8888ToRGBA8888(u32 *dst, const u32 *src, u32 numPixels) {
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

void ConvertBGRA8888ToRGB888(u8 *dst, const u32 *src, u32 numPixels) {
	for (uint32_t x = 0; x < numPixels; ++x) {
		uint32_t c = src[x];
		dst[x * 3 + 0] = (c >> 16) & 0xFF;
		dst[x * 3 + 1] = (c >> 8) & 0xFF;
		dst[x * 3 + 2] = (c >> 0) & 0xFF;
	}
}

#if defined(_M_SSE)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline void ConvertRGBA8888ToRGBA5551_SSE4(__m128i *dstp, const __m128i *srcp, u32 sseChunks) {
	const __m128i maskAG = _mm_set1_epi32(0x8000F800);
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i mask = _mm_set1_epi32(0x0000FFFF);

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
}
#endif

void ConvertRGBA8888ToRGBA5551(u16 *dst, const u32 *src, u32 numPixels) {
#if defined(_M_SSE)
	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = (numPixels / 4) & ~1;
	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF) || !cpu_info.bSSE4_1) {
		sseChunks = 0;
	} else {
		ConvertRGBA8888ToRGBA5551_SSE4(dstp, srcp, sseChunks);
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

#if defined(_M_SSE)
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
static inline void ConvertBGRA8888ToRGBA5551_SSE4(__m128i *dstp, const __m128i *srcp, u32 sseChunks) {
	const __m128i maskAG = _mm_set1_epi32(0x8000F800);
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i mask = _mm_set1_epi32(0x0000FFFF);

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
}
#endif

void ConvertBGRA8888ToRGBA5551(u16 *dst, const u32 *src, u32 numPixels) {
#if defined(_M_SSE)
	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = (numPixels / 4) & ~1;
	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF) || !cpu_info.bSSE4_1) {
		sseChunks = 0;
	} else {
		ConvertBGRA8888ToRGBA5551_SSE4(dstp, srcp, sseChunks);
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

void ConvertBGRA8888ToRGB565(u16 *dst, const u32 *src, u32 numPixels) {
	for (u32 i = 0; i < numPixels; i++) {
		dst[i] = BGRA8888toRGB565(src[i]);
	}
}

void ConvertBGRA8888ToRGBA4444(u16 *dst, const u32 *src, u32 numPixels) {
	for (u32 i = 0; i < numPixels; i++) {
		dst[i] = BGRA8888toRGBA4444(src[i]);
	}
}

void ConvertRGBA8888ToRGB565(u16 *dst, const u32 *src, u32 numPixels) {
	for (u32 x = 0; x < numPixels; ++x) {
		dst[x] = RGBA8888toRGB565(src[x]);
	}
}

void ConvertRGBA8888ToRGBA4444(u16 *dst, const u32 *src, u32 numPixels) {
	for (u32 x = 0; x < numPixels; ++x) {
		dst[x] = RGBA8888toRGBA4444(src[x]);
	}
}

void ConvertRGBA8888ToRGB888(u8 *dst, const u32 *src, u32 numPixels) {
	for (uint32_t x = 0; x < numPixels; ++x) {
		memcpy(dst + x * 3, src + x, 3);
	}
}

void ConvertRGB565ToRGBA8888(u32 *dst32, const u16 *src, u32 numPixels) {
#ifdef _M_SSE
	const __m128i mask5 = _mm_set1_epi16(0x001f);
	const __m128i mask6 = _mm_set1_epi16(0x003f);
	const __m128i mask8 = _mm_set1_epi16(0x00ff);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst32;
	u32 sseChunks = numPixels / 8;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst32 & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		const __m128i c = _mm_load_si128(&srcp[i]);

		// Swizzle, resulting in RR00 RR00.
		__m128i r = _mm_and_si128(c, mask5);
		r = _mm_or_si128(_mm_slli_epi16(r, 3), _mm_srli_epi16(r, 2));
		r = _mm_and_si128(r, mask8);

		// This one becomes 00GG 00GG.
		__m128i g = _mm_and_si128(_mm_srli_epi16(c, 5), mask6);
		g = _mm_or_si128(_mm_slli_epi16(g, 2), _mm_srli_epi16(g, 4));
		g = _mm_slli_epi16(g, 8);

		// Almost done, we aim for BB00 BB00 again here.
		__m128i b = _mm_and_si128(_mm_srli_epi16(c, 11), mask5);
		b = _mm_or_si128(_mm_slli_epi16(b, 3), _mm_srli_epi16(b, 2));
		b = _mm_and_si128(b, mask8);

		// Always set alpha to 00FF 00FF.
		__m128i a = _mm_slli_epi16(mask8, 8);

		// Now combine them, RRGG RRGG and BBAA BBAA, and then interleave.
		const __m128i rg = _mm_or_si128(r, g);
		const __m128i ba = _mm_or_si128(b, a);
		_mm_store_si128(&dstp[i * 2 + 0], _mm_unpacklo_epi16(rg, ba));
		_mm_store_si128(&dstp[i * 2 + 1], _mm_unpackhi_epi16(rg, ba));
	}
	u32 i = sseChunks * 8;
#else
	u32 i = 0;
#endif

	u8 *dst = (u8 *)dst32;
	for (u32 x = i; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col) & 0x1f);
		dst[x * 4 + 1] = Convert6To8((col >> 5) & 0x3f);
		dst[x * 4 + 2] = Convert5To8((col >> 11) & 0x1f);
		dst[x * 4 + 3] = 255;
	}
}

void ConvertRGBA5551ToRGBA8888(u32 *dst32, const u16 *src, u32 numPixels) {
#ifdef _M_SSE
	const __m128i mask5 = _mm_set1_epi16(0x001f);
	const __m128i mask8 = _mm_set1_epi16(0x00ff);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst32;
	u32 sseChunks = numPixels / 8;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst32 & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		const __m128i c = _mm_load_si128(&srcp[i]);

		// Swizzle, resulting in RR00 RR00.
		__m128i r = _mm_and_si128(c, mask5);
		r = _mm_or_si128(_mm_slli_epi16(r, 3), _mm_srli_epi16(r, 2));
		r = _mm_and_si128(r, mask8);

		// This one becomes 00GG 00GG.
		__m128i g = _mm_and_si128(_mm_srli_epi16(c, 5), mask5);
		g = _mm_or_si128(_mm_slli_epi16(g, 3), _mm_srli_epi16(g, 2));
		g = _mm_slli_epi16(g, 8);

		// Almost done, we aim for BB00 BB00 again here.
		__m128i b = _mm_and_si128(_mm_srli_epi16(c, 10), mask5);
		b = _mm_or_si128(_mm_slli_epi16(b, 3), _mm_srli_epi16(b, 2));
		b = _mm_and_si128(b, mask8);

		// 1 bit A to 00AA 00AA.
		__m128i a = _mm_srai_epi16(c, 15);
		a = _mm_slli_epi16(a, 8);

		// Now combine them, RRGG RRGG and BBAA BBAA, and then interleave.
		const __m128i rg = _mm_or_si128(r, g);
		const __m128i ba = _mm_or_si128(b, a);
		_mm_store_si128(&dstp[i * 2 + 0], _mm_unpacklo_epi16(rg, ba));
		_mm_store_si128(&dstp[i * 2 + 1], _mm_unpackhi_epi16(rg, ba));
	}
	u32 i = sseChunks * 8;
#else
	u32 i = 0;
#endif

	u8 *dst = (u8 *)dst32;
	for (u32 x = i; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col) & 0x1f);
		dst[x * 4 + 1] = Convert5To8((col >> 5) & 0x1f);
		dst[x * 4 + 2] = Convert5To8((col >> 10) & 0x1f);
		dst[x * 4 + 3] = (col >> 15) ? 255 : 0;
	}
}

void ConvertRGBA4444ToRGBA8888(u32 *dst32, const u16 *src, u32 numPixels) {
#ifdef _M_SSE
	const __m128i mask4 = _mm_set1_epi16(0x000f);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst32;
	u32 sseChunks = numPixels / 8;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst32 & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		const __m128i c = _mm_load_si128(&srcp[i]);

		// Let's just grab R000 R000, without swizzling yet.
		__m128i r = _mm_and_si128(c, mask4);
		// And then 00G0 00G0.
		__m128i g = _mm_and_si128(_mm_srli_epi16(c, 4), mask4);
		g = _mm_slli_epi16(g, 8);
		// Now B000 B000.
		__m128i b = _mm_and_si128(_mm_srli_epi16(c, 8), mask4);
		// And lastly 00A0 00A0.  No mask needed, we have a wall.
		__m128i a = _mm_srli_epi16(c, 12);
		a = _mm_slli_epi16(a, 8);

		// We swizzle after combining - R0G0 R0G0 and B0A0 B0A0 -> RRGG RRGG and BBAA BBAA.
		__m128i rg = _mm_or_si128(r, g);
		__m128i ba = _mm_or_si128(b, a);
		rg = _mm_or_si128(rg, _mm_slli_epi16(rg, 4));
		ba = _mm_or_si128(ba, _mm_slli_epi16(ba, 4));

		// And then we can store.
		_mm_store_si128(&dstp[i * 2 + 0], _mm_unpacklo_epi16(rg, ba));
		_mm_store_si128(&dstp[i * 2 + 1], _mm_unpackhi_epi16(rg, ba));
	}
	u32 i = sseChunks * 8;
#else
	u32 i = 0;
#endif

	u8 *dst = (u8 *)dst32;
	for (u32 x = i; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert4To8(col & 0xf);
		dst[x * 4 + 1] = Convert4To8((col >> 4) & 0xf);
		dst[x * 4 + 2] = Convert4To8((col >> 8) & 0xf);
		dst[x * 4 + 3] = Convert4To8(col >> 12);
	}
}

void ConvertBGR565ToRGBA8888(u32 *dst32, const u16 *src, u32 numPixels) {
	u8 *dst = (u8 *)dst32;
	for (u32 x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col >> 11) & 0x1f);
		dst[x * 4 + 1] = Convert6To8((col >> 5) & 0x3f);
		dst[x * 4 + 2] = Convert5To8((col) & 0x1f);
		dst[x * 4 + 3] = 255;
	}
}

void ConvertABGR1555ToRGBA8888(u32 *dst32, const u16 *src, u32 numPixels) {
	u8 *dst = (u8 *)dst32;
	for (u32 x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert5To8((col >> 11) & 0x1f);
		dst[x * 4 + 1] = Convert5To8((col >> 6) & 0x1f);
		dst[x * 4 + 2] = Convert5To8((col >> 1) & 0x1f);
		dst[x * 4 + 3] = (col & 1) ? 255 : 0;
	}
}

void ConvertABGR4444ToRGBA8888(u32 *dst32, const u16 *src, u32 numPixels) {
	u8 *dst = (u8 *)dst32;
	for (u32 x = 0; x < numPixels; x++) {
		u16 col = src[x];
		dst[x * 4] = Convert4To8(col >> 12);
		dst[x * 4 + 1] = Convert4To8((col >> 8) & 0xf);
		dst[x * 4 + 2] = Convert4To8((col >> 4) & 0xf);
		dst[x * 4 + 3] = Convert4To8(col & 0xf);
	}
}

void ConvertRGBA4444ToBGRA8888(u32 *dst, const u16 *src, u32 numPixels) {
	for (u32 x = 0; x < numPixels; x++) {
		u16 c = src[x];
		u32 r = Convert4To8(c & 0x000f);
		u32 g = Convert4To8((c >> 4) & 0x000f);
		u32 b = Convert4To8((c >> 8) & 0x000f);
		u32 a = Convert4To8((c >> 12) & 0x000f);

		dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
	}
}

void ConvertRGBA5551ToBGRA8888(u32 *dst, const u16 *src, u32 numPixels) {
	for (u32 x = 0; x < numPixels; x++) {
		u16 c = src[x];
		u32 r = Convert5To8(c & 0x001f);
		u32 g = Convert5To8((c >> 5) & 0x001f);
		u32 b = Convert5To8((c >> 10) & 0x001f);
		// We force an arithmetic shift to get the sign bits.
		u32 a = SignExtend16ToU32(c) & 0xff000000;

		dst[x] = a | (r << 16) | (g << 8) | b;
	}
}

void ConvertRGB565ToBGRA8888(u32 *dst, const u16 *src, u32 numPixels) {
	for (u32 x = 0; x < numPixels; x++) {
		u16 c = src[x];
		u32 r = Convert5To8(c & 0x001f);
		u32 g = Convert6To8((c >> 5) & 0x003f);
		u32 b = Convert5To8((c >> 11) & 0x001f);

		dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
	}
}

void ConvertRGBA4444ToABGR4444(u16 *dst, const u16 *src, u32 numPixels) {
#ifdef _M_SSE
	const __m128i mask0040 = _mm_set1_epi16(0x00F0);

	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = numPixels / 8;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	}
	for (u32 i = 0; i < sseChunks; ++i) {
		const __m128i c = _mm_load_si128(&srcp[i]);
		__m128i v = _mm_srli_epi16(c, 12);
		v = _mm_or_si128(v, _mm_and_si128(_mm_srli_epi16(c, 4), mask0040));
		v = _mm_or_si128(v, _mm_slli_epi16(_mm_and_si128(c, mask0040), 4));
		v = _mm_or_si128(v, _mm_slli_epi16(c, 12));
		_mm_store_si128(&dstp[i], v);
	}
	// The remainder is done in chunks of 2, SSE was chunks of 8.
	u32 i = sseChunks * 8 / 2;
#elif PPSSPP_ARCH(ARM_NEON)
	const uint16x8_t mask0040 = vdupq_n_u16(0x00F0);

	if (((uintptr_t)dst & 15) == 0 && ((uintptr_t)src & 15) == 0) {
		u32 simdable = (numPixels / 8) * 8;
		for (u32 i = 0; i < simdable; i += 8) {
			uint16x8_t c = vld1q_u16(src);

			const uint16x8_t a = vshrq_n_u16(c, 12);
			const uint16x8_t b = vandq_u16(vshrq_n_u16(c, 4), mask0040);
			const uint16x8_t g = vshlq_n_u16(vandq_u16(c, mask0040), 4);
			const uint16x8_t r = vshlq_n_u16(c, 12);

			uint16x8_t res = vorrq_u16(vorrq_u16(r, g), vorrq_u16(b, a));
			vst1q_u16(dst, res);

			src += 8;
			dst += 8;
		}
		numPixels -= simdable;
	}
	u32 i = 0;  // already moved the pointers forward
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

void ConvertRGBA5551ToABGR1555(u16 *dst, const u16 *src, u32 numPixels) {
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
#elif PPSSPP_ARCH(ARM_NEON)
	const uint16x8_t maskB = vdupq_n_u16(0x003E);
	const uint16x8_t maskG = vdupq_n_u16(0x07C0);

	if (((uintptr_t)dst & 15) == 0 && ((uintptr_t)src & 15) == 0) {
		u32 simdable = (numPixels / 8) * 8;
		for (u32 i = 0; i < simdable; i += 8) {
			uint16x8_t c = vld1q_u16(src);

			const uint16x8_t a = vshrq_n_u16(c, 15);
			const uint16x8_t b = vandq_u16(vshrq_n_u16(c, 9), maskB);
			const uint16x8_t g = vandq_u16(vshlq_n_u16(c, 1), maskG);
			const uint16x8_t r = vshlq_n_u16(c, 11);

			uint16x8_t res = vorrq_u16(vorrq_u16(r, g), vorrq_u16(b, a));
			vst1q_u16(dst, res);

			src += 8;
			dst += 8;
		}
		numPixels -= simdable;
	}
	u32 i = 0;
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

void ConvertRGB565ToBGR565(u16 *dst, const u16 *src, u32 numPixels) {
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
#elif PPSSPP_ARCH(ARM_NEON)
	const uint16x8_t maskG = vdupq_n_u16(0x07E0);

	if (((uintptr_t)dst & 15) == 0 && ((uintptr_t)src & 15) == 0) {
		u32 simdable = (numPixels / 8) * 8;
		for (u32 i = 0; i < simdable; i += 8) {
			uint16x8_t c = vld1q_u16(src);

			const uint16x8_t b = vshrq_n_u16(c, 11);
			const uint16x8_t g = vandq_u16(c, maskG);
			const uint16x8_t r = vshlq_n_u16(c, 11);

			uint16x8_t res = vorrq_u16(vorrq_u16(r, g), b);
			vst1q_u16(dst, res);

			src += 8;
			dst += 8;
		}
		numPixels -= simdable;
	}

	u32 i = 0;
#else
	u32 i = 0;
#endif

	// TODO: Add a 64-bit loop too.
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

void ConvertBGRA5551ToABGR1555(u16 *dst, const u16 *src, u32 numPixels) {
	const u32 *src32 = (const u32 *)src;
	u32 *dst32 = (u32 *)dst;
	for (u32 i = 0; i < numPixels / 2; i++) {
		const u32 c = src32[i];
		dst32[i] = ((c >> 15) & 0x00010001) | ((c << 1) & 0xFFFEFFFE);
	}

	if (numPixels & 1) {
		const u32 i = numPixels - 1;
		const u16 c = src[i];
		dst[i] = (c >> 15) | (c << 1);
	}
}
