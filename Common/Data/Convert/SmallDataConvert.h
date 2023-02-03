#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>

#include "Common/Common.h"
#include "ppsspp_config.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

extern const float one_over_255_x4[4];
extern const float exactly_255_x4[4];

// Utilities useful for filling in std140-layout uniform buffers, and similar.
// NEON intrinsics: https://developer.arm.com/documentation/den0018/a/NEON-Intrinsics?lang=en

// LSBs in f[0], etc.
inline void Uint8x4ToFloat4(float f[4], uint32_t u) {
#ifdef _M_SSE
	__m128i zero = _mm_setzero_si128();
	__m128i value = _mm_set1_epi32(u);
	__m128i value32 = _mm_unpacklo_epi16(_mm_unpacklo_epi8(value, zero), zero);
	__m128 fvalues = _mm_mul_ps(_mm_cvtepi32_ps(value32), _mm_load_ps(one_over_255_x4));
	_mm_storeu_ps(f, fvalues);
#elif PPSSPP_ARCH(ARM_NEON)
	const uint8x8_t value = (uint8x8_t)vdup_n_u32(u);
	const uint16x8_t value16 = vmovl_u8(value);
	const uint32x4_t value32 = vmovl_u16(vget_low_u16(value16));
	const float32x4_t valueFloat = vmulq_f32(vcvtq_f32_u32(value32), vdupq_n_f32(1.0f / 255.0f));
	vst1q_f32(f, valueFloat);
#else
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = ((u >> 24) & 0xFF) * (1.0f / 255.0f);
#endif
}

// Could be SSE optimized.
inline uint32_t Float4ToUint8x4(const float f[4]) {
#ifdef _M_SSE
	__m128i zero = _mm_setzero_si128();
	__m128 value = _mm_mul_ps(_mm_loadu_ps(f), _mm_load_ps(exactly_255_x4));
	__m128i ivalue = _mm_packus_epi16(_mm_packs_epi32(_mm_cvtps_epi32(value), zero), zero);
	return _mm_cvtsi128_si32(ivalue);
#elif PPSSPP_ARCH(ARM_NEON)
	const float32x4_t value = vmulq_f32(vld1q_f32(f), vdupq_n_f32(255.0f));
	uint32x4_t ivalue32 = vcvtq_u32_f32(value);
	uint16x4_t ivalue16 = vqmovn_u32(ivalue32);
	uint8x8_t ivalue8 = vqmovn_u16(vcombine_u16(ivalue16, ivalue16));  // Is there no way to avoid the combine here?
	uint32x2_t outValue32 = vreinterpret_u32_u8(ivalue8);
	return vget_lane_u32(outValue32, 0);
#else
	int i4[4];
	for (int i = 0; i < 4; i++) {
		if (f[i] > 1.0f) {
			i4[i] = 255;
		} else if (f[i] < 0.0f) {
			i4[i] = 0;
		} else {
			i4[i] = (int)(f[i] * 255.0f);
		}
	}
	return i4[0] | (i4[1] << 8) | (i4[2] << 16) | (i4[3] << 24);
#endif
}

inline uint32_t Float4ToUint8x4_NoClamp(const float f[4]) {
#ifdef _M_SSE
	// Does actually clamp, no way to avoid it with the pack ops!
	__m128i zero = _mm_setzero_si128();
	__m128 value = _mm_mul_ps(_mm_loadu_ps(f), _mm_load_ps(exactly_255_x4));
	__m128i ivalue = _mm_packus_epi16(_mm_packs_epi32(_mm_cvtps_epi32(value), zero), zero);
	return _mm_cvtsi128_si32(ivalue);
#elif PPSSPP_ARCH(ARM_NEON)
	const float32x4_t value = vmulq_f32(vld1q_f32(f), vdupq_n_f32(255.0f));
	uint32x4_t ivalue32 = vcvtq_u32_f32(value);
	uint16x4_t ivalue16 = vqmovn_u32(ivalue32);
	uint8x8_t ivalue8 = vqmovn_u16(vcombine_u16(ivalue16, ivalue16));  // Is there no way to avoid the combine here?
	uint32x2_t outValue32 = vreinterpret_u32_u8(ivalue8);
	return vget_lane_u32(outValue32, 0);
#else
	u32 i4[4];
	for (int i = 0; i < 4; i++) {
		i4[i] = (int)(f[i] * 255.0f);
	}
	return i4[0] | (i4[1] << 8) | (i4[2] << 16) | (i4[3] << 24);
#endif
}

inline void Uint8x3ToFloat4_AlphaUint8(float f[4], uint32_t u, uint8_t alpha) {
#if defined(_M_SSE) || PPSSPP_ARCH(ARM_NEON)
	Uint8x4ToFloat4(f, (u & 0xFFFFFF) | (alpha << 24));
#else
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = alpha * (1.0f / 255.0f);
#endif
}

inline void Uint8x3ToFloat4(float f[4], uint32_t u) {
#if defined(_M_SSE) || PPSSPP_ARCH(ARM_NEON)
	Uint8x4ToFloat4(f, u & 0xFFFFFF);
#else
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = ((u >> 24) & 0xFF) * (1.0f / 255.0f);
#endif
}

inline void Uint8x3ToFloat3(float f[4], uint32_t u) {
#if defined(_M_SSE) || PPSSPP_ARCH(ARM_NEON)
	float temp[4];
	Uint8x4ToFloat4(temp, u & 0xFFFFFF);
	f[0] = temp[0];
	f[1] = temp[1];
	f[2] = temp[2];
#else
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
#endif
}

inline void Uint8x3ToInt4(int i[4], uint32_t u) {
	i[0] = ((u >> 0) & 0xFF);
	i[1] = ((u >> 8) & 0xFF);
	i[2] = ((u >> 16) & 0xFF);
	i[3] = 0;
}

inline void Uint8x3ToInt4_Alpha(int i[4], uint32_t u, uint8_t alpha) {
	i[0] = ((u >> 0) & 0xFF);
	i[1] = ((u >> 8) & 0xFF);
	i[2] = ((u >> 16) & 0xFF);
	i[3] = alpha;
}

inline void Uint8x3ToFloat4_Alpha(float f[4], uint32_t u, float alpha) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = alpha;
}

inline void Uint8x1ToFloat4(float f[4], uint32_t u) {
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = 0.0f;
	f[2] = 0.0f;
	f[3] = 0.0f;
}

// These are just for readability.

inline void CopyFloat2(float dest[2], const float src[2]) {
	dest[0] = src[0];
	dest[1] = src[1];
}

inline void CopyFloat3(float dest[3], const float src[3]) {
	dest[0] = src[0];
	dest[1] = src[1];
	dest[2] = src[2];
}

inline void CopyFloat4(float dest[4], const float src[4]) {
#ifdef _M_SSE
	_mm_storeu_ps(dest, _mm_loadu_ps(src));
#else
	dest[0] = src[0];
	dest[1] = src[1];
	dest[2] = src[2];
	dest[3] = src[3];
#endif
}

inline void CopyFloat1To4(float dest[4], const float src) {
#ifdef _M_SSE
	_mm_storeu_ps(dest, _mm_set_ss(src));
#else
	dest[0] = src;
	dest[1] = 0.0f;
	dest[2] = 0.0f;
	dest[3] = 0.0f;
#endif
}

inline void CopyFloat2To4(float dest[4], const float src[2]) {
	dest[0] = src[0];
	dest[1] = src[1];
	dest[2] = 0.0f;
	dest[3] = 0.0f;
}

inline void CopyFloat3To4(float dest[4], const float src[3]) {
	dest[0] = src[0];
	dest[1] = src[1];
	dest[2] = src[2];
	dest[3] = 0.0f;
}

inline void CopyMatrix4x4(float dest[16], const float src[16]) {
	memcpy(dest, src, sizeof(float) * 16);
}

inline void ExpandFloat24x3ToFloat4(float dest[4], const uint32_t src[3]) {
#ifdef _M_SSE
	__m128i values = _mm_slli_epi32(_mm_loadu_si128((const __m128i *)src), 8);
	_mm_storeu_si128((__m128i *)dest, values);
#elif PPSSPP_ARCH(ARM_NEON)
	const uint32x4_t values = vshlq_n_u32(vld1q_u32(src), 8);
	vst1q_u32((uint32_t *)dest, values);
#else
	uint32_t temp[4] = { src[0] << 8, src[1] << 8, src[2] << 8, 0 };
	memcpy(dest, temp, sizeof(float) * 4);
#endif
}

// Note: If length is 0.0, it's gonna be left as 0.0 instead of trying to normalize. This is important.
inline void ExpandFloat24x3ToFloat4AndNormalize(float dest[4], const uint32_t src[3]) {
	float temp[4];
	ExpandFloat24x3ToFloat4(temp, src);
	// TODO: Reuse code from NormalizedOr001 and optimize
	float x = temp[0];
	float y = temp[1];
	float z = temp[2];
	float len = sqrtf(x * x + y * y + z * z);
	if (len != 0.0f)
		len = 1.0f / len;
	dest[0] = x * len;
	dest[1] = y * len;
	dest[2] = z * len;
	dest[3] = 0.0f;
}

inline uint32_t BytesToUint32(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return (a) | (b << 8) | (c << 16) | (d << 24);
}

constexpr int32_t SignExtend8ToS32(uint32_t value) {
	// This extends this sign at the 8th bit to the other 24 bits.
	return (int8_t)(value & 0xFF);
}

constexpr uint32_t SignExtend8ToU32(uint32_t value) {
	// Just treat the bits as unsigned.
	return (uint32_t)SignExtend8ToS32(value);
}

constexpr int32_t SignExtend16ToS32(uint32_t value) {
	// Same as SignExtend8toS32, but from the 16th bit.
	return (int16_t)(value & 0xFFFF);
}

constexpr uint32_t SignExtend16ToU32(uint32_t value) {
	return (uint32_t)SignExtend16ToS32(value);
}
