// Copyright (c) 2026- PPSSPP Project.

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

// The Media Engine's YCbCr to RGB conversion over a run of a row, 16 pixels at a time with SSE2
// or NEON, bit for bit what the scalar conversion gives:
//   r = (298 (y - 16) + 409 (cr - 128) + 128) >> 8
//   g = (298 (y - 16) - 100 (cb - 128) - 208 (cr - 128) + 128) >> 8
//   b = (298 (y - 16) + 516 (cb - 128) + 128) >> 8
// each clamped to 0..255, then packed with alpha zero. Kept to C so it can be tested on its own.
//
// yrow holds the run's luma, starting on the first pixel of a chroma sample; cbrow and crrow its
// chroma. Returns how many pixels it did, a multiple of 16; the caller does the rest.

#include <stdint.h>
#include <string.h>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define MPEGCSC_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#define MPEGCSC_NEON 1
#endif

enum {
	MPEGCSC_5650 = 0,
	MPEGCSC_5551 = 1,
	MPEGCSC_4444 = 2,
	MPEGCSC_8888 = 3,
};

#if defined(MPEGCSC_SSE2)

// Four 32-bit sums, shifted, for 4 pixels each in a..d, to 16 clamped bytes.
static inline __m128i MpegCsc_Clamp16(__m128i a, __m128i b, __m128i c, __m128i d) {
	return _mm_packus_epi16(_mm_packs_epi32(_mm_srai_epi32(a, 8), _mm_srai_epi32(b, 8)),
		_mm_packs_epi32(_mm_srai_epi32(c, 8), _mm_srai_epi32(d, 8)));
}

static inline int MpegCscRow(uint8_t *out, int mode, const uint8_t *yrow, const uint8_t *cbrow,
	const uint8_t *crrow, int n) {
	const __m128i zero = _mm_setzero_si128();
	const __m128i k16 = _mm_set1_epi16(16), k128 = _mm_set1_epi16(128);
	const __m128i k298 = _mm_set1_epi16(298), round = _mm_set1_epi32(128);
	// (cb, cr) pairs against these: 409 cr, -100 cb - 208 cr, 516 cb.
	const __m128i kr = _mm_set1_epi32(409 << 16), kg = _mm_set1_epi32((int)(((uint32_t)(uint16_t)-208 << 16) | (uint16_t)-100));
	const __m128i kb = _mm_set1_epi32(516);
	int x;
	for (x = 0; x + 16 <= n; x += 16) {
		__m128i yv = _mm_loadu_si128((const __m128i *)(yrow + x));
		__m128i cbv = _mm_loadl_epi64((const __m128i *)(cbrow + x / 2));
		__m128i crv = _mm_loadl_epi64((const __m128i *)(crrow + x / 2));
		__m128i ylo = _mm_sub_epi16(_mm_unpacklo_epi8(yv, zero), k16);
		__m128i yhi = _mm_sub_epi16(_mm_unpackhi_epi8(yv, zero), k16);
		__m128i plo = _mm_mullo_epi16(ylo, k298), phi = _mm_mulhi_epi16(ylo, k298);
		__m128i qlo = _mm_mullo_epi16(yhi, k298), qhi = _mm_mulhi_epi16(yhi, k298);
		// 298 (y - 16) + 128 for the 16 pixels, four at a time.
		__m128i y0 = _mm_add_epi32(_mm_unpacklo_epi16(plo, phi), round);
		__m128i y1 = _mm_add_epi32(_mm_unpackhi_epi16(plo, phi), round);
		__m128i y2 = _mm_add_epi32(_mm_unpacklo_epi16(qlo, qhi), round);
		__m128i y3 = _mm_add_epi32(_mm_unpackhi_epi16(qlo, qhi), round);
		__m128i d = _mm_sub_epi16(_mm_unpacklo_epi8(cbv, zero), k128);
		__m128i e = _mm_sub_epi16(_mm_unpacklo_epi8(crv, zero), k128);
		__m128i de0 = _mm_unpacklo_epi16(d, e), de1 = _mm_unpackhi_epi16(d, e);
		__m128i r0 = _mm_madd_epi16(de0, kr), r1 = _mm_madd_epi16(de1, kr);
		__m128i g0 = _mm_madd_epi16(de0, kg), g1 = _mm_madd_epi16(de1, kg);
		__m128i b0 = _mm_madd_epi16(de0, kb), b1 = _mm_madd_epi16(de1, kb);
		// Each chroma term twice, for its two pixels.
		__m128i r = MpegCsc_Clamp16(_mm_add_epi32(y0, _mm_unpacklo_epi32(r0, r0)), _mm_add_epi32(y1, _mm_unpackhi_epi32(r0, r0)),
			_mm_add_epi32(y2, _mm_unpacklo_epi32(r1, r1)), _mm_add_epi32(y3, _mm_unpackhi_epi32(r1, r1)));
		__m128i g = MpegCsc_Clamp16(_mm_add_epi32(y0, _mm_unpacklo_epi32(g0, g0)), _mm_add_epi32(y1, _mm_unpackhi_epi32(g0, g0)),
			_mm_add_epi32(y2, _mm_unpacklo_epi32(g1, g1)), _mm_add_epi32(y3, _mm_unpackhi_epi32(g1, g1)));
		__m128i b = MpegCsc_Clamp16(_mm_add_epi32(y0, _mm_unpacklo_epi32(b0, b0)), _mm_add_epi32(y1, _mm_unpackhi_epi32(b0, b0)),
			_mm_add_epi32(y2, _mm_unpacklo_epi32(b1, b1)), _mm_add_epi32(y3, _mm_unpackhi_epi32(b1, b1)));
		if (mode == MPEGCSC_8888) {
			__m128i rglo = _mm_unpacklo_epi8(r, g), rghi = _mm_unpackhi_epi8(r, g);
			__m128i b0lo = _mm_unpacklo_epi8(b, zero), b0hi = _mm_unpackhi_epi8(b, zero);
			uint8_t *o = out + (size_t)x * 4;
			_mm_storeu_si128((__m128i *)o, _mm_unpacklo_epi16(rglo, b0lo));
			_mm_storeu_si128((__m128i *)(o + 16), _mm_unpackhi_epi16(rglo, b0lo));
			_mm_storeu_si128((__m128i *)(o + 32), _mm_unpacklo_epi16(rghi, b0hi));
			_mm_storeu_si128((__m128i *)(o + 48), _mm_unpackhi_epi16(rghi, b0hi));
		} else {
			__m128i rl = _mm_unpacklo_epi8(r, zero), rh = _mm_unpackhi_epi8(r, zero);
			__m128i gl = _mm_unpacklo_epi8(g, zero), gh = _mm_unpackhi_epi8(g, zero);
			__m128i bl = _mm_unpacklo_epi8(b, zero), bh = _mm_unpackhi_epi8(b, zero);
			__m128i lo, hi;
			if (mode == MPEGCSC_5650) {
				const __m128i m5 = _mm_set1_epi16(0xF8), m6 = _mm_set1_epi16(0xFC);
				lo = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(_mm_and_si128(bl, m5), 8), _mm_slli_epi16(_mm_and_si128(gl, m6), 3)), _mm_srli_epi16(rl, 3));
				hi = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(_mm_and_si128(bh, m5), 8), _mm_slli_epi16(_mm_and_si128(gh, m6), 3)), _mm_srli_epi16(rh, 3));
			} else if (mode == MPEGCSC_5551) {
				const __m128i m5 = _mm_set1_epi16(0xF8);
				lo = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(_mm_and_si128(bl, m5), 7), _mm_slli_epi16(_mm_and_si128(gl, m5), 2)), _mm_srli_epi16(rl, 3));
				hi = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(_mm_and_si128(bh, m5), 7), _mm_slli_epi16(_mm_and_si128(gh, m5), 2)), _mm_srli_epi16(rh, 3));
			} else {
				const __m128i m4 = _mm_set1_epi16(0xF0);
				lo = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(_mm_and_si128(bl, m4), 4), _mm_and_si128(gl, m4)), _mm_srli_epi16(rl, 4));
				hi = _mm_or_si128(_mm_or_si128(_mm_slli_epi16(_mm_and_si128(bh, m4), 4), _mm_and_si128(gh, m4)), _mm_srli_epi16(rh, 4));
			}
			_mm_storeu_si128((__m128i *)(out + (size_t)x * 2), lo);
			_mm_storeu_si128((__m128i *)(out + (size_t)x * 2 + 16), hi);
		}
	}
	return x;
}

#elif defined(MPEGCSC_NEON)

// 32-bit sums for 4 pixels each, shifted, to 8 clamped bytes.
static inline uint8x8_t MpegCsc_Clamp8(int32x4_t a, int32x4_t b) {
	return vqmovun_s16(vcombine_s16(vqshrn_n_s32(a, 8), vqshrn_n_s32(b, 8)));
}

static inline int MpegCscRow(uint8_t *out, int mode, const uint8_t *yrow, const uint8_t *cbrow,
	const uint8_t *crrow, int n) {
	const int16x8_t k16 = vdupq_n_s16(16), k128 = vdupq_n_s16(128);
	const int32x4_t round = vdupq_n_s32(128);
	int x;
	for (x = 0; x + 16 <= n; x += 16) {
		uint8x16_t yv = vld1q_u8(yrow + x);
		int16x8_t ylo = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(yv))), k16);
		int16x8_t yhi = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(yv))), k16);
		int32x4_t y0 = vmlal_n_s16(round, vget_low_s16(ylo), 298);
		int32x4_t y1 = vmlal_n_s16(round, vget_high_s16(ylo), 298);
		int32x4_t y2 = vmlal_n_s16(round, vget_low_s16(yhi), 298);
		int32x4_t y3 = vmlal_n_s16(round, vget_high_s16(yhi), 298);
		int16x8_t d = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(cbrow + x / 2))), k128);
		int16x8_t e = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(crrow + x / 2))), k128);
		int32x4_t r0 = vmull_n_s16(vget_low_s16(e), 409), r1 = vmull_n_s16(vget_high_s16(e), 409);
		int32x4_t g0 = vmlal_n_s16(vmull_n_s16(vget_low_s16(d), -100), vget_low_s16(e), -208);
		int32x4_t g1 = vmlal_n_s16(vmull_n_s16(vget_high_s16(d), -100), vget_high_s16(e), -208);
		int32x4_t b0 = vmull_n_s16(vget_low_s16(d), 516), b1 = vmull_n_s16(vget_high_s16(d), 516);
		// Each chroma term twice, for its two pixels.
		int32x4x2_t rz0 = vzipq_s32(r0, r0), rz1 = vzipq_s32(r1, r1);
		int32x4x2_t gz0 = vzipq_s32(g0, g0), gz1 = vzipq_s32(g1, g1);
		int32x4x2_t bz0 = vzipq_s32(b0, b0), bz1 = vzipq_s32(b1, b1);
		uint8x16_t r = vcombine_u8(MpegCsc_Clamp8(vaddq_s32(y0, rz0.val[0]), vaddq_s32(y1, rz0.val[1])),
			MpegCsc_Clamp8(vaddq_s32(y2, rz1.val[0]), vaddq_s32(y3, rz1.val[1])));
		uint8x16_t g = vcombine_u8(MpegCsc_Clamp8(vaddq_s32(y0, gz0.val[0]), vaddq_s32(y1, gz0.val[1])),
			MpegCsc_Clamp8(vaddq_s32(y2, gz1.val[0]), vaddq_s32(y3, gz1.val[1])));
		uint8x16_t b = vcombine_u8(MpegCsc_Clamp8(vaddq_s32(y0, bz0.val[0]), vaddq_s32(y1, bz0.val[1])),
			MpegCsc_Clamp8(vaddq_s32(y2, bz1.val[0]), vaddq_s32(y3, bz1.val[1])));
		if (mode == MPEGCSC_8888) {
			uint8x16x4_t px;
			px.val[0] = r;
			px.val[1] = g;
			px.val[2] = b;
			px.val[3] = vdupq_n_u8(0);
			vst4q_u8(out + (size_t)x * 4, px);
		} else {
			uint16x8_t rl = vmovl_u8(vget_low_u8(r)), rh = vmovl_u8(vget_high_u8(r));
			uint16x8_t gl = vmovl_u8(vget_low_u8(g)), gh = vmovl_u8(vget_high_u8(g));
			uint16x8_t bl = vmovl_u8(vget_low_u8(b)), bh = vmovl_u8(vget_high_u8(b));
			uint16x8_t lo, hi;
			if (mode == MPEGCSC_5650) {
				const uint16x8_t m5 = vdupq_n_u16(0xF8), m6 = vdupq_n_u16(0xFC);
				lo = vorrq_u16(vorrq_u16(vshlq_n_u16(vandq_u16(bl, m5), 8), vshlq_n_u16(vandq_u16(gl, m6), 3)), vshrq_n_u16(rl, 3));
				hi = vorrq_u16(vorrq_u16(vshlq_n_u16(vandq_u16(bh, m5), 8), vshlq_n_u16(vandq_u16(gh, m6), 3)), vshrq_n_u16(rh, 3));
			} else if (mode == MPEGCSC_5551) {
				const uint16x8_t m5 = vdupq_n_u16(0xF8);
				lo = vorrq_u16(vorrq_u16(vshlq_n_u16(vandq_u16(bl, m5), 7), vshlq_n_u16(vandq_u16(gl, m5), 2)), vshrq_n_u16(rl, 3));
				hi = vorrq_u16(vorrq_u16(vshlq_n_u16(vandq_u16(bh, m5), 7), vshlq_n_u16(vandq_u16(gh, m5), 2)), vshrq_n_u16(rh, 3));
			} else {
				const uint16x8_t m4 = vdupq_n_u16(0xF0);
				lo = vorrq_u16(vorrq_u16(vshlq_n_u16(vandq_u16(bl, m4), 4), vandq_u16(gl, m4)), vshrq_n_u16(rl, 4));
				hi = vorrq_u16(vorrq_u16(vshlq_n_u16(vandq_u16(bh, m4), 4), vandq_u16(gh, m4)), vshrq_n_u16(rh, 4));
			}
			vst1q_u8(out + (size_t)x * 2, vreinterpretq_u8_u16(lo));
			vst1q_u8(out + (size_t)x * 2 + 16, vreinterpretq_u8_u16(hi));
		}
	}
	return x;
}

#else

static inline int MpegCscRow(uint8_t *out, int mode, const uint8_t *yrow, const uint8_t *cbrow,
	const uint8_t *crrow, int n) {
	(void)out; (void)mode; (void)yrow; (void)cbrow; (void)crrow; (void)n;
	return 0;
}

#endif
