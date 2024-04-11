/*
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

#pragma once

void vector_fmul(float *dst, const float *src0, const float *src1, int len);

/**
    * Multiply a vector of floats by a scalar float.  Source and
    * destination vectors must overlap exactly or not at all.
    */
void vector_fmul_scalar(float *dst, const float *src, float mul, int len);

/**
    * Calculate the entry wise product of two vectors of floats, add a third vector of
    * floats and store the result in a vector of floats.
    *
    * @param dst  output vector
    *             constraints: 32-byte aligned
    * @param src0 first input vector
    *             constraints: 32-byte aligned
    * @param src1 second input vector
    *             constraints: 32-byte aligned
    * @param src2 third input vector
    *             constraints: 32-byte aligned
    * @param len  number of elements in the input
    *             constraints: multiple of 16
    */
void vector_fmul_add(float *dst, const float *src0, const float *src1, const float *src2, int len);

/**
    * Calculate the entry wise product of two vectors of floats, and store the result
    * in a vector of floats. The second vector of floats is iterated over
    * in reverse order.
    *
    * @param dst  output vector
    *             constraints: 32-byte aligned
    * @param src0 first input vector
    *             constraints: 32-byte aligned
    * @param src1 second input vector
    *             constraints: 32-byte aligned
    * @param len  number of elements in the input
    *             constraints: multiple of 16
    */
void vector_fmul_reverse(float *dst, const float *src0, const float *src1, int len);
