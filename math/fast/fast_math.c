#include "base/arch.h"
#include "fast_math.h"
#include "fast_matrix.h"

void InitFastMath(int enableNEON) {
#if !defined(_M_IX86) && !defined(_M_X64) && defined(ARMEABI_V7A)
	if (enableNEON) {
		fast_matrix_mul_4x4 = &fast_matrix_mul_4x4_neon;
	}
#endif
}
