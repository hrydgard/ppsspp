#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1700
#error You need a newer version of Visual Studio
#else
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x601 // Compile for Win7 on Visual Studio 2012 and above
#undef WINVER
#define WINVER 0x601
#endif

#undef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1

#ifdef _WIN32
#pragma warning(disable:4091)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#undef min
#undef max
#undef DrawText
#endif
