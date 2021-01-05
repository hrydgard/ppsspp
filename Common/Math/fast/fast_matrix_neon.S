#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM_NEON) && !PPSSPP_ARCH(ARM64)

	.syntax unified		// Allow both ARM and Thumb-2 instructions
	.text
	.align 2		// Align the function code to a 4-byte (2^n) word boundary.
	.arm			// Use ARM instructions instead of Thumb.

@ From ARM samples
@
@ matrix_mul_float:
@ Calculate 4x4 (matrix 0) * (matrix 1) and store to result 4x4 matrix.
@  matrix 0, matrix 1 and result pointers can be the same,
@  ie. my_matrix = my_matrix * my_matrix is possible.
@
@ r0 = pointer to 4x4 result matrix, single precision floats, column major order
@ r1 = pointer to 4x4 matrix 0, single precision floats, column major order
@ r2 = pointer to 4x4 matrix 1, single precision floats, column major order
@

	.globl _fast_matrix_mul_4x4_neon
_fast_matrix_mul_4x4_neon:
	.globl fast_matrix_mul_4x4_neon
fast_matrix_mul_4x4_neon:
    vld1.32     {d16-d19}, [r1]!            @ load first eight elements of matrix 0
    vld1.32     {d20-d23}, [r1]!            @ load second eight elements of matrix 0
    vld1.32     {d0-d3}, [r2]!              @ load first eight elements of matrix 1
    vld1.32     {d4-d7}, [r2]!              @ load second eight elements of matrix 1

    vmul.f32    q12, q8, d0[0]              @ rslt col0  = (mat0 col0) * (mat1 col0 elt0)
    vmul.f32    q13, q8, d2[0]              @ rslt col1  = (mat0 col0) * (mat1 col1 elt0)
    vmul.f32    q14, q8, d4[0]              @ rslt col2  = (mat0 col0) * (mat1 col2 elt0)
    vmul.f32    q15, q8, d6[0]              @ rslt col3  = (mat0 col0) * (mat1 col3 elt0)

    vmla.f32    q12, q9, d0[1]              @ rslt col0 += (mat0 col1) * (mat1 col0 elt1)
    vmla.f32    q13, q9, d2[1]              @ rslt col1 += (mat0 col1) * (mat1 col1 elt1)
    vmla.f32    q14, q9, d4[1]              @ rslt col2 += (mat0 col1) * (mat1 col2 elt1)
    vmla.f32    q15, q9, d6[1]              @ rslt col3 += (mat0 col1) * (mat1 col3 elt1)

    vmla.f32    q12, q10, d1[0]             @ rslt col0 += (mat0 col2) * (mat1 col0 elt2)
    vmla.f32    q13, q10, d3[0]             @ rslt col1 += (mat0 col2) * (mat1 col1 elt2)
    vmla.f32    q14, q10, d5[0]             @ rslt col2 += (mat0 col2) * (mat1 col2 elt2)
    vmla.f32    q15, q10, d7[0]             @ rslt col3 += (mat0 col2) * (mat1 col2 elt2)

    vmla.f32    q12, q11, d1[1]             @ rslt col0 += (mat0 col3) * (mat1 col0 elt3)
    vmla.f32    q13, q11, d3[1]             @ rslt col1 += (mat0 col3) * (mat1 col1 elt3)
    vmla.f32    q14, q11, d5[1]             @ rslt col2 += (mat0 col3) * (mat1 col2 elt3)
    vmla.f32    q15, q11, d7[1]             @ rslt col3 += (mat0 col3) * (mat1 col3 elt3)

    vst1.32     {d24-d27}, [r0]!            @ store first eight elements of result
    vst1.32     {d28-d31}, [r0]!            @ store second eight elements of result
	bx lr

#endif
