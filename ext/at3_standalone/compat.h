#pragma once

// Compat hacks

#include "attributes.h"

#include "error.h"

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
#define CONFIG_MDCT 1
#define CONFIG_FFT 1

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
