// Copyright (c) 2020 PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "ppsspp_config.h"
#include "Core/Instance.h"


#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#endif

#include "Common/Log.h"
#include "Common/SysError.h"

#include <cstdint>

#include <retro_atomic.h>

uint8_t PPSSPP_ID = 0;

#if PPSSPP_PLATFORM(WINDOWS)
static HANDLE hIDMapFile = nullptr;
#else
static int hIDMapFile = -1;
static long BUF_SIZE = 4096;
#endif

// Shared between processes. One little-endian word: two pad bytes, then next, then total. Updated with
// CAS, which works across processes on the shared mapping.
struct InstanceInfo {
	retro_atomic_int_t word;
};

static uint8_t InstanceNext(uint32_t word) { return (uint8_t)(word >> 16); }
static uint8_t InstanceTotal(uint32_t word) { return (uint8_t)(word >> 24); }
static uint32_t MakeInstanceWord(uint32_t word, uint8_t next, uint8_t total) {
	return (word & 0xFFFF) | ((uint32_t)next << 16) | ((uint32_t)total << 24);
}

// update returns the new word for the old one. It may run more than once; the last run is the one
// that took effect.
static void ApplyInstanceUpdate(InstanceInfo *buf, uint32_t (*update)(uint32_t)) {
	uint32_t cur = (uint32_t)retro_atomic_load_acquire_int(&buf->word);
	while (!retro_atomic_cas_int(&buf->word, (int)cur, (int)update(cur))) {
		cur = (uint32_t)retro_atomic_load_acquire_int(&buf->word);
	}
}

#define ID_SHM_NAME "/PPSSPP_ID"

static bool UpdateInstanceCounter(uint32_t (*update)(uint32_t)) {
#if PPSSPP_PLATFORM(WINDOWS)
	if (!hIDMapFile) {
		return false;
	}
	InstanceInfo *buf = (InstanceInfo *)MapViewOfFile(hIDMapFile,   // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		sizeof(InstanceInfo));

	if (!buf) {
		auto err = GetLastError();
		ERROR_LOG(Log::sceNet, "Could not map view of file %s, %08x %s", ID_SHM_NAME, (uint32_t)err, GetStringErrorMsg(err).c_str());
		return false;
	}

	ApplyInstanceUpdate(buf, update);
	UnmapViewOfFile(buf);
	return true;
#else
	// TODO: replace shm_open & shm_unlink with ashmem or android-shmem
	return false;
#endif
}

int GetInstancePeerCount() {
	static int c = 0;
	UpdateInstanceCounter([](uint32_t word) {
		c = InstanceTotal(word);
		return word;
	});
	return c;
}

// Get current number of instance of PPSSPP running.
// Must be called only once during init.
void InitInstanceCounter() {
#if PPSSPP_PLATFORM(WINDOWS)
	uint32_t BUF_SIZE = 4096;
	SYSTEM_INFO sysInfo;

	GetSystemInfo(&sysInfo);
	int gran = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
	BUF_SIZE = (BUF_SIZE + gran - 1) & ~(gran - 1);

	hIDMapFile = CreateFileMapping(
		INVALID_HANDLE_VALUE,    // use paging file
		NULL,                    // default security
		PAGE_READWRITE,          // read/write access
		0,                       // maximum object size (high-order DWORD)
		BUF_SIZE,                // maximum object size (low-order DWORD)
		TEXT(ID_SHM_NAME));       // name of mapping object

	DWORD lasterr = GetLastError();
	if (!hIDMapFile) {
		ERROR_LOG(Log::sceNet, "Could not create %s file mapping object, %08x %s", ID_SHM_NAME, (uint32_t)lasterr, GetStringErrorMsg(lasterr).c_str());
		PPSSPP_ID = 1;
		return;
	}
#else
	// TODO : replace shm_open & shm_unlink with ashmem or android-shmem
#endif

	bool success = UpdateInstanceCounter([](uint32_t word) {
		// The shared segment outlives the processes that used it (see the shm_unlink comment),
		// so next keeps climbing across runs and eventually wraps this uint8_t. ID 0 is not a
		// valid instance - it fails the IsFirstInstance() check, which quietly disables config
		// saving - so skip past it.
		uint8_t next = InstanceNext(word) + 1;
		if (next == 0) {
			next = 1;
		}
		PPSSPP_ID = next;
		return MakeInstanceWord(word, next, InstanceTotal(word) + 1);
	});
	if (!success) {
		PPSSPP_ID = 1;
	}
}

void ShutdownInstanceCounter() {
	UpdateInstanceCounter([](uint32_t word) {
		return MakeInstanceWord(word, InstanceNext(word), InstanceTotal(word) - 1);
	});

#if PPSSPP_PLATFORM(WINDOWS)
	if (hIDMapFile) {
		CloseHandle(hIDMapFile); // If program exited(or crashed?) or the last handle reference closed the shared memory object will be deleted.
		hIDMapFile = nullptr;
	}
#else
	// Do nothing
#endif
}
