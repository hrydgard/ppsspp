#include <emmintrin.h>

#include "fast_matrix.h"

void fast_matrix_mul_4x4_sse(float *dest, const float *a, const float *b) {
	for (int i = 0; i < 16; i += 4) {
		__m128 a_col_1 = _mm_load_ps(a);
		__m128 a_col_2 = _mm_load_ps(&a[4]);
		__m128 a_col_3 = _mm_load_ps(&a[8]);
		__m128 a_col_4 = _mm_load_ps(&a[12]);

		__m128 r_col = _mm_mul_ps(a_col_1, _mm_set1_ps(b[i]));
		r_col = _mm_add_ps(r_col, _mm_mul_ps(a_col_2, _mm_set1_ps(b[i + 1])));
		r_col = _mm_add_ps(r_col, _mm_mul_ps(a_col_3, _mm_set1_ps(b[i + 2])));
		r_col = _mm_add_ps(r_col, _mm_mul_ps(a_col_4, _mm_set1_ps(b[i + 3])));
		_mm_store_ps(&dest[i], r_col);
	}
}
