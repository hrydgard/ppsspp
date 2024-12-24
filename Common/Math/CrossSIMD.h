// CrossSIMD
//
// This file will contain cross-instruction-set SIMD instruction wrappers.

#pragma once

#include "Common/Math/SIMDHeaders.h"

#if PPSSPP_ARCH(SSE2)

struct Mat4F32 {
	Mat4F32(const float *matrix) {
		col0 = _mm_loadu_ps(matrix);
		col1 = _mm_loadu_ps(matrix + 4);
		col2 = _mm_loadu_ps(matrix + 8);
		col3 = _mm_loadu_ps(matrix + 12);
	}

	__m128 col0;
	__m128 col1;
	__m128 col2;
	__m128 col3;
};

struct Vec4S32 {
	__m128i v;

	static Vec4S32 Zero() { return Vec4S32{ _mm_setzero_si128() }; }
	static Vec4S32 Splat(int lane) { return Vec4S32{ _mm_set1_epi32(lane) }; }

	static Vec4S32 Load(const int *src) { return Vec4S32{ _mm_loadu_si128((const __m128i *)src) }; }
	static Vec4S32 LoadAligned(const int *src) { return Vec4S32{ _mm_load_si128((const __m128i *)src) }; }
	void Store(int *dst) { _mm_storeu_si128((__m128i *)dst, v); }
	void StoreAligned(int *dst) { _mm_store_si128((__m128i *)dst, v);}

	// Swaps the two lower elements. Useful for reversing triangles..
	Vec4S32 SwapLowerElements() {
		return Vec4S32{
			_mm_shuffle_epi32(v, _MM_SHUFFLE(3, 2, 0, 1))
		};
	}
	Vec4S32 SignBits32ToMask() {
		return Vec4S32{
			_mm_srai_epi32(v, 31)
		};
	}

	// Reads 16 bits from both operands, produces a 32-bit result per lane.
	// On SSE2, much faster than _mm_mullo_epi32_SSE2.
	// On NEON though, it'll read the full 32 bits, so beware.
	// See https://fgiesen.wordpress.com/2016/04/03/sse-mind-the-gap/.
	Vec4S32 MulAsS16(Vec4S32 other) const {
		// Note that we only need to mask one of the inputs, so we get zeroes - multiplying
		// by zero is zero, so it doesn't matter what the upper halfword of each 32-bit word is
		// in the other register.
		return Vec4S32{ _mm_madd_epi16(v, _mm_and_si128(other.v, _mm_set1_epi32(0x0000FFFF))) };
	}

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ _mm_add_epi32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ _mm_sub_epi32(v, other.v) }; }
	Vec4S32 operator |(Vec4S32 other) const { return Vec4S32{ _mm_or_si128(v, other.v) }; }
	Vec4S32 operator &(Vec4S32 other) const { return Vec4S32{ _mm_and_si128(v, other.v) }; }
	Vec4S32 operator ^(Vec4S32 other) const { return Vec4S32{ _mm_xor_si128(v, other.v) }; }
	// TODO: andnot
	void operator +=(Vec4S32 other) { v = _mm_add_epi32(v, other.v); }
	void operator -=(Vec4S32 other) { v = _mm_sub_epi32(v, other.v); }

	// NOTE: This uses a CrossSIMD wrapper if we don't compile with SSE4 support, and is thus slow.
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ _mm_mullo_epi32_SSE2(v, other.v) }; }  // (ab3,ab2,ab1,ab0)
};

inline bool AnyZeroSignBit(Vec4S32 value) {
	return _mm_movemask_ps(_mm_castsi128_ps(value.v)) != 0xF;
}

struct Vec4F32 {
	__m128 v;

	static Vec4F32 Zero() { return Vec4F32{ _mm_setzero_ps() }; }
	static Vec4F32 Splat(float lane) { return Vec4F32{ _mm_set1_ps(lane) }; }

	static Vec4F32 Load(const float *src) { return Vec4F32{ _mm_loadu_ps(src) }; }
	static Vec4F32 LoadAligned(const float *src) { return Vec4F32{ _mm_load_ps(src) }; }
	void Store(float *dst) { _mm_storeu_ps(dst, v); }
	void StoreAligned (float *dst) { _mm_store_ps(dst, v); }

	static Vec4F32 LoadConvertS16(const int16_t *src) {  // Note: will load 8 bytes
		__m128i value = _mm_loadl_epi64((const __m128i *)src);
		// 16-bit to 32-bit, use the upper words and an arithmetic shift right to sign extend
		return Vec4F32{ _mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(value, value), 16)) };
	}

	static Vec4F32 LoadConvertS8(const int8_t *src) {  // Note: will load 8 bytes
		__m128i value = _mm_loadl_epi64((const __m128i *)src);
		__m128i value16 = _mm_unpacklo_epi8(value, value);
		// 16-bit to 32-bit, use the upper words and an arithmetic shift right to sign extend
		return Vec4F32{ _mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(value16, value16), 24)) };
	}

	static Vec4F32 FromVec4S32(Vec4S32 other) { return Vec4F32{ _mm_cvtepi32_ps(other.v) }; }

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ _mm_add_ps(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ _mm_sub_ps(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ _mm_mul_ps(v, other.v) }; }
	void operator +=(Vec4F32 other) { v = _mm_add_ps(v, other.v); }
	void operator -=(Vec4F32 other) { v = _mm_sub_ps(v, other.v); }
	void operator *=(Vec4F32 other) { v = _mm_mul_ps(v, other.v); }
	void operator /=(Vec4F32 other) { v = _mm_div_ps(v, other.v); }
	Vec4F32 operator *(float f) const { return Vec4F32{ _mm_mul_ps(v, _mm_set1_ps(f)) }; }

	Vec4F32 Mul(float f) const { return Vec4F32{ _mm_mul_ps(v, _mm_set1_ps(f)) }; }
	Vec4F32 Recip() { return Vec4F32{ _mm_rcp_ps(v) }; }

	Vec4F32 Clamp(float lower, float higher) {
		return Vec4F32{
			_mm_min_ps(_mm_max_ps(v, _mm_set1_ps(lower)), _mm_set1_ps(higher))
		};
	}

	Vec4F32 WithLane3Zeroed() const {
		alignas(16) static uint32_t mask[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0 };
		return Vec4F32{ _mm_and_ps(v, _mm_load_ps((float *)mask)) };
	}

	// Swaps the two lower elements. Useful for reversing triangles..
	Vec4F32 SwapLowerElements() {
		return Vec4F32{
			_mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 2, 0, 1))
		};
	}

	inline Vec4F32 AsVec3ByMatrix44(const Mat4F32 &m) {
		return Vec4F32{ _mm_add_ps(
			_mm_add_ps(
				_mm_mul_ps(m.col0, _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0))),
				_mm_mul_ps(m.col1, _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1)))
			),
			_mm_add_ps(
				_mm_mul_ps(m.col2, _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2))),
				m.col3)
			)
		};
	}

	static void Transpose(Vec4F32 &col0, Vec4F32 &col1, Vec4F32 &col2, Vec4F32 &col3) {
		_MM_TRANSPOSE4_PS(col0.v, col1.v, col2.v, col3.v);
	}
};

inline Vec4S32 Vec4S32FromF32(Vec4F32 f) { return Vec4S32{ _mm_cvttps_epi32(f.v) }; }
inline Vec4F32 Vec4F32FromS32(Vec4S32 f) { return Vec4F32{ _mm_cvtepi32_ps(f.v) }; }

struct Vec4U16 {
	__m128i v;  // we only use the lower 64 bits.

	static Vec4U16 Zero() { return Vec4U16{ _mm_setzero_si128() }; }
	// static Vec4U16 AllOnes() { return Vec4U16{ _mm_cmpeq_epi16(_mm_setzero_si128(), _mm_setzero_si128()) }; }

	static Vec4U16 Load(const uint16_t *mem) { return Vec4U16{ _mm_loadl_epi64((__m128i *)mem) }; }
	void Store(uint16_t *mem) { _mm_storel_epi64((__m128i *)mem, v); }

	// NOTE: 16-bit signed saturation! Will work for a lot of things, but not all.
	static Vec4U16 FromVec4S32(Vec4S32 v) {
		return Vec4U16{ _mm_packu_epi32_SSE2(v.v)};
	}
	static Vec4U16 FromVec4F32(Vec4F32 v) {
		return Vec4U16{ _mm_packu_epi32_SSE2(_mm_cvtps_epi32(v.v)) };
	}

	Vec4U16 operator |(Vec4U16 other) const { return Vec4U16{ _mm_or_si128(v, other.v) }; }
	Vec4U16 operator &(Vec4U16 other) const { return Vec4U16{ _mm_and_si128(v, other.v) }; }
	Vec4U16 operator ^(Vec4U16 other) const { return Vec4U16{ _mm_xor_si128(v, other.v) }; }

	Vec4U16 Max(Vec4U16 other) const { return Vec4U16{ _mm_max_epu16_SSE2(v, other.v) }; }
	Vec4U16 Min(Vec4U16 other) const { return Vec4U16{ _mm_min_epu16_SSE2(v, other.v) }; }
	Vec4U16 CompareLT(Vec4U16 other) { return Vec4U16{ _mm_cmplt_epu16(v, other.v) }; }
};

struct Vec8U16 {
	__m128i v;

	static Vec8U16 Zero() { return Vec8U16{ _mm_setzero_si128() }; }
	static Vec8U16 Splat(uint16_t value) { return Vec8U16{ _mm_set1_epi16((int16_t)value) }; }

	static Vec8U16 Load(const uint16_t *mem) { return Vec8U16{ _mm_loadu_si128((__m128i *)mem) }; }
	void Store(uint16_t *mem) { _mm_storeu_si128((__m128i *)mem, v); }
};

Vec4U16 SignBits32ToMaskU16(Vec4S32 v) {
	__m128i temp = _mm_srai_epi32(v.v, 31);
	return Vec4U16 {
		_mm_packs_epi32(temp, temp)
	};
}

Vec4U16 AndNot(Vec4U16 a, Vec4U16 inverted) {
	return Vec4U16{
		_mm_andnot_si128(inverted.v, a.v)  // NOTE: with andnot, the first parameter is inverted, and then and is performed.
	};
}

#elif PPSSPP_ARCH(ARM_NEON)

struct Mat4F32 {
	Mat4F32(const float *matrix) {
		col0 = vld1q_f32(matrix);
		col1 = vld1q_f32(matrix + 4);
		col2 = vld1q_f32(matrix + 8);
		col3 = vld1q_f32(matrix + 12);
	}
	float32x4_t col0;
	float32x4_t col1;
	float32x4_t col2;
	float32x4_t col3;
};

struct Vec4S32 {
	int32x4_t v;

	static Vec4S32 Zero() { return Vec4S32{ vdupq_n_s32(0) }; }
	static Vec4S32 Splat(int lane) { return Vec4S32{ vdupq_n_s32(lane) }; }

	static Vec4S32 Load(const int *src) { return Vec4S32{ vld1q_s32(src) }; }
	static Vec4S32 LoadAligned(const int *src) { return Vec4S32{ vld1q_s32(src) }; }
	void Store(int *dst) { vst1q_s32(dst, v); }
	void StoreAligned(int *dst) { vst1q_s32(dst, v); }

	// Swaps the two lower elements, but NOT the two upper ones. Useful for reversing triangles..
	// This is quite awkward on ARM64 :/ Maybe there's a better solution?
	Vec4S32 SwapLowerElements() {
		int32x2_t upper = vget_high_s32(v);
		int32x2_t lowerSwapped = vrev64_s32(vget_low_s32(v));
		return Vec4S32{ vcombine_s32(lowerSwapped, upper) };
	};

	// Warning: Unlike on x86, this is a full 32-bit multiplication.
	Vec4S32 MulAsS16(Vec4S32 other) const { return Vec4S32{ vmulq_s32(v, other.v) }; }

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ vaddq_s32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ vsubq_s32(v, other.v) }; }
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ vmulq_s32(v, other.v) }; }
	Vec4S32 operator |(Vec4S32 other) const { return Vec4S32{ vorrq_s32(v, other.v) }; }
	Vec4S32 operator &(Vec4S32 other) const { return Vec4S32{ vandq_s32(v, other.v) }; }
	Vec4S32 operator ^(Vec4S32 other) const { return Vec4S32{ veorq_s32(v, other.v) }; }

	void operator +=(Vec4S32 other) { v = vaddq_s32(v, other.v); }
	void operator -=(Vec4S32 other) { v = vsubq_s32(v, other.v); }
};

struct Vec4F32 {
	float32x4_t v;

	static Vec4F32 Zero() { return Vec4F32{ vdupq_n_f32(0.0f) }; }
	static Vec4F32 Splat(float lane) { return Vec4F32{ vdupq_n_f32(lane) }; }

	static Vec4F32 Load(const float *src) { return Vec4F32{ vld1q_f32(src) }; }
	static Vec4F32 LoadAligned(const float *src) { return Vec4F32{ vld1q_f32(src) }; }
	void Store(float *dst) { vst1q_f32(dst, v); }
	void StoreAligned(float *dst) { vst1q_f32(dst, v); }

	static Vec4F32 LoadConvertS16(const int16_t *src) {  // Note: will load 8 bytes
		int16x4_t value = vld1_s16(src);
		return Vec4F32{ vcvtq_f32_s32(vmovl_s16(value)) };
	}

	static Vec4F32 LoadConvertS8(const int8_t *src) {  // Note: will load 8 bytes
		int8x8_t value = vld1_s8(src);
		int16x4_t value16 = vget_low_s16(vmovl_s8(value));
		return Vec4F32{ vcvtq_f32_s32(vmovl_s16(value)) };
	}

	static Vec4F32 FromVec4S32(Vec4S32 other) {
		return Vec4F32{ vcvtq_f32_s32(other.v) };
	}

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ vaddq_f32(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ vsubq_f32(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ vmulq_f32(v, other.v) }; }
	void operator +=(Vec4F32 other) { v = vaddq_f32(v, other.v); }
	void operator -=(Vec4F32 other) { v = vsubq_f32(v, other.v); }
	void operator *=(Vec4F32 other) { v = vmulq_f32(v, other.v); }
	void operator /=(Vec4F32 other) { v = vmulq_f32(v, other.Recip().v); }
	Vec4F32 operator *(float f) const { return Vec4F32{ vmulq_f32(v, vdupq_n_f32(f)) }; }

	Vec4F32 Mul(float f) const { return Vec4F32{ vmulq_f32(v, vdupq_n_f32(f)) }; }

	Vec4F32 Recip() {
		float32x4_t recip = vrecpeq_f32(v);
		// Use a couple Newton-Raphson steps to refine the estimate.
		// May be able to get away with only one refinement, not sure!
		recip = vmulq_f32(vrecpsq_f32(v, recip), recip);
		recip = vmulq_f32(vrecpsq_f32(v, recip), recip);
		return Vec4F32{ recip };
	}

	Vec4F32 Clamp(float lower, float higher) {
		return Vec4F32{
			vminq_f32(vmaxq_f32(v, vdupq_n_f32(lower)), vdupq_n_f32(higher))
		};
	}

	Vec4F32 WithLane3Zeroed() const {
		return Vec4F32{ vsetq_lane_f32(0.0f, v, 3) };
	}

	// Swaps the two lower elements, but NOT the two upper ones. Useful for reversing triangles..
	// This is quite awkward on ARM64 :/ Maybe there's a better solution?
	Vec4F32 SwapLowerElements() {
		float32x2_t lowerSwapped = vrev64_f32(vget_low_f32(v));
		return Vec4F32{ vcombine_f32(lowerSwapped, vget_high_f32(v)) };
	};

	// One of many possible solutions. Sometimes we could also use vld4q_f32 probably..
	static void Transpose(Vec4F32 &col0, Vec4F32 &col1, Vec4F32 &col2, Vec4F32 &col3) {
#if PPSSPP_ARCH(ARM64_NEON)
		// Only works on ARM64
		float32x4_t temp0 = vzip1q_f32(col0.v, col2.v);
		float32x4_t temp1 = vzip2q_f32(col0.v, col2.v);
		float32x4_t temp2 = vzip1q_f32(col1.v, col3.v);
		float32x4_t temp3 = vzip2q_f32(col1.v, col3.v);
		col0.v = vzip1q_f32(temp0, temp2);
		col1.v = vzip2q_f32(temp0, temp2);
		col2.v = vzip1q_f32(temp1, temp3);
		col3.v = vzip2q_f32(temp1, temp3);
#else
   		float32x4x2_t col01 = vtrnq_f32(col0.v, col1.v);
        float32x4x2_t col23 = vtrnq_f32(col2.v, col3.v);
        col0.v = vcombine_f32(vget_low_f32(col01.val[0]), vget_low_f32(col23.val[0]));
        col1.v = vcombine_f32(vget_low_f32(col01.val[1]), vget_low_f32(col23.val[1]));
        col2.v = vcombine_f32(vget_high_f32(col01.val[0]), vget_high_f32(col23.val[0]));
        col3.v = vcombine_f32(vget_high_f32(col01.val[1]), vget_high_f32(col23.val[1]));
#endif
	}

	inline Vec4F32 AsVec3ByMatrix44(const Mat4F32 &m) {
#if PPSSPP_ARCH(ARM64_NEON)
		float32x4_t sum = vaddq_f32(
			vaddq_f32(vmulq_laneq_f32(m.col0, v, 0), vmulq_laneq_f32(m.col1, v, 1)),
			vaddq_f32(vmulq_laneq_f32(m.col2, v, 2), m.col3));
#else
		float32x4_t sum = vaddq_f32(
			vaddq_f32(vmulq_lane_f32(m.col0, vget_low_f32(v), 0), vmulq_lane_f32(m.col1, vget_low_f32(v), 1)),
			vaddq_f32(vmulq_lane_f32(m.col2, vget_high_f32(v), 0), m.col3));
#endif
		return Vec4F32{ sum };
	}
};

inline Vec4S32 Vec4S32FromF32(Vec4F32 f) { return Vec4S32{ vcvtq_s32_f32(f.v) }; }
inline Vec4F32 Vec4F32FromS32(Vec4S32 s) { return Vec4F32{ vcvtq_f32_s32(s.v) }; }

inline bool AnyZeroSignBit(Vec4S32 value) {
	// Very suboptimal, let's optimize later.
	int32x2_t prod = vand_s32(vget_low_s32(value.v), vget_high_s32(value.v));
	int mask = vget_lane_s32(prod, 0) & vget_lane_s32(prod, 1);
	return (mask & 0x80000000) == 0;
}

struct Vec4U16 {
	uint16x4_t v;  // 64 bits.

	static Vec4U16 Zero() { return Vec4U16{ vdup_n_u16(0) }; }
	static Vec4U16 Splat(uint16_t value) { return Vec4U16{ vdup_n_u16(value) }; }

	static Vec4U16 Load(const uint16_t *mem) { return Vec4U16{ vld1_u16(mem) }; }
	void Store(uint16_t *mem) { vst1_u16(mem, v); }

	static Vec4U16 FromVec4S32(Vec4S32 v) {
		return Vec4U16{ vmovn_u16(v.v) };
	}
	static Vec4U16 FromVec4F32(Vec4F32 v) {
		return Vec4U16{ vmovn_u32(vreinterpretq_u32_s32(vcvtq_s32_f32(v.v))) };
	}

	Vec4U16 operator |(Vec4U16 other) const { return Vec4U16{ vorr_u16(v, other.v) }; }
	Vec4U16 operator &(Vec4U16 other) const { return Vec4U16{ vand_u16(v, other.v) }; }
	Vec4U16 operator ^(Vec4U16 other) const { return Vec4U16{ veor_u16(v, other.v) }; }

	Vec4U16 Max(Vec4U16 other) const { return Vec4U16{ vmax_u16(v, other.v) }; }
	Vec4U16 Min(Vec4U16 other) const { return Vec4U16{ vmin_u16(v, other.v) }; }
	Vec4U16 CompareLT(Vec4U16 other) { return Vec4U16{ vclt_u16(v, other.v) }; }
};

Vec4U16 SignBits32ToMaskU16(Vec4S32 v) {
	int32x4_t sign_mask = vshrq_n_s32(v.v, 31);
	uint16x4_t result = vreinterpret_u16_s16(vmovn_s32(sign_mask));
	return Vec4U16{ result };
}

Vec4U16 AndNot(Vec4U16 a, Vec4U16 inverted) {
	return Vec4U16{ vand_u16(a.v, vmvn_u16(inverted.v)) };
}

struct Vec8U16 {
	uint16x8_t v;

	static Vec8U16 Zero() { return Vec8U16{ vdupq_n_u16(0) }; }
	static Vec8U16 Splat(uint16_t value) { return Vec8U16{ vdupq_n_u16(value) }; }

	static Vec8U16 Load(const uint16_t *mem) { return Vec8U16{ vld1q_u16(mem) }; }
	void Store(uint16_t *mem) { vst1q_u16(mem, v); }
};

#else

struct Vec4S32 {
	s32 v[4];

	Vec4S32 operator +(Vec4S32 other) const {
		return Vec4S32{ { v[0] + other.v[0], v[1] + other.v[1], v[2] + other.v[2], v[3] + other.v[3], } };
	}
	Vec4S32 operator -(Vec4S32 other) const {
		return Vec4S32{ { v[0] - other.v[0], v[1] - other.v[1], v[2] - other.v[2], v[3] - other.v[3], } };
	}
};

#endif
