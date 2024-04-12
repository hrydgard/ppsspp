/*
 * Copyright (c) 2005-2012 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * miscellaneous math routines and tables
 */

#include <stdint.h>
#include <limits.h>

#include "mathematics.h"
#include "compat.h"

/* Stein's binary GCD algorithm:
 * https://en.wikipedia.org/wiki/Binary_GCD_algorithm */
int64_t av_gcd(int64_t a, int64_t b) {
    int za, zb, k;
    int64_t u, v;
    if (a == 0)
        return b;
    if (b == 0)
        return a;
    za = ff_ctzll(a);
    zb = ff_ctzll(b);
    k  = FFMIN(za, zb);
    u = llabs(a >> za);
    v = llabs(b >> zb);
    while (u != v) {
        if (u > v)
            FFSWAP(int64_t, v, u);
        v -= u;
        v >>= ff_ctzll(v);
    }
    return (uint64_t)u << k;
}

int64_t av_rescale_rnd(int64_t a, int64_t b, int64_t c, AVRounding rnd)
{
    int64_t r = 0;
    av_assert2(c > 0);
    av_assert2(b >=0);
    av_assert2((unsigned)(rnd&~AV_ROUND_PASS_MINMAX)<=5 && (rnd&~AV_ROUND_PASS_MINMAX)!=4);

    if (c <= 0 || b < 0 || !((unsigned)(rnd&~AV_ROUND_PASS_MINMAX)<=5 && (rnd&~AV_ROUND_PASS_MINMAX)!=4))
        return INT64_MIN;

    if (rnd & AV_ROUND_PASS_MINMAX) {
        if (a == INT64_MIN || a == INT64_MAX)
            return a;
        rnd = (AVRounding)(rnd - AV_ROUND_PASS_MINMAX);
    }

    if (a < 0)
        return -(uint64_t)av_rescale_rnd(-FFMAX(a, -INT64_MAX), b, c, (AVRounding)((int)rnd ^ (((int)rnd >> 1) & 1)));

    if (rnd == AV_ROUND_NEAR_INF)
        r = c / 2;
    else if (rnd & 1)
        r = c - 1;

    if (b <= INT_MAX && c <= INT_MAX) {
        if (a <= INT_MAX)
            return (a * b + r) / c;
        else {
            int64_t ad = a / c;
            int64_t a2 = (a % c * b + r) / c;
            if (ad >= INT32_MAX && b && ad > (INT64_MAX - a2) / b)
                return INT64_MIN;
            return ad * b + a2;
        }
    } else {
#if 1
        uint64_t a0  = a & 0xFFFFFFFF;
        uint64_t a1  = a >> 32;
        uint64_t b0  = b & 0xFFFFFFFF;
        uint64_t b1  = b >> 32;
        uint64_t t1  = a0 * b1 + a1 * b0;
        uint64_t t1a = t1 << 32;
        int i;

        a0  = a0 * b0 + t1a;
        a1  = a1 * b1 + (t1 >> 32) + (a0 < t1a);
        a0 += r;
        a1 += (int64_t)a0 < r;

        for (i = 63; i >= 0; i--) {
            a1 += a1 + ((a0 >> i) & 1);
            t1 += t1;
            if (c <= (int64_t)a1) {
                a1 -= c;
                t1++;
            }
        }
        if (t1 > INT64_MAX)
            return INT64_MIN;
        return t1;
    }
#else
        AVInteger ai;
        ai = av_mul_i(av_int2i(a), av_int2i(b));
        ai = av_add_i(ai, av_int2i(r));

        return av_i2int(av_div_i(ai, av_int2i(c)));
    }
#endif
}

int64_t av_rescale(int64_t a, int64_t b, int64_t c)
{
    return av_rescale_rnd(a, b, c, AV_ROUND_NEAR_INF);
}

int64_t av_rescale_q_rnd(int64_t a, AVRational bq, AVRational cq, AVRounding rnd)
{
    int64_t b = bq.num * (int64_t)cq.den;
    int64_t c = cq.num * (int64_t)bq.den;
    return av_rescale_rnd(a, b, c, rnd);
}

int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq)
{
    return av_rescale_q_rnd(a, bq, cq, AV_ROUND_NEAR_INF);
}

int av_reduce(int *dst_num, int *dst_den,
	int64_t num, int64_t den, int64_t max)
{
	AVRational a0 = { 0, 1 }, a1 = { 1, 0 };
	int sign = (num < 0) ^ (den < 0);
	int64_t gcd = av_gcd(FFABS(num), FFABS(den));

	if (gcd) {
		num = FFABS(num) / gcd;
		den = FFABS(den) / gcd;
	}
	if (num <= max && den <= max) {
		a1 = AVRational{ (int)num, (int)den };
		den = 0;
	}

	while (den) {
		uint64_t x = num / den;
		int64_t next_den = num - den * x;
		int64_t a2n = x * a1.num + a0.num;
		int64_t a2d = x * a1.den + a0.den;

		if (a2n > max || a2d > max) {
			if (a1.num) x = (max - a0.num) / a1.num;
			if (a1.den) x = FFMIN(x, (max - a0.den) / a1.den);

			if (den * (2 * x * a1.den + a0.den) > num * a1.den)
				a1 = AVRational{(int)(x * a1.num + a0.num), (int)(x * a1.den + a0.den) };
			break;
		}

		a0 = a1;
		a1 = AVRational{(int)a2n, (int)a2d };
		num = den;
		den = next_den;
	}
	av_assert2(av_gcd(a1.num, a1.den) <= 1U);
	av_assert2(a1.num <= max && a1.den <= max);

	*dst_num = sign ? -a1.num : a1.num;
	*dst_den = a1.den;

	return den == 0;
}

AVRational av_mul_q(AVRational b, AVRational c)
{
	av_reduce(&b.num, &b.den,
		b.num * (int64_t)c.num,
		b.den * (int64_t)c.den, INT_MAX);
	return b;
}

AVRational av_div_q(AVRational b, AVRational c)
{
	return av_mul_q(b, AVRational { c.den, c.num });
}

AVRational av_add_q(AVRational b, AVRational c) {
	av_reduce(&b.num, &b.den,
		b.num * (int64_t)c.den +
		c.num * (int64_t)b.den,
		b.den * (int64_t)c.den, INT_MAX);
	return b;
}

static const uint8_t ff_logg2_tab[256] = {
	0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};

int av_log2(unsigned int v)
{
	int n = 0;
	if (v & 0xffff0000) {
		v >>= 16;
		n += 16;
	}
	if (v & 0xff00) {
		v >>= 8;
		n += 8;
	}
	n += ff_logg2_tab[v];

	return n;
}

int av_log2_16bit(unsigned int v)
{
	int n = 0;
	if (v & 0xff00) {
		v >>= 8;
		n += 8;
	}
	n += ff_logg2_tab[v];

	return n;
}
