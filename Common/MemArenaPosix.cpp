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

#if !defined(_WIN32) && !defined(ANDROID) && !defined(__APPLE__) && !PPSSPP_PLATFORM(SWITCH)

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>

#ifndef MAP_NORESERVE
// Not implemented on BSDs
#define MAP_NORESERVE 0
#endif

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/MemoryUtil.h"
#include "Common/MemArena.h"

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

bool MemArena::GrabMemSpace(size_t size) {
#ifndef NO_MMAP
	constexpr mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	// Try a few times in case multiple instances are started near each other.
	char ram_temp_filename[128]{};
	bool is_shm = false;
	for (int i = 0; i < 256; ++i) {
		snprintf(ram_temp_filename, sizeof(ram_temp_filename), "/ppsspp_%d.ram", i);
		// This opens atomically, so will fail if another process is starting.
		fd = shm_open(ram_temp_filename, O_RDWR | O_CREAT | O_EXCL, mode);
		if (fd >= 0) {
			INFO_LOG(Log::MemMap, "Got shm file: %s", ram_temp_filename);
			is_shm = true;
			// Our handle persists per POSIX, so no need to keep it around.
			if (shm_unlink(ram_temp_filename) != 0) {
				WARN_LOG(Log::MemMap, "Failed to shm_unlink %s", ram_temp_file.c_str());
			}
			break;
		}
	}

	// Fall back to old tmpfs behavior.
	if (fd < 0 && File::Exists(Path(tmpfs_location))) {
		fd = open(tmpfs_ram_temp_file.c_str(), O_RDWR | O_CREAT, mode);
		if (fd >= 0) {
			// Great, this definitely shouldn't flush to disk.
			ram_temp_file = tmpfs_ram_temp_file;
			INFO_LOG(Log::MemMap, "Got tmpfs ram file: %s", tmpfs_ram_temp_file.c_str());
		}
	}

	if (fd < 0) {
		INFO_LOG(Log::MemMap, "Trying '%s' as ram temp file", ram_temp_file.c_str());
		fd = open(ram_temp_file.c_str(), O_RDWR | O_CREAT, mode);
	}
	if (fd < 0) {
		ERROR_LOG(Log::MemMap, "Failed to grab memory space as a file: %s of size: %08x. Error: %s", ram_temp_file.c_str(), (int)size, strerror(errno));
		return false;
	}
	// delete immediately, we keep the fd so it still lives
	if (!is_shm && unlink(ram_temp_file.c_str()) != 0) {
		WARN_LOG(Log::MemMap, "Failed to unlink %s", ram_temp_file.c_str());
	}
	if (ftruncate(fd, size) != 0) {
		ERROR_LOG(Log::MemMap, "Failed to ftruncate %d (%s) to size %08x", (int)fd, ram_temp_file.c_str(), (int)size);
		// Should this be a failure?
	}
#endif
	return true;
}

void MemArena::ReleaseSpace() {
#ifndef NO_MMAP
	close(fd);
#endif
}

void *MemArena::CreateView(s64 offset, size_t size, void *base)
{
#ifdef NO_MMAP
    return (void*) base;
#else
	void *retval = mmap(base, size, PROT_READ | PROT_WRITE, MAP_SHARED |
// Do not sync memory to underlying file. Linux has this by default.
#if defined(__DragonFly__) || defined(__FreeBSD__)
		MAP_NOSYNC |
#endif
		((base == 0) ? 0 : MAP_FIXED), fd, offset);

	if (retval == MAP_FAILED) {
		NOTICE_LOG(Log::MemMap, "mmap on %s (fd: %d) failed: %s", ram_temp_file.c_str(), (int)fd, strerror(errno));
		return 0;
	}
	return retval;
#endif
}

void MemArena::ReleaseView(s64 offset, void* view, size_t size) {
#ifndef NO_MMAP
	munmap(view, size);
#endif
}

u8* MemArena::Find4GBBase() {
	// Now, create views in high memory where there's plenty of space.
#if PPSSPP_ARCH(64BIT) && !defined(USE_ASAN) && !defined(NO_MMAP)
	// We should probably just go look in /proc/self/maps for some free space.
	// But let's try the anonymous mmap trick, just like on 32-bit, but bigger and
	// aligned to 4GB for the movk trick. We can ensure that we get an aligned 4GB
	// address by grabbing 8GB and aligning the pointer.
	const uint64_t EIGHT_GIGS = 0x200000000ULL;
	void *base = mmap(0, EIGHT_GIGS, PROT_NONE, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (base && base != MAP_FAILED) {
		INFO_LOG(Log::MemMap, "base: %p", base);
		uint64_t aligned_base = ((uint64_t)base + 0xFFFFFFFF) & ~0xFFFFFFFFULL;
		INFO_LOG(Log::MemMap, "aligned_base: %p", (void *)aligned_base);
		munmap(base, EIGHT_GIGS);
		return reinterpret_cast<u8 *>(aligned_base);
	} else {
		u8 *hardcoded_ptr = reinterpret_cast<u8*>(0x2300000000ULL);
		INFO_LOG(Log::MemMap, "Failed to anonymously map 8GB (%s). Fall back to the hardcoded pointer %p.", strerror(errno), hardcoded_ptr);
		// Just grab some random 4GB...
		// This has been known to fail lately though, see issue #12249.
		return hardcoded_ptr;
	}
#elif defined(NO_MMAP)
    void* base = std::malloc(0x0A000000);
    return static_cast<u8*>(base);
#else
	size_t size = 0x10000000;
	void* base = mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED | MAP_NORESERVE, -1, 0);
	_assert_msg_(base != MAP_FAILED, "Failed to map 256 MB of memory space: %s", strerror(errno));
	munmap(base, size);
	return static_cast<u8*>(base);
#endif
}

#endif
