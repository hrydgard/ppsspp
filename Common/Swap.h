// Copyright (c) 2012- PPSSPP Project / Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <algorithm>

#include "Common/CommonTypes.h"
#include "Common/BitSet.h"
#include "Common/Log.h"

#if !defined(__BIG_ENDIAN__) && defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define __BIG_ENDIAN__ 1
#endif

#if !defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__)
#define __LITTLE_ENDIAN__ 1
#endif

template <typename T> struct swap_t {
	static_assert(std::is_scalar<T>::value && (sizeof(T) > 1), "swap_t used with an invalid type");

private:
	static T swap(T val) {
		switch (sizeof(T)) {
		case 2: *(u16 *)&val = swap16(*(u16 *)&val); break;
		case 4: *(u32 *)&val = swap32(*(u32 *)&val); break;
		case 8: *(u64 *)&val = swap64(*(u64 *)&val); break;
		default: break;
		}
		return val;
	}

public:
	swap_t() {}
	swap_t(T val) : swapped(swap(val)) {}

	swap_t &operator=(T val) { return *this = swap_t(val); }

	swap_t &operator&=(T val) { return *this = *this & val; }
	swap_t &operator|=(T val) { return *this = *this | val; }
	swap_t &operator^=(T val) { return *this = *this ^ val; }
	swap_t &operator+=(T val) { return *this = *this + val; }
	swap_t &operator-=(T val) { return *this = *this - val; }
	swap_t &operator*=(T val) { return *this = *this * val; }
	swap_t &operator/=(T val) { return *this = *this / val; }
	swap_t &operator%=(T val) { return *this = *this % val; }
	swap_t &operator<<=(T val) { return *this = *this << val; }
	swap_t &operator>>=(T val) { return *this = *this >> val; }
	swap_t &operator++() { return *this += 1; }
	swap_t &operator--() { return *this -= 1; }

	T operator++(int) {
		T old = *this;
		*this += 1;
		return old;
	}

	T operator--(int) {
		T old = *this;
		*this -= 1;
		return old;
	}

	operator T() const { return swap(swapped); }

private:
	T swapped;
};

#ifdef __LITTLE_ENDIAN__
template <typename T> using LEndian = T;
template <typename T> using BEndian = swap_t<T>;
#else
template <typename T> using LEndian = swap_t<T>;
template <typename T> using BEndian = T;
#endif

typedef LEndian<u16> u16_le;
typedef LEndian<u32> u32_le;
typedef LEndian<u64> u64_le;

typedef LEndian<s16> s16_le;
typedef LEndian<s32> s32_le;
typedef LEndian<s64> s64_le;

typedef LEndian<float> float_le;
typedef LEndian<double> double_le;

typedef BEndian<u16> u16_be;
typedef BEndian<u32> u32_be;
typedef BEndian<u64> u64_be;

typedef BEndian<s16> s16_be;
typedef BEndian<s32> s32_be;
typedef BEndian<s64> s64_be;

typedef BEndian<float> float_be;
typedef BEndian<double> double_be;

template <typename T>
static inline void ToLEndian(BEndian<T> *ptr, size_t count) {
	for (int i = 0; i < count; i++) {
		((LEndian<T>*)ptr)[i] = ptr[i];
	}
}

template <typename T>
static inline void ToLEndian(LEndian<T> *ptr, size_t count) {
	return;
}

template <typename T>
static inline void ToBEndian(LEndian<T> *ptr, size_t count) {
	for (int i = 0; i < count; i++) {
		((BEndian<T>*)ptr)[i] = ptr[i];
	}
}

template <typename T>
static inline void ToBEndian(BEndian<T> *ptr, size_t count) {
	return;
}

namespace std {
template <typename T>
inline const T &min(const T &a, const swap_t<T> &b) {
	return min(a, (T)b);
}

template <typename T>
inline const T &min(const swap_t<T> &a, const T &b) {
	return min((T)a, b);
}

template <typename T>
inline const T &max(const T &a, const swap_t<T> &b) {
	return max(a, (T)b);
}

template <typename T>
inline const T &max(const swap_t<T> &a, const T &b) {
	return max((T)a, b);
}
}

template <typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, (T)v, args...);
}

template <typename T0, typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, T0 v0, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, v0, (T)v, args...);
}

template <typename T0, typename T1, typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, T0 v0, T1 v1, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, v0, v1, (T)v, args...);
}

template <typename T0, typename T1, typename T2, typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, T0 v0, T1 v1, T2 v2, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, v0, v1, v2, (T)v, args...);
}

template <typename T0, typename T1, typename T2, typename T3, typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, T0 v0, T1 v1, T2 v2, T3 v3, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, v0, v1, v2, v3, (T)v, args...);
}

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, T0 v0, T1 v1, T2 v2, T3 v3, T4 v4, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, v0, v1, v2, v3, v4, (T)v, args...);
}

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, T0 v0, T1 v1, T2 v2, T3 v3, T4 v4, T5 v5, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, v0, v1, v2, v3, v4, v5, (T)v, args...);
}

template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T, typename... Targs>
static inline void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *fmt, T0 v0, T1 v1, T2 v2, T3 v3, T4 v4, T5 v5, T6 v6, swap_t<T> v, Targs... args) {
	GenericLog(level, type, file, line, fmt, v0, v1, v2, v3, v4, v5, v6, (T)v, args...);
}
