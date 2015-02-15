#pragma once
/**
 * @file SFMT-common.h
 *
 * @brief SIMD oriented Fast Mersenne Twister(SFMT) pseudorandom
 * number generator with jump function. This file includes common functions
 * used in random number generation and jump.
 *
 * @author Mutsuo Saito (Hiroshima University)
 * @author Makoto Matsumoto (The University of Tokyo)
 *
 * Copyright (C) 2006, 2007 Mutsuo Saito, Makoto Matsumoto and Hiroshima
 * University.
 * Copyright (C) 2012 Mutsuo Saito, Makoto Matsumoto, Hiroshima
 * University and The University of Tokyo.
 * All rights reserved.
 *
 * The 3-clause BSD License is applied to this software, see
 * LICENSE.txt
 */
#ifndef SFMT_COMMON_H
#define SFMT_COMMON_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "SFMT.h"

inline static void do_recursion(w128_t * r, w128_t * a, w128_t * b,
				w128_t * c, w128_t * d);

inline static void rshift128(w128_t *out,  w128_t const *in, int shift);
inline static void lshift128(w128_t *out,  w128_t const *in, int shift);

/**
 * This function simulates SIMD 128-bit right shift by the standard C.
 * The 128-bit integer given in in is shifted by (shift * 8) bits.
 * This function simulates the LITTLE ENDIAN SIMD.
 * @param out the output of this function
 * @param in the 128-bit data to be shifted
 * @param shift the shift value
 */
#ifdef ONLY64
inline static void rshift128(w128_t *out, w128_t const *in, int shift) {
    uint64_t th, tl, oh, ol;

    th = ((uint64_t)in->u[2] << 32) | ((uint64_t)in->u[3]);
    tl = ((uint64_t)in->u[0] << 32) | ((uint64_t)in->u[1]);

    oh = th >> (shift * 8);
    ol = tl >> (shift * 8);
    ol |= th << (64 - shift * 8);
    out->u[0] = (uint32_t)(ol >> 32);
    out->u[1] = (uint32_t)ol;
    out->u[2] = (uint32_t)(oh >> 32);
    out->u[3] = (uint32_t)oh;
}
#else
inline static void rshift128(w128_t *out, w128_t const *in, int shift)
{
    uint64_t th, tl, oh, ol;

    th = ((uint64_t)in->u[3] << 32) | ((uint64_t)in->u[2]);
    tl = ((uint64_t)in->u[1] << 32) | ((uint64_t)in->u[0]);

    oh = th >> (shift * 8);
    ol = tl >> (shift * 8);
    ol |= th << (64 - shift * 8);
    out->u[1] = (uint32_t)(ol >> 32);
    out->u[0] = (uint32_t)ol;
    out->u[3] = (uint32_t)(oh >> 32);
    out->u[2] = (uint32_t)oh;
}
#endif
/**
 * This function simulates SIMD 128-bit left shift by the standard C.
 * The 128-bit integer given in in is shifted by (shift * 8) bits.
 * This function simulates the LITTLE ENDIAN SIMD.
 * @param out the output of this function
 * @param in the 128-bit data to be shifted
 * @param shift the shift value
 */
#ifdef ONLY64
inline static void lshift128(w128_t *out, w128_t const *in, int shift) {
    uint64_t th, tl, oh, ol;

    th = ((uint64_t)in->u[2] << 32) | ((uint64_t)in->u[3]);
    tl = ((uint64_t)in->u[0] << 32) | ((uint64_t)in->u[1]);

    oh = th << (shift * 8);
    ol = tl << (shift * 8);
    oh |= tl >> (64 - shift * 8);
    out->u[0] = (uint32_t)(ol >> 32);
    out->u[1] = (uint32_t)ol;
    out->u[2] = (uint32_t)(oh >> 32);
    out->u[3] = (uint32_t)oh;
}
#else
inline static void lshift128(w128_t *out, w128_t const *in, int shift)
{
    uint64_t th, tl, oh, ol;

    th = ((uint64_t)in->u[3] << 32) | ((uint64_t)in->u[2]);
    tl = ((uint64_t)in->u[1] << 32) | ((uint64_t)in->u[0]);

    oh = th << (shift * 8);
    ol = tl << (shift * 8);
    oh |= tl >> (64 - shift * 8);
    out->u[1] = (uint32_t)(ol >> 32);
    out->u[0] = (uint32_t)ol;
    out->u[3] = (uint32_t)(oh >> 32);
    out->u[2] = (uint32_t)oh;
}
#endif
/**
 * This function represents the recursion formula.
 * @param r output
 * @param a a 128-bit part of the internal state array
 * @param b a 128-bit part of the internal state array
 * @param c a 128-bit part of the internal state array
 * @param d a 128-bit part of the internal state array
 */
#ifdef ONLY64
inline static void do_recursion(w128_t *r, w128_t *a, w128_t *b, w128_t *c,
				w128_t *d) {
    w128_t x;
    w128_t y;

    lshift128(&x, a, SFMT_SL2);
    rshift128(&y, c, SFMT_SR2);
    r->u[0] = a->u[0] ^ x.u[0] ^ ((b->u[0] >> SFMT_SR1) & SFMT_MSK2) ^ y.u[0]
	^ (d->u[0] << SFMT_SL1);
    r->u[1] = a->u[1] ^ x.u[1] ^ ((b->u[1] >> SFMT_SR1) & SFMT_MSK1) ^ y.u[1]
	^ (d->u[1] << SFMT_SL1);
    r->u[2] = a->u[2] ^ x.u[2] ^ ((b->u[2] >> SFMT_SR1) & SFMT_MSK4) ^ y.u[2]
	^ (d->u[2] << SFMT_SL1);
    r->u[3] = a->u[3] ^ x.u[3] ^ ((b->u[3] >> SFMT_SR1) & SFMT_MSK3) ^ y.u[3]
	^ (d->u[3] << SFMT_SL1);
}
#else
inline static void do_recursion(w128_t *r, w128_t *a, w128_t *b,
				w128_t *c, w128_t *d)
{
    w128_t x;
    w128_t y;

    lshift128(&x, a, SFMT_SL2);
    rshift128(&y, c, SFMT_SR2);
    r->u[0] = a->u[0] ^ x.u[0] ^ ((b->u[0] >> SFMT_SR1) & SFMT_MSK1)
	^ y.u[0] ^ (d->u[0] << SFMT_SL1);
    r->u[1] = a->u[1] ^ x.u[1] ^ ((b->u[1] >> SFMT_SR1) & SFMT_MSK2)
	^ y.u[1] ^ (d->u[1] << SFMT_SL1);
    r->u[2] = a->u[2] ^ x.u[2] ^ ((b->u[2] >> SFMT_SR1) & SFMT_MSK3)
	^ y.u[2] ^ (d->u[2] << SFMT_SL1);
    r->u[3] = a->u[3] ^ x.u[3] ^ ((b->u[3] >> SFMT_SR1) & SFMT_MSK4)
	^ y.u[3] ^ (d->u[3] << SFMT_SL1);
}
#endif
#endif

#if defined(__cplusplus)
}
#endif
