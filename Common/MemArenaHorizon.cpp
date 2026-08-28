// Copyright (C) 2023 M4xw

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

#include "ppsspp_config.h"
#if PPSSPP_PLATFORM(SWITCH)

#include <stdio.h>
#include <malloc.h> // memalign
#include <switch.h>

#include "Common/MemArena.h"

static uintptr_t memoryBase = 0;
static uintptr_t memoryCodeBase = 0;
static uintptr_t memorySrcBase = 0;
// Held for the lifetime of the process: the two base addresses are looked up
// once and then kept in the statics above across ReleaseSpace()/Find4GBBase()
// cycles, so the reservations that keep libnx from handing the same address
// space to anyone else have to live exactly as long.
static VirtmemReservation *memoryBaseReservation = nullptr;
static VirtmemReservation *memoryCodeBaseReservation = nullptr;

size_t MemArena::roundup(size_t x) {
	return x;
}

bool MemArena::NeedsProbing() {
	return false;
}

bool MemArena::GrabMemSpace(size_t size) {
	return true;
}

void MemArena::ReleaseSpace() {
	if (R_FAILED(svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)memoryCodeBase, (u64)memorySrcBase, 0x10000000)))
		printf("Failed to release view space...\n");

	free((void *)memorySrcBase);
	memorySrcBase = 0;
}

void *MemArena::CreateView(s64 offset, size_t size, void *base) {
	Result rc = svcMapProcessMemory(base, envGetOwnProcessHandle(), (u64)(memoryCodeBase + offset), size);
	if (R_FAILED(rc)) {
		printf("Fatal error creating the view... base: %p offset: %p size: %p src: %p err: %d\n",
			   (void *)base, (void *)offset, (void *)size, (void *)(memoryCodeBase + offset), rc);
		// Returning base here reports success, so the caller happily uses an unmapped address
		// and we take a fault later with nothing pointing back at this.
		return nullptr;
	}

	printf("Created the view... base: %p offset: %p size: %p src: %p err: %d\n",
		   (void *)base, (void *)offset, (void *)size, (void *)(memoryCodeBase + offset), rc);
	return base;
}

void MemArena::ReleaseView(s64 offset, void *view, size_t size) {
	if (R_FAILED(svcUnmapProcessMemory(view, envGetOwnProcessHandle(), (u64)(memoryCodeBase + offset), size)))
		printf("Failed to unmap view...\n");
}

u8 *MemArena::Find4GBBase() {
	memorySrcBase = (uintptr_t)memalign(0x1000, 0x10000000);

	// The virtmemFind* calls only locate free address space; unlike the
	// virtmemReserve() they replaced, they do not claim it. libnx wants the
	// manager mutex held while looking, and a reservation to stand in for the
	// mapping wherever the range is not mapped on the spot - without one, the
	// next caller can be handed a slice that overlaps this one.
	if (!memoryBase || !memoryCodeBase) {
		virtmemLock();

		if (!memoryBase) {
			memoryBase = (uintptr_t)virtmemFindAslr(0x10000000, 0x1000);
			// Nothing maps this range here - CreateView() maps the individual
			// views into it later - so the reservation is all that holds it.
			if (memoryBase)
				memoryBaseReservation = virtmemAddReservation((void *)memoryBase, 0x10000000);
		}

		if (!memoryCodeBase) {
			// This one goes to svcMapProcessCodeMemory(), which wants an
			// address out of the code memory region rather than general
			// purpose address space.
			memoryCodeBase = (uintptr_t)virtmemFindCodeMemory(0x10000000, 0x1000);
			// ReleaseSpace() unmaps this again while the address stays cached
			// for the next Find4GBBase(), so it needs holding across that gap
			// as well.
			if (memoryCodeBase)
				memoryCodeBaseReservation = virtmemAddReservation((void *)memoryCodeBase, 0x10000000);
		}

		virtmemUnlock();
	}

	if (!memorySrcBase || !memoryBase || !memoryCodeBase || !memoryBaseReservation || !memoryCodeBaseReservation) {
		printf("Failed to reserve the address space... src: %p base: %p code: %p\n",
			   (void *)memorySrcBase, (void *)memoryBase, (void *)memoryCodeBase);
		return nullptr;
	}

	if (R_FAILED(svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)memoryCodeBase, (u64)memorySrcBase, 0x10000000)))
		printf("Failed to map memory...\n");
	if (R_FAILED(svcSetProcessMemoryPermission(envGetOwnProcessHandle(), memoryCodeBase, 0x10000000, Perm_Rx)))
		printf("Failed to set perms...\n");

	return (u8 *)memoryBase;
}

#endif // PPSSPP_PLATFORM(SWITCH)
