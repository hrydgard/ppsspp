/*
 * default memory allocator for libavutil
 * Copyright (c) 2002 Fabrice Bellard
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
 * default memory allocator for libavutil
 */

#define _XOPEN_SOURCE 600

#include "config.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "avutil.h"
#include "common.h"
#include "intreadwrite.h"
#include "mem.h"

int ff_fast_malloc(void *ptr, unsigned int *size, size_t min_size, int zero_realloc)
{
	void *val;

	memcpy(&val, ptr, sizeof(val));
	if (min_size <= *size) {
		av_assert0(val || !min_size);
		return 0;
	}
	min_size = FFMAX(min_size + min_size / 16 + 32, min_size);
	av_freep(ptr);
	val = zero_realloc ? av_mallocz(min_size) : av_malloc(min_size);
	memcpy(ptr, &val, sizeof(val));
	if (!val)
		min_size = 0;
	*size = min_size;
	return 1;
}

#define ALIGN (HAVE_AVX ? 32 : 16)

/* NOTE: if you want to override these functions with your own
 * implementations (not recommended) you have to link libav* as
 * dynamic libraries and remove -Wl,-Bsymbolic from the linker flags.
 * Note that this will cost performance. */

static size_t max_alloc_size= INT_MAX;

void av_max_alloc(size_t max){
    max_alloc_size = max;
}

void *av_malloc(size_t size)
{
    void *ptr = NULL;
#if CONFIG_MEMALIGN_HACK
    long diff;
#endif

    /* let's disallow possibly ambiguous cases */
    if (size > (max_alloc_size - 32))
        return NULL;

#if CONFIG_MEMALIGN_HACK
    ptr = malloc(size + ALIGN);
    if (!ptr)
        return ptr;
    diff              = ((~(long)ptr)&(ALIGN - 1)) + 1;
    ptr               = (char *)ptr + diff;
    ((char *)ptr)[-1] = diff;
#elif HAVE_POSIX_MEMALIGN
    if (size) //OS X on SDK 10.6 has a broken posix_memalign implementation
    if (posix_memalign(&ptr, ALIGN, size))
        ptr = NULL;
#elif HAVE_ALIGNED_MALLOC
    ptr = _aligned_malloc(size, ALIGN);
#elif HAVE_MEMALIGN
#ifndef __DJGPP__
    ptr = memalign(ALIGN, size);
#else
    ptr = memalign(size, ALIGN);
#endif
    /* Why 64?
     * Indeed, we should align it:
     *   on  4 for 386
     *   on 16 for 486
     *   on 32 for 586, PPro - K6-III
     *   on 64 for K7 (maybe for P3 too).
     * Because L1 and L2 caches are aligned on those values.
     * But I don't want to code such logic here!
     */
    /* Why 32?
     * For AVX ASM. SSE / NEON needs only 16.
     * Why not larger? Because I did not see a difference in benchmarks ...
     */
    /* benchmarks with P3
     * memalign(64) + 1          3071, 3051, 3032
     * memalign(64) + 2          3051, 3032, 3041
     * memalign(64) + 4          2911, 2896, 2915
     * memalign(64) + 8          2545, 2554, 2550
     * memalign(64) + 16         2543, 2572, 2563
     * memalign(64) + 32         2546, 2545, 2571
     * memalign(64) + 64         2570, 2533, 2558
     *
     * BTW, malloc seems to do 8-byte alignment by default here.
     */
#else
    ptr = malloc(size);
#endif
    if(!ptr && !size) {
        size = 1;
        ptr= av_malloc(1);
    }
#if CONFIG_MEMORY_POISONING
    if (ptr)
        memset(ptr, FF_MEMORY_POISON, size);
#endif
    return ptr;
}

void *av_realloc(void *ptr, size_t size)
{
#if CONFIG_MEMALIGN_HACK
    int diff;
#endif

    /* let's disallow possibly ambiguous cases */
    if (size > (max_alloc_size - 32))
        return NULL;

#if CONFIG_MEMALIGN_HACK
    //FIXME this isn't aligned correctly, though it probably isn't needed
    if (!ptr)
        return av_malloc(size);
    diff = ((char *)ptr)[-1];
    av_assert0(diff>0 && diff<=ALIGN);
    ptr = realloc((char *)ptr - diff, size + diff);
    if (ptr)
        ptr = (char *)ptr + diff;
    return ptr;
#elif HAVE_ALIGNED_MALLOC
    return _aligned_realloc(ptr, size + !size, ALIGN);
#else
    return realloc(ptr, size + !size);
#endif
}

void *av_realloc_f(void *ptr, size_t nelem, size_t elsize)
{
    size_t size;
    void *r;

    if (av_size_mult(elsize, nelem, &size)) {
        av_free(ptr);
        return NULL;
    }
    r = av_realloc(ptr, size);
    if (!r && size)
        av_free(ptr);
    return r;
}

void *av_realloc_array(void *ptr, size_t nmemb, size_t size)
{
    if (!size || nmemb >= INT_MAX / size)
        return NULL;
    return av_realloc(ptr, nmemb * size);
}

void av_free(void *ptr)
{
#if CONFIG_MEMALIGN_HACK
    if (ptr) {
        int v= ((char *)ptr)[-1];
        av_assert0(v>0 && v<=ALIGN);
        free((char *)ptr - v);
    }
#elif HAVE_ALIGNED_MALLOC
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

void av_freep(void *arg)
{
    void *val;

    memcpy(&val, arg, sizeof(val));
    memcpy(arg, &(void *){ NULL }, sizeof(val));
    av_free(val);
}

void *av_mallocz(size_t size)
{
    void *ptr = av_malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

static void fill16(uint8_t *dst, int len)
{
    uint32_t v = AV_RN16(dst - 2);

    v |= v << 16;

    while (len >= 4) {
        AV_WN32(dst, v);
        dst += 4;
        len -= 4;
    }

    while (len--) {
        *dst = dst[-2];
        dst++;
    }
}

static void fill24(uint8_t *dst, int len)
{
#if HAVE_BIGENDIAN
    uint32_t v = AV_RB24(dst - 3);
    uint32_t a = v << 8  | v >> 16;
    uint32_t b = v << 16 | v >> 8;
    uint32_t c = v << 24 | v;
#else
    uint32_t v = AV_RL24(dst - 3);
    uint32_t a = v       | v << 24;
    uint32_t b = v >> 8  | v << 16;
    uint32_t c = v >> 16 | v << 8;
#endif

    while (len >= 12) {
        AV_WN32(dst,     a);
        AV_WN32(dst + 4, b);
        AV_WN32(dst + 8, c);
        dst += 12;
        len -= 12;
    }

    if (len >= 4) {
        AV_WN32(dst, a);
        dst += 4;
        len -= 4;
    }

    if (len >= 4) {
        AV_WN32(dst, b);
        dst += 4;
        len -= 4;
    }

    while (len--) {
        *dst = dst[-3];
        dst++;
    }
}

static void fill32(uint8_t *dst, int len)
{
    uint32_t v = AV_RN32(dst - 4);

    while (len >= 4) {
        AV_WN32(dst, v);
        dst += 4;
        len -= 4;
    }

    while (len--) {
        *dst = dst[-4];
        dst++;
    }
}

void *av_fast_realloc(void *ptr, unsigned int *size, size_t min_size)
{
    if (min_size < *size)
        return ptr;

    min_size = FFMAX(min_size + min_size / 16 + 32, min_size);

    ptr = av_realloc(ptr, min_size);
    /* we could set this to the unmodified min_size but this is safer
     * if the user lost the ptr and uses NULL now
     */
    if (!ptr)
        min_size = 0;

    *size = min_size;

    return ptr;
}

void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    ff_fast_malloc(ptr, size, min_size, 0);
}

void av_fast_mallocz(void *ptr, unsigned int *size, size_t min_size)
{
    ff_fast_malloc(ptr, size, min_size, 1);
}
