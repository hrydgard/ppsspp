#pragma once

// This file is included by C, C++ and ASM files
// So do not output any token!

#define PPSSPP_ARCH(PPSSPP_FEATURE) (PPSSPP_ARCH_##PPSSPP_FEATURE)
#define PPSSPP_PLATFORM(PPSSPP_FEATURE) (PPSSPP_PLATFORM_##PPSSPP_FEATURE)

// ARCH defines
#if defined(_M_IX86) || defined(__i386__)
    #define PPSSPP_ARCH_X86 1
    #define PPSSPP_ARCH_32BIT 1
    //TODO: Remove this compat define
    #ifndef _M_IX86
        #define _M_IX86 600
    #endif
#endif

#if defined(_M_X64) || defined(__amd64__) || defined(__x86_64__)
    #define PPSSPP_ARCH_AMD64 1
    #define PPSSPP_ARCH_64BIT 1
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
    //TODO: Remove this compat define
    #ifndef ARM
        #define ARM 1
    #endif
#endif

#if defined(__aarch64__)
    #define PPSSPP_ARCH_ARM64 1
    #define PPSSPP_ARCH_64BIT 1
    #define PPSSPP_ARCH_ARM_NEON 1
    //TODO: Remove this compat define
    #ifndef ARM64
        #define ARM64 1
    #endif
#endif

#if defined(__mips64__)
    #define PPSSPP_ARCH_MIPS64 1
    #define PPSSPP_ARCH_64BIT 1
#elif defined(__mips__)
    #define PPSSPP_ARCH_MIPS 1
    #define PPSSPP_ARCH_32BIT 1
    //TODO: Remove this compat define
    #ifndef MIPS
        #define MIPS 1
    #endif
#endif


// PLATFORM defines
#if defined(_WIN32)
    // Covers both 32 and 64bit Windows
    #define PPSSPP_PLATFORM_WINDOWS 1
    // UWP trickery
    #if defined(WINAPI_FAMILY) && defined(WINAPI_FAMILY_PARTITION)
        #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP
            #define PPSSPP_PLATFORM_UWP 1
            #ifdef _M_ARM
                #define PPSSPP_ARCH_ARM_HARDFP 1
            #endif
        #endif
    #endif
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR
        #define PPSSPP_PLATFORM_IOS 1
        #define PPSSPP_PLATFORM_IOS_SIMULATOR 1
        //TODO: Remove this compat define
        #ifndef IOS
            #define IOS 1
        #endif
    #elif TARGET_OS_IPHONE
        #define PPSSPP_PLATFORM_IOS 1
        //TODO: Remove this compat define
        #ifndef IOS
            #define IOS 1
        #endif
    #elif TARGET_OS_MAC
        #define PPSSPP_PLATFORM_MAC 1
    #else
        #error "Unknown Apple platform"
    #endif
#elif defined(__ANDROID__)
    #define PPSSPP_PLATFORM_ANDROID 1
    #define PPSSPP_PLATFORM_LINUX 1
#elif defined(__linux__)
    #define PPSSPP_PLATFORM_LINUX 1
#endif
