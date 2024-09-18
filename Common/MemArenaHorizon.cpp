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
	} else {
		printf("Created the view... base: %p offset: %p size: %p src: %p err: %d\n",
			   (void *)base, (void *)offset, (void *)size, (void *)(memoryCodeBase + offset), rc);
	}

	return base;
}

void MemArena::ReleaseView(s64 offset, void *view, size_t size) {
	if (R_FAILED(svcUnmapProcessMemory(view, envGetOwnProcessHandle(), (u64)(memoryCodeBase + offset), size)))
		printf("Failed to unmap view...\n");
}

u8 *MemArena::Find4GBBase() {
	memorySrcBase = (uintptr_t)memalign(0x1000, 0x10000000);

	if (!memoryBase)
		memoryBase = (uintptr_t)virtmemReserve(0x10000000);

	if (!memoryCodeBase)
		memoryCodeBase = (uintptr_t)virtmemReserve(0x10000000);

	if (R_FAILED(svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)memoryCodeBase, (u64)memorySrcBase, 0x10000000)))
		printf("Failed to map memory...\n");
	if (R_FAILED(svcSetProcessMemoryPermission(envGetOwnProcessHandle(), memoryCodeBase, 0x10000000, Perm_Rx)))
		printf("Failed to set perms...\n");

	return (u8 *)memoryBase;
}

#endif // PPSSPP_PLATFORM(SWITCH)
