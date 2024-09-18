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

#ifndef _WIN32
#ifndef __SWITCH__
#include <sys/mman.h>
#else
#include <switch.h>
#endif // !__SWITCH__
#endif
#include <stdint.h>

// Returns true if we need to avoid setting both writable and executable at the same time (W^X)
bool PlatformIsWXExclusive();

#define MEM_PROT_READ  1
#define MEM_PROT_WRITE 2
#define MEM_PROT_EXEC  4

// Note that some platforms go through special contortions to allocate executable memory. So for memory
// that's intended for execution, allocate it first using AllocateExecutableMemory, then modify protection as desired.
// AllocateMemoryPages is simpler and more generic.
// Note that on W^X platforms, this will return writable memory that can later be changed to executable!
void* AllocateExecutableMemory(size_t size);
void FreeExecutableMemory(void *ptr, size_t size);

void* AllocateMemoryPages(size_t size, uint32_t memProtFlags);
// Note that on platforms returning PlatformIsWXExclusive, you cannot set a page to be both readable and writable at the same time.
bool ProtectMemoryPages(const void* ptr, size_t size, uint32_t memProtFlags);
void FreeMemoryPages(void* ptr, size_t size);

// Regular aligned memory. Don't try to apply memory protection willy-nilly to memory allocated this way as in-page alignment is unknown (though could be checked).
void* AllocateAlignedMemory(size_t size, size_t alignment);
void FreeAlignedMemory(void* ptr);

int GetMemoryProtectPageSize();

// A buffer that uses aligned memory. Can be useful for image processing.
template <typename T, size_t A>
class AlignedVector {
public:
	AlignedVector() : buf_(0), size_(0) {}

	AlignedVector(size_t size) : buf_(0) {
		resize(size);
	}

	AlignedVector(const AlignedVector &o) : buf_(o.buf_), size_(o.size_) {}

	// Move constructor
	AlignedVector(AlignedVector &&o) noexcept : buf_(o.buf_), size_(o.size_) { o.buf_ = nullptr; o.size_ = 0; }

	~AlignedVector() {
		if (buf_ != 0) {
			FreeAlignedMemory(buf_);
		}
	}

	inline T &operator[](size_t index) {
		return buf_[index];
	}

	// Doesn't preserve contents.
	void resize(size_t size) {
		if (size_ < size) {
			if (buf_ != 0) {
				FreeAlignedMemory(buf_);
			}
			buf_ = (T *)AllocateAlignedMemory(size * sizeof(T), A);
			size_ = size;
		}
	}

	T *data() {
		return buf_;
	}

	size_t size() const {
		return size_;
	}

private:
	T *buf_;
	size_t size_;
};
