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

	void Transpose() {
		_MM_TRANSPOSE4_PS(col0, col1, col2, col3);
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

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ _mm_add_epi32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ _mm_sub_epi32(v, other.v) }; }
	// NOTE: This uses a CrossSIMD wrapper if we don't compile with SSE4 support, and is thus slow.
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ _mm_mullo_epi32_SSE2(v, other.v) }; }  // (ab3,ab2,ab1,ab0)
};

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
		return Vec4F32{ _mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(value16, value16), 16)) };
	}

	static Vec4F32 FromVec4S32(Vec4S32 other) { return Vec4F32{ _mm_cvtepi32_ps(other.v) }; }

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ _mm_add_ps(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ _mm_sub_ps(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ _mm_mul_ps(v, other.v) }; }

	Vec4F32 Mul(float f) const { return Vec4F32{ _mm_mul_ps(v, _mm_set1_ps(f)) }; }

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
};

struct Vec4U16 {
	__m128i v;  // we only use the lower 64 bits.

	static Vec4U16 Zero() { return Vec4U16{ _mm_setzero_si128() }; }
	// static Vec4U16 AllOnes() { return Vec4U16{ _mm_cmpeq_epi16(_mm_setzero_si128(), _mm_setzero_si128()) }; }

	static Vec4U16 Load(const uint16_t *mem) { return Vec4U16{ _mm_loadl_epi64((__m128i *)mem) }; }
	void Store(uint16_t *mem) { _mm_storel_epi64((__m128i *)mem, v); }

	static Vec4U16 Max(Vec4U16 a, Vec4U16 b) { return Vec4U16{ _mm_max_epu16_SSE2(a.v, b.v) }; }
	static Vec4U16 Min(Vec4U16 a, Vec4U16 b) { return Vec4U16{ _mm_max_epu16_SSE2(a.v, b.v) }; }
	Vec4U16 CompareLT(Vec4U16 other) { return Vec4U16{ _mm_cmplt_epu16(v, other.v) }; }
};

#elif PPSSPP_ARCH(ARM_NEON)

struct Mat4F32 {
	Mat4F32(const float *matrix) {
		col0 = vld1q_f32(matrix);
		col1 = vld1q_f32(matrix + 4);
		col2 = vld1q_f32(matrix + 8);
		col3 = vld1q_f32(matrix + 12);
	}
	void Transpose() {
		float32x4_t temp0 = vzip1q_s32(col0, col2);
		float32x4_t temp1 = vzip2q_s32(col0, col2);
		float32x4_t temp2 = vzip1q_s32(col1, col3);
		float32x4_t temp3 = vzip2q_s32(col1, col3);
		col0 = vzip1q_s32(temp0, temp2);
		col1 = vzip2q_s32(temp0, temp2);
		col2 = vzip1q_s32(temp1, temp3);
		col3 = vzip2q_s32(temp1, temp3);
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

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ vaddq_s32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ vsubq_s32(v, other.v) }; }
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ vmulq_s32(v, other.v) }; }
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
		// 16-bit to 32-bit, use the upper words and an arithmetic shift right to sign extend
		return Vec4F32{ vcvtq_f32_s32(vmovl_s16(value)) };
	}

	static Vec4F32 LoadConvertS8(const int8_t *src) {  // Note: will load 8 bytes
		int8x8_t value = vld1_s8(src);
		int16x4_t value16 = vget_low_s16(vmovl_s8(value));
		// 16-bit to 32-bit, use the upper words and an arithmetic shift right to sign extend
		return Vec4F32{ vcvtq_f32_s32(vmovl_s16(value)) };
	}

	static Vec4F32 FromVec4S32(Vec4S32 other) {
		return Vec4F32{ vcvtq_f32_s32(other.v) };
	}

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ vaddq_f32(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ vsubq_f32(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ vmulq_f32(v, other.v) }; }

	Vec4F32 Mul(float f) const { return Vec4F32{ vmulq_f32(v, vdupq_n_f32(f)) }; }

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

struct Vec4U16 {
	uint16x4_t v;  // we only use the lower 64 bits.

	static Vec4U16 Zero() { return Vec4U16{ vdup_n_u16(0) }; }

	static Vec4U16 Load(const uint16_t *mem) { return Vec4U16{ vld1_u16(mem) }; }
	void Store(uint16_t *mem) { vst1_u16(mem, v); }

	static Vec4U16 Max(Vec4U16 a, Vec4U16 b) { return Vec4U16{ vmax_u16(a.v, b.v) }; }
	static Vec4U16 Min(Vec4U16 a, Vec4U16 b) { return Vec4U16{ vmin_u16(a.v, b.v) }; }
	Vec4U16 CompareLT(Vec4U16 other) { return Vec4U16{ vclt_u16(v, other.v) }; }
};

#else

struct Vec4S32 {
	s32 v[4];
};

#endif
