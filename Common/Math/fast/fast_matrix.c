#include "ppsspp_config.h"

#include "Common/Math/CrossSIMD.h"

#include "fast_matrix.h"

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <emmintrin.h>

#include "fast_matrix.h"

void fast_matrix_mul_4x4_sse(float *dest, const float *a, const float *b) {
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
void fast_matrix_mul_4x4_neon(float *C, const float *A, const float *B) {
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
	C0 = vmulq_laneq_f32(A0, B0, 0);
	C0 = vfmaq_laneq_f32(C0, A1, B0, 1);
	C0 = vfmaq_laneq_f32(C0, A2, B0, 2);
	C0 = vfmaq_laneq_f32(C0, A3, B0, 3);
	vst1q_f32(C, C0);

	B1 = vld1q_f32(B + 4);
	C1 = vmulq_laneq_f32(A0, B1, 0);
	C1 = vfmaq_laneq_f32(C1, A1, B1, 1);
	C1 = vfmaq_laneq_f32(C1, A2, B1, 2);
	C1 = vfmaq_laneq_f32(C1, A3, B1, 3);
	vst1q_f32(C + 4, C1);

	B2 = vld1q_f32(B + 8);
	C2 = vmulq_laneq_f32(A0, B2, 0);
	C2 = vfmaq_laneq_f32(C2, A1, B2, 1);
	C2 = vfmaq_laneq_f32(C2, A2, B2, 2);
	C2 = vfmaq_laneq_f32(C2, A3, B2, 3);
	vst1q_f32(C + 8, C2);

	B3 = vld1q_f32(B + 12);
	C3 = vmulq_laneq_f32(A0, B3, 0);
	C3 = vfmaq_laneq_f32(C3, A1, B3, 1);
	C3 = vfmaq_laneq_f32(C3, A2, B3, 2);
	C3 = vfmaq_laneq_f32(C3, A3, B3, 3);
	vst1q_f32(C + 12, C3);
}

#else

#define xx 0
#define xy 1
#define xz 2
#define xw 3
#define yx 4
#define yy 5
#define yz 6
#define yw 7
#define zx 8
#define zy 9
#define zz 10
#define zw 11
#define wx 12
#define wy 13
#define wz 14
#define ww 15

void fast_matrix_mul_4x4_c(float *dest, const float *a, const float *b) {
	dest[xx] = b[xx] * a[xx] + b[xy] * a[yx] + b[xz] * a[zx] + b[xw] * a[wx];
	dest[xy] = b[xx] * a[xy] + b[xy] * a[yy] + b[xz] * a[zy] + b[xw] * a[wy];
	dest[xz] = b[xx] * a[xz] + b[xy] * a[yz] + b[xz] * a[zz] + b[xw] * a[wz];
	dest[xw] = b[xx] * a[xw] + b[xy] * a[yw] + b[xz] * a[zw] + b[xw] * a[ww];

	dest[yx] = b[yx] * a[xx] + b[yy] * a[yx] + b[yz] * a[zx] + b[yw] * a[wx];
	dest[yy] = b[yx] * a[xy] + b[yy] * a[yy] + b[yz] * a[zy] + b[yw] * a[wy];
	dest[yz] = b[yx] * a[xz] + b[yy] * a[yz] + b[yz] * a[zz] + b[yw] * a[wz];
	dest[yw] = b[yx] * a[xw] + b[yy] * a[yw] + b[yz] * a[zw] + b[yw] * a[ww];

	dest[zx] = b[zx] * a[xx] + b[zy] * a[yx] + b[zz] * a[zx] + b[zw] * a[wx];
	dest[zy] = b[zx] * a[xy] + b[zy] * a[yy] + b[zz] * a[zy] + b[zw] * a[wy];
	dest[zz] = b[zx] * a[xz] + b[zy] * a[yz] + b[zz] * a[zz] + b[zw] * a[wz];
	dest[zw] = b[zx] * a[xw] + b[zy] * a[yw] + b[zz] * a[zw] + b[zw] * a[ww];

	dest[wx] = b[wx] * a[xx] + b[wy] * a[yx] + b[wz] * a[zx] + b[ww] * a[wx];
	dest[wy] = b[wx] * a[xy] + b[wy] * a[yy] + b[wz] * a[zy] + b[ww] * a[wy];
	dest[wz] = b[wx] * a[xz] + b[wy] * a[yz] + b[wz] * a[zz] + b[ww] * a[wz];
	dest[ww] = b[wx] * a[xw] + b[wy] * a[yw] + b[wz] * a[zw] + b[ww] * a[ww];
}

#endif
