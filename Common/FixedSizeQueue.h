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

#ifndef _FIXED_SIZE_QUEUE_H_
#define _FIXED_SIZE_QUEUE_H_

#include <cstring>
#include "ChunkFile.h"

// STL-look-a-like interface, but name is mixed case to distinguish it clearly from the
// real STL classes.

// Not fully featured, no safety checking yet. Add features as needed.

template <class T, int N>
class FixedSizeQueue {
public:
	FixedSizeQueue() {
		storage_ = new T[N];
		clear();
	}

	~FixedSizeQueue() {
		delete [] storage_;
	}

	void clear() {
		head_ = 0;
		tail_ = 0;
		count_ = 0;
	}

	void push(T t) {
		storage_[tail_] = t;
		tail_++;
		if (tail_ == N)
			tail_ = 0;
		count_++;
	}

	void pop() {
		head_++;
		if (head_ == N)
			head_ = 0;
		count_--;
	}

	/*
	void push_array(const T *ptr, size_t num) {
		// TODO: memcpy
		for (size_t i = 0; i < num; i++) {
			push(ptr[i]);
		}
	}

	void pop_array(T *outptr, size_t num) {
		for (size_t i = 0; i < num; i++) {
			outptr[i] = front();
			pop();
		}
	}*/

	T pop_front() {
		const T &temp = storage_[head_];
		pop();
		return temp;
	}

	T &front() { return storage_[head_]; }

	const T &front() const { return storage_[head_]; }

	size_t size() const {
		return count_;
	}

	size_t capacity() const {
		return N;
	}

	int room() const {
		return N - count_;
	}

	bool empty() {
		return count_ == 0;
	}

	void DoState(PointerWrap &p) {
		int size = N;
		p.Do(size);
		if (size != N)
		{
			ERROR_LOG(HLE, "Savestate failure: Incompatible queue size.");
			return;
		}
		p.DoArray<T>(storage_, N);
		p.Do(head_);
		p.Do(tail_);
		p.Do(count_);
		p.DoMarker("FixedSizeQueue");
	}

private:
	T *storage_;
	int head_;
	int tail_;
	int count_;  // sacrifice 4 bytes for a simpler implementation. may optimize away in the future.

	// Make copy constructor private for now.
	FixedSizeQueue(FixedSizeQueue &other) {	}
};

#endif // _FIXED_SIZE_QUEUE_H_

