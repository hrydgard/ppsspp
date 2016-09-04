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

#include "Common.h"
#include "MemoryUtil.h"
#include "StringUtils.h"

#ifdef _WIN32
#include "CommonWindows.h"
#else
#include <errno.h>
#include <stdio.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/mman.h>
#include <mach/vm_param.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif
static int hint_location;
#ifdef __APPLE__
#define MEM_PAGE_SIZE (PAGE_SIZE)
#elif defined(_WIN32)
static SYSTEM_INFO sys_info;
#define MEM_PAGE_SIZE (uintptr_t)(sys_info.dwPageSize)
#else
#define MEM_PAGE_SIZE (getpagesize())
#endif

#define MEM_PAGE_MASK ((MEM_PAGE_SIZE)-1)
#define round_page(x) ((((uintptr_t)(x)) + MEM_PAGE_MASK) & ~(MEM_PAGE_MASK))

#ifdef __SYMBIAN32__
#include <e32std.h>
#define CODECHUNK_SIZE 1024*1024*20
static RChunk* g_code_chunk = NULL;
static RHeap* g_code_heap = NULL;
static u8* g_next_ptr = NULL;
static u8* g_orig_ptr = NULL;

void ResetExecutableMemory(void* ptr) {
	// Just reset the ptr to the base
	g_next_ptr = g_orig_ptr;
}
#endif

#ifdef _WIN32
// Win32 flags are odd...
uint32_t ConvertProtFlagsWin32(uint32_t flags) {
	uint32_t protect = 0;
	switch (flags) {
	case 0: protect = PAGE_NOACCESS; break;
	case MEM_PROT_READ: protect = PAGE_READONLY; break;
	case MEM_PROT_WRITE: protect = PAGE_READWRITE; break;   // Can't set write-only
	case MEM_PROT_EXEC: protect = PAGE_EXECUTE; break;
	case MEM_PROT_READ | MEM_PROT_EXEC: protect = PAGE_EXECUTE_READ; break;
	case MEM_PROT_WRITE | MEM_PROT_EXEC: protect = PAGE_EXECUTE_READWRITE; break;  // Can't set write-only
	case MEM_PROT_READ | MEM_PROT_WRITE: protect = PAGE_READWRITE; break;
	case MEM_PROT_READ | MEM_PROT_WRITE | MEM_PROT_EXEC: protect = PAGE_EXECUTE_READWRITE; break;
	}
	return protect;
}

#else

uint32_t ConvertProtFlagsUnix(uint32_t flags) {
	uint32_t protect = 0;
	if (flags & MEM_PROT_READ)
		protect |= PROT_READ;
	if (flags & MEM_PROT_WRITE)
		protect |= PROT_WRITE;
	if (flags & MEM_PROT_EXEC)
		protect |= PROT_EXEC;
	return protect;
}

#endif

#if defined(_WIN32) && defined(_M_X64)
static uintptr_t last_executable_addr;
static void *SearchForFreeMem(size_t size) {
	if (!last_executable_addr)
		last_executable_addr = (uintptr_t) &hint_location - sys_info.dwPageSize;
	last_executable_addr -= size;

	MEMORY_BASIC_INFORMATION info;
	while (VirtualQuery((void *)last_executable_addr, &info, sizeof(info)) == sizeof(info)) {
		// went too far, unusable for executable memory
		if (last_executable_addr + 0x80000000 < (uintptr_t) &hint_location)
			return NULL;

		uintptr_t end = last_executable_addr + size;
		if (info.State != MEM_FREE)
		{
			last_executable_addr = (uintptr_t) info.AllocationBase - size;
			continue;
		}

		if ((uintptr_t)info.BaseAddress + (uintptr_t)info.RegionSize >= end &&
			(uintptr_t)info.BaseAddress <= last_executable_addr)
			return (void *)last_executable_addr;

		last_executable_addr -= size;
	}

	return NULL;
}
#endif

// This is purposely not a full wrapper for virtualalloc/mmap, but it
// provides exactly the primitive operations that PPSSPP needs.

void *AllocateExecutableMemory(size_t size) {
#if defined(_WIN32)
	void *ptr;
#if defined(_M_X64)
	if ((uintptr_t)&hint_location > 0xFFFFFFFFULL) {
		if (sys_info.dwPageSize == 0)
			GetSystemInfo(&sys_info);

		size_t aligned_size = round_page(size);
		ptr = SearchForFreeMem(aligned_size);
		if (!ptr) {
			// Let's try again, from the top.
			// When we deallocate, this doesn't change, so we eventually run out of space.
			last_executable_addr = 0;
			ptr = SearchForFreeMem(aligned_size);
		}
		if (ptr) {
			ptr = VirtualAlloc(ptr, aligned_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		} else {
			ERROR_LOG(COMMON, "Unable to find nearby executable memory for jit");
		}
	}
	else
#endif
		ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#elif defined(__SYMBIAN32__)
	//This function may be called more than once, and we want to create only one big
	//memory chunk for all the executable code for the JIT
	if( g_code_chunk == NULL && g_code_heap == NULL)
	{
		g_code_chunk = new RChunk();
		g_code_chunk->CreateLocalCode(CODECHUNK_SIZE, CODECHUNK_SIZE + 3*GetMemoryProtectPageSize());
		g_code_heap = UserHeap::ChunkHeap(*g_code_chunk, CODECHUNK_SIZE, 1, CODECHUNK_SIZE + 3*GetMemoryProtectPageSize());
		g_next_ptr = reinterpret_cast<u8*>(g_code_heap->AllocZ(CODECHUNK_SIZE));
		g_orig_ptr = g_next_ptr;
	}
	void* ptr = (void*)g_next_ptr;
	g_next_ptr += size;
#else
	static char *map_hint = 0;
#if defined(_M_X64)
	// Try to request one that is close to our memory location if we're in high memory.
	// We use a dummy global variable to give us a good location to start from.
	if (!map_hint) {
		if ((uintptr_t) &hint_location > 0xFFFFFFFFULL)
			map_hint = (char*)round_page(&hint_location) - 0x20000000; // 0.5gb lower than our approximate location
		else
			map_hint = (char*)0x20000000; // 0.5GB mark in memory
	}
	else if ((uintptr_t) map_hint > 0xFFFFFFFFULL)
	{
		map_hint -= round_page(size); /* round down to the next page if we're in high memory */
	}
#endif
	void* ptr = mmap(map_hint, size, PROT_READ | PROT_WRITE	| PROT_EXEC,
		MAP_ANON | MAP_PRIVATE
#if defined(_M_X64) && defined(MAP_32BIT)
		| ((uintptr_t) map_hint == 0 ? MAP_32BIT : 0)
#endif
		, -1, 0);

#endif /* defined(_WIN32) */

#if !defined(_WIN32) && !defined(__SYMBIAN32__)
	static const void *failed_result = MAP_FAILED;
#else
	static const void *failed_result = nullptr;
#endif

	if (ptr == failed_result) {
		ptr = nullptr;
		PanicAlert("Failed to allocate executable memory\n%s", GetLastErrorMsg());
	}
#if defined(_M_X64) && !defined(_WIN32)
	else if ((uintptr_t)map_hint <= 0xFFFFFFFF) {
		// Round up if we're below 32-bit mark, probably allocating sequentially.
		map_hint += round_page(size);

		// If we moved ahead too far, skip backwards and recalculate.
		// When we free, we keep moving forward and eventually move too far.
		if ((uintptr_t)map_hint - (uintptr_t) &hint_location >= 0x70000000) {
			map_hint = 0;
		}
	}
#endif

	return ptr;
}

void *AllocateMemoryPages(size_t size, uint32_t memProtFlags) {
	size = (size + 4095) & (~4095);
#ifdef _WIN32
	uint32_t protect = ConvertProtFlagsWin32(memProtFlags);
	void* ptr = VirtualAlloc(0, size, MEM_COMMIT, protect);
#elif defined(__SYMBIAN32__)
	void* ptr = malloc(size);
#else
	uint32_t protect = ConvertProtFlagsUnix(memProtFlags);
	void* ptr = mmap(0, size, protect, MAP_ANON | MAP_PRIVATE, -1, 0);
#endif

	// printf("Mapped memory at %p (size %ld)\n", ptr,
	//	(unsigned long)size);
	if (ptr == NULL)
		PanicAlert("Failed to allocate raw memory");
	return ptr;
}

void *AllocateAlignedMemory(size_t size, size_t alignment) {
#ifdef _WIN32
	void* ptr =  _aligned_malloc(size,alignment);
#else
	void* ptr = NULL;
#ifdef ANDROID
	ptr = memalign(alignment, size);
#elif defined(__SYMBIAN32__)
	// On Symbian, alignment won't matter as NEON isn't supported.
	ptr = malloc(size);
#else
	if (posix_memalign(&ptr, alignment, size) != 0)
		ptr = NULL;
#endif
#endif

	// printf("Mapped memory at %p (size %ld)\n", ptr,
	//	(unsigned long)size);

	if (ptr == NULL)
		PanicAlert("Failed to allocate aligned memory");

	return ptr;
}

void FreeMemoryPages(void *ptr, size_t size) {
	if (!ptr)
		return;
	size = (size + 4095) & (~4095);
#ifdef _WIN32
	if (!VirtualFree(ptr, 0, MEM_RELEASE))
		PanicAlert("FreeMemoryPages failed!\n%s", GetLastErrorMsg());
#elif defined(__SYMBIAN32__)
	free(ptr);
#else
	munmap(ptr, size);
#endif
}

void FreeAlignedMemory(void* ptr) {
	if (!ptr)
		return;
#ifdef _WIN32
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

bool PlatformIsWXExclusive() {
	// Only iOS really needs this mode currently. Even without block linking, still should be much faster than IR JIT.
	// This might also come in useful for UWP (Universal Windows Platform) if I'm understanding things correctly.
#ifdef IOS
	return true;
#else
	// Returning true here lets you test the W^X path on Windows and other non-W^X platforms.
	return false;
#endif
}

void ProtectMemoryPages(const void* ptr, size_t size, uint32_t memProtFlags) {
	VERBOSE_LOG(JIT, "ProtectMemoryPages: %p (%d) : r%d w%d x%d", ptr, (int)size, (memProtFlags & MEM_PROT_READ) != 0, (memProtFlags & MEM_PROT_WRITE) != 0, (memProtFlags & MEM_PROT_EXEC) != 0);

	if (PlatformIsWXExclusive()) {
		if ((memProtFlags & (MEM_PROT_WRITE | MEM_PROT_EXEC)) == (MEM_PROT_WRITE | MEM_PROT_EXEC))
			PanicAlert("Bad memory protect : W^X is in effect, can't both write and exec");
	}
	// Note - VirtualProtect will affect the full pages containing the requested range.
	// mprotect does not seem to, at least not on Android unless I made a mistake somewhere, so we manually round.
#ifdef _WIN32
	uint32_t protect = ConvertProtFlagsWin32(memProtFlags);
	DWORD oldValue;
	if (!VirtualProtect((void *)ptr, size, protect, &oldValue))
		PanicAlert("WriteProtectMemory failed!\n%s", GetLastErrorMsg());
#elif defined(__SYMBIAN32__)
	// Do nothing
#else
	uint32_t protect = ConvertProtFlagsUnix(memProtFlags);
	uint32_t page_size = GetMemoryProtectPageSize();

	uintptr_t start = (uintptr_t)ptr;
	uintptr_t end = (uintptr_t)ptr + size;
	start &= ~(page_size - 1);
	end = (end + page_size - 1) & ~(page_size - 1);
	mprotect((void *)start, end - start, protect);
#endif
}

int GetMemoryProtectPageSize() {
#ifdef _WIN32
	if (sys_info.dwPageSize == 0)
		GetSystemInfo(&sys_info);
	return sys_info.dwPageSize;
#endif
	return MEM_PAGE_SIZE;
}
