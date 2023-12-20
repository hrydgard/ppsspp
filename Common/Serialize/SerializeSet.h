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
#include <set>
#include "Common/Serialize/SerializeFuncs.h"

template <class T>
void DoSet(PointerWrap &p, std::set<T> &x) {
	unsigned int number = (unsigned int)x.size();
	Do(p, number);

	switch (p.mode) {
	case PointerWrap::MODE_READ:
	{
		x.clear();
		while (number-- > 0) {
			T it = T();
			Do(p, it);
			x.insert(it);
		}
	}
	break;
	case PointerWrap::MODE_WRITE:
	case PointerWrap::MODE_MEASURE:
	case PointerWrap::MODE_VERIFY:
	{
		typename std::set<T>::iterator itr = x.begin();
		while (number-- > 0)
			Do(p, *itr++);
	}
	break;
	case PointerWrap::MODE_NOOP:
		break;
	}
}

template <class T>
void Do(PointerWrap &p, std::set<T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (T *s : x) {
			delete s;
		}
	}
	DoSet(p, x);
}

template <class T>
void Do(PointerWrap &p, std::set<T> &x) {
	DoSet(p, x);
}
