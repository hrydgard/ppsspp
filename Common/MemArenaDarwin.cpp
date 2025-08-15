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

#if defined(__APPLE__)

#include <cstdint>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <mach/mach.h>
#include <mach/vm_map.h>

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/MemoryUtil.h"
#include "Common/MemArena.h"

size_t MemArena::roundup(size_t x) {
	return x;
}

bool MemArena::GrabMemSpace(size_t size) {
	vm_size = size;
	kern_return_t retval = vm_allocate(mach_task_self(), &vm_mem, size, VM_FLAGS_ANYWHERE);
	if (retval != KERN_SUCCESS) {
		ERROR_LOG(Log::MemMap, "Failed to grab a block of virtual memory");
		return false;
	} else {
		INFO_LOG(Log::MemMap, "Successfully allocated %d bytes at %p", (int)size, (void *)vm_mem);
		return true;
	}
}

void MemArena::ReleaseSpace() {
	vm_deallocate(mach_task_self(), vm_mem, vm_size);
	vm_size = 0;
	vm_mem = 0;
}

void *MemArena::CreateView(s64 offset, size_t size, void *base) {
	mach_port_t self = mach_task_self();
	vm_address_t target = (vm_address_t)base;
	uint64_t mask = 0;
	bool anywhere = false;
	vm_address_t source = vm_mem + offset;
	vm_prot_t cur_protection = 0;
	vm_prot_t max_protection = 0;
	kern_return_t retval =
		vm_remap(self, &target, size, mask, anywhere,
				 self, source, false,
				 &cur_protection, &max_protection, VM_INHERIT_DEFAULT);
	if (retval != KERN_SUCCESS) {
		// 1 == KERN_INVALID_ADDRESS
		// 3 == KERN_NO_SPACE (race?)
		// 4 == KERN_INVALID_ARGUMENT
		ERROR_LOG(Log::MemMap, "vm_remap failed (%d) - could not remap from %llx (offset %llx) of size %llx to %p",
				  (int)retval, (uint64_t)source, (uint64_t)offset, (uint64_t)size, base);
		return nullptr;
	}
	return (void *)target;
}

void MemArena::ReleaseView(s64 offset, void* view, size_t size) {
	vm_address_t addr = (vm_address_t)view;
	vm_deallocate(mach_task_self(), addr, size);
}

bool MemArena::NeedsProbing() {
#if PPSSPP_PLATFORM(IOS) && PPSSPP_ARCH(64BIT)
	return true;
#else
	return false;
#endif
}

u8* MemArena::Find4GBBase() {
#if PPSSPP_PLATFORM(IOS) && PPSSPP_ARCH(64BIT)
	// The caller will need to do probing, like on 32-bit Windows.
	return nullptr;
#else
	size_t size;
#if PPSSPP_ARCH(64BIT)
	size = 0xE1000000;
#else
	size = 0x10000000;
#endif

	vm_address_t addr = 0;
	kern_return_t retval = vm_allocate(mach_task_self(), &addr, size, VM_FLAGS_ANYWHERE);
	if (retval == KERN_SUCCESS) {
		// Don't need the memory now, was just probing.
		vm_deallocate(mach_task_self(), addr, size);
		return (u8 *)addr;
	}
#endif
	return nullptr;
}

#endif  // __APPLE__
