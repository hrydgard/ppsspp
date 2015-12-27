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
#include <sys/mman.h>
#endif
#include <stdint.h>

#ifdef BLACKBERRY
using std::size_t;
#endif

void* AllocateExecutableMemory(size_t size, bool exec = true);
void* AllocateMemoryPages(size_t size);
void FreeMemoryPages(void* ptr, size_t size);
void* AllocateAlignedMemory(size_t size,size_t alignment);
void FreeAlignedMemory(void* ptr);
void WriteProtectMemory(void* ptr, size_t size, bool executable = false);
void UnWriteProtectMemory(void* ptr, size_t size, bool allowExecute = false);
#ifdef __SYMBIAN32__
void ResetExecutableMemory(void* ptr);
#endif

inline int GetPageSize() { return 4096; }

#ifdef UWPAPP
void MakeExecutable(void* ptr, size_t size);
void MakeModifiable(void* ptr, size_t size);

#define EXEC_BLOCK(ptr, size) MakeExecutable( (void*)ptr, size );
#define MODF_BLOCK(ptr, size) MakeModifiable( (void*)ptr, size );
#else
#define EXEC_BLOCK(ptr, size)
#define MODF_BLOCK(ptr, size)
#endif

struct MemoryAccess
{
  static volatile int accc;

  template< typename T >
  MemoryAccess( T ptr, size_t size )
    : ptr( (void*)ptr )
    , size( size )
  {
    MODF_BLOCK( this->ptr, size );
    accc++;
  }
  ~MemoryAccess()
  {
    EXEC_BLOCK( ptr, size );
    accc--;
  }
  void* ptr;
  size_t size;
};

template <typename T>
class SimpleBuf {
public:
	SimpleBuf() : buf_(0), size_(0) {
	}

	SimpleBuf(size_t size) : buf_(0) {
		resize(size);
	}

	~SimpleBuf() {
		if (buf_ != 0) {
			FreeMemoryPages(buf_, size_ * sizeof(T));
		}
	}

	inline T &operator[](size_t index) {
		return buf_[index];
	}

	// Doesn't preserve contents.
	void resize(size_t size) {
		if (size_ < size) {
			if (buf_ != 0) {
				FreeMemoryPages(buf_, size_ * sizeof(T));
			}
			buf_ = (T *)AllocateMemoryPages(size * sizeof(T));
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
