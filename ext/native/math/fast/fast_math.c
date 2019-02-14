#include "base/arch.h"
#include "fast_math.h"
#include "fast_matrix.h"

void InitFastMath(int enableNEON) {
	// Every architecture has its own define. This needs to be added to.
	if (enableNEON) {
#ifndef _MSC_VER
#if PPSSPP_ARCH(ARM_NEON) && !PPSSPP_ARCH(ARM64)
		fast_matrix_mul_4x4 = &fast_matrix_mul_4x4_neon;
#endif
#endif
	}
}
