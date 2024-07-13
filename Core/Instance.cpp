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

#if !PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__) && !PPSSPP_PLATFORM(SWITCH)
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#endif

#include "Common/Log.h"
#include "Common/SysError.h"

#include <cstdint>

uint8_t PPSSPP_ID = 0;

#if PPSSPP_PLATFORM(WINDOWS)
static HANDLE hIDMapFile = nullptr;
static HANDLE mapLock = nullptr;
#else
static int hIDMapFile = -1;
static long BUF_SIZE = 4096;
#endif

struct InstanceInfo {
	uint8_t pad[2];
	uint8_t next;
	uint8_t total;
};

#define ID_SHM_NAME "/PPSSPP_ID"

static bool UpdateInstanceCounter(void (*callback)(volatile InstanceInfo *)) {
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

	bool result = false;
	if (!mapLock || WaitForSingleObject(mapLock, INFINITE) == 0) {
		callback(buf);
		if (mapLock) {
			ReleaseMutex(mapLock);
		}
		result = true;
	}
	UnmapViewOfFile(buf);

	return result;
#elif PPSSPP_PLATFORM(ANDROID) || defined(__LIBRETRO__) || PPSSPP_PLATFORM(SWITCH)
	// TODO: replace shm_open & shm_unlink with ashmem or android-shmem
	return false;
#else
	if (hIDMapFile < 0) {
		return false;
	}

	InstanceInfo *buf = (InstanceInfo *)mmap(0, BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, hIDMapFile, 0);
	if (buf == MAP_FAILED) {
		ERROR_LOG(Log::sceNet, "mmap(%s) failure.", ID_SHM_NAME);
		return false;
	}

	bool result = false;
	if (mlock(buf, BUF_SIZE) == 0) {
		callback(buf);
		munlock(buf, BUF_SIZE);
		result = true;
	}

	munmap(buf, BUF_SIZE);
	return result;
#endif
}

int GetInstancePeerCount() {
	static int c = 0;
	UpdateInstanceCounter([](volatile InstanceInfo *buf) {
		c = buf->total;
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

	mapLock = CreateMutex(nullptr, FALSE, L"PPSSPP_ID_mutex");

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
#elif PPSSPP_PLATFORM(ANDROID) || defined(__LIBRETRO__) || PPSSPP_PLATFORM(SWITCH)
	// TODO : replace shm_open & shm_unlink with ashmem or android-shmem
#else
	// Create shared memory object
	hIDMapFile = shm_open(ID_SHM_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	BUF_SIZE = (BUF_SIZE < sysconf(_SC_PAGE_SIZE)) ? sysconf(_SC_PAGE_SIZE) : BUF_SIZE;

	if (hIDMapFile < 0 || (ftruncate(hIDMapFile, BUF_SIZE)) == -1) {    // Set the size
		ERROR_LOG(Log::sceNet, "ftruncate(%s) failure.", ID_SHM_NAME);
		PPSSPP_ID = 1;
		return;
	}
#endif

	bool success = UpdateInstanceCounter([](volatile InstanceInfo *buf) {
		PPSSPP_ID = ++buf->next;
		buf->total++;
	});
	if (!success) {
		PPSSPP_ID = 1;
	}
}

void ShutdownInstanceCounter() {
	UpdateInstanceCounter([](volatile InstanceInfo *buf) {
		buf->total--;
	});

#if PPSSPP_PLATFORM(WINDOWS)
	if (hIDMapFile) {
		CloseHandle(hIDMapFile); // If program exited(or crashed?) or the last handle reference closed the shared memory object will be deleted.
		hIDMapFile = nullptr;
	}
	if (mapLock) {
		CloseHandle(mapLock);
		mapLock = nullptr;
	}
#elif PPSSPP_PLATFORM(ANDROID) || defined(__LIBRETRO__) || PPSSPP_PLATFORM(SWITCH)
	// Do nothing
#else
	if (hIDMapFile >= 0) {
		close(hIDMapFile);
		shm_unlink(ID_SHM_NAME);     // If program exited or crashed before unlinked the shared memory object and it's contents will persist.
		hIDMapFile = -1;
	}
#endif
}
