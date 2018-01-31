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

#if !defined(_WIN32) && !defined(ANDROID) && !defined(__APPLE__)

#include <string>

#include "FileUtil.h"
#include "MemoryUtil.h"
#include "MemArena.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

static const std::string tmpfs_location = "/dev/shm";
static const std::string tmpfs_ram_temp_file = "/dev/shm/gc_mem.tmp";

// do not make this "static"
std::string ram_temp_file = "/tmp/gc_mem.tmp";

size_t MemArena::roundup(size_t x) {
	return x;
}

bool MemArena::NeedsProbing() {
	return false;
}

void MemArena::GrabLowMemSpace(size_t size) {
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	// Some platforms (like Raspberry Pi) end up flushing to disk.
	// To avoid this, we try to use /dev/shm (tmpfs) if it exists.
	fd = -1;
	if (File::Exists(tmpfs_location)) {
		fd = open(tmpfs_ram_temp_file.c_str(), O_RDWR | O_CREAT, mode);
		if (fd >= 0) {
			// Great, this definitely shouldn't flush to disk.
			ram_temp_file = tmpfs_ram_temp_file;
		}
	}

	if (fd < 0) {
		fd = open(ram_temp_file.c_str(), O_RDWR | O_CREAT, mode);
	}
	if (fd < 0) {
		ERROR_LOG(MEMMAP, "Failed to grab memory space as a file: %s of size: %08x  errno: %d", ram_temp_file.c_str(), (int)size, (int)(errno));
		return;
	}
	// delete immediately, we keep the fd so it still lives
	if (unlink(ram_temp_file.c_str()) != 0) {
		WARN_LOG(MEMMAP, "Failed to unlink %s", ram_temp_file.c_str());
	}
	if (ftruncate(fd, size) != 0) {
		ERROR_LOG(MEMMAP, "Failed to ftruncate %d (%s) to size %08x", (int)fd, ram_temp_file.c_str(), (int)size);
	}
	return;
}

void MemArena::ReleaseSpace() {
	close(fd);
}

void *MemArena::CreateView(s64 offset, size_t size, void *base)
{
	void *retval = mmap(base, size, PROT_READ | PROT_WRITE, MAP_SHARED |
// Do not sync memory to underlying file. Linux has this by default.
#if defined(__DragonFly__) || defined(__FreeBSD__)
		MAP_NOSYNC |
#endif
		((base == 0) ? 0 : MAP_FIXED), fd, offset);

	if (retval == MAP_FAILED) {
		NOTICE_LOG(MEMMAP, "mmap on %s (fd: %d) failed", ram_temp_file.c_str(), (int)fd);
		return 0;
	}
	return retval;
}

void MemArena::ReleaseView(void* view, size_t size) {
	munmap(view, size);
}

u8* MemArena::Find4GBBase() {
	// Now, create views in high memory where there's plenty of space.
#if PPSSPP_ARCH(64BIT) && !defined(USE_ADDRESS_SANITIZER)
	// Very precarious - mmap cannot return an error when trying to map already used pages.
	// This makes the Windows approach above unusable on Linux, so we will simply pray...
	return reinterpret_cast<u8*>(0x2300000000ULL);
#else
	size_t size = 0x10000000;
	void* base = mmap(0, size, PROT_READ | PROT_WRITE,
		MAP_ANON | MAP_SHARED, -1, 0);
	if (base == MAP_FAILED) {
		PanicAlert("Failed to map 256 MB of memory space: %s", strerror(errno));
		return 0;
	}
	munmap(base, size);
	return static_cast<u8*>(base);
#endif
}

#endif
