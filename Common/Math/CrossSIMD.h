// CrossSIMD
//
// This file will contain cross-instruction-set SIMD instruction wrappers.

#pragma once

#include "Common/Math/SIMDHeaders.h"

#if PPSSPP_ARCH(SSE2)

struct Vec4S32 {
	__m128i v;

	static Vec4S32 Load(int *src) { return Vec4S32{ _mm_loadu_si128((const __m128i *)src) }; }
	static Vec4S32 LoadAligned(int *src) { return Vec4S32{ _mm_load_si128((const __m128i *)src) }; }
	void Store(int *dst) { _mm_storeu_si128((__m128i *)dst, v); }
	void StoreAligned(int *dst) { _mm_store_si128((__m128i *)dst, v);}

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ _mm_add_epi32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ _mm_sub_epi32(v, other.v) }; }
	// NOTE: This uses a CrossSIMD wrapper if we don't compile with SSE4 support, and is thus slow.
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ _mm_mullo_epi32_SSE2(v, other.v) }; }  // (ab3,ab2,ab1,ab0)
};

struct Vec4F32 {
	__m128 v;

	static Vec4F32 Load(float *src) { return Vec4F32{ _mm_loadu_ps(src) }; }
	static Vec4F32 LoadAligned(float *src) { return Vec4F32{ _mm_load_ps(src) }; }
	void Store(float *dst) { _mm_storeu_ps(dst, v); }
	void StoreAligned (float *dst) { _mm_store_ps(dst, v); }

	static Vec4F32 FromVec4S32(Vec4S32 other) { return Vec4F32{ _mm_cvtepi32_ps(other.v) }; }

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ _mm_add_ps(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ _mm_sub_ps(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ _mm_mul_ps(v, other.v) }; }
};

struct Vec4U16 {
	__m128i v;  // we only use the lower 64 bits.
	static Vec4U16 Load(uint16_t *mem) { return Vec4U16{ _mm_loadl_epi64((__m128i *)mem) }; }
	void Store(uint16_t *mem) { _mm_storel_epi64((__m128i *)mem, v); }

	static Vec4U16 Max(Vec4U16 a, Vec4U16 b) { return Vec4U16{ _mm_max_epu16_SSE2(a.v, b.v) }; }
	static Vec4U16 Min(Vec4U16 a, Vec4U16 b) { return Vec4U16{ _mm_max_epu16_SSE2(a.v, b.v) }; }
	Vec4U16 CompareLT(Vec4U16 other) { return Vec4U16{ _mm_cmplt_epu16(v, other.v) }; }
};

#elif PPSSPP_ARCH(ARM_NEON)

struct Vec4S32 {
	int32x4_t v;

	static Vec4F32 Load(int *src) { return Vec4F32{ vld1q_s32(src) }; }
	static Vec4F32 LoadAligned(int *src) { return Vec4F32{ vld1q_s32(src) }; }
	void Store(int *dst) { vst1q_s32(dst, v); }
	void StoreAligned(int *dst) { vst1q_s32(dst, v); }

	Vec4S32 operator +(Vec4S32 other) const { return Vec4S32{ vaddq_s32(v, other.v) }; }
	Vec4S32 operator -(Vec4S32 other) const { return Vec4S32{ vsubq_s32(v, other.v) }; }
	Vec4S32 operator *(Vec4S32 other) const { return Vec4S32{ vmulq_s32(v, other.v) }; }
};

struct Vec4F32 {
	float32x4_t v;

	static Vec4F32 Load(float *src) { return Vec4F32{ vld1q_f32(src) }; }
	static Vec4F32 LoadAligned(float *src) { return Vec4F32{ vld1q_f32(src) }; }
	void Store(float *dst) { vst1q_f32(dst, v); }
	void StoreAligned(float *dst) { vst1q_f32(dst, v); }

	static Vec4F32 FromVec4S32(Vec4S32 other) {
		return Vec4F32{ vcvtq_f32_s32(other.v) };
	}

	Vec4F32 operator +(Vec4F32 other) const { return Vec4F32{ vaddq_f32(v, other.v) }; }
	Vec4F32 operator -(Vec4F32 other) const { return Vec4F32{ vsubq_f32(v, other.v) }; }
	Vec4F32 operator *(Vec4F32 other) const { return Vec4F32{ vmulq_f32(v, other.v) }; }
};

struct Vec4U16 {
	uint16x4_t v;  // we only use the lower 64 bits.
	static Vec4U16 Load(uint16_t *mem) { return Vec4U16{ vld1_u16(mem) }; }
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
