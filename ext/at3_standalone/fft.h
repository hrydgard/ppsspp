/*
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#pragma once

#define CMUL(dre, dim, are, aim, bre, bim) do { \
        (dre) = (are) * (bre) - (aim) * (bim);  \
        (dim) = (are) * (bim) + (aim) * (bre);  \
    } while (0)

#include <stdint.h>

#include "compat.h"

typedef float FFTSample;

typedef struct FFTComplex {
	FFTSample re, im;
} FFTComplex;

typedef struct FFTContext FFTContext;

typedef float FFTDouble;

/* FFT computation */

enum mdct_permutation_type {
    FF_MDCT_PERM_NONE,
    FF_MDCT_PERM_INTERLEAVE,
};

struct FFTContext {
    int nbits;
    int inverse;
    uint16_t *revtab;
    FFTComplex *tmp_buf;
    int mdct_size; /* size of MDCT (i.e. number of input data * 2) */
    int mdct_bits; /* n = 2^nbits */
    /* pre/post rotation tables */
    FFTSample *tcos;
    FFTSample *tsin;

    enum mdct_permutation_type mdct_permutation;
};

/**
 * Do a complex FFT with the parameters defined in ff_fft_init(). The
 * input data must be permuted before. No 1.0/sqrt(n) normalization is done.
 */
void fft_calc(struct FFTContext *s, FFTComplex *z);
void imdct_calc(struct FFTContext *s, FFTSample *output, const FFTSample *input);
void imdct_half(struct FFTContext *s, FFTSample *output, const FFTSample *input);

#define COSTABLE(size) \
     DECLARE_ALIGNED(32, FFTSample, av_cos_##size)[size/2]

extern COSTABLE(16);
extern COSTABLE(32);
extern COSTABLE(64);
extern COSTABLE(128);
extern COSTABLE(256);
extern COSTABLE(512);
extern COSTABLE(1024);
extern COSTABLE(2048);
extern COSTABLE(4096);
extern COSTABLE(8192);
extern COSTABLE(16384);
extern COSTABLE(32768);
extern COSTABLE(65536);

/**
 * Initialize the cosine table in ff_cos_tabs[index]
 * @param index index in ff_cos_tabs array of the table to initialize
 */
void ff_init_ff_cos_tabs(int index);

/**
 * Set up a complex FFT.
 * @param nbits           log2 of the length of the input array
 * @param inverse         if 0 perform the forward transform, if 1 perform the inverse
 */
int ff_fft_init(FFTContext *s, int nbits, int inverse);

void ff_fft_end(FFTContext *s);

int ff_mdct_init(FFTContext *s, int nbits, int inverse, double scale);
void ff_mdct_end(FFTContext *s);
