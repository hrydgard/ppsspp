#pragma once

#include "ppsspp_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// A mini library of matrix math kernels.

// TODO: Really need to wrap this block in a macro or something, will get repetitive.

typedef void(*fptr_fast_matrix_mul_4x4)(float *dest, const float *a, const float *b);
extern void fast_matrix_mul_4x4_c(float *dest, const float *a, const float *b);
extern void fast_matrix_mul_4x4_neon(float *dest, const float *a, const float *b);
extern void fast_matrix_mul_4x4_sse(float *dest, const float *a, const float *b);

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
// Hard link to SSE implementations on x86/amd64
#define fast_matrix_mul_4x4 fast_matrix_mul_4x4_sse
#elif PPSSPP_ARCH(ARM64)
#define fast_matrix_mul_4x4 fast_matrix_mul_4x4_c
#else
extern fptr_fast_matrix_mul_4x4 fast_matrix_mul_4x4;
#endif


#ifdef __cplusplus
}  // extern "C"
#endif
