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

#include "Instance.h"

#if __linux__ || __APPLE__
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#endif

#include "Common/Log.h"

#if PPSSPP_PLATFORM(WINDOWS)

#include "Common/CommonWindows.h"

#endif

#include <cstdint>

uint8_t PPSSPP_ID = 0;

#if PPSSPP_PLATFORM(WINDOWS)
static HANDLE hIDMapFile = NULL;
#else
static int hIDMapFile = 0;
#endif

static int32_t* pIDBuf = NULL;
#define ID_SHM_NAME "/PPSSPP_ID"

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
	if (hIDMapFile == NULL)
	{
		ERROR_LOG(SCENET, "Could not create %s file mapping object (%d).", ID_SHM_NAME, lasterr);
		PPSSPP_ID = 1;
		return;
	}

	pIDBuf = (int32_t*)MapViewOfFile(hIDMapFile,   // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		sizeof(int32_t)); //BUF_SIZE

	if (pIDBuf == NULL) {
		ERROR_LOG(SCENET, "Could not map view of file %s (%d).", ID_SHM_NAME, GetLastError());
		//CloseHandle(hIDMapFile);
		PPSSPP_ID = 1;
		return;
	}

	(*pIDBuf)++;
	int id = *pIDBuf;
	UnmapViewOfFile(pIDBuf);
	//CloseHandle(hIDMapFile); //Should be called when program exits
	//hIDMapFile = NULL;

	PPSSPP_ID = id;
#elif PPSSPP_PLATFORM(ANDROID)
	// TODO : replace shm_open & shm_unlink with ashmem or android-shmem
	PPSSPP_ID = 1;
#else
	long BUF_SIZE = 4096;
	//caddr_t pIDBuf;
	int status;

	// Create shared memory object 

	hIDMapFile = shm_open(ID_SHM_NAME, O_CREAT | O_RDWR, 0);
	BUF_SIZE = (BUF_SIZE < sysconf(_SC_PAGE_SIZE)) ? sysconf(_SC_PAGE_SIZE) : BUF_SIZE;

	if ((ftruncate(hIDMapFile, BUF_SIZE)) == -1) {    // Set the size 
		ERROR_LOG(SCENET, "ftruncate(%s) failure.", ID_SHM_NAME);
		return 1;
	}

	pIDBuf = (int32_t*)mmap(0, BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, hIDMapFile, 0);
	if (pIDBuf == MAP_FAILED) {    // Set the size 
		ERROR_LOG(SCENET, "mmap(%s) failure.", ID_SHM_NAME);
		pIDBuf = NULL;
		return 1;
	}

	int id = 1;
	if (mlock(pIDBuf, BUF_SIZE) == 0) {
		(*pIDBuf)++;
		id = *pIDBuf;
		munlock(pIDBuf, BUF_SIZE);
	}

	status = munmap(pIDBuf, BUF_SIZE);  // Unmap the page 
	//status = close(hIDMapFile);                   //   Close file, should be called when program exits?
	//status = shm_unlink(ID_SHM_NAME);     // Unlink [& delete] shared-memory object, should be called when program exits

	return id;
#endif
}

void ShutdownInstanceCounter() {
#if PPSSPP_PLATFORM(WINDOWS)
	if (hIDMapFile != NULL) {
		CloseHandle(hIDMapFile); // If program exited(or crashed?) or the last handle reference closed the shared memory object will be deleted.
		hIDMapFile = NULL;
	}
#elif PPSSPP_PLATFORM(ANDROID)
	// Do nothing
#else
	// TODO : This unlink should be called when program exits instead of everytime the game reset.
	if (hIDMapFile != 0) {
		close(hIDMapFile);
		shm_unlink(ID_SHM_NAME);     // If program exited or crashed before unlinked the shared memory object and it's contents will persist.
		hIDMapFile = 0;
	}
#endif
}
