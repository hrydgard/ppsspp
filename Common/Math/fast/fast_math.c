#include "ppsspp_config.h"

#include "fast_math.h"
#include "fast_matrix.h"

void InitFastMath() {
#ifndef _MSC_VER
#if PPSSPP_ARCH(ARM_NEON) && !PPSSPP_ARCH(ARM64)
		fast_matrix_mul_4x4 = &fast_matrix_mul_4x4_neon;
#endif
#endif
}
