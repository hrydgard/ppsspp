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
#include <list>
#include "Common/Serialize/SerializeFuncs.h"

template<class T>
void DoList(PointerWrap &p, std::list<T> &x, T &default_val) {
	u32 list_size = (u32)x.size();
	Do(p, list_size);
	x.resize(list_size, default_val);

	for (T &elem : x)
		Do(p, elem);
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
				WARN_LOG(Log::SaveState, "Savestate failure: incorrect item marker %d", shouldExist);
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

inline void DoIgnoreUnusedLinkedList(PointerWrap &p) {
	u8 shouldExist = 0;
	Do(p, shouldExist);
	if (shouldExist) {
		// We don't support this linked list and haven't used it forever.
		p.SetError(p.ERROR_FAILURE);
	}
}
