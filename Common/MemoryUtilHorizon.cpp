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

#if PPSSPP_PLATFORM(SWITCH)
#include <cstring>
#include <cstdlib>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/SysError.h"

#include <errno.h>
#include <stdio.h>

#include <malloc.h> // memalign

#define MEM_PAGE_SIZE (0x1000)
#define MEM_PAGE_MASK ((MEM_PAGE_SIZE)-1)
#define ppsspp_round_page(x) ((((uintptr_t)(x)) + MEM_PAGE_MASK) & ~(MEM_PAGE_MASK))

// On Switch we dont support allocating executable memory here
// See CodeBlock.h
void *AllocateExecutableMemory(size_t size) {
	return nullptr;
}

void *AllocateMemoryPages(size_t size, uint32_t memProtFlags) {
	void* ptr = nullptr;
	size = ppsspp_round_page(size);
	ptr = memalign(MEM_PAGE_SIZE, size);
	return ptr;
}

void *AllocateAlignedMemory(size_t size, size_t alignment) {
	void* ptr = memalign(alignment, size);

	_assert_msg_(ptr != nullptr, "Failed to allocate aligned memory of size %lu", size);
	return ptr;
}

void FreeMemoryPages(void *ptr, size_t size) {
	if (!ptr)
		return;

	free(ptr);
	return;
}

void FreeExecutableMemory(void *ptr, size_t size) {
	return; // Not supported on Switch
}

void FreeAlignedMemory(void* ptr) {
	if (!ptr)
		return;
		
	free(ptr);
}

bool PlatformIsWXExclusive() {
	return false; // Switch technically is W^X but we use dual mappings instead of reprotecting the pages to allow a W and X mapping
}

bool ProtectMemoryPages(const void* ptr, size_t size, uint32_t memProtFlags) {
	return true;
}

int GetMemoryProtectPageSize() {
	return MEM_PAGE_SIZE;
}
#endif // PPSSPP_PLATFORM(SWITCH)
