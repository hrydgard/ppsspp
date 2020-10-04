#include "ppsspp_config.h"

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

#endif
