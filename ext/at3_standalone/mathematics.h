/*
 * copyright (c) 2005-2012 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVUTIL_MATHEMATICS_H
#define AVUTIL_MATHEMATICS_H

#include <stdint.h>
#include <math.h>
#include <limits.h>

#include "compat.h"

extern const uint32_t ff_inverse[257];
extern const uint8_t ff_sqrt_tab[256];

static inline int sign_extend(int val, unsigned bits)
{
	unsigned shift = 8 * sizeof(int) - bits;
	union { unsigned u; int s; } v = { (unsigned)val << shift };
	return v.s >> shift;
}

static inline unsigned zero_extend(unsigned val, unsigned bits)
{
	return (val << ((8 * sizeof(int)) - bits)) >> ((8 * sizeof(int)) - bits);
}

#ifndef NEG_SSR32
#   define NEG_SSR32(a,s) ((( int32_t)(a))>>(32-(s)))
#endif

#ifndef NEG_USR32
#   define NEG_USR32(a,s) (((uint32_t)(a))>>(32-(s)))
#endif

#ifndef M_LOG2_10
#define M_LOG2_10      3.32192809488736234787  /* log_2 10 */
#endif
#ifndef M_PHI
#define M_PHI          1.61803398874989484820   /* phi / golden ratio */
#endif
#ifndef M_PI
#define M_PI           3.14159265358979323846  /* pi */
#endif
#ifndef M_PI_2
#define M_PI_2         1.57079632679489661923  /* pi/2 */
#endif
#ifndef M_SQRT1_2
#define M_SQRT1_2      0.70710678118654752440  /* 1/sqrt(2) */
#endif
#ifndef M_SQRT2
#define M_SQRT2        1.41421356237309504880  /* sqrt(2) */
#endif

/**
 * @addtogroup lavu_math
 * @{
 */


enum AVRounding {
    AV_ROUND_ZERO     = 0, ///< Round toward zero.
    AV_ROUND_INF      = 1, ///< Round away from zero.
    AV_ROUND_DOWN     = 2, ///< Round toward -infinity.
    AV_ROUND_UP       = 3, ///< Round toward +infinity.
    AV_ROUND_NEAR_INF = 5, ///< Round to nearest and halfway cases away from zero.
    AV_ROUND_PASS_MINMAX = 8192, ///< Flag to pass INT64_MIN/MAX through instead of rescaling, this avoids special cases for AV_NOPTS_VALUE
};

/**
 * rational number numerator/denominator
 */
typedef struct AVRational {
	int num; ///< numerator
	int den; ///< denominator
} AVRational;

/**
 * Compute the greatest common divisor of a and b.
 *
 * @return gcd of a and b up to sign; if a >= 0 and b >= 0, return value is >= 0;
 * if a == 0 and b == 0, returns 0.
 */
int64_t av_gcd(int64_t a, int64_t b);

/**
 * Rescale a 64-bit integer with rounding to nearest.
 * A simple a*b/c isn't possible as it can overflow.
 */
int64_t av_rescale(int64_t a, int64_t b, int64_t c);

/**
 * Rescale a 64-bit integer with specified rounding.
 * A simple a*b/c isn't possible as it can overflow.
 *
 * @return rescaled value a, or if AV_ROUND_PASS_MINMAX is set and a is
 *         INT64_MIN or INT64_MAX then a is passed through unchanged.
 */
int64_t av_rescale_rnd(int64_t a, int64_t b, int64_t c, enum AVRounding);

/**
 * Rescale a 64-bit integer by 2 rational numbers.
 */
int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq);

/**
 * Rescale a 64-bit integer by 2 rational numbers with specified rounding.
 *
 * @return rescaled value a, or if AV_ROUND_PASS_MINMAX is set and a is
 *         INT64_MIN or INT64_MAX then a is passed through unchanged.
 */
int64_t av_rescale_q_rnd(int64_t a, AVRational bq, AVRational cq,
                         enum AVRounding);


/**
 * Create a rational.
 * Useful for compilers that do not support compound literals.
 * @note  The return value is not reduced.
 */
static inline AVRational av_make_q(int num, int den)
{
	AVRational r = { num, den };
	return r;
}

/**
 * Compare two rationals.
 * @param a first rational
 * @param b second rational
 * @return 0 if a==b, 1 if a>b, -1 if a<b, and INT_MIN if one of the
 * values is of the form 0/0
 */
static inline int av_cmp_q(AVRational a, AVRational b) {
	const int64_t tmp = a.num * (int64_t)b.den - b.num * (int64_t)a.den;

	if (tmp) return (int)((tmp ^ a.den ^ b.den) >> 63) | 1;
	else if (b.den && a.den) return 0;
	else if (a.num && b.num) return (a.num >> 31) - (b.num >> 31);
	else                    return INT_MIN;
}

/**
 * Convert rational to double.
 * @param a rational to convert
 * @return (double) a
 */
static inline double av_q2d(AVRational a) {
	return a.num / (double)a.den;
}

/**
 * Reduce a fraction.
 * This is useful for framerate calculations.
 * @param dst_num destination numerator
 * @param dst_den destination denominator
 * @param num source numerator
 * @param den source denominator
 * @param max the maximum allowed for dst_num & dst_den
 * @return 1 if exact, 0 otherwise
 */
int av_reduce(int *dst_num, int *dst_den, int64_t num, int64_t den, int64_t max);

/**
 * Multiply two rationals.
 * @param b first rational
 * @param c second rational
 * @return b*c
 */
AVRational av_mul_q(AVRational b, AVRational c);

/**
 * Divide one rational by another.
 * @param b first rational
 * @param c second rational
 * @return b/c
 */
AVRational av_div_q(AVRational b, AVRational c);

/**
 * Add two rationals.
 * @param b first rational
 * @param c second rational
 * @return b+c
 */
AVRational av_add_q(AVRational b, AVRational c);

/**
 * Invert a rational.
 * @param q value
 * @return 1 / q
 */
static inline AVRational av_inv_q(AVRational q)
{
	AVRational r = { q.den, q.num };
	return r;
}

/**
 * Convert a double precision floating point number to a rational.
 * inf is expressed as {1,0} or {-1,0} depending on the sign.
 *
 * @param d double to convert
 * @param max the maximum allowed numerator and denominator
 * @return (AVRational) d
 */
AVRational av_d2q(double d, int max);


/**
 * Clear high bits from an unsigned integer starting with specific bit position
 * @param  a value to clip
 * @param  p bit position to clip at
 * @return clipped value
 */
static inline unsigned av_mod_uintp2(unsigned a, unsigned p)
{
	return a & ((1 << p) - 1);
}

/**
 * Count number of bits set to one in x
 * @param x value to count bits of
 * @return the number of bits set to one in x
 */
static inline int av_popcount(uint32_t x)
{
	x -= (x >> 1) & 0x55555555;
	x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
	x = (x + (x >> 4)) & 0x0F0F0F0F;
	x += x >> 8;
	return (x + (x >> 16)) & 0x3F;
}

/**
 * Count number of bits set to one in x
 * @param x value to count bits of
 * @return the number of bits set to one in x
 */
static inline int av_popcount64(uint64_t x)
{
	return av_popcount((uint32_t)x) + av_popcount((uint32_t)(x >> 32));
}

#endif /* AVUTIL_MATHEMATICS_H */
