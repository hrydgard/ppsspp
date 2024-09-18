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

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <sys/ioctl.h>
#include <linux/ashmem.h>
#include <dlfcn.h>

#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/MemArena.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"

// Hopefully this ABI will never change...

#define ASHMEM_DEVICE	"/dev/ashmem"

bool MemArena::NeedsProbing() {
	return false;
}

// ashmem_create_region - creates a new ashmem region and returns the file
// descriptor, or <0 on error
// This function is defined in much later version of the ndk, so we can only access it via dlopen().
// `name' is an optional label to give the region (visible in /proc/pid/maps)
// `size' is the size of the region, in page-aligned bytes
static int ashmem_create_region(const char *name, size_t size) {
	static void* handle = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
	using type_ASharedMemory_create = int(*)(const char *name, size_t size);
	static type_ASharedMemory_create function_create = nullptr;

	if (handle != nullptr) {
		function_create =
			reinterpret_cast<type_ASharedMemory_create>(dlsym(handle, "ASharedMemory_create"));
	}

	if (function_create != nullptr) {
		return function_create(name, size);
	} else {
		return -1;
	}
}

// legacy_ashmem_create_region - creates a new ashmem region and returns the file
// descriptor, or <0 on error
// `name' is an optional label to give the region (visible in /proc/pid/maps)
// `size' is the size of the region, in page-aligned bytes
static int legacy_ashmem_create_region(const char *name, size_t size) {
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
	ERROR_LOG(Log::MemMap, "NASTY ASHMEM ERROR: ret = %08x", ret);
	close(fd);
	return ret;
}

// Windows mappings need to be on 64K boundaries, due to Alpha legacy.
size_t MemArena::roundup(size_t x) {
	return x;
}

bool MemArena::GrabMemSpace(size_t size) {
	// Use ashmem so we don't have to allocate a file on disk!
	const char* name = "PPSSPP_RAM";

	// Since version 26 Android provides a new api for accessing SharedMemory.
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 26) {
		fd = ashmem_create_region(name, size);
	} else {
		fd = legacy_ashmem_create_region(name, size);
	}
	// Note that it appears that ashmem is pinned by default, so no need to pin.
	if (fd < 0) {
		ERROR_LOG(Log::MemMap, "Failed to grab ashmem space of size: %08x  errno: %d", (int)size, (int)(errno));
		return false;
	}
	return true;
}

void MemArena::ReleaseSpace() {
	close(fd);
}

void *MemArena::CreateView(s64 offset, size_t size, void *base) {
	void *retval = mmap(base, size, PROT_READ | PROT_WRITE, MAP_SHARED | ((base == 0) ? 0 : MAP_FIXED), fd, offset);
	if (retval == MAP_FAILED) {
		NOTICE_LOG(Log::MemMap, "mmap on ashmem (fd: %d) failed", (int)fd);
		return nullptr;
	}
	return retval;
}

void MemArena::ReleaseView(s64 offset, void* view, size_t size) {
	munmap(view, size);
}

u8* MemArena::Find4GBBase() {
#if PPSSPP_ARCH(64BIT)
	// We should probably just go look in /proc/self/maps for some free space.
	// But let's try the anonymous mmap trick, just like on 32-bit, but bigger and
	// aligned to 4GB for the movk trick. We can ensure that we get an aligned 4GB
	// address by grabbing 8GB and aligning the pointer.
	const uint64_t EIGHT_GIGS = 0x200000000ULL;
	void *base = mmap(0, EIGHT_GIGS, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
	if (base && base != MAP_FAILED) {
		INFO_LOG(Log::System, "base: %p", base);
		uint64_t aligned_base = ((uint64_t)base + 0xFFFFFFFF) & ~0xFFFFFFFFULL;
		INFO_LOG(Log::System, "aligned_base: %p", (void *)aligned_base);
		munmap(base, EIGHT_GIGS);
		return reinterpret_cast<u8 *>(aligned_base);
	} else {
		u8 *hardcoded_ptr = reinterpret_cast<u8*>(0x2300000000ULL);
		INFO_LOG(Log::System, "Failed to anonymously map 8GB. Fall back to the hardcoded pointer %p.", hardcoded_ptr);
		// Just grab some random 4GB...
		// This has been known to fail lately though, see issue #12249.
		return hardcoded_ptr;
	}
#else
	// Address masking is used in 32-bit mode, so we can get away with less memory.
	void *base = mmap(0, 0x10000000, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);

	if (base == MAP_FAILED) {
		ERROR_LOG(Log::System, "Failed to map 256 MB of memory space: %s", strerror(errno));
		return nullptr;
	}

	munmap(base, 0x10000000);
	return static_cast<u8*>(base);
#endif
}

#endif  // __ANDROID__
