/*
 * Copyright 2005 Balatoni Denes
 * Copyright 2006 Loren Merritt
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "compat.h"
#include "float_dsp.h"
#include "mem.h"

void vector_fmul(float *dst, const float *src0, const float *src1, int len) {
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i];
}

void vector_fmul_scalar(float *dst, const float *src, float mul, int len) {
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src[i] * mul;
}

void vector_fmul_add(float *dst, const float *src0, const float *src1, const float *src2, int len) {
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i] + src2[i];
}

void vector_fmul_reverse(float *dst, const float *src0, const float *src1, int len) {
    int i;
    src1 += len-1;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[-i];
}
