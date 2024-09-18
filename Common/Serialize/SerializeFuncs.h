// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

// Templates for save state serialization.  See Serializer.h.
#include <string>
#include <type_traits>
#include "Common/Serialize/Serializer.h"
#include "Common/Swap.h"

void Do(PointerWrap &p, std::string &x);
void Do(PointerWrap &p, std::wstring &x);  // DEPRECATED, do not save wstrings
void Do(PointerWrap &p, std::u16string &x);

void Do(PointerWrap &p, tm &t);

// Don't use DoHelper_ directly.  Just use Do().

// This makes it a compile error if you forget to define DoState() on non-POD.
// Which also can be a problem, for example struct tm is non-POD on linux, for whatever reason...
template<typename T, bool isPOD = std::is_standard_layout<T>::value && std::is_trivial<T>::value, bool isPointer = std::is_pointer<T>::value>
struct DoHelper_ {
	static void DoArray(PointerWrap &p, T *x, int count) {
		for (int i = 0; i < count; ++i)
			Do(p, x[i]);
	}

	static void DoThing(PointerWrap &p, T &x) {
		DoClass(p, x);
	}
};

template<typename T>
struct DoHelper_<T, true, false> {
	static void DoArray(PointerWrap &p, T *x, int count) {
		p.DoVoid((void *)x, sizeof(T) * count);
	}

	static void DoThing(PointerWrap &p, T &x) {
		p.DoVoid((void *)&x, sizeof(x));
	}
};

template<class T>
void DoClass(PointerWrap &p, T &x) {
	x.DoState(p);
}

template<class T>
void DoClass(PointerWrap &p, T *&x) {
	if (p.mode == PointerWrap::MODE_READ) {
		delete x;
		x = new T();
	}
	x->DoState(p);
}

template<class T, class S>
void DoSubClass(PointerWrap &p, T *&x) {
	if (p.mode == PointerWrap::MODE_READ) {
		if (x != nullptr)
			delete x;
		x = new S();
	}
	x->DoState(p);
}


template<class T>
void DoArray(PointerWrap &p, T *x, int count) {
	DoHelper_<T>::DoArray(p, x, count);
}

template<class T>
void Do(PointerWrap &p, T &x) {
	DoHelper_<T>::DoThing(p, x);
}

template<class T>
void DoVector(PointerWrap &p, std::vector<T> &x, T &default_val) {
	u32 vec_size = (u32)x.size();
	Do(p, vec_size);
	if (vec_size != x.size())
		x.resize(vec_size, default_val);
	if (vec_size > 0)
		DoArray(p, &x[0], vec_size);
}

template<class T>
void Do(PointerWrap &p, std::vector<T *> &x) {
	T *dv = nullptr;
	DoVector(p, x, dv);
}

template<class T>
void Do(PointerWrap &p, std::vector<T> &x) {
	T dv = T();
	DoVector(p, x, dv);
}

template<class T>
void Do(PointerWrap &p, std::vector<T> &x, T &default_val) {
	DoVector(p, x, default_val);
}

template<typename T, typename F>
void Do(PointerWrap &p, swap_struct_t<T, F> &x) {
	T v = x.swap();
	Do(p, v);
	x = v;
}

template<class T>
void DoPointer(PointerWrap &p, T *&x, T *const base) {
	// pointers can be more than 2^31 apart, but you're using this function wrong if you need that much range
	s32 offset = x - base;
	Do(p, offset);
	if (p.mode == PointerWrap::MODE_READ)
		x = base + offset;
}
