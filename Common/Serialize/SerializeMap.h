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
#include <map>
#include <unordered_map>
#include "Common/Serialize/SerializeFuncs.h"

template<class M>
void DoMap(PointerWrap &p, M &x, typename M::mapped_type &default_val) {
	unsigned int number = (unsigned int)x.size();
	Do(p, number);
	switch (p.mode) {
	case PointerWrap::MODE_READ:
	{
		x.clear();
		while (number > 0) {
			typename M::key_type first = typename M::key_type();
			Do(p, first);
			typename M::mapped_type second = default_val;
			Do(p, second);
			x[first] = second;
			--number;
		}
		break;
	}
	case PointerWrap::MODE_WRITE:
	case PointerWrap::MODE_MEASURE:
	case PointerWrap::MODE_VERIFY:
	{
		typename M::iterator itr = x.begin();
		while (number > 0) {
			typename M::key_type first = itr->first;
			Do(p, first);
			Do(p, itr->second);
			--number;
			++itr;
		}
		break;
	}
	case PointerWrap::MODE_NOOP:
		break;
	}
}

template<class K, class T>
void Do(PointerWrap &p, std::map<K, T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (auto &iter : x) {
			delete iter.second;
		}
	}
	T *dv = nullptr;
	DoMap(p, x, dv);
}

template<class K, class T>
void Do(PointerWrap &p, std::map<K, T> &x) {
	T dv = T();
	DoMap(p, x, dv);
}

template<class K, class T>
void Do(PointerWrap &p, std::unordered_map<K, T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (auto &iter : x) {
			delete iter.second;
		}
	}
	T *dv = nullptr;
	DoMap(p, x, dv);
}

template<class K, class T>
void Do(PointerWrap &p, std::unordered_map<K, T> &x) {
	T dv = T();
	DoMap(p, x, dv);
}

template<class M>
void DoMultimap(PointerWrap &p, M &x, typename M::mapped_type &default_val) {
	unsigned int number = (unsigned int)x.size();
	Do(p, number);
	switch (p.mode) {
	case PointerWrap::MODE_READ:
	{
		x.clear();
		while (number > 0) {
			typename M::key_type first = typename M::key_type();
			Do(p, first);
			typename M::mapped_type second = default_val;
			Do(p, second);
			x.insert(std::make_pair(first, second));
			--number;
		}
		break;
	}
	case PointerWrap::MODE_WRITE:
	case PointerWrap::MODE_MEASURE:
	case PointerWrap::MODE_VERIFY:
	{
		typename M::iterator itr = x.begin();
		while (number > 0) {
			Do(p, itr->first);
			Do(p, itr->second);
			--number;
			++itr;
		}
		break;
	}
	case PointerWrap::MODE_NOOP:
		break;
	}
}

template<class K, class T>
void Do(PointerWrap &p, std::multimap<K, T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (auto &iter : x) {
			delete iter.second;
		}
	}
	T *dv = nullptr;
	DoMultimap(p, x, dv);
}

template<class K, class T>
void Do(PointerWrap &p, std::multimap<K, T> &x) {
	T dv = T();
	DoMultimap(p, x, dv);
}

template<class K, class T>
void Do(PointerWrap &p, std::unordered_multimap<K, T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (auto &iter : x) {
			delete iter.second;
		}
	}
	T *dv = nullptr;
	DoMultimap(p, x, dv);
}

template<class K, class T>
void Do(PointerWrap &p, std::unordered_multimap<K, T> &x) {
	T dv = T();
	DoMultimap(p, x, dv);
}
