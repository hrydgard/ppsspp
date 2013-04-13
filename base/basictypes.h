#pragma once

#include <stdint.h>
#include <stdlib.h>  // for byte swapping

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

#ifdef _WIN32
inline uint16 swap16(uint16 _data) {return _byteswap_ushort(_data);}
inline uint32 swap32(uint32 _data) {return _byteswap_ulong (_data);}
inline uint64 swap64(uint64 _data) {return _byteswap_uint64(_data);}
#elif __linux__
#include <byteswap.h>
#undef swap16
#undef swap32
#undef swap64
inline uint16 swap16(uint16 _data) {return bswap_16(_data);}
inline uint32 swap32(uint32 _data) {return bswap_32(_data);}
inline uint64 swap64(uint64 _data) {return bswap_64(_data);}
#else
// Slow generic implementation.
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
#ifndef TCHAR
typedef char TCHAR;
#endif
