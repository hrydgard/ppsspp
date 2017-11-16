// This file is under the public domain.

#pragma once

#include <cstddef>
#include "CommonTypes.h"

// Helper functions:

#ifdef _WIN32
#include <intrin.h>
template <typename T>
inline int CountSetBits(T v) {
	// from https://graphics.stanford.edu/~seander/bithacks.html
	// GCC has this built in, but MSVC's intrinsic will only emit the actual
	// POPCNT instruction, which we're not depending on
	v = v - ((v >> 1) & (T)~(T)0/3);
	v = (v & (T)~(T)0/15*3) + ((v >> 2) & (T)~(T)0/15*3);
	v = (v + (v >> 4)) & (T)~(T)0/255*15;
	return (T)(v * ((T)~(T)0/255)) >> (sizeof(T) - 1) * 8;
}
inline int LeastSignificantSetBit(u32 val)
{
	unsigned long index;
	_BitScanForward(&index, val);
	return (int)index;
}
#ifdef _M_X64
inline int LeastSignificantSetBit(u64 val)
{
	unsigned long index;
	_BitScanForward64(&index, val);
	return (int)index;
}
#endif
#else
inline int CountSetBits(u32 val) { return __builtin_popcount(val); }
inline int CountSetBits(u64 val) { return __builtin_popcountll(val); }
inline int LeastSignificantSetBit(u32 val) { return __builtin_ctz(val); }
inline int LeastSignificantSetBit(u64 val) { return __builtin_ctzll(val); }
#endif
