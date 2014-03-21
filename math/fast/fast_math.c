#include "fast_math.h"
#include "fast_matrix.h"

void InitFastMath(int enableNEON) {
#if !defined(_M_IX86) && !defined(_M_X64)
	if (enableNEON) {
		fast_matrix_mul_4x4 = &fast_matrix_mul_4x4_neon;
	}
#endif
}