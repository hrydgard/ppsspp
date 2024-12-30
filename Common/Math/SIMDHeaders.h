#pragma once

// SIMD headers
// Let's include these in one consistent way across the code base.
// Here we'll also add wrappers that paper over differences between different versions
// of an instruction set, like NEON vs ASIMD (64-bit).

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

#define vfmaq_laneq_f32 vmlaq_laneq_f32

static inline uint32x4_t vcgezq_f32(float32x4_t v) {
	return vcgeq_f32(v, vdupq_n_f32(0.0f));
}

#endif

#if PPSSPP_ARCH(SSE2)

#if defined __SSE4_2__
# define _M_SSE 0x402
#elif defined __SSE4_1__
# define _M_SSE 0x401
#elif defined __SSSE3__
# define _M_SSE 0x301
#elif defined __SSE3__
# define _M_SSE 0x300
#elif defined __SSE2__
# define _M_SSE 0x200
#elif !defined(__GNUC__) && (defined(_M_X64) || defined(_M_IX86))
# define _M_SSE 0x402
#endif

// These are SSE2 versions of SSE4.1 instructions, for compatibility and ease of
// writing code.
// May later figure out how to use the appropriate ones depending on compile flags.

inline __m128i _mm_mullo_epi32_SSE2(const __m128i v0, const __m128i v1) {
	__m128i a13 = _mm_shuffle_epi32(v0, 0xF5);             // (-,a3,-,a1)
	__m128i b13 = _mm_shuffle_epi32(v1, 0xF5);             // (-,b3,-,b1)
	__m128i prod02 = _mm_mul_epu32(v0, v1);                // (-,a2*b2,-,a0*b0)
	__m128i prod13 = _mm_mul_epu32(a13, b13);              // (-,a3*b3,-,a1*b1)
	__m128i prod01 = _mm_unpacklo_epi32(prod02, prod13);   // (-,-,a1*b1,a0*b0)
	__m128i prod23 = _mm_unpackhi_epi32(prod02, prod13);   // (-,-,a3*b3,a2*b2)
	return _mm_unpacklo_epi64(prod01, prod23);
}

inline __m128i _mm_max_epu16_SSE2(const __m128i v0, const __m128i v1) {
	return _mm_xor_si128(
		_mm_max_epi16(
			_mm_xor_si128(v0, _mm_set1_epi16((int16_t)0x8000)),
			_mm_xor_si128(v1, _mm_set1_epi16((int16_t)0x8000))),
		_mm_set1_epi16((int16_t)0x8000));
}

inline __m128i _mm_min_epu16_SSE2(const __m128i v0, const __m128i v1) {
	return _mm_xor_si128(
		_mm_min_epi16(
			_mm_xor_si128(v0, _mm_set1_epi16((int16_t)0x8000)),
			_mm_xor_si128(v1, _mm_set1_epi16((int16_t)0x8000))),
		_mm_set1_epi16((int16_t)0x8000));
}

// SSE2 replacement for half of a _mm_packus_epi32 but without the saturation.
inline __m128i _mm_packu_epi32_SSE2(const __m128i v0) {
	__m128i temp = _mm_shufflelo_epi16(v0, _MM_SHUFFLE(3, 3, 2, 0));
	__m128 temp2 = _mm_castsi128_ps(_mm_shufflehi_epi16(temp, _MM_SHUFFLE(3, 3, 2, 0)));
	return _mm_castps_si128(_mm_shuffle_ps(temp2, temp2, _MM_SHUFFLE(3, 3, 2, 0)));
}

#define _mm_splat_lane_ps(v, l) _mm_shuffle_ps((v), (v), _MM_SHUFFLE(l, l, l, l))

#ifdef __cplusplus

alignas(16) static const uint32_t g_sign32[4] = { 0x00008000, 0x00008000, 0x00008000, 0x00008000 };
alignas(16) static const uint32_t g_sign16[4] = { 0x80008000, 0x80008000, 0x80008000, 0x80008000 };

// Alternate solution to the above, not sure if faster or slower.
// SSE2 replacement for half of _mm_packus_epi32 but without the saturation.
// Not ideal! pshufb would make this faster but that's SSSE3.
inline __m128i _mm_packu1_epi32_SSE2(const __m128i v0) {
	// Toggle the sign bit, pack, then toggle back.
	__m128i toggled = _mm_sub_epi32(v0, _mm_load_si128((const __m128i *)g_sign32));
	__m128i temp = _mm_packs_epi32(toggled, toggled);
	__m128i restored = _mm_add_epi16(temp, _mm_load_si128((const __m128i *)g_sign16));
	return restored;
}

#endif

// SSE2 replacement for the entire _mm_packus_epi32 but without the saturation.
// Not ideal! pshufb would make this faster but that's SSSE3.
inline __m128i _mm_packu2_epi32_SSE2(const __m128i v0, const __m128i v1) {
	__m128i a0 = _mm_shufflelo_epi16(v0, _MM_SHUFFLE(3, 3, 2, 0));
	__m128 packed0 = _mm_castsi128_ps(_mm_shufflehi_epi16(a0, _MM_SHUFFLE(3, 3, 2, 0)));
	__m128i a1 = _mm_shufflelo_epi16(v1, _MM_SHUFFLE(3, 3, 2, 0));
	__m128 packed1 = _mm_castsi128_ps(_mm_shufflehi_epi16(a1, _MM_SHUFFLE(3, 3, 2, 0)));
	return _mm_castps_si128(_mm_shuffle_ps(packed0, packed1, _MM_SHUFFLE(2, 0, 2, 0)));
}

// The below are not real SSE instructions in any generation, but should exist.

// Return 0xFFFF where x <= y, else 0x0000.
inline __m128i _mm_cmple_epu16(__m128i x, __m128i y) {
	return _mm_cmpeq_epi16(_mm_subs_epu16(x, y), _mm_setzero_si128());
}

// Return 0xFFFF where x >= y, else 0x0000.
inline __m128i _mm_cmpge_epu16(__m128i x, __m128i y) {
	return _mm_cmple_epu16(y, x);
}

// Return 0xFFFF where x > y, else 0x0000.
inline __m128i _mm_cmpgt_epu16(__m128i x, __m128i y) {
	return _mm_andnot_si128(_mm_cmpeq_epi16(x, y), _mm_cmple_epu16(y, x));
}

// Return 0xFFFF where x < y, else 0x0000.
inline __m128i _mm_cmplt_epu16(__m128i x, __m128i y) {
	return _mm_cmpgt_epu16(y, x);
}

#endif
