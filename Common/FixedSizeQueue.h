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
#include "MemoryUtil.h"

// STL-look-a-like interface, but name is mixed case to distinguish it clearly from the
// real STL classes.

// Not fully featured, no safety checking yet. Add features as needed.

template <class T, int N>
class FixedSizeQueue {
public:
	FixedSizeQueue() {
		// Allocate aligned memory, just because.
		//int sizeInBytes = N * sizeof(T);
		//storage_ = (T *)AllocateMemoryPages(sizeInBytes);
		storage_ = new T[N];
		clear();
	}

	~FixedSizeQueue() {
		// FreeMemoryPages((void *)storage_, N * sizeof(T));
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

	// Gets pointers to write to directly.
	void pushPointers(size_t size, T **dest1, size_t *sz1, T **dest2, size_t *sz2) {
		if (tail_ + size < N) {
			*dest1 = &storage_[tail_];
			*sz1 = size;
			tail_ += (int)size;
			if (tail_ == N) tail_ = 0;
			*dest2 = 0;
			*sz2 = 0;
		} else {
			*dest1 = &storage_[tail_];
			*sz1 = N - tail_;
			tail_ = (int)(size - *sz1);
			*dest2 = &storage_[0];
			*sz2 = tail_;
		}
		count_ += (int)size;
	}

	void popPointers(size_t size, const T **src1, size_t *sz1, const T **src2, size_t *sz2) {
		if (size > count_) size = count_;

		if (head_ + size < N) {
			*src1 = &storage_[head_];
			*sz1 = size;
			head_ += (int)size;
			if (head_ == N) head_ = 0;
			*src2 = 0;
			*sz2 = 0;
		} else {
			*src1 = &storage_[head_];
			*sz1 = N - head_;
			head_ = (int)(size - *sz1);
			*src2 = &storage_[0];
			*sz2 = head_;
		}
		count_ -= (int)size;
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


// I'm not sure this is 100% safe but it might be "Good Enough" :)
// TODO: Use this, maybe make it safer first by using proper atomics
// instead of volatile
template<class T, int blockSize, int numBlocks>
class LockFreeBlockQueue {
public:
	LockFreeBlockQueue() {
		curReadBlock = 0;
		curWriteBlock = 0;
		for (size_t i = 0; i < numBlocks; i++) {
			blocks[i] = new T[blockSize];
		}
	}
	~LockFreeBlockQueue() {
		for (size_t i = 0; i < numBlocks; i++) {
			delete [] blocks[i];
		}
	}

	// Write to the returned pointer then call EndPush to finish the push.
	T *BeginPush() {
		return blocks[curWriteBlock];
	}
	void EndPush() {
		curWriteBlock++;
		if (curWriteBlock == NUM_BLOCKS)
			curWriteBlock = 0;
	}

	bool CanPush() { 
		int nextBlock = curWriteBlock + 1;
		if (nextBlock == NUM_BLOCKS) nextBlock = 0;
		return nextBlock != curReadBlock;
	}

	bool CanPop() { return curReadBlock != curWriteBlock; }

	// Read from the returned pointer then call EndPush to finish the pop.
	T *BeginPop() {
		return blocks[curReadBlock];
	}
	T *EndPop() {
		curReadBlock++;
		if (curReadBlock == NUM_BLOCKS)
			curReadBlock = 0;
	}

private:
	enum { NUM_BLOCKS = 16 };
	T **blocks[NUM_BLOCKS];

	volatile int curReadBlock;
	volatile int curWriteBlock;
};

#endif // _FIXED_SIZE_QUEUE_H_

