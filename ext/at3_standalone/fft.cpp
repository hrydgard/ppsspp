/*
 * FFT/IFFT transforms
 * Copyright (c) 2008 Loren Merritt
 * Copyright (c) 2002 Fabrice Bellard
 * Partly based on libdjbfft by D. J. Bernstein
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
 * FFT/IFFT transforms.
 */

#include <stdlib.h>
#include <string.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include "mem.h"
#include "fft.h"

#define sqrthalf (float)M_SQRT1_2

void imdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input);
void imdct_half(FFTContext *s, FFTSample *output, const FFTSample *input);

/* cos(2*pi*x/n) for 0<=x<=n/4, followed by its reverse */
COSTABLE(16);
COSTABLE(32);
COSTABLE(64);
COSTABLE(128);
COSTABLE(256);
COSTABLE(512);
COSTABLE(1024);

static FFTSample * const av_cos_tabs[] = {
    NULL, NULL, NULL, NULL,
    av_cos_16,
    av_cos_32,
    av_cos_64,
    av_cos_128,
    av_cos_256,
    av_cos_512,
    av_cos_1024,
};

void fft_calc(FFTContext *s, FFTComplex *z);

static int split_radix_permutation(int i, int n, int inverse)
{
    int m;
    if(n <= 2) return i&1;
    m = n >> 1;
    if(!(i&m))            return split_radix_permutation(i, m, inverse)*2;
    m >>= 1;
    if(inverse == !(i&m)) return split_radix_permutation(i, m, inverse)*4 + 1;
    else                  return split_radix_permutation(i, m, inverse)*4 - 1;
}

void ff_init_ff_cos_tabs(int index)
{
    int i;
    int m = 1<<index;
    double freq = 2*M_PI/m;
    FFTSample *tab = av_cos_tabs[index];
    for(i=0; i<=m/4; i++)
        tab[i] = cos(i*freq);
    for(i=1; i<m/4; i++)
        tab[m/2-i] = tab[i];
}

static const int avx_tab[] = {
    0, 4, 1, 5, 8, 12, 9, 13, 2, 6, 3, 7, 10, 14, 11, 15
};

static int is_second_half_of_fft32(int i, int n)
{
    if (n <= 32)
        return i >= 16;
    else if (i < n/2)
        return is_second_half_of_fft32(i, n/2);
    else if (i < 3*n/4)
        return is_second_half_of_fft32(i - n/2, n/4);
    else
        return is_second_half_of_fft32(i - 3*n/4, n/4);
}

int ff_fft_init(FFTContext *s, int nbits, int inverse)
{
    int i, j, n;

    if (nbits < 2 || nbits > 16)
        goto fail;
    s->nbits = nbits;
    n = 1 << nbits;

    s->revtab = (uint16_t *)av_malloc(n * sizeof(uint16_t));
    if (!s->revtab)
        goto fail;
    s->tmp_buf = (FFTComplex *)av_malloc(n * sizeof(FFTComplex));
    if (!s->tmp_buf)
        goto fail;
    s->inverse = inverse;

    for(j=4; j<=nbits; j++) {
        ff_init_ff_cos_tabs(j);
    }

    for(i=0; i<n; i++) {
        j = i;
		int index = -split_radix_permutation(i, n, s->inverse) & (n - 1);
        s->revtab[index] = j;
    }

    return 0;
 fail:
    av_freep(&s->revtab);
    av_freep(&s->tmp_buf);
    return -1;
}

void ff_fft_end(FFTContext *s)
{
    av_freep(&s->revtab);
    av_freep(&s->tmp_buf);
}

#define BF(x, y, a, b) do {                     \
        x = a - b;                              \
        y = a + b;                              \
    } while (0)

#define BUTTERFLIES(a0,a1,a2,a3) {\
    BF(t3, t5, t5, t1);\
    BF(a2.re, a0.re, a0.re, t5);\
    BF(a3.im, a1.im, a1.im, t3);\
    BF(t4, t6, t2, t6);\
    BF(a3.re, a1.re, a1.re, t4);\
    BF(a2.im, a0.im, a0.im, t6);\
}

// force loading all the inputs before storing any.
// this is slightly slower for small data, but avoids store->load aliasing
// for addresses separated by large powers of 2.
#define BUTTERFLIES_BIG(a0,a1,a2,a3) {\
    FFTSample r0=a0.re, i0=a0.im, r1=a1.re, i1=a1.im;\
    BF(t3, t5, t5, t1);\
    BF(a2.re, a0.re, r0, t5);\
    BF(a3.im, a1.im, i1, t3);\
    BF(t4, t6, t2, t6);\
    BF(a3.re, a1.re, r1, t4);\
    BF(a2.im, a0.im, i0, t6);\
}

#define TRANSFORM(a0,a1,a2,a3,wre,wim) {\
    CMUL(t1, t2, a2.re, a2.im, wre, -wim);\
    CMUL(t5, t6, a3.re, a3.im, wre,  wim);\
    BUTTERFLIES(a0,a1,a2,a3)\
}

#define TRANSFORM_ZERO(a0,a1,a2,a3) {\
    t1 = a2.re;\
    t2 = a2.im;\
    t5 = a3.re;\
    t6 = a3.im;\
    BUTTERFLIES(a0,a1,a2,a3)\
}

/* z[0...8n-1], w[1...2n-1] */
#define PASS(name)\
static void name(FFTComplex *z, const FFTSample *wre, unsigned int n)\
{\
    FFTDouble t1, t2, t3, t4, t5, t6;\
    int o1 = 2*n;\
    int o2 = 4*n;\
    int o3 = 6*n;\
    const FFTSample *wim = wre+o1;\
    n--;\
\
    TRANSFORM_ZERO(z[0],z[o1],z[o2],z[o3]);\
    TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1]);\
    do {\
        z += 2;\
        wre += 2;\
        wim -= 2;\
        TRANSFORM(z[0],z[o1],z[o2],z[o3],wre[0],wim[0]);\
        TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1]);\
    } while(--n);\
}

PASS(pass)
#undef BUTTERFLIES
#define BUTTERFLIES BUTTERFLIES_BIG
PASS(pass_big)

#define DECL_FFT(n,n2,n4)\
static void fft##n(FFTComplex *z)\
{\
    fft##n2(z);\
    fft##n4(z+n4*2);\
    fft##n4(z+n4*3);\
    pass(z,av_cos_##n,n4/2);\
}

static void fft4(FFTComplex *z)
{
    FFTDouble t1, t2, t3, t4, t5, t6, t7, t8;

    BF(t3, t1, z[0].re, z[1].re);
    BF(t8, t6, z[3].re, z[2].re);
    BF(z[2].re, z[0].re, t1, t6);
    BF(t4, t2, z[0].im, z[1].im);
    BF(t7, t5, z[2].im, z[3].im);
    BF(z[3].im, z[1].im, t4, t8);
    BF(z[3].re, z[1].re, t3, t7);
    BF(z[2].im, z[0].im, t2, t5);
}

static void fft8(FFTComplex *z)
{
    FFTDouble t1, t2, t3, t4, t5, t6;

    fft4(z);

    BF(t1, z[5].re, z[4].re, -z[5].re);
    BF(t2, z[5].im, z[4].im, -z[5].im);
    BF(t5, z[7].re, z[6].re, -z[7].re);
    BF(t6, z[7].im, z[6].im, -z[7].im);

    BUTTERFLIES(z[0],z[2],z[4],z[6]);
    TRANSFORM(z[1],z[3],z[5],z[7],sqrthalf,sqrthalf);
}

static void fft16(FFTComplex *z)
{
    FFTDouble t1, t2, t3, t4, t5, t6;
    FFTSample cos_16_1 = av_cos_16[1];
    FFTSample cos_16_3 = av_cos_16[3];

    fft8(z);
    fft4(z+8);
    fft4(z+12);

    TRANSFORM_ZERO(z[0],z[4],z[8],z[12]);
    TRANSFORM(z[2],z[6],z[10],z[14],sqrthalf,sqrthalf);
    TRANSFORM(z[1],z[5],z[9],z[13],cos_16_1,cos_16_3);
    TRANSFORM(z[3],z[7],z[11],z[15],cos_16_3,cos_16_1);
}
DECL_FFT(32,16,8)
DECL_FFT(64,32,16)
DECL_FFT(128,64,32)
DECL_FFT(256,128,64)
DECL_FFT(512,256,128)
#define pass pass_big
DECL_FFT(1024,512,256)

static void (* const fft_dispatch[])(FFTComplex*) = {
    fft4, fft8, fft16, fft32, fft64, fft128, fft256, fft512, fft1024,
};

void fft_calc(FFTContext *s, FFTComplex *z) {
    fft_dispatch[s->nbits-2](z);
}

#include <stdlib.h>
#include <string.h>

#include "fft.h"
#include "mem.h"

/**
 * init MDCT or IMDCT computation.
 */
int ff_mdct_init(FFTContext *s, int nbits, int inverse, double scale)
{
	int n, n4, i;
	double alpha, theta;
	int tstep;

	memset(s, 0, sizeof(*s));
	n = 1 << nbits;
	s->mdct_bits = nbits;
	s->mdct_size = n;
	n4 = n >> 2;
	s->mdct_permutation = FF_MDCT_PERM_NONE;

	if (ff_fft_init(s, s->mdct_bits - 2, inverse) < 0)
		goto fail;

	s->tcos = (FFTSample *)av_malloc_array(n / 2, sizeof(FFTSample));
	if (!s->tcos)
		goto fail;

	switch (s->mdct_permutation) {
	case FF_MDCT_PERM_NONE:
		s->tsin = s->tcos + n4;
		tstep = 1;
		break;
	case FF_MDCT_PERM_INTERLEAVE:
		s->tsin = s->tcos + 1;
		tstep = 2;
		break;
	default:
		goto fail;
	}

	theta = 1.0 / 8.0 + (scale < 0 ? n4 : 0);
	scale = sqrt(fabs(scale));
	for (i = 0; i < n4; i++) {
		alpha = 2 * M_PI * (i + theta) / n;
		s->tcos[i * tstep] = -cos(alpha) * scale;
		s->tsin[i * tstep] = -sin(alpha) * scale;
	}
	return 0;
fail:
	ff_mdct_end(s);
	return -1;
}

/**
 * Compute the middle half of the inverse MDCT of size N = 2^nbits,
 * thus excluding the parts that can be derived by symmetry
 * @param output N/2 samples
 * @param input N/2 samples
 */
void imdct_half(FFTContext *s, FFTSample *output, const FFTSample *input)
{
	int k, n8, n4, n2, n, j;
	const uint16_t *revtab = s->revtab;
	const FFTSample *tcos = s->tcos;
	const FFTSample *tsin = s->tsin;
	const FFTSample *in1, *in2;
	FFTComplex *z = (FFTComplex *)output;

	n = 1 << s->mdct_bits;
	n2 = n >> 1;
	n4 = n >> 2;
	n8 = n >> 3;

	/* pre rotation */
	in1 = input;
	in2 = input + n2 - 1;
	for (k = 0; k < n4; k++) {
		j = revtab[k];
		CMUL(z[j].re, z[j].im, *in2, *in1, tcos[k], tsin[k]);
		in1 += 2;
		in2 -= 2;
	}
	fft_calc(s, z);

	/* post rotation + reordering */
	for (k = 0; k < n8; k++) {
		FFTSample r0, i0, r1, i1;
		CMUL(r0, i1, z[n8 - k - 1].im, z[n8 - k - 1].re, tsin[n8 - k - 1], tcos[n8 - k - 1]);
		CMUL(r1, i0, z[n8 + k].im, z[n8 + k].re, tsin[n8 + k], tcos[n8 + k]);
		z[n8 - k - 1].re = r0;
		z[n8 - k - 1].im = i0;
		z[n8 + k].re = r1;
		z[n8 + k].im = i1;
	}
}

/**
 * Compute inverse MDCT of size N = 2^nbits
 * @param output N samples
 * @param input N/2 samples
 */
void imdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
	int k;
	int n = 1 << s->mdct_bits;
	int n2 = n >> 1;
	int n4 = n >> 2;

	imdct_half(s, output + n4, input);

	for (k = 0; k < n4; k++) {
		output[k] = -output[n2 - k - 1];
		output[n - k - 1] = output[n2 + k];
	}
}

void ff_mdct_end(FFTContext *s)
{
	av_freep(&s->tcos);
	ff_fft_end(s);
}
