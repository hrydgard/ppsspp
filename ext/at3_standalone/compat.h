#pragma once

// Compat hacks

#define CONFIG_MEMORY_POISONING 0
#define CONFIG_HARDCODED_TABLES 0
#define CONFIG_ME_CMP 0
#define HWACCEL_CODEC_CAP_EXPERIMENTAL 0
#define HAVE_THREADS 0
#define CONFIG_FRAME_THREAD_ENCODER 0
#define CONFIG_GRAY 0
#define NULL_IF_CONFIG_SMALL(x) NULL
#define ARCH_AARCH64 0
#define ARCH_ARM 0
#define ARCH_PPC 0
#define ARCH_X86 0
#define HAVE_MIPSFPU 0
#define FF_API_AVPACKET_OLD_API 1
#define FF_DISABLE_DEPRECATION_WARNINGS
#define FF_ENABLE_DEPRECATION_WARNINGS
#define CONFIG_FFT 1

#define DECLARE_ALIGNED(bits, type, name)  type name
#define LOCAL_ALIGNED(bits, type, name, subscript) type name subscript
#define av_restrict
#define av_always_inline __forceinline
#define av_const
#define av_alias
#define av_unused
#define av_pure
#define av_warn_unused_result
#define av_assert0(cond)
#define av_assert1(cond)
#define av_assert2(cond)
#define attribute_deprecated
#define attribute_align_arg
#define av_printf_format(a,b)
#define avpriv_report_missing_feature(...)


#define AVERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.
#define AVUNERROR(e) (-(e)) ///< Returns a POSIX error code from a library function error return value.

#define FFERRTAG(a, b, c, d) (-(int)MKTAG(a, b, c, d))

#define AVERROR_INVALIDDATA        FFERRTAG( 'I','N','D','A') ///< Invalid data found when processing input
#define AVERROR_PATCHWELCOME       FFERRTAG( 'P','A','W','E') ///< Not yet implemented in FFmpeg, patches welcome

#pragma warning(disable:4305)
#pragma warning(disable:4244)

int ff_fast_malloc(void *ptr, unsigned int *size, size_t min_size, int zero_realloc);

/**
 * Copy the string src to dst, but no more than size - 1 bytes, and
 * null-terminate dst.
 *
 * This function is the same as BSD strlcpy().
 *
 * @param dst destination buffer
 * @param src source string
 * @param size size of destination buffer
 * @return the length of src
 *
 * @warning since the return value is the length of src, src absolutely
 * _must_ be a properly 0-terminated string, otherwise this will read beyond
 * the end of the buffer and possibly crash.
 */
size_t av_strlcpy(char *dst, const char *src, size_t size);

/**
 * Locale-independent conversion of ASCII characters to uppercase.
 */
static inline av_const int av_toupper(int c)
{
	if (c >= 'a' && c <= 'z')
		c ^= 0x20;
	return c;
}


#define AV_BSWAP16C(x) (((x) << 8 & 0xff00)  | ((x) >> 8 & 0x00ff))
#define AV_BSWAP32C(x) (AV_BSWAP16C(x) << 16 | AV_BSWAP16C((x) >> 16))
#define av_be2ne32(x) AV_BSWAP32C((x))
