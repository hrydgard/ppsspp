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
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#endif

#include "Common/Log.h"
#include "Common/SysError.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Encoding/Utf8.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>

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
	// An actual advisory lock on the shm object. This used to call mlock(), which only pins
	// pages in RAM and provides no mutual exclusion whatsoever - two instances starting at the
	// same moment could both read the counter and come away with the same PPSSPP_ID, then both
	// believe they were the first instance and write the config over each other.
	if (flock(hIDMapFile, LOCK_EX) == 0) {
		callback(buf);
		flock(hIDMapFile, LOCK_UN);
		result = true;
	} else {
		ERROR_LOG(Log::sceNet, "flock(%s) failure: %s", ID_SHM_NAME, GetLastErrorMsg().c_str());
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
		// The shared segment outlives the processes that used it (see the shm_unlink comment),
		// so next keeps climbing across runs and eventually wraps this uint8_t. ID 0 is not a
		// valid instance - it fails the IsFirstInstance() check, which quietly disables config
		// saving - so skip past it.
		if (++buf->next == 0) {
			buf->next = 1;
		}
		PPSSPP_ID = buf->next;
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

// Instance sync (see Instance.h). Each slot holds the emulated millisecond its instance has reached,
// plus one: 0 is an instance that hasn't started yet, and SYNC_LEFT one that has quit.
static constexpr int SYNC_MAX_INSTANCES = 255;
static constexpr int32_t SYNC_LEFT = INT32_MAX;
// An instance that stops moving (paused in a debugger, or crashed) is waited for this long, in wall
// seconds, and then left out until it moves again. One that hasn't started yet gets longer, to boot.
static constexpr double SYNC_STALL_SECONDS = 2.0;
static constexpr double SYNC_JOIN_SECONDS = 10.0;

static std::atomic<int32_t> *syncSlots = nullptr;
static int syncIndex = 0;
static int syncCount = 0;
static int32_t syncPublished = 0;
static int32_t syncLastSeen[SYNC_MAX_INSTANCES];
static double syncLastChange[SYNC_MAX_INSTANCES];
static bool syncIgnored[SYNC_MAX_INSTANCES];
#if PPSSPP_PLATFORM(WINDOWS)
static HANDLE syncFile = INVALID_HANDLE_VALUE;
static HANDLE syncMapping = nullptr;
#endif

static_assert(sizeof(std::atomic<int32_t>) == sizeof(int32_t) && SYNC_MAX_INSTANCES * sizeof(int32_t) <= INSTANCE_SYNC_FILE_SIZE, "Instance sync slots don't fit");

bool InitInstanceSync(const std::string &path, int index, int count) {
	if (index < 1 || count < 1 || index > count || count > SYNC_MAX_INSTANCES) {
		ERROR_LOG(Log::System, "Instance sync: bad instance %d of %d", index, count);
		return false;
	}
	void *mem = nullptr;
#if PPSSPP_PLATFORM(WINDOWS)
	syncFile = CreateFileW(ConvertUTF8ToWString(path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (syncFile != INVALID_HANDLE_VALUE) {
		syncMapping = CreateFileMappingW(syncFile, nullptr, PAGE_READWRITE, 0, INSTANCE_SYNC_FILE_SIZE, nullptr);
	}
	if (syncMapping) {
		mem = MapViewOfFile(syncMapping, FILE_MAP_ALL_ACCESS, 0, 0, INSTANCE_SYNC_FILE_SIZE);
	}
#elif PPSSPP_PLATFORM(ANDROID) || defined(__LIBRETRO__) || PPSSPP_PLATFORM(SWITCH)
	// Not needed there: the instances run on a desktop.
#else
	int fd = open(path.c_str(), O_RDWR);
	if (fd >= 0) {
		mem = mmap(nullptr, INSTANCE_SYNC_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		// The mapping keeps the file.
		close(fd);
		if (mem == MAP_FAILED) {
			mem = nullptr;
		}
	}
#endif
	if (!mem) {
		ERROR_LOG(Log::System, "Instance sync: can't map %s: %s", path.c_str(), GetLastErrorMsg().c_str());
		ShutdownInstanceSync();
		return false;
	}
	syncSlots = (std::atomic<int32_t> *)mem;
	syncIndex = index;
	syncCount = count;
	const double now = time_now_d();
	for (int i = 0; i < SYNC_MAX_INSTANCES; i++) {
		syncLastSeen[i] = -1;
		syncLastChange[i] = now;
		syncIgnored[i] = false;
	}
	INFO_LOG(Log::System, "Instance sync: instance %d of %d, through %s", index, count, path.c_str());
	return true;
}

void ShutdownInstanceSync() {
	if (syncSlots) {
		// Don't keep the others waiting for us.
		syncSlots[syncIndex - 1].store(SYNC_LEFT);
#if PPSSPP_PLATFORM(WINDOWS)
		UnmapViewOfFile(syncSlots);
#elif !PPSSPP_PLATFORM(ANDROID) && !defined(__LIBRETRO__) && !PPSSPP_PLATFORM(SWITCH)
		munmap(syncSlots, INSTANCE_SYNC_FILE_SIZE);
#endif
		syncSlots = nullptr;
	}
#if PPSSPP_PLATFORM(WINDOWS)
	if (syncMapping) {
		CloseHandle(syncMapping);
		syncMapping = nullptr;
	}
	if (syncFile != INVALID_HANDLE_VALUE) {
		CloseHandle(syncFile);
		syncFile = INVALID_HANDLE_VALUE;
	}
#endif
}

bool InstanceSyncEnabled() {
	return syncSlots != nullptr;
}

// Whether instance i (0-based) still holds us back from passing `value`.
static bool SyncBehind(int i, int32_t value, double now) {
	const int32_t v = syncSlots[i].load(std::memory_order_acquire);
	if (v != syncLastSeen[i]) {
		syncLastSeen[i] = v;
		syncLastChange[i] = now;
		if (syncIgnored[i]) {
			INFO_LOG(Log::System, "Instance sync: instance %d is moving again", i + 1);
			syncIgnored[i] = false;
		}
	}
	if (v >= value || syncIgnored[i]) {
		return false;
	}
	if (now - syncLastChange[i] > (v == 0 ? SYNC_JOIN_SECONDS : SYNC_STALL_SECONDS)) {
		WARN_LOG(Log::System, "Instance sync: instance %d %s, going on without it", i + 1, v == 0 ? "hasn't started" : "has stopped");
		syncIgnored[i] = true;
		return false;
	}
	return true;
}

void InstanceSyncStandAside(bool aside) {
	if (syncSlots) {
		syncSlots[syncIndex - 1].store(aside ? SYNC_LEFT : syncPublished, std::memory_order_release);
	}
}

void InstanceSyncReached(int64_t ms) {
	if (!syncSlots) {
		return;
	}
	const int32_t value = (int32_t)std::min<int64_t>(ms + 1, SYNC_LEFT - 1);
	syncPublished = value;
	syncSlots[syncIndex - 1].store(value, std::memory_order_release);
	// Usually the others are a few microseconds of wall time away, so yield first. Spinning longer
	// would take the CPU from the instance being waited for.
	for (int tries = 0; ; tries++) {
		const double now = time_now_d();
		bool behind = false;
		for (int i = 0; i < syncCount && !behind; i++) {
			if (i != syncIndex - 1) {
				behind = SyncBehind(i, value, now);
			}
		}
		if (!behind) {
			return;
		}
		if (tries < 16) {
			std::this_thread::yield();
		} else {
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
	}
}
