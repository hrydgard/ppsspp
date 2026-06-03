#pragma once

// SIMD headers
// Let's include these in one consistent way across the code base.
// Here we'll also add wrappers that paper over differences between different versions
// of an instruction set, like NEON vs ASIMD (64-bit).

#pragma once

#include "ppsspp_config.h"

#include "stdint.h"
#include <string.h>

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

#if PPSSPP_ARCH(LOONGARCH64) && PPSSPP_ARCH(LOONGARCH64_LSX)

#include <lsxintrin.h>

static inline __m128 __lsx_vreplfr2vr_s(float val) {
	int32_t bits;
	memcpy(&bits, &val, sizeof(bits));
	return (__m128)__lsx_vreplgr2vr_w(bits);
}

static inline __m128 zero_nans_lsx(__m128 val) {
	// vfcmp.ceq.s compares each float lane.
	// NaN == NaN is false, so NaNs result in 0x00000000.
	// Numbers result in 0xFFFFFFFF.
	__m128i mask = (__m128i)__lsx_vfcmp_ceq_s(val, val);
	// Bitwise AND to keep non-NaNs and turn NaNs into 0.0f
	return (__m128)__lsx_vand_v((__m128i)val, mask);
}

static inline __m128 clean_nan_inf_lsx(__m128 val) {
	// Strip sign bit
	__m128i sign_mask = __lsx_vreplgr2vr_w(0x7FFFFFFF);
	__m128i abs_x = __lsx_vand_v((__m128i)val, sign_mask);

	// Infinity threshold
	__m128i inf_val = __lsx_vreplgr2vr_w(0x7F800000);

	// Compare: Is |x| < Inf? (using signed comparison since 0x7F800000 fits)
	__m128i finite_mask = __lsx_vslt_w(abs_x, inf_val);

	return (__m128)__lsx_vand_v((__m128i)val, finite_mask);
}

static inline __m128i zero_nans_lsx(__m128i x) {
	// vfcmp.ceq.s compares each float lane.
	// NaN == NaN is false, so NaNs result in 0x00000000.
	// Numbers result in 0xFFFFFFFF.
	__m128i mask = (__m128i)__lsx_vfcmp_ceq_s((__m128)x, (__m128)x);
	// Bitwise AND to keep non-NaNs and turn NaNs into 0.0f
	return __lsx_vand_v(x, mask);
}

static inline __m128 zero_nan_inf_lsx(__m128 x) {
	// 1. Take absolute value by masking off the sign bit
	// LSX doesn't have __lsx_vfabs_s, so we compute |x| manually
	__m128i sign_mask = __lsx_vreplgr2vr_w(0x7FFFFFFF);
	__m128 x_abs = (__m128)__lsx_vand_v((__m128i)x, sign_mask);

	// 2. Load the Positive Infinity bit pattern (0x7F800000)
	// Emits: vreplgr2vr.w to duplicate the GPR value into all SIMD lanes
	__m128i inf_bytes = __lsx_vreplgr2vr_w(0x7F800000);
	__m128 inf_vec = (__m128)inf_bytes;

	// 3. Compare x_abs < inf_vec
	// clt variant: Compare Less Than, returns all ones for true, zero for false/NaN
	// Emits: vfcmp.clt.s vr, vr, vr
	__m128i valid_mask = __lsx_vfcmp_clt_s(x_abs, inf_vec);

	// 4. Bitwise AND to clear the invalid lanes
	// Emits: vand.v vr, vr, vr
	__m128i result_bytes = __lsx_vand_v((__m128i)x, valid_mask);

	return (__m128)result_bytes;
}

#elif PPSSPP_ARCH(ARM64_NEON)

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

static inline float32x4_t vdupq_laneq_f32(float32x4_t vec, int lane) {
	switch (lane & 3) {
	case 0: return vdupq_lane_f32(vget_low_f32(vec), 0);
	case 1: return vdupq_lane_f32(vget_low_f32(vec), 1);
	case 2: return vdupq_lane_f32(vget_high_f32(vec), 0);
	default: return vdupq_lane_f32(vget_high_f32(vec), 1);
	}
}

#define vfmaq_laneq_f32 vmlaq_laneq_f32

static inline uint32x4_t vcgezq_f32(float32x4_t v) {
	return vcgeq_f32(v, vdupq_n_f32(0.0f));
}

#endif

#if PPSSPP_ARCH(ARM_NEON) || PPSSPP_ARCH(ARM64_NEON)

static inline float32x4_t zero_nans_neon(float32x4_t x) {
	// vceqq_f32 returns 0 for NaNs (since NaN != NaN)
	uint32x4_t mask = vceqq_f32(x, x);
	// Bitwise AND the float bits with the mask
	return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(x), mask));
}

static inline float32x4_t clean_inf_and_nan_neon(float32x4_t x) {
	// Alternate solution that zeroes always
	uint32x4_t raw_bits = vreinterpretq_u32_f32(x);
	// Shift left by 1 to remove the sign bit. 
	// Finite numbers will be less than (0x7F800000 << 1) which is 0xFF000000
	uint32x4_t abs_shifted = vshlq_n_u32(raw_bits, 1);
	uint32x4_t inf_shifted = vdupq_n_u32(0xFF000000);
	// Compare less-than (unsigned)
	uint32x4_t finite_mask = vcltq_u32(abs_shifted, inf_shifted);
	return vreinterpretq_f32_u32(vandq_u32(raw_bits, finite_mask));
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

inline __m128 zero_nans_sse(__m128 x) {
	// Returns 0xFFFFFFFF where x is NOT NaN, 0x00000000 where it IS NaN
	__m128 mask = _mm_cmpord_ps(x, x);
	// NaN & 0 = 0.0f. Finite & 0xFF... = Finite.
	return _mm_and_ps(x, mask);
}

inline __m128 clean_nan_inf_sse(__m128 x) {
#if 0
	// 1. Create a mask to strip the sign bit (0x7FFFFFFF)
	__m128i sign_mask = _mm_set1_epi32(0x7FFFFFFF);

	// 2. Create a vector representing Positive Infinity (0x7F800000)
	__m128i inf_bytes = _mm_set1_epi32(0x7F800000);
	__m128 inf_vec = _mm_castsi128_ps(inf_bytes);

	// 3. Get absolute value: x_abs = x & 0x7FFFFFFF
	__m128 x_abs = _mm_and_ps(x, _mm_castsi128_ps(sign_mask));

	// 4. Compare x_abs < INF
	// Finite numbers -> 0xFFFFFFFF
	// INF and NaN    -> 0x00000000
	__m128 valid_mask = _mm_cmplt_ps(x_abs, inf_vec);

	// 5. Zero out the invalid elements
	return _mm_and_ps(x, valid_mask);
#else
	// 1. Establish your maximum and minimum finite bounds
	__m128 max_finite = _mm_set1_ps(3.40282347e+38f);  // FLT_MAX
	__m128 min_finite = _mm_set1_ps(-3.40282347e+38f); // -FLT_MAX
	// 2. Clamp the upper bound. 
	// If x is +Inf, min(Inf, FLT_MAX) -> FLT_MAX.
	// If x is NaN, it outputs the second operand -> max_finite (FLT_MAX).
	__m128 upper_clamped = _mm_min_ps(x, max_finite);
	// 3. Clamp the lower bound.
	// If upper_clamped is -Inf, max(-Inf, -FLT_MAX) -> -FLT_MAX.
	// If upper_clamped became FLT_MAX because it was a NaN, max(FLT_MAX, -FLT_MAX) -> FLT_MAX.
	return _mm_max_ps(upper_clamped, min_finite);
#endif
}

#endif
