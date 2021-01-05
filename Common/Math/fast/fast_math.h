#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Fast Math
// A mini library of math kernels. These should be large enough to be worth calling
// as functions, and generic enough to fit in the "native" library (not PSP specific stuff).

// NEON versions are dynamically selected at runtime, when you call InitFastMath.

// SSE versions are hard linked at compile time.

// See fast_matrix.h for the first set of functions.

void InitFastMath(int enableNEON);

#ifdef __cplusplus
}
#endif