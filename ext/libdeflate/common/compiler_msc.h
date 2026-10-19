/*
 * compiler_msc.h - definitions for the Microsoft C Compiler
 */

#include <stdint.h>
#include <stdlib.h> /* for _byteswap_*() */

#define LIBEXPORT	__declspec(dllexport)

/*
 * Old versions (e.g. VS2010) of MSC don't have the C99 header stdbool.h.
 * Beware: the below replacement isn't fully standard, since normally any value
 * != 0 should be implicitly cast to a bool with value 1... but that doesn't
 * happen if bool is really just an 'int'.
 */
typedef int bool;
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

/* Define ssize_t */
#ifdef _WIN64
typedef long long ssize_t;
#else
typedef int ssize_t;
#endif

/* Assume a little endian architecture with fast unaligned access */
#define CPU_IS_LITTLE_ENDIAN()		1
#define UNALIGNED_ACCESS_IS_FAST	1

/* __restrict has nonstandard behavior; don't use it */
#define restrict

/* ... but we can use __inline and __forceinline */
#define inline		__inline
#define forceinline	__forceinline

/* Byte swap functions */
#define bswap16	_byteswap_ushort
#define bswap32	_byteswap_ulong
#define bswap64	_byteswap_uint64

/* Bit scan functions (32-bit) */

static forceinline unsigned
bsr32(uint32_t n)
{
	_BitScanReverse(&n, n);
	return n;
}
#define bsr32 bsr32

static forceinline unsigned
bsf32(uint32_t n)
{
	_BitScanForward(&n, n);
	return n;
}
#define bsf32 bsf32

#ifdef _M_X64 /* Bit scan functions (64-bit) */

static forceinline unsigned
bsr64(uint64_t n)
{
	_BitScanReverse64(&n, n);
	return n;
}
#define bsr64 bsr64

static forceinline unsigned
bsf64(uint64_t n)
{
	_BitScanForward64(&n, n);
	return n;
}
#define bsf64 bsf64

#endif /* _M_X64 */
