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
	swap_t(T val) : swapped_(swap(val)) {}
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

	operator T() const { return swap(swapped_); }

private:
	T swapped_;
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
