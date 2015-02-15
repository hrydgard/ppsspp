#pragma once
/**
 * @file SFMT.h
 *
 * @brief SIMD oriented Fast Mersenne Twister(SFMT) pseudorandom
 * number generator using C structure.
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
 *
 * @note We assume that your system has inttypes.h.  If your system
 * doesn't have inttypes.h, you have to typedef uint32_t and uint64_t,
 * and you have to define PRIu64 and PRIx64 in this file as follows:
 * @verbatim
 typedef unsigned int uint32_t
 typedef unsigned long long uint64_t
 #define PRIu64 "llu"
 #define PRIx64 "llx"
@endverbatim
 * uint32_t must be exactly 32-bit unsigned integer type (no more, no
 * less), and uint64_t must be exactly 64-bit unsigned integer type.
 * PRIu64 and PRIx64 are used for printf function to print 64-bit
 * unsigned int and 64-bit unsigned int in hexadecimal format.
 */

#ifndef SFMTST_H
#define SFMTST_H
#if defined(__cplusplus)
extern "C" {
#endif

#include <stdio.h>
#include <assert.h>

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
  #include <inttypes.h>
#elif defined(_MSC_VER) || defined(__BORLANDC__)
  typedef unsigned int uint32_t;
  typedef unsigned __int64 uint64_t;
  #define inline __inline
#else
  #include <inttypes.h>
  #if defined(__GNUC__)
    #define inline __inline__
  #endif
#endif

#ifndef PRIu64
  #if defined(_MSC_VER) || defined(__BORLANDC__)
    #define PRIu64 "I64u"
    #define PRIx64 "I64x"
  #else
    #define PRIu64 "llu"
    #define PRIx64 "llx"
  #endif
#endif

#include "SFMT-params.h"

/*------------------------------------------
  128-bit SIMD like data type for standard C
  ------------------------------------------*/
#if defined(HAVE_ALTIVEC)
  #if !defined(__APPLE__)
    #include <altivec.h>
  #endif
/** 128-bit data structure */
union W128_T {
    vector unsigned int s;
    uint32_t u[4];
    uint64_t u64[2];
};
#elif defined(HAVE_SSE2)
  #include <emmintrin.h>

/** 128-bit data structure */
union W128_T {
    uint32_t u[4];
    uint64_t u64[2];
    __m128i si;
};
#else
/** 128-bit data structure */
union W128_T {
    uint32_t u[4];
    uint64_t u64[2];
};
#endif

/** 128-bit data type */
typedef union W128_T w128_t;

/**
 * SFMT internal state
 */
struct SFMT_T {
    /** the 128-bit internal state array */
    w128_t state[SFMT_N];
    /** index counter to the 32-bit internal state array */
    int idx;
};

typedef struct SFMT_T sfmt_t;

void sfmt_fill_array32(sfmt_t * sfmt, uint32_t * array, int size);
void sfmt_fill_array64(sfmt_t * sfmt, uint64_t * array, int size);
void sfmt_init_gen_rand(sfmt_t * sfmt, uint32_t seed);
void sfmt_init_by_array(sfmt_t * sfmt, uint32_t * init_key, int key_length);
const char * sfmt_get_idstring(sfmt_t * sfmt);
int sfmt_get_min_array_size32(sfmt_t * sfmt);
int sfmt_get_min_array_size64(sfmt_t * sfmt);
void sfmt_gen_rand_all(sfmt_t * sfmt);

#ifndef ONLY64
/**
 * This function generates and returns 32-bit pseudorandom number.
 * init_gen_rand or init_by_array must be called before this function.
 * @param sfmt SFMT internal state
 * @return 32-bit pseudorandom number
 */
inline static uint32_t sfmt_genrand_uint32(sfmt_t * sfmt) {
    uint32_t r;
    uint32_t * psfmt32 = &sfmt->state[0].u[0];

    if (sfmt->idx >= SFMT_N32) {
        sfmt_gen_rand_all(sfmt);
        sfmt->idx = 0;
    }
    r = psfmt32[sfmt->idx++];
    return r;
}
#endif
/**
 * This function generates and returns 64-bit pseudorandom number.
 * init_gen_rand or init_by_array must be called before this function.
 * The function gen_rand64 should not be called after gen_rand32,
 * unless an initialization is again executed.
 * @param sfmt SFMT internal state
 * @return 64-bit pseudorandom number
 */
inline static uint64_t sfmt_genrand_uint64(sfmt_t * sfmt) {
#if defined(BIG_ENDIAN64) && !defined(ONLY64)
    uint32_t * psfmt32 = &sfmt->state[0].u[0];
    uint32_t r1, r2;
#else
    uint64_t r;
#endif
    uint64_t * psfmt64 = &sfmt->state[0].u64[0];
    assert(sfmt->idx % 2 == 0);

    if (sfmt->idx >= SFMT_N32) {
        sfmt_gen_rand_all(sfmt);
        sfmt->idx = 0;
    }
#if defined(BIG_ENDIAN64) && !defined(ONLY64)
    r1 = psfmt32[sfmt->idx];
    r2 = psfmt32[sfmt->idx + 1];
    sfmt->idx += 2;
    return ((uint64_t)r2 << 32) | r1;
#else
    r = psfmt64[sfmt->idx / 2];
    sfmt->idx += 2;
    return r;
#endif
}

/* =================================================
   The following real versions are due to Isaku Wada
   ================================================= */
/**
 * converts an unsigned 32-bit number to a double on [0,1]-real-interval.
 * @param v 32-bit unsigned integer
 * @return double on [0,1]-real-interval
 */
inline static double sfmt_to_real1(uint32_t v)
{
    return v * (1.0/4294967295.0);
    /* divided by 2^32-1 */
}

/**
 * generates a random number on [0,1]-real-interval
 * @param sfmt SFMT internal state
 * @return double on [0,1]-real-interval
 */
inline static double sfmt_genrand_real1(sfmt_t * sfmt)
{
    return sfmt_to_real1(sfmt_genrand_uint32(sfmt));
}

/**
 * converts an unsigned 32-bit integer to a double on [0,1)-real-interval.
 * @param v 32-bit unsigned integer
 * @return double on [0,1)-real-interval
 */
inline static double sfmt_to_real2(uint32_t v)
{
    return v * (1.0/4294967296.0);
    /* divided by 2^32 */
}

/**
 * generates a random number on [0,1)-real-interval
 * @param sfmt SFMT internal state
 * @return double on [0,1)-real-interval
 */
inline static double sfmt_genrand_real2(sfmt_t * sfmt)
{
    return sfmt_to_real2(sfmt_genrand_uint32(sfmt));
}

/**
 * converts an unsigned 32-bit integer to a double on (0,1)-real-interval.
 * @param v 32-bit unsigned integer
 * @return double on (0,1)-real-interval
 */
inline static double sfmt_to_real3(uint32_t v)
{
    return (((double)v) + 0.5)*(1.0/4294967296.0);
    /* divided by 2^32 */
}

/**
 * generates a random number on (0,1)-real-interval
 * @param sfmt SFMT internal state
 * @return double on (0,1)-real-interval
 */
inline static double sfmt_genrand_real3(sfmt_t * sfmt)
{
    return sfmt_to_real3(sfmt_genrand_uint32(sfmt));
}

/**
 * converts an unsigned 32-bit integer to double on [0,1)
 * with 53-bit resolution.
 * @param v 32-bit unsigned integer
 * @return double on [0,1)-real-interval with 53-bit resolution.
 */
inline static double sfmt_to_res53(uint64_t v)
{
    return v * (1.0/18446744073709551616.0);
}

/**
 * generates a random number on [0,1) with 53-bit resolution
 * @param sfmt SFMT internal state
 * @return double on [0,1) with 53-bit resolution
 */
inline static double sfmt_genrand_res53(sfmt_t * sfmt)
{
    return sfmt_to_res53(sfmt_genrand_uint64(sfmt));
}


/* =================================================
   The following function are added by Saito.
   ================================================= */
/**
 * generates a random number on [0,1) with 53-bit resolution from two
 * 32 bit integers
 */
inline static double sfmt_to_res53_mix(uint32_t x, uint32_t y)
{
    return sfmt_to_res53(x | ((uint64_t)y << 32));
}

/**
 * generates a random number on [0,1) with 53-bit resolution
 * using two 32bit integers.
 * @param sfmt SFMT internal state
 * @return double on [0,1) with 53-bit resolution
 */
inline static double sfmt_genrand_res53_mix(sfmt_t * sfmt)
{
    uint32_t x, y;

    x = sfmt_genrand_uint32(sfmt);
    y = sfmt_genrand_uint32(sfmt);
    return sfmt_to_res53_mix(x, y);
}

#if defined(__cplusplus)
}
#endif

#endif
