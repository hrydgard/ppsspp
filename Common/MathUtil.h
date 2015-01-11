
// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

namespace MathUtil
{

#define ROUND_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define ROUND_DOWN(x, a) ((x) & ~((a) - 1))

template<class T>
inline void Clamp(T* val, const T& min, const T& max)
{
	if (*val < min)
		*val = min;
	else if (*val > max)
		*val = max;
}

template<class T>
inline T Clamp(const T val, const T& min, const T& max)
{
	T ret = val;
	Clamp(&ret, min, max);
	return ret;
}

}