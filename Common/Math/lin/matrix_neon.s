@
@ NEON matrix multiplication examples
@

.syntax unified

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

    .global matrix_mul_float
matrix_mul_float:
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

    mov         pc, lr                      @ return to caller





@ Macro: mul_col_s16
@
@ Multiply a four s16 element column of a matrix by the columns of a second matrix
@ to give a column of results. Elements are assumed to be in Q1.14 format.
@ Inputs:   col_d - d register containing a column of the matrix
@ Outputs:  res_d - d register containing the column of results 
@ Corrupts: register q12
@ Assumes:  the second matrix columns are in registers d16-d19 in column major order
@

    .macro mul_col_s16 res_d, col_d
    vmull.s16   q12, d16, \col_d[0]         @ multiply col element 0 by matrix col 0
    vmlal.s16   q12, d17, \col_d[1]         @ multiply-acc col element 1 by matrix col 1
    vmlal.s16   q12, d18, \col_d[2]         @ multiply-acc col element 2 by matrix col 2
    vmlal.s16   q12, d19, \col_d[3]         @ multiply-acc col element 3 by matrix col 3
    vqrshrn.s32 \res_d, q12, #14            @ shift right and narrow accumulator into
                                            @  Q1.14 fixed point format, with saturation
    .endm

@
@ matrix_mul_fixed:
@ Calculate 4x4 (matrix 0) * (matrix 1) and store to result 4x4 matrix.
@  matrix 0, matrix 1 and result pointers can be the same,
@  ie. my_matrix = my_matrix * my_matrix is possible
@
@ r0 = pointer to 4x4 result matrix, Q1.14 fixed point, column major order
@ r1 = pointer to 4x4 matrix 0, Q1.14 fixed point, column major order
@ r2 = pointer to 4x4 matrix 1, Q1.14 fixed point, column major order
@

    .global matrix_mul_fixed
matrix_mul_fixed:
    vld1.16     {d16-d19}, [r1]             @ load sixteen elements of matrix 0
    vld1.16     {d0-d3}, [r2]               @ load sixteen elements of matrix 1

    mul_col_s16 d4, d0                      @ matrix 0 * matrix 1 col 0
    mul_col_s16 d5, d1                      @ matrix 0 * matrix 1 col 1
    mul_col_s16 d6, d2                      @ matrix 0 * matrix 1 col 2
    mul_col_s16 d7, d3                      @ matrix 0 * matrix 1 col 3

    vst1.16     {d4-d7}, [r0]               @ store sixteen elements of result

    mov         pc, lr                      @ return to caller










