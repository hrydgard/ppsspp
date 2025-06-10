#pragma once

// This file is included by C, C++ and ASM files
// So do not output any token!

#define PPSSPP_ARCH(PPSSPP_FEATURE) (PPSSPP_ARCH_##PPSSPP_FEATURE)
#define PPSSPP_PLATFORM(PPSSPP_FEATURE) (PPSSPP_PLATFORM_##PPSSPP_FEATURE)
#define PPSSPP_API(PPSSPP_FEATURE) (PPSSPP_API_##PPSSPP_FEATURE)

// ARCH defines
#if defined(_M_IX86) || defined(__i386__) || defined (__EMSCRIPTEN__)
    #define PPSSPP_ARCH_X86 1
    #define PPSSPP_ARCH_32BIT 1
    #define PPSSPP_ARCH_SSE2 1
    //TODO: Remove this compat define
    #ifndef _M_IX86
        #define _M_IX86 600
    #endif
#endif

#if (defined(_M_X64) || defined(__amd64__) || defined(__x86_64__)) && !defined(__EMSCRIPTEN__)
    #define PPSSPP_ARCH_AMD64 1
    #define PPSSPP_ARCH_SSE2 1
    #if defined(__ILP32__)
        #define PPSSPP_ARCH_32BIT 1
    #else
        #define PPSSPP_ARCH_64BIT 1
    #endif
    //TODO: Remove this compat define
    #ifndef _M_X64
        #define _M_X64 1
    #endif
#endif

#if defined(__arm__) || defined(_M_ARM)
    #define PPSSPP_ARCH_ARM 1
    #define PPSSPP_ARCH_32BIT 1

    #if defined(__ARM_ARCH_7__) || \
      defined(__ARM_ARCH_7A__) || \
      defined(__ARM_ARCH_7S__)
        #define PPSSPP_ARCH_ARMV7 1
    #endif

    #if defined(__ARM_ARCH_7S__)
        #define PPSSPP_ARCH_ARMV7S 1
    #endif

    #if defined(__ARM_NEON) || defined(__ARM_NEON__)
        #define PPSSPP_ARCH_ARM_NEON 1
    #endif

    #if defined(_M_ARM)
        #define PPSSPP_ARCH_ARMV7 1
        #define PPSSPP_ARCH_ARM_NEON 1
    #endif
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    #define PPSSPP_ARCH_ARM64 1
    #define PPSSPP_ARCH_64BIT 1
    #define PPSSPP_ARCH_ARM_NEON 1  // Applies to both ARM32 and ARM64
    #define PPSSPP_ARCH_ARM64_NEON 1
#endif

#if defined(__mips64__)
    #define PPSSPP_ARCH_MIPS64 1
    #define PPSSPP_ARCH_64BIT 1
#elif defined(__mips__)
    #define PPSSPP_ARCH_MIPS 1
    #define PPSSPP_ARCH_32BIT 1
#endif

#if defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
    //https://github.com/riscv/riscv-c-api-doc/blob/master/riscv-c-api.md
    #define PPSSPP_ARCH_RISCV64 1
    #define PPSSPP_ARCH_64BIT 1
#endif

#if defined(__loongarch_lp64)
    //https://github.com/gcc-mirror/gcc/blob/master/gcc/config/loongarch/loongarch-c.cc
    #define PPSSPP_ARCH_LOONGARCH64 1
    #define PPSSPP_ARCH_64BIT 1
    #define PPSSPP_ARCH_LOONGARCH64_LSX 1
#endif

// PLATFORM defines
#if defined(_WIN32)
    // Covers both 32 and 64bit Windows
    #define PPSSPP_PLATFORM_WINDOWS 1
	#ifdef _M_ARM
        #define PPSSPP_ARCH_ARM_HARDFP 1
	#endif
	// UWP trickery
    #if defined(WINAPI_FAMILY) && defined(WINAPI_FAMILY_PARTITION)
        #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP
            #define PPSSPP_PLATFORM_UWP 1
        #endif
    #endif
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR
        #define PPSSPP_PLATFORM_IOS 1
        #define PPSSPP_PLATFORM_IOS_SIMULATOR 1
    #elif TARGET_OS_IPHONE
        #define PPSSPP_PLATFORM_IOS 1
    #elif TARGET_OS_MAC
        #define PPSSPP_PLATFORM_MAC 1
    #else
        #error "Unknown Apple platform"
    #endif
#elif defined(__SWITCH__)
    #define PPSSPP_PLATFORM_SWITCH 1
#elif defined(__ANDROID__)
    #define PPSSPP_PLATFORM_ANDROID 1
    #define PPSSPP_PLATFORM_LINUX 1
#elif defined(__linux__)
    #define PPSSPP_PLATFORM_LINUX 1
#elif defined(__OpenBSD__)
    #define PPSSPP_PLATFORM_OPENBSD 1
#endif

// Windows ARM/ARM64, and Windows UWP (all), are the only platform that don't do GL at all (until Apple finally removes it)
#if !PPSSPP_PLATFORM(WINDOWS) || ((!PPSSPP_ARCH(ARM) && !PPSSPP_ARCH(ARM64)) && !PPSSPP_PLATFORM(UWP))
#define PPSSPP_API_ANY_GL 1
#endif

#if PPSSPP_PLATFORM(WINDOWS)
#define PPSSPP_API_D3D11 1
#endif
