#pragma once

#include <cstdlib>
#include <cstring>

#include "Common/CommonTypes.h"

class MemoryArena {
public:
	MemoryArena(size_t size) : ptr_(0), totalSize_(size) {
		data_ = new u8[size];
		memset(data_, 0, size);
	}
	~MemoryArena() {
		delete [] data_;
	}

	template <class T>
	T *Allocate(T **ptr) {
		*ptr = reinterpret_cast<T *>(data_ + ptr_);
		ptr_ += sizeof(T);
		return *ptr;
	}

	u8 *AllocateBytes(size_t bytes) {
		u8 *ptr = data_ + ptr_;
		ptr_ += bytes;
		return ptr;
	}

	template<class T>
	void Rewind(T *ptr) {
		ptr_ -= sizeof(T);
	}

	void Rewind(size_t bytes) {
		ptr_ -= bytes;
	}

	// This invalidates all pointers allocated.
	void Clear() {
		ptr_ = 0;
	}

	bool HasRoomFor(size_t size) const {
		return ptr_ + size <= totalSize_;
	}

private:
	MemoryArena(const MemoryArena &);
	void operator=(const MemoryArena &);
	u8 *data_;
	size_t ptr_;
	size_t totalSize_;
};

