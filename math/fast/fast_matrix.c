#include "fast_math.h"
#include "fast_matrix.h"

#define xx 0
#define xy 1
#define xz 2
#define xw 3
#define yx 4
#define yy 5
#define yz 6
#define yw 7
#define zx 8
#define zy 9
#define zz 10
#define zw 11
#define wx 12
#define wy 13
#define wz 14
#define ww 15

void fast_matrix_mul_4x4_c(float *dest, const float *a, const float *b) {
	dest[xx] = b[xx] * a[xx] + b[xy] * a[yx] + b[xz] * a[zx] + b[xw] * a[wx];
	dest[xy] = b[xx] * a[xy] + b[xy] * a[yy] + b[xz] * a[zy] + b[xw] * a[wy];
	dest[xz] = b[xx] * a[xz] + b[xy] * a[yz] + b[xz] * a[zz] + b[xw] * a[wz];
	dest[xw] = b[xx] * a[xw] + b[xy] * a[yw] + b[xz] * a[zw] + b[xw] * a[ww];

	dest[yx] = b[yx] * a[xx] + b[yy] * a[yx] + b[yz] * a[zx] + b[yw] * a[wx];
	dest[yy] = b[yx] * a[xy] + b[yy] * a[yy] + b[yz] * a[zy] + b[yw] * a[wy];
	dest[yz] = b[yx] * a[xz] + b[yy] * a[yz] + b[yz] * a[zz] + b[yw] * a[wz];
	dest[yw] = b[yx] * a[xw] + b[yy] * a[yw] + b[yz] * a[zw] + b[yw] * a[ww];

	dest[zx] = b[zx] * a[xx] + b[zy] * a[yx] + b[zz] * a[zx] + b[zw] * a[wx];
	dest[zy] = b[zx] * a[xy] + b[zy] * a[yy] + b[zz] * a[zy] + b[zw] * a[wy];
	dest[zz] = b[zx] * a[xz] + b[zy] * a[yz] + b[zz] * a[zz] + b[zw] * a[wz];
	dest[zw] = b[zx] * a[xw] + b[zy] * a[yw] + b[zz] * a[zw] + b[zw] * a[ww];

	dest[wx] = b[wx] * a[xx] + b[wy] * a[yx] + b[wz] * a[zx] + b[ww] * a[wx];
	dest[wy] = b[wx] * a[xy] + b[wy] * a[yy] + b[wz] * a[zy] + b[ww] * a[wy];
	dest[wz] = b[wx] * a[xz] + b[wy] * a[yz] + b[wz] * a[zz] + b[ww] * a[wz];
	dest[ww] = b[wx] * a[xw] + b[wy] * a[yw] + b[wz] * a[zw] + b[ww] * a[ww];
}

#ifndef fast_matrix_mul_4x4
fptr_fast_matrix_mul_4x4 fast_matrix_mul_4x4 = &fast_matrix_mul_4x4_c;
#endif