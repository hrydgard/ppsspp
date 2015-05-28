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
#endif



#if defined(_M_X64)
#ifndef _WIN32
#include <unistd.h>
#endif
int hint_location;
#ifdef __APPLE__
#define PAGE_MASK (4096-1)
#elif defined(_WIN32)
static SYSTEM_INFO sys_info;
#define PAGE_MASK (sys_info.dwPageSize - 1)
#else
#define PAGE_MASK     (getpagesize() - 1)
#endif
#define round_page(x) ((((uintptr_t)(x)) + PAGE_MASK) & ~(PAGE_MASK))
#endif

#ifdef __SYMBIAN32__
#include <e32std.h>
#define CODECHUNK_SIZE 1024*1024*20
static RChunk* g_code_chunk = NULL;
static RHeap* g_code_heap = NULL;
static u8* g_next_ptr = NULL;
static u8* g_orig_ptr = NULL;

void ResetExecutableMemory(void* ptr)
{
	// Just reset the ptr to the base
	g_next_ptr = g_orig_ptr;
}
#endif

#if defined(_WIN32) && defined(_M_X64)
static uintptr_t last_addr;
static void *SearchForFreeMem(size_t size)
{
	if (!last_addr)
		last_addr = (uintptr_t) &hint_location - sys_info.dwPageSize;
	last_addr -= size;

	MEMORY_BASIC_INFORMATION info;
	while (VirtualQuery((void *)last_addr, &info, sizeof(info)) == sizeof(info))
	{
		// went too far, unusable for executable memory
		if (last_addr + 0x80000000 < (uintptr_t) &hint_location)
			return NULL;

		uintptr_t end = last_addr + size;
		if (info.State != MEM_FREE)
		{
			last_addr = (uintptr_t) info.AllocationBase - size;
			continue;
		}

		if ((uintptr_t)info.BaseAddress + (uintptr_t)info.RegionSize >= end &&
			(uintptr_t)info.BaseAddress <= last_addr)
			return (void *)last_addr;

		last_addr -= size;
	}

	return NULL;
}
#endif

// This is purposely not a full wrapper for virtualalloc/mmap, but it
// provides exactly the primitive operations that PPSSPP needs.

void* AllocateExecutableMemory(size_t size, bool exec)
{
#if defined(_WIN32)
	void* ptr;
#if defined(_M_X64)
	if (exec && (uintptr_t) &hint_location > 0xFFFFFFFFULL)
	{
		if (!last_addr)
			GetSystemInfo(&sys_info);

		size_t _size = round_page(size);
		ptr = SearchForFreeMem(_size);
		if (ptr)
			ptr = VirtualAlloc(ptr, _size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
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
		g_code_chunk->CreateLocalCode(CODECHUNK_SIZE, CODECHUNK_SIZE + 3*GetPageSize());
		g_code_heap = UserHeap::ChunkHeap(*g_code_chunk, CODECHUNK_SIZE, 1, CODECHUNK_SIZE + 3*GetPageSize());
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
	if (exec && (!map_hint))
	{
		if ((uintptr_t) &hint_location > 0xFFFFFFFFULL)
			map_hint = (char*)round_page(&hint_location) - 0x20000000; // 0.5gb lower than our approximate location
		else
			map_hint = (char*)0x20000000; // 0.5GB mark in memory
	}
	else if (exec && (uintptr_t) map_hint > 0xFFFFFFFFULL)
	{
		map_hint -= round_page(size); /* round down to the next page if we're in high memory */
	}
#endif
	void* ptr = mmap(map_hint, size, PROT_READ | PROT_WRITE	| PROT_EXEC,
		MAP_ANON | MAP_PRIVATE
#if defined(_M_X64) && defined(MAP_32BIT)
		| (exec && (uintptr_t) map_hint == 0 ? MAP_32BIT : 0)
#endif
		, -1, 0);

#endif /* defined(_WIN32) */

	// printf("Mapped executable memory at %p (size %ld)\n", ptr,
	//	(unsigned long)size);

#if !defined(_WIN32) && !defined(__SYMBIAN32__)
	if (ptr == MAP_FAILED)
	{
		ptr = NULL;
#else
	if (ptr == NULL)
	{
#endif
		PanicAlert("Failed to allocate executable memory");
	}
#if defined(_M_X64) && !defined(_WIN32)
	else if (exec && (uintptr_t) map_hint <= 0xFFFFFFFF)
	{
		map_hint += round_page(size); /* round up if we're below 32-bit mark, probably allocating sequentially */
	}
#endif

	return ptr;
}

void* AllocateMemoryPages(size_t size)
{
	size = (size + 4095) & (~4095);
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
#elif defined(__SYMBIAN32__)
	void* ptr = malloc(size);
#else
	void* ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
#endif

	// printf("Mapped memory at %p (size %ld)\n", ptr,
	//	(unsigned long)size);
	if (ptr == NULL)
		PanicAlert("Failed to allocate raw memory");

	return ptr;
}

void* AllocateAlignedMemory(size_t size,size_t alignment)
{
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
	if(posix_memalign(&ptr, alignment, size) != 0)
		ptr = NULL;
#endif
#endif

	// printf("Mapped memory at %p (size %ld)\n", ptr,
	//	(unsigned long)size);

	if (ptr == NULL)
		PanicAlert("Failed to allocate aligned memory");

	return ptr;
}

void FreeMemoryPages(void* ptr, size_t size)
{
	size = (size + 4095) & (~4095);
	if (ptr)
	{
#ifdef _WIN32
	
		if (!VirtualFree(ptr, 0, MEM_RELEASE))
			PanicAlert("FreeMemoryPages failed!\n%s", GetLastErrorMsg());
		ptr = NULL; // Is this our responsibility?
#elif defined(__SYMBIAN32__)
		free(ptr);
#else
		munmap(ptr, size);
#endif
	}
}

void FreeAlignedMemory(void* ptr)
{
	if (ptr)
	{
#ifdef _WIN32
		_aligned_free(ptr);
#else
		free(ptr);
#endif
	}
}

void WriteProtectMemory(void* ptr, size_t size, bool allowExecute)
{
#ifdef _WIN32
	DWORD oldValue;
	if (!VirtualProtect(ptr, size, allowExecute ? PAGE_EXECUTE_READ : PAGE_READONLY, &oldValue))
		PanicAlert("WriteProtectMemory failed!\n%s", GetLastErrorMsg());
#else
	mprotect(ptr, size, allowExecute ? (PROT_READ | PROT_EXEC) : PROT_READ);
#endif
}

void UnWriteProtectMemory(void* ptr, size_t size, bool allowExecute)
{
#ifdef _WIN32
	DWORD oldValue;
	if (!VirtualProtect(ptr, size, allowExecute ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &oldValue))
		PanicAlert("UnWriteProtectMemory failed!\n%s", GetLastErrorMsg());
#else
	mprotect(ptr, size, allowExecute ? (PROT_READ | PROT_WRITE | PROT_EXEC) : PROT_WRITE | PROT_READ);
#endif
}
