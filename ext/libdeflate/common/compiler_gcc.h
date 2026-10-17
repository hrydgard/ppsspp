/*
 * compiler_gcc.h - definitions for the GNU C Compiler.  This also handles clang
 * and the Intel C Compiler (icc).
 *
 * TODO: icc is not well tested, so some things are currently disabled even
 * though they maybe can be enabled on some icc versions.
 */

#if !defined(__clang__) && !defined(__INTEL_COMPILER)
#  define GCC_PREREQ(major, minor)		\
	(__GNUC__ > (major) ||			\
	 (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#else
#  define GCC_PREREQ(major, minor)	0
#endif

/* Note: only check the clang version when absolutely necessary!
 * "Vendors" such as Apple can use different version numbers. */
#ifdef __clang__
#  ifdef __apple_build_version__
#    define CLANG_PREREQ(major, minor, apple_version)	\
	(__apple_build_version__ >= (apple_version))
#  else
#    define CLANG_PREREQ(major, minor, apple_version)	\
	(__clang_major__ > (major) ||			\
	 (__clang_major__ == (major) && __clang_minor__ >= (minor)))
#  endif
#else
#  define CLANG_PREREQ(major, minor, apple_version)	0
#endif

#ifndef __has_attribute
#  define __has_attribute(attribute)	0
#endif
#ifndef __has_feature
#  define __has_feature(feature)	0
#endif
#ifndef __has_builtin
#  define __has_builtin(builtin)	0
#endif

#ifdef _WIN32
#  define LIBEXPORT __declspec(dllexport)
#else
#  define LIBEXPORT __attribute__((visibility("default")))
#endif

#define inline			inline
#define forceinline		inline __attribute__((always_inline))
#define restrict		__restrict__
#define likely(expr)		__builtin_expect(!!(expr), 1)
#define unlikely(expr)		__builtin_expect(!!(expr), 0)
#define prefetchr(addr)		__builtin_prefetch((addr), 0)
#define prefetchw(addr)		__builtin_prefetch((addr), 1)
#define _aligned_attribute(n)	__attribute__((aligned(n)))

#define COMPILER_SUPPORTS_TARGET_FUNCTION_ATTRIBUTE	\
	(GCC_PREREQ(4, 4) || __has_attribute(target))

#if COMPILER_SUPPORTS_TARGET_FUNCTION_ATTRIBUTE

#  if defined(__i386__) || defined(__x86_64__)

#    define COMPILER_SUPPORTS_PCLMUL_TARGET	\
	(GCC_PREREQ(4, 4) || __has_builtin(__builtin_ia32_pclmulqdq128))

#    define COMPILER_SUPPORTS_AVX_TARGET	\
	(GCC_PREREQ(4, 6) || __has_builtin(__builtin_ia32_maxps256))

#    define COMPILER_SUPPORTS_BMI2_TARGET	\
	(GCC_PREREQ(4, 7) || __has_builtin(__builtin_ia32_pdep_di))

#    define COMPILER_SUPPORTS_AVX2_TARGET	\
	(GCC_PREREQ(4, 7) || __has_builtin(__builtin_ia32_psadbw256))

#    define COMPILER_SUPPORTS_AVX512BW_TARGET	\
	(GCC_PREREQ(5, 1) || __has_builtin(__builtin_ia32_psadbw512))

	/*
	 * Prior to gcc 4.9 (r200349) and clang 3.8 (r239883), x86 intrinsics
	 * not available in the main target could not be used in 'target'
	 * attribute functions.  Unfortunately clang has no feature test macro
	 * for this so we have to check its version.
	 */
#    if GCC_PREREQ(4, 9) || CLANG_PREREQ(3, 8, 7030000)
#      define COMPILER_SUPPORTS_SSE2_TARGET_INTRINSICS	1
#      define COMPILER_SUPPORTS_PCLMUL_TARGET_INTRINSICS	\
		COMPILER_SUPPORTS_PCLMUL_TARGET
#      define COMPILER_SUPPORTS_AVX2_TARGET_INTRINSICS	\
		COMPILER_SUPPORTS_AVX2_TARGET
#      define COMPILER_SUPPORTS_AVX512BW_TARGET_INTRINSICS	\
		COMPILER_SUPPORTS_AVX512BW_TARGET
#    endif

#  elif defined(__arm__) || defined(__aarch64__)

    /*
     * Determine whether NEON and crypto intrinsics are supported.
     *
     * With gcc prior to 6.1, (r230411 for arm32, r226563 for arm64), neither
     * was available unless enabled in the main target.
     *
     * But even after that, to include <arm_neon.h> (which contains both the
     * basic NEON intrinsics and the crypto intrinsics) the main target still
     * needs to have:
     *   - gcc: hardware floating point support
     *   - clang: NEON support (but not necessarily crypto support)
     */
#    if (GCC_PREREQ(6, 1) && defined(__ARM_FP)) || \
        (defined(__clang__) && defined(__ARM_NEON))
#      define COMPILER_SUPPORTS_NEON_TARGET_INTRINSICS 1
       /*
        * The crypto intrinsics are broken on arm32 with clang, even when using
        * -mfpu=crypto-neon-fp-armv8, because clang's <arm_neon.h> puts them
        * behind __aarch64__.  Undefine __ARM_FEATURE_CRYPTO in that case...
        */
#      if defined(__clang__) && defined(__arm__)
#        undef __ARM_FEATURE_CRYPTO
#      elif __has_builtin(__builtin_neon_vmull_p64) || !defined(__clang__)
#        define COMPILER_SUPPORTS_PMULL_TARGET_INTRINSICS 1
#      endif
#    endif

     /*
      * Determine whether CRC32 intrinsics are supported.
      *
      * With gcc r274827 or later (gcc 10.1+, 9.3+, or 8.4+), or with clang,
      * they work as expected.  (Well, not quite.  There's still a bug, but we
      * have to work around it later when including arm_acle.h.)
      */
#    if GCC_PREREQ(10, 1) || \
        (GCC_PREREQ(9, 3) && !GCC_PREREQ(10, 0)) || \
        (GCC_PREREQ(8, 4) && !GCC_PREREQ(9, 0)) || \
        (defined(__clang__) && __has_builtin(__builtin_arm_crc32b))
#      define COMPILER_SUPPORTS_CRC32_TARGET_INTRINSICS 1
#    endif

#  endif /* __arm__ || __aarch64__ */

#endif /* COMPILER_SUPPORTS_TARGET_FUNCTION_ATTRIBUTE */

/*
 * Prior to gcc 5.1 and clang 3.9, emmintrin.h only defined vectors of signed
 * integers (e.g. __v4si), not vectors of unsigned integers (e.g.  __v4su).  But
 * we need the unsigned ones in order to avoid signed integer overflow, which is
 * undefined behavior.  Add the missing definitions for the unsigned ones if
 * needed.
 */
#if (GCC_PREREQ(4, 0) && !GCC_PREREQ(5, 1)) || \
    (defined(__clang__) && !CLANG_PREREQ(3, 9, 8020000))
typedef unsigned long long  __v2du __attribute__((__vector_size__(16)));
typedef unsigned int        __v4su __attribute__((__vector_size__(16)));
typedef unsigned short      __v8hu __attribute__((__vector_size__(16)));
typedef unsigned char      __v16qu __attribute__((__vector_size__(16)));
typedef unsigned long long  __v4du __attribute__((__vector_size__(32)));
typedef unsigned int        __v8su __attribute__((__vector_size__(32)));
typedef unsigned short     __v16hu __attribute__((__vector_size__(32)));
typedef unsigned char      __v32qu __attribute__((__vector_size__(32)));
#endif

/* Newer gcc supports __BYTE_ORDER__.  Older gcc doesn't. */
#ifdef __BYTE_ORDER__
#  define CPU_IS_LITTLE_ENDIAN() (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#endif

#if GCC_PREREQ(4, 8) || __has_builtin(__builtin_bswap16)
#  define bswap16	__builtin_bswap16
#endif

#if GCC_PREREQ(4, 3) || __has_builtin(__builtin_bswap32)
#  define bswap32	__builtin_bswap32
#endif

#if GCC_PREREQ(4, 3) || __has_builtin(__builtin_bswap64)
#  define bswap64	__builtin_bswap64
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(__ARM_FEATURE_UNALIGNED) || defined(__powerpc64__)
#  define UNALIGNED_ACCESS_IS_FAST 1
#endif

#define bsr32(n)	(31 - __builtin_clz(n))
#define bsr64(n)	(63 - __builtin_clzll(n))
#define bsf32(n)	__builtin_ctz(n)
#define bsf64(n)	__builtin_ctzll(n)
