#pragma once

// This provides MSVC's arch-detection defines on other platforms as well.

#ifndef _WIN32

#if defined(__x86_64__) && !defined(_M_X64)
#define _M_X64 1
#endif

#if defined(__x86__) && !defined(_M_IX86)
#define _M_IX86 1
#endif

// TODO: ARM, ARM64

#endif
