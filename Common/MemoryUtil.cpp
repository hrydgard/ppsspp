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

#if !PPSSPP_PLATFORM(SWITCH)
#include <cstring>
#include <cstdlib>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/SysError.h"
#include "Common/Data/Text/Parsers.h"

#ifdef _WIN32
#include "Common/CommonWindows.h"
#else
#include <cerrno>
#include <cstdio>
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
#define ppsspp_round_page(x) ((((uintptr_t)(x)) + MEM_PAGE_MASK) & ~(MEM_PAGE_MASK))

#ifdef _WIN32
// Win32 memory protection flags are odd...
static uint32_t ConvertProtFlagsWin32(uint32_t flags) {
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

static uint32_t ConvertProtFlagsUnix(uint32_t flags) {
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

#if defined(_WIN32) && PPSSPP_ARCH(AMD64)
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

#if PPSSPP_PLATFORM(WINDOWS)

// This is purposely not a full wrapper for virtualalloc/mmap, but it
// provides exactly the primitive operations that PPSSPP needs.
void *AllocateExecutableMemory(size_t size) {
	void *ptr = nullptr;
	DWORD prot = PAGE_EXECUTE_READWRITE;
	if (PlatformIsWXExclusive())
		prot = PAGE_READWRITE;
	if (sys_info.dwPageSize == 0)
		GetSystemInfo(&sys_info);
#if PPSSPP_ARCH(AMD64)
	if ((uintptr_t)&hint_location > 0xFFFFFFFFULL) {
		size_t aligned_size = ppsspp_round_page(size);
#if 1   // Turn off to hunt for RIP bugs on x86-64.
		ptr = SearchForFreeMem(aligned_size);
		if (!ptr) {
			// Let's try again, from the top.
			// When we deallocate, this doesn't change, so we eventually run out of space.
			last_executable_addr = 0;
			ptr = SearchForFreeMem(aligned_size);
		}
#endif
		if (ptr) {
			ptr = VirtualAlloc(ptr, aligned_size, MEM_RESERVE | MEM_COMMIT, prot);
		} else {
			WARN_LOG(Log::Common, "Unable to find nearby executable memory for jit. Proceeding with far memory.");
			// Can still run, thanks to "RipAccessible".
			ptr = VirtualAlloc(nullptr, aligned_size, MEM_RESERVE | MEM_COMMIT, prot);
		}
	}
	else
#endif
	{
#if PPSSPP_PLATFORM(UWP)
		ptr = VirtualAllocFromApp(0, size, MEM_RESERVE | MEM_COMMIT, prot);
#else
		ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, prot);
#endif
	}
	if (!ptr) {
		ERROR_LOG(Log::MemMap, "Failed to allocate executable memory (%d)", (int)size);
	}
	return ptr;
}

#else  // Non-Windows platforms

void *AllocateExecutableMemory(size_t size) {
	static char *map_hint = nullptr;

#if PPSSPP_ARCH(AMD64)
	// Try to request one that is close to our memory location if we're in high memory.
	// We use a dummy global variable to give us a good location to start from.
	// TODO: Should we also do this for ARM64?
	if (!map_hint) {
		if ((uintptr_t) &hint_location > 0xFFFFFFFFULL)
			map_hint = (char*)ppsspp_round_page(&hint_location) - 0x20000000; // 0.5gb lower than our approximate location
		else
			map_hint = (char*)0x20000000; // 0.5GB mark in memory
	} else if ((uintptr_t) map_hint > 0xFFFFFFFFULL) {
		map_hint -= ppsspp_round_page(size); /* round down to the next page if we're in high memory */
	}
#endif

	int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
	if (PlatformIsWXExclusive())
		prot = PROT_READ | PROT_WRITE;  // POST_EXEC is added later in this case.

	void* ptr = mmap(map_hint, size, prot, MAP_ANON | MAP_PRIVATE, -1, 0);

	if (ptr == MAP_FAILED) {
		ptr = nullptr;
		ERROR_LOG(Log::MemMap, "Failed to allocate executable memory (%d) errno=%d", (int)size, errno);
	}

#if PPSSPP_ARCH(AMD64)
	else if ((uintptr_t)map_hint <= 0xFFFFFFFF) {
		// Round up if we're below 32-bit mark, probably allocating sequentially.
		map_hint += ppsspp_round_page(size);

		// If we moved ahead too far, skip backwards and recalculate.
		// When we free, we keep moving forward and eventually move too far.
		if ((uintptr_t)map_hint - (uintptr_t) &hint_location >= 0x70000000) {
			map_hint = 0;
		}
	}
#endif

	return ptr;
}

#endif  // non-windows

void *AllocateMemoryPages(size_t size, uint32_t memProtFlags) {
#ifdef _WIN32
	if (sys_info.dwPageSize == 0)
		GetSystemInfo(&sys_info);
	uint32_t protect = ConvertProtFlagsWin32(memProtFlags);
	// Make sure to do this after GetSystemInfo().
	size = ppsspp_round_page(size);
#if PPSSPP_PLATFORM(UWP)
	void* ptr = VirtualAllocFromApp(0, size, MEM_COMMIT, protect);
#else
	void* ptr = VirtualAlloc(0, size, MEM_COMMIT, protect);
#endif
	if (!ptr) {
		ERROR_LOG(Log::MemMap, "Failed to allocate raw memory pages");
		return nullptr;
	}
#else
	size = ppsspp_round_page(size);
	uint32_t protect = ConvertProtFlagsUnix(memProtFlags);
	void *ptr = mmap(0, size, protect, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (ptr == MAP_FAILED) {
		ERROR_LOG(Log::MemMap, "Failed to allocate raw memory pages: errno=%d", errno);
		return nullptr;
	}
#endif

	// printf("Mapped memory at %p (size %ld)\n", ptr,
	//	(unsigned long)size);
	return ptr;
}

void *AllocateAlignedMemory(size_t size, size_t alignment) {
#ifdef _WIN32
	void* ptr = _aligned_malloc(size, alignment);
#else
	void* ptr = NULL;
#ifdef __ANDROID__
	ptr = memalign(alignment, size);
#else
	if (posix_memalign(&ptr, alignment, size) != 0) {
		ptr = nullptr;
	}
#endif
#endif
	return ptr;
}

void FreeMemoryPages(void *ptr, size_t size) {
	if (!ptr)
		return;
	uintptr_t page_size = GetMemoryProtectPageSize();
	size = (size + page_size - 1) & (~(page_size - 1));
#ifdef _WIN32
	if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
		ERROR_LOG(Log::MemMap, "FreeMemoryPages failed!\n%s", GetLastErrorMsg().c_str());
	}
#else
	munmap(ptr, size);
#endif
}

void FreeExecutableMemory(void *ptr, size_t size) {
	FreeMemoryPages(ptr, size);
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
	// Needed on platforms that disable W^X pages for security. Even without block linking, still should be much faster than IR JIT.
	// This might also come in useful for UWP (Universal Windows Platform) if I'm understanding things correctly.
#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(UWP) || defined(__OpenBSD__)
	return true;
#elif PPSSPP_PLATFORM(MAC) && PPSSPP_ARCH(ARM64)
	return true;
#else
	// Returning true here lets you test the W^X path on Windows and other non-W^X platforms.
	return false;
#endif
}

bool ProtectMemoryPages(const void* ptr, size_t size, uint32_t memProtFlags) {
	VERBOSE_LOG(Log::JIT, "ProtectMemoryPages: %p (%d) : r%d w%d x%d", ptr, (int)size,
			(memProtFlags & MEM_PROT_READ) != 0, (memProtFlags & MEM_PROT_WRITE) != 0, (memProtFlags & MEM_PROT_EXEC) != 0);

	if (PlatformIsWXExclusive()) {
		if ((memProtFlags & (MEM_PROT_WRITE | MEM_PROT_EXEC)) == (MEM_PROT_WRITE | MEM_PROT_EXEC)) {
			_assert_msg_(false, "Bad memory protect flags %d: W^X is in effect, can't both write and exec", memProtFlags);
		}
	}
	// Note - VirtualProtect will affect the full pages containing the requested range.
	// mprotect does not seem to, at least not on Android unless I made a mistake somewhere, so we manually round.
#ifdef _WIN32
	uint32_t protect = ConvertProtFlagsWin32(memProtFlags);

#if PPSSPP_PLATFORM(UWP)
	DWORD oldValue;
	if (!VirtualProtectFromApp((void *)ptr, size, protect, &oldValue)) {
		ERROR_LOG(Log::MemMap, "WriteProtectMemory failed!\n%s", GetLastErrorMsg().c_str());
		return false;
	}
#else
	DWORD oldValue;
	if (!VirtualProtect((void *)ptr, size, protect, &oldValue)) {
		ERROR_LOG(Log::MemMap, "WriteProtectMemory failed!\n%s", GetLastErrorMsg().c_str());
		return false;
	}
#endif
	return true;
#else
	uint32_t protect = ConvertProtFlagsUnix(memProtFlags);
	uintptr_t page_size = GetMemoryProtectPageSize();

	uintptr_t start = (uintptr_t)ptr;
	uintptr_t end = (uintptr_t)ptr + size;
	start &= ~(page_size - 1);
	end = (end + page_size - 1) & ~(page_size - 1);
	int retval = mprotect((void *)start, end - start, protect);
	if (retval != 0) {
		ERROR_LOG(Log::MemMap, "mprotect failed (%p)! errno=%d (%s)", (void *)start, errno, strerror(errno));
		return false;
	}
	return true;
#endif
}

int GetMemoryProtectPageSize() {
#ifdef _WIN32
	if (sys_info.dwPageSize == 0)
		GetSystemInfo(&sys_info);
	return sys_info.dwPageSize;
#else
	static int pageSize = 0;
	if (!pageSize) {
		pageSize = sysconf(_SC_PAGE_SIZE);
	}
	return pageSize;
#endif
}
#endif // !PPSSPP_PLATFORM(SWITCH)
