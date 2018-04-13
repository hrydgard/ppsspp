#pragma once

#include <cstdint>
#include <cstring>

#include "Common/Common.h"
#include "ppsspp_config.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif
#if PPSSPP_PLATFORM(ARM_NEON)
#include <arm_neon.h>
#endif

extern const float one_over_255_x4[4];

// Utilities useful for filling in std140-layout uniform buffers, and similar.
// NEON intrinsics: http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0491f/BABDCGGF.html

// LSBs in f[0], etc.
// Could be SSE optimized.
inline void Uint8x4ToFloat4(float f[4], uint32_t u) {
#ifdef _M_SSE
	__m128i zero = _mm_setzero_si128();
	__m128i value = _mm_set1_epi32(u);
	__m128i value32 = _mm_unpacklo_epi16(_mm_unpacklo_epi8(value, zero), zero);
	__m128 fvalues = _mm_mul_ps(_mm_cvtepi32_ps(value32), _mm_load_ps(one_over_255_x4));
	_mm_storeu_ps(f, fvalues);
#elif PPSSPP_PLATFORM(ARM_NEON)
	const float32x4_t one_over = vdupq_n_f32(1.0f/255.0f);
	const uint8x8_t value = vld1_lane_u32(u);
	const uint16x8_t value16 = vmovl_s8(value);
	const uint32x4_t value32 = vmovl_s16(vget_low_s16(value16));
	const float32x4_t valueFloat = vmulq_f32(vcvtq_f32_u32(value32), one_over);
	vst1q_u32((uint32_t *)dest, valueFloat);
#else
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = ((u >> 24) & 0xFF) * (1.0f / 255.0f);
#endif
}

inline void Uint8x3ToFloat4_AlphaUint8(float f[4], uint32_t u, uint8_t alpha) {
#if defined(_M_SSE) || PPSSPP_PLATFORM(ARM_NEON)
	Uint8x4ToFloat4(f, (u & 0xFFFFFF) | (alpha << 24));
#else
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = alpha * (1.0f / 255.0f);
#endif
}

inline void Uint8x3ToFloat4(float f[4], uint32_t u) {
#if defined(_M_SSE) || PPSSPP_PLATFORM(ARM_NEON)
	Uint8x4ToFloat4(f, u & 0xFFFFFF);
#else
	f[0] = ((u >> 0) & 0xFF) * (1.0f / 255.0f);
	f[1] = ((u >> 8) & 0xFF) * (1.0f / 255.0f);
	f[2] = ((u >> 16) & 0xFF) * (1.0f / 255.0f);
	f[3] = ((u >> 24) & 0xFF) * (1.0f / 255.0f);
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

inline void CopyFloat4(float dest[3], const float src[3]) {
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
#elif PPSSPP_PLATFORM(ARM_NEON)
	const uint32x4_t values = vshlq_n_u32(vld1q_u32(&gstate.texscaleu), 8);
	vst1q_u32((uint32_t *)dest, values);
#else
	uint32_t temp[4] = { src[0] << 8, src[1] << 8, src[2] << 8, 0 };
	memcpy(dest, temp, sizeof(float) * 4);
#endif
}

inline uint32_t BytesToUint32(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return (a) | (b << 8) | (c << 16) | (d << 24);
}