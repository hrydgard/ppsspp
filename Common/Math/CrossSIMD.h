// CrossSIMD
//
// Compatibility wrappers for SIMD dialects.
//
// In the long run, might do a more general single-source-SIMD wrapper here consisting
// of defines that translate to either NEON or SSE. It would be possible to write quite a lot of
// our various color conversion functions and so on in a pretty generic manner.

#pragma once

#include "ppsspp_config.h"

#include "stdint.h"

#ifdef __clang__
// Weird how you can't just use #pragma in a macro.
#define DO_NOT_VECTORIZE_LOOP _Pragma("clang loop vectorize(disable)")
#else
#define DO_NOT_VECTORIZE_LOOP
#endif

#if PPSSPP_ARCH(SSE2)
#include <emmintrin.h>
#ifdef __SSE4_1__
#include <smmintrin.h>
#endif
#endif

#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

// Basic types

#if PPSSPP_ARCH(ARM64_NEON)

// No special ones here.

#elif PPSSPP_ARCH(ARM_NEON)

// Compatibility wrappers making ARM64 NEON code run on ARM32
// With optimization on, these should compile down to the optimal code.

static inline float32x4_t vmulq_laneq_f32(float32x4_t a, float32x4_t b, int lane) {
	switch (lane & 3) {
	case 0: return vmulq_lane_f32(a, vget_low_f32(b), 0);
	case 1: return vmulq_lane_f32(a, vget_low_f32(b), 1);
	case 2: return vmulq_lane_f32(a, vget_high_f32(b), 0);
	default: return vmulq_lane_f32(a, vget_high_f32(b), 1);
	}
}

static inline float32x4_t vmlaq_laneq_f32(float32x4_t a, float32x4_t b, float32x4_t c, int lane) {
	switch (lane & 3) {
	case 0: return vmlaq_lane_f32(a, b, vget_low_f32(c), 0);
	case 1: return vmlaq_lane_f32(a, b, vget_low_f32(c), 1);
	case 2: return vmlaq_lane_f32(a, b, vget_high_f32(c), 0);
	default: return vmlaq_lane_f32(a, b, vget_high_f32(c), 1);
	}
}

static inline uint32x4_t vcgezq_f32(float32x4_t v) {
	return vcgeq_f32(v, vdupq_n_f32(0.0f));
}

#endif

#if PPSSPP_ARCH(SSE2)

// Wrappers
#ifndef __SSE4_1__

// https://stackoverflow.com/questions/17264399/fastest-way-to-multiply-two-vectors-of-32bit-integers-in-c-with-sse
inline __m128i _mm_mullo_epi32(const __m128i v0, const __m128i v1) {
	__m128i a13 = _mm_shuffle_epi32(v0, 0xF5);             // (-,a3,-,a1)
	__m128i b13 = _mm_shuffle_epi32(v1, 0xF5);             // (-,b3,-,b1)
	__m128i prod02 = _mm_mul_epu32(v0, v1);                // (-,a2*b2,-,a0*b0)
	__m128i prod13 = _mm_mul_epu32(a13, b13);              // (-,a3*b3,-,a1*b1)
	__m128i prod01 = _mm_unpacklo_epi32(prod02, prod13);   // (-,-,a1*b1,a0*b0)
	__m128i prod23 = _mm_unpackhi_epi32(prod02, prod13);   // (-,-,a3*b3,a2*b2)
	return _mm_unpacklo_epi64(prod01, prod23);
}

inline __m128i _mm_max_epu16(const __m128i v0, const __m128i v1) {
	return _mm_xor_si128(
		_mm_max_epi16(
			_mm_xor_si128(v0, _mm_set1_epi16(0x8000)),
			_mm_xor_si128(v1, _mm_set1_epi16(0x8000))),
		_mm_set1_epi16(0x8000));
}

inline __m128i _mm_min_epu16(const __m128i v0, const __m128i v1) {
	return _mm_xor_si128(
		_mm_min_epi16(
			_mm_xor_si128(v0, _mm_set1_epi16(0x8000)),
			_mm_xor_si128(v1, _mm_set1_epi16(0x8000))),
		_mm_set1_epi16(0x8000));
}
#endif

#endif
