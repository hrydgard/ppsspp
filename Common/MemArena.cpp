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

#include "MemoryUtil.h"
#include "MemArena.h"

#ifdef _WIN32
#include <windows.h>
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
#if defined(MAEMO) || defined(MEEGO_EDITION_HARMATTAN)
std::string ram_temp_file = "/home/user/gc_mem.tmp";
#else
std::string ram_temp_file = "/tmp/gc_mem.tmp";
#endif
#else
SYSTEM_INFO sysInfo;
#endif


// Windows mappings need to be on 64K boundaries, due to Alpha legacy.
#ifdef _WIN32
size_t roundup(size_t x) {
	int gran = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
	return (x + gran - 1) & ~(gran - 1);
}
#else
size_t roundup(size_t x) {
	return x;
}
#endif


void MemArena::GrabLowMemSpace(size_t size)
{
#ifdef _WIN32
	hMemoryMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)(size), NULL);
	GetSystemInfo(&sysInfo);
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
#elif defined(__SYMBIAN32__)
	memmap->Close();
	delete memmap;
#else
	close(fd);
#endif
}


void *MemArena::CreateView(s64 offset, size_t size, void *base)
{
#ifdef _WIN32
	size = roundup(size);
	void *ptr = MapViewOfFileEx(hMemoryMapping, FILE_MAP_ALL_ACCESS, 0, (DWORD)((u64)offset), size, base);
	return ptr;
#else
	void *retval = mmap(base, size, PROT_READ | PROT_WRITE, MAP_SHARED |
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
	UnmapViewOfFile(view);
#elif defined(__SYMBIAN32__)
	memmap->Decommit(((int)view - (int)memmap->Base()) & 0x3FFFFFFF, size);
#else
	munmap(view, size);
#endif
}

#ifndef __SYMBIAN32__
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
	// The highest thing in any 1GB section of memory space is the locked cache. We only need to fit it.
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
#endif


// yeah, this could also be done in like two bitwise ops...
#define SKIP(a_flags, b_flags) 
//	if (!(a_flags & MV_WII_ONLY) && (b_flags & MV_WII_ONLY)) 
//		continue; 
//	if (!(a_flags & MV_FAKE_VMEM) && (b_flags & MV_FAKE_VMEM)) 
//		continue; 



static bool Memory_TryBase(u8 *base, const MemoryView *views, int num_views, u32 flags, MemArena *arena) {
	// OK, we know where to find free space. Now grab it!
	// We just mimic the popular BAT setup.
	size_t position = 0;
	size_t last_position = 0;

	// Zero all the pointers to be sure.
	for (int i = 0; i < num_views; i++)
	{
		if (views[i].out_ptr_low)
			*views[i].out_ptr_low = 0;
		if (views[i].out_ptr)
			*views[i].out_ptr = 0;
	}

	int i;
	for (i = 0; i < num_views; i++)
	{
		const MemoryView &view = views[i];
		SKIP(flags, view.flags);
		if (view.flags & MV_MIRROR_PREVIOUS) {
			position = last_position;
		} else {
#ifdef __SYMBIAN32__
			*(view.out_ptr_low) = (u8*)((int)arena->memmap->Base() + view.virtual_address);
			arena->memmap->Commit(view.virtual_address & 0x3FFFFFFF, view.size);
		}
		*(view.out_ptr) = (u8*)((int)arena->memmap->Base() + view.virtual_address & 0x3FFFFFFF);
#else
			*(view.out_ptr_low) = (u8*)arena->CreateView(position, view.size);
			if (!*view.out_ptr_low)
				goto bail;
		}
#ifdef _M_X64
		*view.out_ptr = (u8*)arena->CreateView(
			position, view.size, base + view.virtual_address);
#else
		if (view.flags & MV_MIRROR_PREVIOUS) {  // TODO: should check if the two & 0x3FFFFFFF are identical.
			// No need to create multiple identical views.
			*view.out_ptr = *views[i - 1].out_ptr;
		} else {
			*view.out_ptr = (u8*)arena->CreateView(
				position, view.size, base + (view.virtual_address & 0x3FFFFFFF));
			if (!*view.out_ptr)
				goto bail;
		}
#endif

#endif
		last_position = position;
		position += roundup(view.size);
	}

	return true;

bail:
	// Argh! ERROR! Free what we grabbed so far so we can try again.
	for (int j = 0; j <= i; j++)
	{
		SKIP(flags, views[i].flags);
		if (views[j].out_ptr_low && *views[j].out_ptr_low)
		{
			arena->ReleaseView(*views[j].out_ptr_low, views[j].size);
			*views[j].out_ptr_low = NULL;
		}
		if (*views[j].out_ptr)
		{
#ifdef _M_X64
			arena->ReleaseView(*views[j].out_ptr, views[j].size);
#else
			if (!(views[j].flags & MV_MIRROR_PREVIOUS))
			{
				arena->ReleaseView(*views[j].out_ptr, views[j].size);
			}
#endif
			*views[j].out_ptr = NULL;
		}
	}
	return false;
}

u8 *MemoryMap_Setup(const MemoryView *views, int num_views, u32 flags, MemArena *arena)
{
	size_t total_mem = 0;
	int base_attempts = 0;

	for (int i = 0; i < num_views; i++)
	{
		SKIP(flags, views[i].flags);
		if ((views[i].flags & MV_MIRROR_PREVIOUS) == 0)
			total_mem += roundup(views[i].size);
	}
	// Grab some pagefile backed memory out of the void ...
#ifndef __SYMBIAN32__
	arena->GrabLowMemSpace(total_mem);
#endif

	// Now, create views in high memory where there's plenty of space.
#ifdef _M_X64
	u8 *base = MemArena::Find4GBBase();
	// This really shouldn't fail - in 64-bit, there will always be enough
	// address space.
	if (!Memory_TryBase(base, views, num_views, flags, arena))
	{
		PanicAlert("MemoryMap_Setup: Failed finding a memory base.");
		exit(0);
		return 0;
	}
#else
#ifdef _WIN32
	// Try a whole range of possible bases. Return once we got a valid one.
	u32 max_base_addr = 0x7FFF0000 - 0x10000000;
	u8 *base = NULL;

	for (u32 base_addr = 0x01000000; base_addr < max_base_addr; base_addr += 0x400000)
	{
		base_attempts++;
		base = (u8 *)base_addr;
		if (Memory_TryBase(base, views, num_views, flags, arena)) 
		{
			INFO_LOG(MEMMAP, "Found valid memory base at %p after %i tries.", base, base_attempts);
			base_attempts = 0;
			break;
		}
	}
#elif defined(__SYMBIAN32__)
	arena->memmap = new RChunk();
	arena->memmap->CreateDisconnectedLocal(0 , 0, 0x10000000);
	if (!Memory_TryBase(arena->memmap->Base(), views, num_views, flags, arena))
	{
		PanicAlert("MemoryMap_Setup: Failed finding a memory base.");
		exit(0);
		return 0;
	}
	u8* base = arena->memmap->Base();
#else
	// Linux32 is fine with the x64 method, although limited to 32-bit with no automirrors.
	u8 *base = MemArena::Find4GBBase();
	if (!Memory_TryBase(base, views, num_views, flags, arena))
	{
		PanicAlert("MemoryMap_Setup: Failed finding a memory base.");
		exit(0);
		return 0;
	}
#endif

#endif
	if (base_attempts)
		PanicAlert("No possible memory base pointer found!");
	return base;
}

void MemoryMap_Shutdown(const MemoryView *views, int num_views, u32 flags, MemArena *arena)
{
	for (int i = 0; i < num_views; i++)
	{
		SKIP(flags, views[i].flags);
		if (views[i].out_ptr_low && *views[i].out_ptr_low)
			arena->ReleaseView(*views[i].out_ptr_low, views[i].size);
		if (*views[i].out_ptr && (views[i].out_ptr_low && *views[i].out_ptr != *views[i].out_ptr_low))
			arena->ReleaseView(*views[i].out_ptr, views[i].size);
		*views[i].out_ptr = NULL;
		if (views[i].out_ptr_low)
			*views[i].out_ptr_low = NULL;
	}
}
