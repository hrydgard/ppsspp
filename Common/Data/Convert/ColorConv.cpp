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
#include "Common/Math/SIMDHeaders.h"

void ConvertBGRA8888ToRGBA8888(u32 *dst, const u32 *src, u32 numPixels) {
#if PPSSPP_ARCH(SSE2)
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

#if PPSSPP_ARCH(SSE2)
// fp64's improved SSE2 version, see #19751. SSE4 no longer required here.
static inline void ConvertRGBA8888ToRGBA5551(__m128i *dstp, const __m128i *srcp, u32 sseChunks) {
	const __m128i maskRB = _mm_set1_epi32(0x00F800F8);
	const __m128i maskGA = _mm_set1_epi32(0x8000F800);
	const __m128i mulRB = _mm_set1_epi32(0x04000001);
	const __m128i mulGA = _mm_set1_epi32(0x00400001);

	for (u32 i = 0; i < sseChunks; i += 2) {
		__m128i c0 = _mm_load_si128(&srcp[i + 0]);
		__m128i c1 = _mm_load_si128(&srcp[i + 1]);

		__m128i rb0 = _mm_and_si128(c0, maskRB);              // 00000000bbbbb00000000000rrrrr000 (each 32-bit lane)
		__m128i rb1 = _mm_and_si128(c1, maskRB);              // 00000000bbbbb00000000000rrrrr000
		__m128i ga0 = _mm_and_si128(c0, maskGA);              // a000000000000000ggggg00000000000
		__m128i ga1 = _mm_and_si128(c1, maskGA);              // a000000000000000ggggg00000000000
		rb0 = _mm_madd_epi16(_mm_srli_epi32(rb0,  3), mulRB); // 00000000000000000bbbbb00000rrrrr
		rb1 = _mm_madd_epi16(_mm_srli_epi32(rb1,  3), mulRB); // 00000000000000000bbbbb00000rrrrr
		ga0 = _mm_madd_epi16(_mm_srli_epi32(ga0, 11), mulGA); // 000000000000000000000a00000ggggg
		ga1 = _mm_madd_epi16(_mm_srli_epi32(ga1, 11), mulGA); // 000000000000000000000a00000ggggg
		__m128i rb = _mm_packs_epi32(rb0, rb1);
		__m128i ga = _mm_slli_epi32(_mm_packs_epi32(ga0, ga1), 5);

		_mm_store_si128(&dstp[i / 2], _mm_or_si128(ga, rb));
	}
}
#endif

void ConvertRGBA8888ToRGBA5551(u16 *dst, const u32 *src, u32 numPixels) {
#if PPSSPP_ARCH(SSE2)
	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = (numPixels / 4) & ~1;
	// SSE 4.1 required for _mm_packus_epi32.
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	} else {
		ConvertRGBA8888ToRGBA5551(dstp, srcp, sseChunks);
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

#if PPSSPP_ARCH(SSE2)
/*
#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
*/
static inline void ConvertBGRA8888ToRGBA5551(__m128i *dstp, const __m128i *srcp, u32 sseChunks) {
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

		// Unfortunately no good SSE2 way to do _mm_packus_epi32.
		// We can approximate it with a few shuffles.
#if 0
		_mm_store_si128(&dstp[i / 2], _mm_packus_epi32(c1, c2));
#else
		// SSE2 path.
		_mm_store_si128(&dstp[i / 2], _mm_packu2_epi32_SSE2(c1, c2));
#endif
	}
}
#endif

void ConvertBGRA8888ToRGBA5551(u16 *dst, const u32 *src, u32 numPixels) {
#if defined(_M_SSE)
	const __m128i *srcp = (const __m128i *)src;
	__m128i *dstp = (__m128i *)dst;
	u32 sseChunks = (numPixels / 4) & ~1;
	if (((intptr_t)src & 0xF) || ((intptr_t)dst & 0xF)) {
		sseChunks = 0;
	} else {
		ConvertBGRA8888ToRGBA5551(dstp, srcp, sseChunks);
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
#if PPSSPP_ARCH(SSE2)
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
#if PPSSPP_ARCH(SSE2)
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
#if PPSSPP_ARCH(SSE2)
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

static inline u32 premul_pixel_scalar(u32 px) {
	u32 r = (px) & 0xFFu;
	u32 g = (px >> 8) & 0xFFu;
	u32 b = (px >> 16) & 0xFFu;
	u32 a = (px >> 24) & 0xFFu;

	if (a == 255) return px; // already fully opaque
	if (a == 0)  return (a << 24); // transparent (r,g,b = 0)

	// Use (c*a + 128) * 257 >> 16  to approximate (c*a)/255 with good rounding
	u32 ra = ((r * a + 128) * 257) >> 16;
	u32 ga = ((g * a + 128) * 257) >> 16;
	u32 ba = ((b * a + 128) * 257) >> 16;

	return (a << 24) | (ba << 16) | (ga << 8) | ra;
}

void ConvertRGBA8888ToPremulAlpha(u32 *dst, const u32 *src, u32 numPixels) {
	if (!dst || !src || numPixels == 0)
		return;

	u32 i = 0;

#if defined(__SSE2__)
	// SSE2 path: process 4 pixels at a time (16 bytes)
	const u32 stride = 4;
	const u32 vecCount = numPixels / stride;

	// constants
	const __m128i zero8 = _mm_setzero_si128();
	const __m128i const128_16 = _mm_set1_epi16((short)128); // for adding 128 (16-bit lanes)
	const __m128i mul257_32 = _mm_set1_epi32(257); // multiply 32-bit by 257
	const __m128i alphaMask = _mm_set1_epi32(0xFF000000u);
	for (; i + 3 < numPixels; i += 4) {
		__m128i px = _mm_loadu_si128((const __m128i*)(src + i));

		u32 tmpu[4];
		_mm_storeu_si128((__m128i*)tmpu, px);

		const int a0 = (tmpu[0] >> 24) & 0xFF;
		const int a1 = (tmpu[1] >> 24) & 0xFF;
		const int a2 = (tmpu[2] >> 24) & 0xFF;
		const int a3 = (tmpu[3] >> 24) & 0xFF;

		__m128i alpha_lo16 = _mm_set_epi16((short)a1, (short)a1, (short)a1, (short)a1,
			(short)a0, (short)a0, (short)a0, (short)a0);
		__m128i alpha_hi16 = _mm_set_epi16((short)a3, (short)a3, (short)a3, (short)a3,
			(short)a2, (short)a2, (short)a2, (short)a2);

		__m128i zero8 = _mm_setzero_si128();

		__m128i lo16 = _mm_unpacklo_epi8(px, zero8);
		__m128i hi16 = _mm_unpackhi_epi8(px, zero8);

		__m128i prod_lo = _mm_mullo_epi16(lo16, alpha_lo16);
		__m128i prod_hi = _mm_mullo_epi16(hi16, alpha_hi16);

		// widen to 32-bit
		__m128i prod_lo_0 = _mm_unpacklo_epi16(prod_lo, zero8); // 4x32
		__m128i prod_lo_1 = _mm_unpackhi_epi16(prod_lo, zero8);
		__m128i prod_hi_0 = _mm_unpacklo_epi16(prod_hi, zero8);
		__m128i prod_hi_1 = _mm_unpackhi_epi16(prod_hi, zero8);

		// tmp = prod + 128
		__m128i c128_32 = _mm_set1_epi32(128);
		prod_lo_0 = _mm_add_epi32(prod_lo_0, c128_32);
		prod_lo_1 = _mm_add_epi32(prod_lo_1, c128_32);
		prod_hi_0 = _mm_add_epi32(prod_hi_0, c128_32);
		prod_hi_1 = _mm_add_epi32(prod_hi_1, c128_32);

		// multiply by 257 using (x + (x << 8))  (SSE2-only)
		__m128i tmp0 = _mm_add_epi32(prod_lo_0, _mm_slli_epi32(prod_lo_0, 8));
		__m128i tmp1 = _mm_add_epi32(prod_lo_1, _mm_slli_epi32(prod_lo_1, 8));
		__m128i tmp2 = _mm_add_epi32(prod_hi_0, _mm_slli_epi32(prod_hi_0, 8));
		__m128i tmp3 = _mm_add_epi32(prod_hi_1, _mm_slli_epi32(prod_hi_1, 8));

		// >> 16
		tmp0 = _mm_srli_epi32(tmp0, 16);
		tmp1 = _mm_srli_epi32(tmp1, 16);
		tmp2 = _mm_srli_epi32(tmp2, 16);
		tmp3 = _mm_srli_epi32(tmp3, 16);

		// pack back to 16-bit
		__m128i res_lo16 = _mm_packs_epi32(tmp0, tmp1);
		__m128i res_hi16 = _mm_packs_epi32(tmp2, tmp3);

		// pack to bytes
		__m128i outBytes = _mm_packus_epi16(res_lo16, res_hi16);

		// keep original alpha
		const __m128i alphaMask = _mm_set1_epi32(0xFF000000u);
		__m128i origAlpha = _mm_and_si128(px, alphaMask);
		__m128i outNoAlpha = _mm_andnot_si128(alphaMask, outBytes);
		__m128i finalOut = _mm_or_si128(outNoAlpha, origAlpha);

		_mm_storeu_si128((__m128i*)(dst + i), finalOut);
	}
#endif // __SSE2__

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
	// NEON path: process 4 pixels (16 bytes) per iteration
	for (; i + 3 < numPixels; i += 4)
	{
		// load 16 bytes as uint8x16_t
		uint8x16_t v = vld1q_u8((const uint8_t*)(src + i)); // bytes: R0,G0,B0,A0,R1,G1,B1,A1,...

		// widen to 16-bit lanes (two halves)
		uint16x8_t lo16 = vmovl_u8(vget_low_u8(v));  // first 8 bytes -> 8 x u16
		uint16x8_t hi16 = vmovl_u8(vget_high_u8(v)); // last 8 bytes -> 8 x u16

		// Extract alpha bytes (one per pixel) into ints
		// Using vgetq_lane on shifted/aliased values is simpler here
		uint32_t tmp[4];
		vst1q_u32(tmp, vreinterpretq_u32_u8(v)); // store as 4 x u32
		const uint16_t a0 = (tmp[0] >> 24) & 0xFFu;
		const uint16_t a1 = (tmp[1] >> 24) & 0xFFu;
		const uint16_t a2 = (tmp[2] >> 24) & 0xFFu;
		const uint16_t a3 = (tmp[3] >> 24) & 0xFFu;

		// Build alpha 16-bit vectors that match lo16 and hi16 ordering:
		// lo16 lanes: R0,G0,B0,A0, R1,G1,B1,A1 -> need [a0,a0,a0,a0,a1,a1,a1,a1]
		uint16x8_t alpha_lo16 = {a0, a0, a0, a0, a1, a1, a1, a1};
		// hi16 lanes: R2,G2,B2,A2, R3,G3,B3,A3 -> [a2,a2,a2,a2,a3,a3,a3,a3]
		uint16x8_t alpha_hi16 = {a2, a2, a2, a2, a3, a3, a3, a3};

		// multiply 16-bit lanes
		uint16x8_t prod_lo = vmulq_u16(lo16, alpha_lo16);
		uint16x8_t prod_hi = vmulq_u16(hi16, alpha_hi16);

		// compute (prod + 128) * 257 >> 16 per lane:
		// widen to 32-bit and do the math
		uint32x4_t p0 = vmovl_u16(vget_low_u16(prod_lo));  // first 4
		uint32x4_t p1 = vmovl_u16(vget_high_u16(prod_lo)); // next 4
		uint32x4_t p2 = vmovl_u16(vget_low_u16(prod_hi));
		uint32x4_t p3 = vmovl_u16(vget_high_u16(prod_hi));

		const uint32x4_t c128 = vdupq_n_u32(128);
		const uint32x4_t c257 = vdupq_n_u32(257);

		p0 = vmulq_u32(vaddq_u32(p0, c128), c257);
		p1 = vmulq_u32(vaddq_u32(p1, c128), c257);
		p2 = vmulq_u32(vaddq_u32(p2, c128), c257);
		p3 = vmulq_u32(vaddq_u32(p3, c128), c257);

		p0 = vshrq_n_u32(p0, 16);
		p1 = vshrq_n_u32(p1, 16);
		p2 = vshrq_n_u32(p2, 16);
		p3 = vshrq_n_u32(p3, 16);

		// narrow back to 16-bit
		uint16x8_t r_lo = vcombine_u16(vqmovn_u32(p0), vqmovn_u32(p1));
		uint16x8_t r_hi = vcombine_u16(vqmovn_u32(p2), vqmovn_u32(p3));

		// narrow to bytes
		uint8x16_t out = vcombine_u8(vqmovn_u16(r_lo), vqmovn_u16(r_hi));

		// preserve original alpha bytes: mask and combine
		uint8x16_t alpha_mask = {0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF};
		// above initializer may not be supported by all compilers; use bitwise ops:
		uint8x16_t orig_alpha = vandq_u8(v, vdupq_n_u8(0xFF)); // not correct; keep simpler:
		// easier: compute orig alpha bytes by shifting each 32-bit lane >> 24 and replicating into byte positions
		// Do a simple scalar replacement for alpha bytes (straightforward and cheap)
		uint8_t out_bytes[16];
		vst1q_u8(out_bytes, out);
		uint32_t orig32[4];
		vst1q_u32(orig32, vreinterpretq_u32_u8(v));
		for (int p = 0; p < 4; ++p) {
			uint8_t alpha = (orig32[p] >> 24) & 0xFFu;
			out_bytes[p * 4 + 3] = alpha;
		}
		vst1q_u8((uint8_t*)(dst + i), vld1q_u8(out_bytes));
	}
#endif // NEON

	// Scalar fallback for remaining pixels (or if above SIMD not present)
	for (; i < numPixels; ++i) {
		dst[i] = premul_pixel_scalar(src[i]);
	}
}
