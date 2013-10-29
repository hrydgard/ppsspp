#pragma once

#include <stdint.h>
#include <stdlib.h>  // for byte swapping

#ifdef _WIN32
// We need this to compile without hundreds of std::bind errors in Visual Studio 2012
// since by default VARIADIC_MAX is something low like 3, 4, or 5.
// It's a good idea to include this file wherever std::bind is being used with more than 3, 4 or 5 args.
// Visual Studio 2013 doesn't need this after all, so we can simply check for equality.
#if _MSC_VER == 1700
 #undef _VARIADIC_MAX
 #define _VARIADIC_MAX 10
 #endif

#endif

#ifdef _WIN32
#pragma warning(disable:4244)
#pragma warning(disable:4996)
#pragma warning(disable:4305)  // truncation from double to float
#endif

#define DISALLOW_COPY_AND_ASSIGN(t) \
private: \
	t(const t &other);  \
	void operator =(const t &other);


typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t sint8;
typedef int16_t sint16;
typedef int32_t sint32;
typedef int64_t sint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

#ifdef _WIN32

typedef intptr_t ssize_t;

#include <tchar.h>

#define ALIGNED16(x) __declspec(align(16)) x
#define ALIGNED32(x) __declspec(align(32)) x
#define ALIGNED64(x) __declspec(align(64)) x
#define ALIGNED16_DECL(x) __declspec(align(16)) x
#define ALIGNED64_DECL(x) __declspec(align(64)) x

#else

#define ALIGNED16(x)  __attribute__((aligned(16))) x
#define ALIGNED32(x)  __attribute__((aligned(32))) x
#define ALIGNED64(x)  __attribute__((aligned(64))) x
#define ALIGNED16_DECL(x) __attribute__((aligned(16))) x
#define ALIGNED64_DECL(x) __attribute__((aligned(64))) x

#endif  // _WIN32

// Byteswapping
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

inline uint8 swap8(uint8 _data) {return _data;}

// Just in case this has been defined by platform
#undef swap16
#undef swap32
#undef swap64

#ifdef _WIN32
inline uint16 swap16(uint16 _data) {return _byteswap_ushort(_data);}
inline uint32 swap32(uint32 _data) {return _byteswap_ulong (_data);}
inline uint64 swap64(uint64 _data) {return _byteswap_uint64(_data);}
#elif defined(ARM)
inline uint16 swap16 (uint16 _data) { uint32 data = _data; __asm__ ("rev16 %0, %1\n" : "=l" (data) : "l" (data)); return (uint16)data;} 
inline uint32 swap32 (uint32 _data) {__asm__ ("rev %0, %1\n" : "=l" (_data) : "l" (_data)); return _data;} 
inline uint64 swap64(uint64 _data) {return ((uint64)swap32(_data) << 32) | swap32(_data >> 32);}
#elif __linux__ && !defined(ANDROID)
#include <byteswap.h>
inline uint16 swap16(uint16 _data) {return bswap_16(_data);}
inline uint32 swap32(uint32 _data) {return bswap_32(_data);}
inline uint64 swap64(uint64 _data) {return bswap_64(_data);}
#elif defined(__FreeBSD__)
#include <sys/endian.h>
inline uint16 swap16(uint16 _data) {return bswap16(_data);}
inline uint32 swap32(uint32 _data) {return bswap32(_data);}
inline uint64 swap64(uint64 _data) {return bswap64(_data);}
#elif defined(__GNUC__)
inline uint16 swap16(uint16 _data) {return (_data >> 8) | (_data << 8);}
inline uint32 swap32(uint32 _data) {return __builtin_bswap32(_data);}
inline uint64 swap64(uint64 _data) {return __builtin_bswap64(_data);}
#else
// Slow generic implementation. Hopefully this never hits
inline uint16 swap16(uint16 data) {return (data >> 8) | (data << 8);}
inline uint32 swap32(uint32 data) {return (swap16(data) << 16) | swap16(data >> 16);}
inline uint64 swap64(uint64 data) {return ((uint64)swap32(data) << 32) | swap32(data >> 32);}
#endif

inline uint16 swap16(const uint8* _pData) {return swap16(*(const uint16*)_pData);}
inline uint32 swap32(const uint8* _pData) {return swap32(*(const uint32*)_pData);}
inline uint64 swap64(const uint8* _pData) {return swap64(*(const uint64*)_pData);}

// Thread local storage
#ifdef _WIN32
#define __THREAD __declspec( thread ) 
#else
#define __THREAD __thread
#endif

// For really basic windows code compat
#ifndef _TCHAR_DEFINED
typedef char TCHAR;
#endif
