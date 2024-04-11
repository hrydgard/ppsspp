/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>

#include "compat.h"

#define FF_MEMORY_POISON 0x2a

/**
 * Allocate a block of size bytes with alignment suitable for all
 * memory accesses (including vectors if available on the CPU).
 * @param size Size in bytes for the memory block to be allocated.
 * @return Pointer to the allocated block, NULL if the block cannot
 * be allocated.
 * @see av_mallocz()
 */
void *av_malloc(size_t size);

/**
 * Allocate a block of size * nmemb bytes with av_malloc().
 * @param nmemb Number of elements
 * @param size Size of the single element
 * @return Pointer to the allocated block, NULL if the block cannot
 * be allocated.
 * @see av_malloc()
 */
static inline void *av_malloc_array(size_t nmemb, size_t size)
{
    if (!size || nmemb >= INT_MAX / size)
        return NULL;
    return av_malloc(nmemb * size);
}

/**
 * Allocate or reallocate a block of memory.
 * If ptr is NULL and size > 0, allocate a new block. If
 * size is zero, free the memory block pointed to by ptr.
 * @param ptr Pointer to a memory block already allocated with
 * av_realloc() or NULL.
 * @param size Size in bytes of the memory block to be allocated or
 * reallocated.
 * @return Pointer to a newly-reallocated block or NULL if the block
 * cannot be reallocated or the function is used to free the memory block.
 * @warning Pointers originating from the av_malloc() family of functions must
 *          not be passed to av_realloc(). The former can be implemented using
 *          memalign() (or other functions), and there is no guarantee that
 *          pointers from such functions can be passed to realloc() at all.
 *          The situation is undefined according to POSIX and may crash with
 *          some libc implementations.
 * @see av_fast_realloc()
 */
void *av_realloc(void *ptr, size_t size);

/**
 * Allocate or reallocate a block of memory.
 * This function does the same thing as av_realloc, except:
 * - It takes two arguments and checks the result of the multiplication for
 *   integer overflow.
 * - It frees the input block in case of failure, thus avoiding the memory
 *   leak with the classic "buf = realloc(buf); if (!buf) return -1;".
 */
void *av_realloc_f(void *ptr, size_t nelem, size_t elsize);

/**
 * Allocate or reallocate an array.
 * If ptr is NULL and nmemb > 0, allocate a new block. If
 * nmemb is zero, free the memory block pointed to by ptr.
 * @param ptr Pointer to a memory block already allocated with
 * av_realloc() or NULL.
 * @param nmemb Number of elements
 * @param size Size of the single element
 * @return Pointer to a newly-reallocated block or NULL if the block
 * cannot be reallocated or the function is used to free the memory block.
 * @warning Pointers originating from the av_malloc() family of functions must
 *          not be passed to av_realloc(). The former can be implemented using
 *          memalign() (or other functions), and there is no guarantee that
 *          pointers from such functions can be passed to realloc() at all.
 *          The situation is undefined according to POSIX and may crash with
 *          some libc implementations.
 */
void *av_realloc_array(void *ptr, size_t nmemb, size_t size);

/**
 * Free a memory block which has been allocated with av_malloc(z)() or
 * av_realloc().
 * @param ptr Pointer to the memory block which should be freed.
 * @note ptr = NULL is explicitly allowed.
 * @note It is recommended that you use av_freep() instead.
 * @see av_freep()
 */
void av_free(void *ptr);

/**
 * Allocate a block of size bytes with alignment suitable for all
 * memory accesses (including vectors if available on the CPU) and
 * zero all the bytes of the block.
 * @param size Size in bytes for the memory block to be allocated.
 * @return Pointer to the allocated block, NULL if it cannot be allocated.
 * @see av_malloc()
 */
void *av_mallocz(size_t size);

/**
 * Allocate a block of size * nmemb bytes with av_mallocz().
 * @param nmemb Number of elements
 * @param size Size of the single element
 * @return Pointer to the allocated block, NULL if the block cannot
 * be allocated.
 * @see av_mallocz()
 * @see av_malloc_array()
 */
static inline void *av_mallocz_array(size_t nmemb, size_t size)
{
    if (!size || nmemb >= INT_MAX / size)
        return NULL;
    return av_mallocz(nmemb * size);
}

/**
 * Free a memory block which has been allocated with av_malloc(z)() or
 * av_realloc() and set the pointer pointing to it to NULL.
 * @param ptr Pointer to the pointer to the memory block which should
 * be freed.
 * @note passing a pointer to a NULL pointer is safe and leads to no action.
 * @see av_free()
 */
void av_freep(void *ptr);

/**
 * Multiply two size_t values checking for overflow.
 * @return  0 if success, AVERROR(EINVAL) if overflow.
 */
static inline int av_size_mult(size_t a, size_t b, size_t *r)
{
    size_t t = a * b;
    /* Hack inspired from glibc: only try the division if nelem and elsize
     * are both greater than sqrt(SIZE_MAX). */
    if ((a | b) >= ((size_t)1 << (sizeof(size_t) * 4)) && a && t / a != b)
        return AVERROR(EINVAL);
    *r = t;
    return 0;
}

/**
 * Reallocate the given block if it is not large enough, otherwise do nothing.
 *
 * @see av_realloc
 */
void *av_fast_realloc(void *ptr, unsigned int *size, size_t min_size);

/**
 * Allocate a buffer, reusing the given one if large enough.
 *
 * Contrary to av_fast_realloc the current buffer contents might not be
 * preserved and on error the old buffer is freed, thus no special
 * handling to avoid memleaks is necessary.
 *
 * @param ptr pointer to pointer to already allocated buffer, overwritten with pointer to new buffer
 * @param size size of the buffer *ptr points to
 * @param min_size minimum size of *ptr buffer after returning, *ptr will be NULL and
 *                 *size 0 if an error occurred.
 */
void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size);

/**
 * Allocate a buffer, reusing the given one if large enough.
 *
 * All newly allocated space is initially cleared
 * Contrary to av_fast_realloc the current buffer contents might not be
 * preserved and on error the old buffer is freed, thus no special
 * handling to avoid memleaks is necessary.
 *
 * @param ptr pointer to pointer to already allocated buffer, overwritten with pointer to new buffer
 * @param size size of the buffer *ptr points to
 * @param min_size minimum size of *ptr buffer after returning, *ptr will be NULL and
 *                 *size 0 if an error occurred.
 */
void av_fast_mallocz(void *ptr, unsigned int *size, size_t min_size);

/**
 * @}
 */
