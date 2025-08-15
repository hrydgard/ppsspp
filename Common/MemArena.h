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

#include <cstdint>

#ifdef _WIN32
#include "CommonWindows.h"
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

#include "Common/CommonTypes.h"

// This class lets you create a block of anonymous RAM, and then arbitrarily map views into it.
// Multiple views can mirror the same section of the block, which makes it very convient for emulating
// memory mirrors.

struct MemArenaData;

class MemArena {
public:
	size_t roundup(size_t x);
	bool GrabMemSpace(size_t size);
	void ReleaseSpace();
	void *CreateView(s64 offset, size_t size, void *base = 0);
	void ReleaseView(s64 offset, void *view, size_t size);

	// This only finds 1 GB in 32-bit
	u8 *Find4GBBase();
	bool NeedsProbing();

private:
#ifdef _WIN32
	HANDLE hMemoryMapping;
	SYSTEM_INFO sysInfo;
#elif defined(__APPLE__)
	size_t vm_size;
	vm_address_t vm_mem;  // same type as vm_address_t
#else
	int fd = -1;
#endif
};
