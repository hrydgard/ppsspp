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

#include <string>

#include "MemoryUtil.h"
#include "MemArena.h"

#ifdef _WIN32
#include "CommonWindows.h"
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#ifdef ANDROID
#include <sys/ioctl.h>
#include <linux/ashmem.h>
#endif
#endif

#ifdef ANDROID

// Hopefully this ABI will never change...


#define ASHMEM_DEVICE	"/dev/ashmem"

/*
 * ashmem_create_region - creates a new ashmem region and returns the file
 * descriptor, or <0 on error
 *
 * `name' is an optional label to give the region (visible in /proc/pid/maps)
 * `size' is the size of the region, in page-aligned bytes
 */
int ashmem_create_region(const char *name, size_t size)
{
	int fd, ret;

	fd = open(ASHMEM_DEVICE, O_RDWR);
	if (fd < 0)
		return fd;

	if (name) {
		char buf[ASHMEM_NAME_LEN];

		strncpy(buf, name, sizeof(buf));
		ret = ioctl(fd, ASHMEM_SET_NAME, buf);
		if (ret < 0)
			goto error;
	}

	ret = ioctl(fd, ASHMEM_SET_SIZE, size);
	if (ret < 0)
		goto error;

	return fd;

error:
	ERROR_LOG(MEMMAP, "NASTY ASHMEM ERROR: ret = %08x", ret);
	close(fd);
	return ret;
}

int ashmem_set_prot_region(int fd, int prot)
{
	return ioctl(fd, ASHMEM_SET_PROT_MASK, prot);
}

int ashmem_pin_region(int fd, size_t offset, size_t len)
{
	struct ashmem_pin pin = { offset, len };
	return ioctl(fd, ASHMEM_PIN, &pin);
}

int ashmem_unpin_region(int fd, size_t offset, size_t len)
{
	struct ashmem_pin pin = { offset, len };
	return ioctl(fd, ASHMEM_UNPIN, &pin);
}
#endif  // Android

#ifndef _WIN32
// do not make this "static"
#ifdef MAEMO
std::string ram_temp_file = "/home/user/gc_mem.tmp";
#else
std::string ram_temp_file = "/tmp/gc_mem.tmp";
#endif
#elif !defined(_XBOX)
SYSTEM_INFO sysInfo;
#endif


// Windows mappings need to be on 64K boundaries, due to Alpha legacy.
#ifdef _WIN32
size_t MemArena::roundup(size_t x) {
#ifndef _XBOX
	int gran = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
#else
	int gran = 0x10000; // 64k in 360
#endif
	return (x + gran - 1) & ~(gran - 1);
}
#else
size_t MemArena::roundup(size_t x) {
	return x;
}
#endif

void MemArena::GrabLowMemSpace(size_t size)
{
#ifdef _WIN32
#ifndef _XBOX
	hMemoryMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)(size), NULL);
	GetSystemInfo(&sysInfo);
#endif
#elif defined(ANDROID)
	// Use ashmem so we don't have to allocate a file on disk!
	fd = ashmem_create_region("PPSSPP_RAM", size);
	// Note that it appears that ashmem is pinned by default, so no need to pin.
	if (fd < 0)
	{
		ERROR_LOG(MEMMAP, "Failed to grab ashmem space of size: %08x  errno: %d", (int)size, (int)(errno));
		return;
	}
#else
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	fd = open(ram_temp_file.c_str(), O_RDWR | O_CREAT, mode);
	if (fd < 0)
	{
		ERROR_LOG(MEMMAP, "Failed to grab memory space as a file: %s of size: %08x  errno: %d", ram_temp_file.c_str(), (int)size, (int)(errno));
		return;
	}
	// delete immediately, we keep the fd so it still lives
	unlink(ram_temp_file.c_str());
	if (ftruncate(fd, size) != 0)
	{
		ERROR_LOG(MEMMAP, "Failed to ftruncate %d to size %08x", (int)fd, (int)size);
	}
	return;
#endif
}


void MemArena::ReleaseSpace()
{
#ifdef _WIN32
	CloseHandle(hMemoryMapping);
	hMemoryMapping = 0;
#else
	close(fd);
#endif
}


void *MemArena::CreateView(s64 offset, size_t size, void *base)
{
#ifdef _WIN32
#ifdef _XBOX
    size = roundup(size);
	// use 64kb pages
    void * ptr = VirtualAlloc(NULL, size, MEM_COMMIT|MEM_LARGE_PAGES, PAGE_READWRITE);
    return ptr;
#else
	size = roundup(size);
	void *ptr = MapViewOfFileEx(hMemoryMapping, FILE_MAP_ALL_ACCESS, 0, (DWORD)((u64)offset), size, base);
	return ptr;
#endif
#else
	void *retval = mmap(base, size, PROT_READ | PROT_WRITE, MAP_SHARED |
// Do not sync memory to underlying file. Linux has this by default.
#ifdef BLACKBERRY
		MAP_NOSYNCFILE |
#elif defined(__FreeBSD__)
		MAP_NOSYNC |
#endif
		((base == 0) ? 0 : MAP_FIXED), fd, offset);

	if (retval == MAP_FAILED)
	{
		NOTICE_LOG(MEMMAP, "mmap on %s (fd: %d) failed", ram_temp_file.c_str(), (int)fd);
		return 0;
	}
	return retval;
#endif
}


void MemArena::ReleaseView(void* view, size_t size)
{
#ifdef _WIN32
#ifndef _XBOX
	UnmapViewOfFile(view);
#endif
#else
	munmap(view, size);
#endif
}

u8* MemArena::Find4GBBase()
{
#ifdef _M_X64
#ifdef _WIN32
	// 64 bit
	u8* base = (u8*)VirtualAlloc(0, 0xE1000000, MEM_RESERVE, PAGE_READWRITE);
	VirtualFree(base, 0, MEM_RELEASE);
	return base;
#else
	// Very precarious - mmap cannot return an error when trying to map already used pages.
	// This makes the Windows approach above unusable on Linux, so we will simply pray...
	return reinterpret_cast<u8*>(0x2300000000ULL);
#endif

#else // 32 bit

#ifdef _WIN32
	u8* base = (u8*)VirtualAlloc(0, 0x10000000, MEM_RESERVE, PAGE_READWRITE);
	if (base) {
		VirtualFree(base, 0, MEM_RELEASE);
	}
	return base;
#else
	void* base = mmap(0, 0x10000000, PROT_READ | PROT_WRITE,
		MAP_ANON | MAP_SHARED, -1, 0);
	if (base == MAP_FAILED) {
		PanicAlert("Failed to map 256 MB of memory space: %s", strerror(errno));
		return 0;
	}
	munmap(base, 0x10000000);
	return static_cast<u8*>(base);
#endif
#endif
}

