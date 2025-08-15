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

#include "ppsspp_config.h"

#ifdef _WIN32

#include "MemArena.h"
#include "CommonWindows.h"

// Windows mappings need to be on 64K boundaries, due to Alpha legacy.
size_t MemArena::roundup(size_t x) {
	int gran = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
	return (x + gran - 1) & ~(gran - 1);
}

bool MemArena::GrabMemSpace(size_t size) {
#if !PPSSPP_PLATFORM(UWP)
	hMemoryMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)(size), NULL);
	GetSystemInfo(&sysInfo);
#else
	hMemoryMapping = 0;
#endif
	return true;
}

void MemArena::ReleaseSpace() {
	CloseHandle(hMemoryMapping);
	hMemoryMapping = 0;
}

void *MemArena::CreateView(s64 offset, size_t size, void *viewbase) {
	size = roundup(size);
#if PPSSPP_PLATFORM(UWP)
	// We just grabbed some RAM before using RESERVE. This commits it.
	void *ptr = VirtualAllocFromApp(viewbase, size, MEM_COMMIT, PAGE_READWRITE);
#else
	void *ptr = MapViewOfFileEx(hMemoryMapping, FILE_MAP_ALL_ACCESS, 0, (DWORD)((u64)offset), size, viewbase);
#endif
	return ptr;
}

void MemArena::ReleaseView(s64 offset, void* view, size_t size) {
#if PPSSPP_PLATFORM(UWP)
#else
	UnmapViewOfFile(view);
#endif
}

bool MemArena::NeedsProbing() {
#if PPSSPP_ARCH(32BIT)
	return true;
#else
	return false;
#endif
}

u8* MemArena::Find4GBBase() {
	// Now, create views in high memory where there's plenty of space.
#if PPSSPP_ARCH(32BIT)
	// Caller will need to find one in a different way.
	return nullptr;

#elif PPSSPP_ARCH(64BIT)
	u8 *base = (u8*)VirtualAlloc(0, 0xE1000000, MEM_RESERVE, PAGE_READWRITE);
	if (base) {
		VirtualFree(base, 0, MEM_RELEASE);
	}
	return base;
#else
#error Arch not supported
#endif
}

#endif // _WIN32
