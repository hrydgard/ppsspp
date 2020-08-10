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
#include <deque>
#include "Common/Serialize/SerializeFuncs.h"

template<class T>
void DoDeque(PointerWrap &p, std::deque<T> &x, T &default_val) {
	u32 deq_size = (u32)x.size();
	Do(p, deq_size);
	x.resize(deq_size, default_val);
	u32 i;
	for (i = 0; i < deq_size; i++)
		Do(p, x[i]);
}

template<class T>
void Do(PointerWrap &p, std::deque<T *> &x) {
	T *dv = nullptr;
	DoDeque(p, x, dv);
}

template<class T>
void Do(PointerWrap &p, std::deque<T> &x) {
	T dv = T();
	DoDeque(p, x, dv);
}
