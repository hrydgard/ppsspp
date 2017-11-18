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

#ifdef __ANDROID__

#include "MemoryUtil.h"
#include "MemArena.h"
#include "StringUtils.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sys/ioctl.h>
#include <linux/ashmem.h>

// Hopefully this ABI will never change...

#define ASHMEM_DEVICE	"/dev/ashmem"

bool MemArena::NeedsProbing() {
	return false;
}

// ashmem_create_region - creates a new ashmem region and returns the file
// descriptor, or <0 on error
// `name' is an optional label to give the region (visible in /proc/pid/maps)
// `size' is the size of the region, in page-aligned bytes
static int ashmem_create_region(const char *name, size_t size) {
	int fd = open(ASHMEM_DEVICE, O_RDWR);
	if (fd < 0)
		return fd;

	int ret;
	if (name) {
		char buf[ASHMEM_NAME_LEN];
		truncate_cpy(buf, name);
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

// Windows mappings need to be on 64K boundaries, due to Alpha legacy.
size_t MemArena::roundup(size_t x) {
	return x;
}

void MemArena::GrabLowMemSpace(size_t size) {
	// Use ashmem so we don't have to allocate a file on disk!
	fd = ashmem_create_region("PPSSPP_RAM", size);
	// Note that it appears that ashmem is pinned by default, so no need to pin.
	if (fd < 0) {
		ERROR_LOG(MEMMAP, "Failed to grab ashmem space of size: %08x  errno: %d", (int)size, (int)(errno));
		return;
	}
}

void MemArena::ReleaseSpace() {
	close(fd);
}

void *MemArena::CreateView(s64 offset, size_t size, void *base) {
	void *retval = mmap(base, size, PROT_READ | PROT_WRITE, MAP_SHARED | ((base == 0) ? 0 : MAP_FIXED), fd, offset);
	if (retval == MAP_FAILED) {
		NOTICE_LOG(MEMMAP, "mmap on ashmem (fd: %d) failed", (int)fd);
		return 0;
	}
	return retval;
}

void MemArena::ReleaseView(void* view, size_t size) {
	munmap(view, size);
}

u8* MemArena::Find4GBBase() {
#if PPSSPP_ARCH(64BIT)
	// Just grab some random 4GB...
	return reinterpret_cast<u8*>(0x2300000000ULL);
#else
	// Address masking is used in 32-bit mode, so we can get away with less memory.
	void* base = mmap(0, 0x10000000, PROT_READ | PROT_WRITE,
		MAP_ANON | MAP_SHARED, -1, 0);
	if (base == MAP_FAILED) {
		PanicAlert("Failed to map 256 MB of memory space: %s", strerror(errno));
		return 0;
	}
	munmap(base, 0x10000000);
	return static_cast<u8*>(base);
#endif
}

#endif  // __ANDROID__
