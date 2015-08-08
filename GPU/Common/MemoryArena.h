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
#ifdef _DEBUG
		if (!HasRoomFor(sizeof(T))) {
			fprintf(stderr, "MemoryArena full! Failed to allocate object of size %d", (int)sizeof(T));
		}
#endif
		*ptr = reinterpret_cast<T *>(data_ + ptr_);
		ptr_ += sizeof(T);
		return *ptr;
	}

	u8 *AllocateBytes(size_t bytes) {
#ifdef _DEBUG
		if (!HasRoomFor(bytes)) {
			fprintf(stderr, "MemoryArena full, failed to allocate %d bytes!", (int)bytes);
		}
#endif
		u8 *ptr = data_ + ptr_;
		ptr_ += bytes;
		return ptr;
	}

	u8 *AllocateAligned(size_t bytes, int alignment) {
		ptr_ = (ptr_ + alignment - 1) & ~(alignment - 1);
		return AllocateBytes(bytes);
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

