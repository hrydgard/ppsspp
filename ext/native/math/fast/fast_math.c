#include "base/arch.h"
#include "fast_math.h"
#include "fast_matrix.h"

void InitFastMath(int enableNEON) {
	// Every architecture has its own define. This needs to be added to.
	if (enableNEON) {
#if defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7S__)
		fast_matrix_mul_4x4 = &fast_matrix_mul_4x4_neon;
#endif
	}
}
