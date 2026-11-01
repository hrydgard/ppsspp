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
#if defined(__APPLE__)
	// Asks the kernel for a free range of this size for the views, and keeps it reserved (inaccessible), so
	// CreateView can map over it without anything else taking it in between. Any arena's views can go in
	// it, and there can be a few at once (one per address space).
	// alignMask: the address bits that must be zero.
	static u8 *ReserveViewSpace(size_t size, size_t alignMask);
	static void ReleaseViewSpace(u8 *base);
#endif

private:
#ifdef _WIN32
	HANDLE hMemoryMapping;
	SYSTEM_INFO sysInfo;
#elif defined(__APPLE__)
	size_t vm_size;
	vm_address_t vm_mem;  // same type as vm_address_t

	struct Reservation {
		vm_address_t addr = 0;
		size_t size = 0;
	};
	static constexpr int MAX_RESERVATIONS = 4;
	static Reservation reservations_[MAX_RESERVATIONS];
	static bool InReservation(vm_address_t addr, size_t size);
#else
	int fd = -1;
	// Where Find4GBBase keeps the view range reserved (inaccessible) until ReleaseSpace, so nothing else
	// can be mapped between the views.
	void *reserved_mem = nullptr;
	size_t reserved_size = 0;
#endif
};
