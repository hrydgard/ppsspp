/*
 * common_defs.h
 *
 * Copyright 2016 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef COMMON_COMMON_DEFS_H
#define COMMON_COMMON_DEFS_H

#ifdef __GNUC__
#  include "compiler_gcc.h"
#elif defined(_MSC_VER)
#  include "compiler_msc.h"
#else
#  pragma message("Unrecognized compiler.  Please add a header file for your compiler.  Compilation will proceed, but performance may suffer!")
#endif

/* ========================================================================== */
/*                              Type definitions                              */
/* ========================================================================== */

#include <stddef.h> /* size_t */

#ifndef __bool_true_false_are_defined
#  include <stdbool.h> /* bool */
#endif

/* Fixed-width integer types */
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/*
 * Word type of the target architecture.  Use 'size_t' instead of 'unsigned
 * long' to account for platforms such as Windows that use 32-bit 'unsigned
 * long' on 64-bit architectures.
 */
typedef size_t machine_word_t;

/* Number of bytes in a word */
#define WORDBYTES	((int)sizeof(machine_word_t))

/* Number of bits in a word */
#define WORDBITS	(8 * WORDBYTES)

/* ========================================================================== */
/*                         Optional compiler features                         */
/* ========================================================================== */

/* LIBEXPORT - export a function from a shared library */
#ifndef LIBEXPORT
#  define LIBEXPORT
#endif

/* inline - suggest that a function be inlined */
#ifndef inline
#  define inline
#endif

/* forceinline - force a function to be inlined, if possible */
#ifndef forceinline
#  define forceinline inline
#endif

/* restrict - annotate a non-aliased pointer */
#ifndef restrict
#  define restrict
#endif

/* likely(expr) - hint that an expression is usually true */
#ifndef likely
#  define likely(expr)		(expr)
#endif

/* unlikely(expr) - hint that an expression is usually false */
#ifndef unlikely
#  define unlikely(expr)	(expr)
#endif

/* prefetchr(addr) - prefetch into L1 cache for read */
#ifndef prefetchr
#  define prefetchr(addr)
#endif

/* prefetchw(addr) - prefetch into L1 cache for write */
#ifndef prefetchw
#  define prefetchw(addr)
#endif

/* Does the compiler support the 'target' function attribute? */
#ifndef COMPILER_SUPPORTS_TARGET_FUNCTION_ATTRIBUTE
#  define COMPILER_SUPPORTS_TARGET_FUNCTION_ATTRIBUTE 0
#endif

/* Which targets are supported with the 'target' function attribute? */
#ifndef COMPILER_SUPPORTS_BMI2_TARGET
#  define COMPILER_SUPPORTS_BMI2_TARGET 0
#endif
#ifndef COMPILER_SUPPORTS_AVX_TARGET
#  define COMPILER_SUPPORTS_AVX_TARGET 0
#endif
#ifndef COMPILER_SUPPORTS_AVX512BW_TARGET
#  define COMPILER_SUPPORTS_AVX512BW_TARGET 0
#endif

/*
 * Which targets are supported with the 'target' function attribute and have
 * intrinsics that work within 'target'-ed functions?
 */
#ifndef COMPILER_SUPPORTS_SSE2_TARGET_INTRINSICS
#  define COMPILER_SUPPORTS_SSE2_TARGET_INTRINSICS 0
#endif
#ifndef COMPILER_SUPPORTS_PCLMUL_TARGET_INTRINSICS
#  define COMPILER_SUPPORTS_PCLMUL_TARGET_INTRINSICS 0
#endif
#ifndef COMPILER_SUPPORTS_AVX2_TARGET_INTRINSICS
#  define COMPILER_SUPPORTS_AVX2_TARGET_INTRINSICS 0
#endif
#ifndef COMPILER_SUPPORTS_AVX512BW_TARGET_INTRINSICS
#  define COMPILER_SUPPORTS_AVX512BW_TARGET_INTRINSICS 0
#endif
#ifndef COMPILER_SUPPORTS_NEON_TARGET_INTRINSICS
#  define COMPILER_SUPPORTS_NEON_TARGET_INTRINSICS 0
#endif
#ifndef COMPILER_SUPPORTS_PMULL_TARGET_INTRINSICS
#  define COMPILER_SUPPORTS_PMULL_TARGET_INTRINSICS 0
#endif
#ifndef COMPILER_SUPPORTS_CRC32_TARGET_INTRINSICS
#  define COMPILER_SUPPORTS_CRC32_TARGET_INTRINSICS 0
#endif

/* _aligned_attribute(n) - declare that the annotated variable, or variables of
 * the annotated type, are to be aligned on n-byte boundaries */
#ifndef _aligned_attribute
#endif

/* ========================================================================== */
/*                          Miscellaneous macros                              */
/* ========================================================================== */

#define ARRAY_LEN(A)		(sizeof(A) / sizeof((A)[0]))
#define MIN(a, b)		((a) <= (b) ? (a) : (b))
#define MAX(a, b)		((a) >= (b) ? (a) : (b))
#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define STATIC_ASSERT(expr)	((void)sizeof(char[1 - 2 * !(expr)]))
#define ALIGN(n, a)		(((n) + (a) - 1) & ~((a) - 1))

/* ========================================================================== */
/*                           Endianness handling                              */
/* ========================================================================== */

/*
 * CPU_IS_LITTLE_ENDIAN() - a macro which evaluates to 1 if the CPU is little
 * endian or 0 if it is big endian.  The macro should be defined in a way such
 * that the compiler can evaluate it at compilation time.  If not defined, a
 * fallback is used.
 */
#ifndef CPU_IS_LITTLE_ENDIAN
static forceinline int CPU_IS_LITTLE_ENDIAN(void)
{
	union {
		unsigned int v;
		unsigned char b;
	} u;
	u.v = 1;
	return u.b;
}
#endif

/* bswap16(n) - swap the bytes of a 16-bit integer */
#ifndef bswap16
static forceinline u16 bswap16(u16 n)
{
	return (n << 8) | (n >> 8);
}
#endif

/* bswap32(n) - swap the bytes of a 32-bit integer */
#ifndef bswap32
static forceinline u32 bswap32(u32 n)
{
	return ((n & 0x000000FF) << 24) |
	       ((n & 0x0000FF00) << 8) |
	       ((n & 0x00FF0000) >> 8) |
	       ((n & 0xFF000000) >> 24);
}
#endif

/* bswap64(n) - swap the bytes of a 64-bit integer */
#ifndef bswap64
static forceinline u64 bswap64(u64 n)
{
	return ((n & 0x00000000000000FF) << 56) |
	       ((n & 0x000000000000FF00) << 40) |
	       ((n & 0x0000000000FF0000) << 24) |
	       ((n & 0x00000000FF000000) << 8) |
	       ((n & 0x000000FF00000000) >> 8) |
	       ((n & 0x0000FF0000000000) >> 24) |
	       ((n & 0x00FF000000000000) >> 40) |
	       ((n & 0xFF00000000000000) >> 56);
}
#endif

#define le16_bswap(n) (CPU_IS_LITTLE_ENDIAN() ? (n) : bswap16(n))
#define le32_bswap(n) (CPU_IS_LITTLE_ENDIAN() ? (n) : bswap32(n))
#define le64_bswap(n) (CPU_IS_LITTLE_ENDIAN() ? (n) : bswap64(n))
#define be16_bswap(n) (CPU_IS_LITTLE_ENDIAN() ? bswap16(n) : (n))
#define be32_bswap(n) (CPU_IS_LITTLE_ENDIAN() ? bswap32(n) : (n))
#define be64_bswap(n) (CPU_IS_LITTLE_ENDIAN() ? bswap64(n) : (n))

/* ========================================================================== */
/*                          Unaligned memory accesses                         */
/* ========================================================================== */

/*
 * UNALIGNED_ACCESS_IS_FAST should be defined to 1 if unaligned memory accesses
 * can be performed efficiently on the target platform.
 */
#ifndef UNALIGNED_ACCESS_IS_FAST
#  define UNALIGNED_ACCESS_IS_FAST 0
#endif

/* ========================================================================== */
/*                             Bit scan functions                             */
/* ========================================================================== */

/*
 * Bit Scan Reverse (BSR) - find the 0-based index (relative to the least
 * significant end) of the *most* significant 1 bit in the input value.  The
 * input value must be nonzero!
 */

#ifndef bsr32
static forceinline unsigned
bsr32(u32 n)
{
	unsigned i = 0;
	while ((n >>= 1) != 0)
		i++;
	return i;
}
#endif

#ifndef bsr64
static forceinline unsigned
bsr64(u64 n)
{
	unsigned i = 0;
	while ((n >>= 1) != 0)
		i++;
	return i;
}
#endif

static forceinline unsigned
bsrw(machine_word_t n)
{
	STATIC_ASSERT(WORDBITS == 32 || WORDBITS == 64);
	if (WORDBITS == 32)
		return bsr32(n);
	else
		return bsr64(n);
}

/*
 * Bit Scan Forward (BSF) - find the 0-based index (relative to the least
 * significant end) of the *least* significant 1 bit in the input value.  The
 * input value must be nonzero!
 */

#ifndef bsf32
static forceinline unsigned
bsf32(u32 n)
{
	unsigned i = 0;
	while ((n & 1) == 0) {
		i++;
		n >>= 1;
	}
	return i;
}
#endif

#ifndef bsf64
static forceinline unsigned
bsf64(u64 n)
{
	unsigned i = 0;
	while ((n & 1) == 0) {
		i++;
		n >>= 1;
	}
	return i;
}
#endif

static forceinline unsigned
bsfw(machine_word_t n)
{
	STATIC_ASSERT(WORDBITS == 32 || WORDBITS == 64);
	if (WORDBITS == 32)
		return bsf32(n);
	else
		return bsf64(n);
}

#endif /* COMMON_COMMON_DEFS_H */
