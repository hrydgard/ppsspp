#pragma once

#include "ppsspp_config.h"

#include "Common/Math/SIMDHeaders.h"
#include "Common/Common.h"

#if PPSSPP_ARCH(SSE2)

inline void fast_matrix_mul_4x4(float *dest, const float *a, const float *b) {
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

#elif PPSSPP_ARCH(LOONGARCH64_LSX)

inline __m128 __lsx_vreplfr2vr_s(float val) {
	typedef union {
		int32_t i;
		float f;
	} FloatInt;
	FloatInt tmpval = {.f = val};
	return (__m128)__lsx_vreplgr2vr_w(tmpval.i);
}

inline void fast_matrix_mul_4x4(float *dest, const float *a, const float *b) {
	__m128 a_col_1 = (__m128)__lsx_vld(a, 0);
	__m128 a_col_2 = (__m128)__lsx_vld(a + 4, 0);
	__m128 a_col_3 = (__m128)__lsx_vld(a + 8, 0);
	__m128 a_col_4 = (__m128)__lsx_vld(a + 12, 0);

	for (int i = 0; i < 16; i += 4) {

		__m128 b1 = __lsx_vreplfr2vr_s(b[i]);
		__m128 b2 = __lsx_vreplfr2vr_s(b[i + 1]);
		__m128 b3 = __lsx_vreplfr2vr_s(b[i + 2]);
		__m128 b4 = __lsx_vreplfr2vr_s(b[i + 3]);

		__m128 result = __lsx_vfmul_s(a_col_1, b1);
		result = __lsx_vfmadd_s(a_col_2, b2, result);
		result = __lsx_vfmadd_s(a_col_3, b3, result);
		result = __lsx_vfmadd_s(a_col_4, b4, result);

		__lsx_vst(result, &dest[i], 0);
	}
}

#elif PPSSPP_ARCH(ARM_NEON)

#ifdef B0
#undef B0
#endif

// From https://developer.arm.com/documentation/102467/0100/Matrix-multiplication-example
inline void fast_matrix_mul_4x4(float *C, const float *A, const float *B) {
	// these are the columns A
	float32x4_t A0;
	float32x4_t A1;
	float32x4_t A2;
	float32x4_t A3;

	// these are the columns B
	float32x4_t B0;
	float32x4_t B1;
	float32x4_t B2;
	float32x4_t B3;

	// these are the columns C
	float32x4_t C0;
	float32x4_t C1;
	float32x4_t C2;
	float32x4_t C3;

	A0 = vld1q_f32(A);
	A1 = vld1q_f32(A + 4);
	A2 = vld1q_f32(A + 8);
	A3 = vld1q_f32(A + 12);

	// Multiply accumulate in 4x1 blocks, i.e. each column in C
	B0 = vld1q_f32(B);
	B1 = vld1q_f32(B + 4);
	B2 = vld1q_f32(B + 8);
	B3 = vld1q_f32(B + 12);

	C0 = vmulq_laneq_f32(A0, B0, 0);
	C0 = vfmaq_laneq_f32(C0, A1, B0, 1);
	C0 = vfmaq_laneq_f32(C0, A2, B0, 2);
	C0 = vfmaq_laneq_f32(C0, A3, B0, 3);
	vst1q_f32(C, C0);

	C1 = vmulq_laneq_f32(A0, B1, 0);
	C1 = vfmaq_laneq_f32(C1, A1, B1, 1);
	C1 = vfmaq_laneq_f32(C1, A2, B1, 2);
	C1 = vfmaq_laneq_f32(C1, A3, B1, 3);
	vst1q_f32(C + 4, C1);

	C2 = vmulq_laneq_f32(A0, B2, 0);
	C2 = vfmaq_laneq_f32(C2, A1, B2, 1);
	C2 = vfmaq_laneq_f32(C2, A2, B2, 2);
	C2 = vfmaq_laneq_f32(C2, A3, B2, 3);
	vst1q_f32(C + 8, C2);

	C3 = vmulq_laneq_f32(A0, B3, 0);
	C3 = vfmaq_laneq_f32(C3, A1, B3, 1);
	C3 = vfmaq_laneq_f32(C3, A2, B3, 2);
	C3 = vfmaq_laneq_f32(C3, A3, B3, 3);
	vst1q_f32(C + 12, C3);
}

#else

inline void fast_matrix_mul_4x4(float * RESTRICT dest, const float * RESTRICT a, const float * RESTRICT b) {
	dest[0] = b[0] * a[0] + b[1] * a[4] + b[2] * a[8] + b[3] * a[12];
	dest[1] = b[0] * a[1] + b[1] * a[5] + b[2] * a[9] + b[3] * a[13];
	dest[2] = b[0] * a[2] + b[1] * a[6] + b[2] * a[10] + b[3] * a[14];
	dest[3] = b[0] * a[3] + b[1] * a[7] + b[2] * a[11] + b[3] * a[15];

	dest[4] = b[4] * a[0] + b[5] * a[4] + b[6] * a[8] + b[7] * a[12];
	dest[5] = b[4] * a[1] + b[5] * a[5] + b[6] * a[9] + b[7] * a[13];
	dest[6] = b[4] * a[2] + b[5] * a[6] + b[6] * a[10] + b[7] * a[14];
	dest[7] = b[4] * a[3] + b[5] * a[7] + b[6] * a[11] + b[7] * a[15];

	dest[8] = b[8] * a[0] + b[9] * a[4] + b[10] * a[8] + b[11] * a[12];
	dest[9] = b[8] * a[1] + b[9] * a[5] + b[10] * a[9] + b[11] * a[13];
	dest[10] = b[8] * a[2] + b[9] * a[6] + b[10] * a[10] + b[11] * a[14];
	dest[11] = b[8] * a[3] + b[9] * a[7] + b[10] * a[11] + b[11] * a[15];

	dest[12] = b[12] * a[0] + b[13] * a[4] + b[14] * a[8] + b[15] * a[12];
	dest[13] = b[12] * a[1] + b[13] * a[5] + b[14] * a[9] + b[15] * a[13];
	dest[14] = b[12] * a[2] + b[13] * a[6] + b[14] * a[10] + b[15] * a[14];
	dest[15] = b[12] * a[3] + b[13] * a[7] + b[14] * a[11] + b[15] * a[15];
}

#endif
