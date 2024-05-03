#pragma once

#include <stdlib.h>

// Compat hacks to make an FFMPEG-like environment, so we can keep the core code mostly unchanged.

#if defined(__clang__)
#define DECLARE_ALIGNED(n, t, v)      t __attribute__((aligned(n))) v
#define DECLARE_ASM_CONST(n, t, v)    static const t av_used __attribute__((aligned(n))) v
#define av_restrict __restrict
#elif defined(__GNUC__)
#define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (n))) v
#define DECLARE_ASM_CONST(n,t,v)    static const t av_used __attribute__ ((aligned (n))) v
#define av_restrict __restrict__
#elif defined(_MSC_VER)
#define DECLARE_ALIGNED(n,t,v)      __declspec(align(n)) t v
#define DECLARE_ASM_CONST(n,t,v)    __declspec(align(n)) static const t v
#define av_restrict __restrict
#else
#define DECLARE_ALIGNED(n,t,v)      t v
#define DECLARE_ASM_CONST(n,t,v)    static const t v
#define av_restrict
#endif

#define AV_HAVE_FAST_UNALIGNED 0
#define AV_INPUT_BUFFER_PADDING_SIZE 32

// TODO: This should work but doesn't??
// #define BITSTREAM_READER_LE

#define LOCAL_ALIGNED(bits, type, name, subscript) type name subscript
#define av_alias
#define av_unused
#define av_assert0(cond)
#define av_assert1(cond)
#define av_assert2(cond)
#define av_printf_format(a,b)
#define avpriv_report_missing_feature(...)

#define AVERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.
#define AVUNERROR(e) (-(e)) ///< Returns a POSIX error code from a library function error return value.

#define FFERRTAG(a, b, c, d) (-(int)MKTAG(a, b, c, d))

#define AVERROR_INVALIDDATA        FFERRTAG( 'I','N','D','A') ///< Invalid data found when processing input
#define AVERROR_PATCHWELCOME       FFERRTAG( 'P','A','W','E') ///< Not yet implemented in FFmpeg, patches welcome

#define AV_LOG_ERROR    16
#define AV_LOG_WARNING  24
#define AV_LOG_INFO     32
#define AV_LOG_VERBOSE  40
#define AV_LOG_DEBUG    48
#define AV_LOG_TRACE    56

void av_log(int level, const char *fmt, ...) av_printf_format(3, 4);

 /**
  * Absolute value, Note, INT_MIN / INT64_MIN result in undefined behavior as they
  * are not representable as absolute values of their type. This is the same
  * as with *abs()
  * @see FFNABS()
  */
#define FFABS(a) ((a) >= 0 ? (a) : (-(a)))

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

#define FFSWAP(type,a,b) do{type SWAP_tmp= b; b= a; a= SWAP_tmp;}while(0)
#define FF_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))

#ifdef _MSC_VER
#pragma warning(disable:4305)
#pragma warning(disable:4244)
#pragma warning(disable:4101)  // unused variable
#endif

#define AV_BSWAP16C(x) (((x) << 8 & 0xff00)  | ((x) >> 8 & 0x00ff))
#define AV_BSWAP32C(x) (AV_BSWAP16C(x) << 16 | AV_BSWAP16C((x) >> 16))
#define av_be2ne32(x) AV_BSWAP32C((x))
