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

// Templates for save state serialization.  See ChunkFile.h.
#include "Common/ChunkFile.h"

void Do(PointerWrap &p, std::string &x);
void Do(PointerWrap &p, std::wstring &x);  // DEPRECATED, do not save wstrings
void Do(PointerWrap &p, std::u16string &x);

void Do(PointerWrap &p, tm &t);

// Don't use DoHelper_ directly.  Just use Do().

// This makes it a compile error if you forget to define DoState() on non-POD.
// Which also can be a problem, for example struct tm is non-POD on linux, for whatever reason...
#ifdef _MSC_VER
template<typename T, bool isPOD = std::is_pod<T>::value, bool isPointer = std::is_pointer<T>::value>
#else
template<typename T, bool isPOD = __is_pod(T), bool isPointer = std::is_pointer<T>::value>
#endif
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
		if (x != nullptr)
			delete x;
		x = new T();
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
	}
	break;
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
	}
	break;
	}
}

template<class K, class T>
void Do(PointerWrap &p, std::map<K, T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (auto it = x.begin(), end = x.end(); it != end; ++it) {
			if (it->second != nullptr)
				delete it->second;
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
		for (auto it = x.begin(), end = x.end(); it != end; ++it) {
			if (it->second != nullptr)
				delete it->second;
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
	}
	break;
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
	}
	break;
	}
}

template<class K, class T>
void Do(PointerWrap &p, std::multimap<K, T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (auto it = x.begin(), end = x.end(); it != end; ++it) {
			if (it->second != nullptr)
				delete it->second;
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
		for (auto it = x.begin(), end = x.end(); it != end; ++it) {
			if (it->second != nullptr)
				delete it->second;
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

template<class T>
void DoVector(PointerWrap &p, std::vector<T> &x, T &default_val) {
	u32 vec_size = (u32)x.size();
	Do(p, vec_size);
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

template<class T>
void DoList(PointerWrap &p, std::list<T> &x, T &default_val) {
	u32 list_size = (u32)x.size();
	Do(p, list_size);
	x.resize(list_size, default_val);

	typename std::list<T>::iterator itr, end;
	for (itr = x.begin(), end = x.end(); itr != end; ++itr)
		Do(p, *itr);
}

template<class T>
void Do(PointerWrap &p, std::list<T *> &x) {
	T *dv = nullptr;
	Do(p, x, dv);
}

template<class T>
void Do(PointerWrap &p, std::list<T> &x) {
	T dv = T();
	DoList(p, x, dv);
}

template<class T>
void Do(PointerWrap &p, std::list<T> &x, T &default_val) {
	DoList(p, x, default_val);
}

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

	default:
		ERROR_LOG(SAVESTATE, "Savestate error: invalid mode %d.", p.mode);
	}
}

// Store STL sets.
template <class T>
void Do(PointerWrap &p, std::set<T *> &x) {
	if (p.mode == PointerWrap::MODE_READ) {
		for (auto it = x.begin(), end = x.end(); it != end; ++it) {
			if (*it != nullptr)
				delete *it;
		}
	}
	DoSet(p, x);
}

template <class T>
void Do(PointerWrap &p, std::set<T> &x) {
	DoSet(p, x);
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

template<class T, LinkedListItem<T> *(*TNew)(), void (*TFree)(LinkedListItem<T> *), void (*TDo)(PointerWrap &, T *)>
void DoLinkedList(PointerWrap &p, LinkedListItem<T> *&list_start, LinkedListItem<T> **list_end = nullptr) {
	LinkedListItem<T> *list_cur = list_start;
	LinkedListItem<T> *prev = nullptr;

	while (true) {
		u8 shouldExist = (list_cur ? 1 : 0);
		Do(p, shouldExist);
		if (shouldExist == 1) {
			LinkedListItem<T> *cur = list_cur ? list_cur : TNew();
			TDo(p, (T *)cur);
			if (!list_cur) {
				if (p.mode == PointerWrap::MODE_READ) {
					cur->next = 0;
					list_cur = cur;
					if (prev)
						prev->next = cur;
					else
						list_start = cur;
				} else {
					TFree(cur);
					continue;
				}
			}
		} else {
			if (shouldExist != 0) {
				WARN_LOG(SAVESTATE, "Savestate failure: incorrect item marker %d", shouldExist);
				p.SetError(p.ERROR_FAILURE);
			}
			if (p.mode == PointerWrap::MODE_READ) {
				if (prev)
					prev->next = nullptr;
				if (list_end)
					*list_end = prev;
				if (list_cur) {
					if (list_start == list_cur)
						list_start = nullptr;
					do {
						LinkedListItem<T> *next = list_cur->next;
						TFree(list_cur);
						list_cur = next;
					} while (list_cur);
				}
			}
			break;
		}
		prev = list_cur;
		list_cur = list_cur->next;
	}
}
