#pragma once

#include "ppsspp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// A mini library of 4x4 matrix muls.
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
// Hard link to SSE implementations on x86/amd64
#define fast_matrix_mul_4x4 fast_matrix_mul_4x4_sse
#elif PPSSPP_ARCH(ARM_NEON)
#define fast_matrix_mul_4x4 fast_matrix_mul_4x4_neon
#elif PPSSPP_ARCH(LOONGARCH64_LSX)
#define fast_matrix_mul_4x4 fast_matrix_mul_4x4_lsx
#else
#define fast_matrix_mul_4x4 fast_matrix_mul_4x4_c
#endif

extern void fast_matrix_mul_4x4_c(float *dest, const float *a, const float *b);

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <emmintrin.h>

#include "fast_matrix.h"

inline void fast_matrix_mul_4x4_sse(float *dest, const float *a, const float *b) {
	int i;
	__m128 a_col_1 = _mm_loadu_ps(a);
	__m128 a_col_2 = _mm_loadu_ps(&a[4]);
	__m128 a_col_3 = _mm_loadu_ps(&a[8]);
	__m128 a_col_4 = _mm_loadu_ps(&a[12]);

	for (i = 0; i < 16; i += 4) {
		__m128 r_col = _mm_mul_ps(a_col_1, _mm_set1_ps(b[i]));
		r_col = _mm_add_ps(r_col, _mm_mul_ps(a_col_2, _mm_set1_ps(b[i + 1])));
		r_col = _mm_add_ps(r_col, _mm_mul_ps(a_col_3, _mm_set1_ps(b[i + 2])));
		r_col = _mm_add_ps(r_col, _mm_mul_ps(a_col_4, _mm_set1_ps(b[i + 3])));
		_mm_storeu_ps(&dest[i], r_col);
	}
}

#elif PPSSPP_ARCH(ARM_NEON)

#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif

#if PPSSPP_ARCH(ARM)
static inline float32x4_t vfmaq_laneq_f32(float32x4_t _s, float32x4_t _a, float32x4_t _b, int lane) {
	if (lane == 0)      return vmlaq_lane_f32(_s, _a, vget_low_f32(_b), 0);
	else if (lane == 1) return vmlaq_lane_f32(_s, _a, vget_low_f32(_b), 1);
	else if (lane == 2) return vmlaq_lane_f32(_s, _a, vget_high_f32(_b), 0);
	else if (lane == 3) return vmlaq_lane_f32(_s, _a, vget_high_f32(_b), 1);
	else return vdupq_n_f32(0.f);
}
#endif

// From https://developer.arm.com/documentation/102467/0100/Matrix-multiplication-example
inline void fast_matrix_mul_4x4_neon(float *C, const float *A, const float *B) {
	// these are the columns A
	float32x4_t A0 = vld1q_f32(A);
	float32x4_t A1 = vld1q_f32(A + 4);
	float32x4_t A2 = vld1q_f32(A + 8);
	float32x4_t A3 = vld1q_f32(A + 12);

	// Multiply accumulate in 4x1 blocks, i.e. each column in C
	float32x4_t B0 = vld1q_f32(B);
	float32x4_t C0 = vmulq_laneq_f32(A0, B0, 0);
	C0 = vfmaq_laneq_f32(C0, A1, B0, 1);
	C0 = vfmaq_laneq_f32(C0, A2, B0, 2);
	C0 = vfmaq_laneq_f32(C0, A3, B0, 3);
	vst1q_f32(C, C0);

	float32x4_t B1 = vld1q_f32(B + 4);
	float32x4_t C1 = vmulq_laneq_f32(A0, B1, 0);
	C1 = vfmaq_laneq_f32(C1, A1, B1, 1);
	C1 = vfmaq_laneq_f32(C1, A2, B1, 2);
	C1 = vfmaq_laneq_f32(C1, A3, B1, 3);
	vst1q_f32(C + 4, C1);

	float32x4_t B2 = vld1q_f32(B + 8);
	float32x4_t C2 = vmulq_laneq_f32(A0, B2, 0);
	C2 = vfmaq_laneq_f32(C2, A1, B2, 1);
	C2 = vfmaq_laneq_f32(C2, A2, B2, 2);
	C2 = vfmaq_laneq_f32(C2, A3, B2, 3);
	vst1q_f32(C + 8, C2);

	float32x4_t B3 = vld1q_f32(B + 12);
	float32x4_t C3 = vmulq_laneq_f32(A0, B3, 0);
	C3 = vfmaq_laneq_f32(C3, A1, B3, 1);
	C3 = vfmaq_laneq_f32(C3, A2, B3, 2);
	C3 = vfmaq_laneq_f32(C3, A3, B3, 3);
	vst1q_f32(C + 12, C3);
}

#endif

#ifdef __cplusplus
}  // extern "C"
#endif
