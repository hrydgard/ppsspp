// Copyright (c) 2012- PPSSPP Project.

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

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/File/DiskFree.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Compatibility.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HW/MemoryStick.h"
#include "Core/System.h"
#include "Common/CommonTypes.h"
#include "Common/Thread/Promise.h"

// MS and FatMS states.
static MemStickState memStickState;
static MemStickFatState memStickFatState;
static bool memStickNeedsAssign = false;
static uint64_t memStickInsertedAt = 0;
static uint64_t memstickInitialFree = 0;
static uint64_t memstickCurrentUse = 0;
static bool memstickCurrentUseValid = false;

enum FreeCalcStatus {
	NONE,
	RUNNING,
	DONE,
	CLEANED_UP,
};

static const uint64_t normalMemstickSize = 9ULL * 1024 * 1024 * 1024;
static const uint64_t smallMemstickSize = 1ULL * 1024 * 1024 * 1024;

static Promise<uint64_t> *g_initialMemstickSizePromise = nullptr;

void MemoryStick_DoState(PointerWrap &p) {
	auto s = p.Section("MemoryStick", 1, 5);
	if (!s)
		return;

	Do(p, memStickState);
	Do(p, memStickFatState);
	if (s >= 4) {
		// Do nothing.
	} else if (s >= 2) {
		// Really no point in storing the memstick size.
		u64 memStickSize = normalMemstickSize;
		Do(p, memStickSize);
	}
	if (s >= 5) {
		Do(p, memstickInitialFree);
	}

	if (s >= 3) {
		Do(p, memStickNeedsAssign);
		Do(p, memStickInsertedAt);
	}
}

MemStickState MemoryStick_State() {
	return memStickState;
}

MemStickFatState MemoryStick_FatState() {
	if (memStickNeedsAssign && CoreTiming::GetTicks() > memStickInsertedAt + msToCycles(500)) {
		// It's been long enough for us to be done mounting the memory stick.
		memStickFatState = PSP_FAT_MEMORYSTICK_STATE_ASSIGNED;
		memStickNeedsAssign = false;
	}
	return memStickFatState;
}

u64 MemoryStick_SectorSize() {
	return 32 * 1024; // 32KB
}

static uint64_t ComputeSizeOfSavedataForGame(const Path &saveFolder, const std::string_view gameID) {
	uint64_t space = 0;
	std::vector<File::FileInfo> subDirs;
	File::GetFilesInDir(saveFolder, &subDirs, nullptr, 0, gameID);  // gameID as directory prefix.
	for (auto &dir : subDirs) {
		if (!dir.isDirectory)
			continue;
		space += File::ComputeRecursiveDirectorySize(saveFolder / dir.name);
	}
	return space;
}

u64 MemoryStick_FreeSpace(std::string gameID) {
	INFO_LOG(Log::IO, "Calculating free disk space (%s)", gameID.c_str());

	const CompatFlags &flags = PSP_CoreParameter().compat.flags();
	u64 realFreeSpace = pspFileSystem.FreeDiskSpace("ms0:/");

	// Cap the memory stick size to avoid math errors when old games get sizes that were
	// not planned for back then (even though 2GB cards were available.)
	// We have a compat setting to make it even smaller for Harry Potter : Goblet of Fire, see #13266.
	const u64 memStickSize = flags.ReportSmallMemstick ? smallMemstickSize : (u64)g_Config.iMemStickSizeGB * 1024 * 1024 * 1024;

	// Assume the memory stick is only used to store savedata, for the current game only.
	if (!memstickCurrentUseValid) {
		Path saveFolder = GetSysDirectory(DIRECTORY_SAVEDATA);
		memstickCurrentUse = ComputeSizeOfSavedataForGame(saveFolder, gameID);
		memstickCurrentUseValid = true;
	}

	u64 simulatedFreeSpace = 0;
	if (memstickCurrentUse < memStickSize) {
		simulatedFreeSpace = memStickSize - memstickCurrentUse;
	} else if (flags.ReportSmallMemstick) {
		// There's more stuff in the memstick than the size we report.
		// This doesn't work, so we'll just have to lie. Not sure what the best way is.
		simulatedFreeSpace = smallMemstickSize / 2;  // just pick a value.
	}
	if (flags.MemstickFixedFree) {
		const u64 memstickInitialFree = g_initialMemstickSizePromise->BlockUntilReady();
		_dbg_assert_(g_initialMemstickSizePromise);
		// Assassin's Creed: Bloodlines fails to save if free space changes incorrectly during game.
		// See issue #12761
		realFreeSpace = 0;
		if (memstickCurrentUse <= memstickInitialFree) {
			realFreeSpace = memstickInitialFree - memstickCurrentUse;
		}
	}

	return std::min(simulatedFreeSpace, realFreeSpace);
}

void MemoryStick_NotifyWrite() {
	memstickCurrentUseValid = false;
}

void MemoryStick_SetFatState(MemStickFatState state) {
	memStickFatState = state;
	memStickNeedsAssign = false;
}

void MemoryStick_SetState(MemStickState state) {
	if (memStickState == state) {
		return;
	}

	memStickState = state;

	// If removed, we unmount.  Otherwise, mounting is delayed.
	if (state == PSP_MEMORYSTICK_STATE_NOT_INSERTED) {
		MemoryStick_SetFatState(PSP_FAT_MEMORYSTICK_STATE_UNASSIGNED);
	} else {
		memStickInsertedAt = CoreTiming::GetTicks();
		memStickNeedsAssign = true;
	}
}

void MemoryStick_NotifyGameName(std::string gameID) {
	const CompatFlags &flags = PSP_CoreParameter().compat.flags();
	if (!flags.MemstickFixedFree) {
		return;
	}

	// See issue #12761
	if (!g_initialMemstickSizePromise) {
		g_initialMemstickSizePromise = Promise<uint64_t>::Spawn(&g_threadManager, [gameID]() -> uint64_t {
			INFO_LOG(Log::System, "Calculating initial savedata size for %s...", gameID.c_str());
			// NOTE: We only really need to sum up the diskspace for subfolders related to this game, and add it to the actual free space,
			// to obtain the free space with the save data removed.
			// We previously went through the meta file system here, but the memstick is always a directory so no need.
			Path saveFolder = GetSysDirectory(DIRECTORY_SAVEDATA);
			int64_t freeSpace = 0;
			free_disk_space(saveFolder, freeSpace);

			// Only add the space of the folders that the game itself touches.
			freeSpace += ComputeSizeOfSavedataForGame(saveFolder, gameID);
			return freeSpace;
		}, TaskType::IO_BLOCKING);
	}
}

void MemoryStick_Init() {
	if (g_Config.bMemStickInserted) {
		memStickState = PSP_MEMORYSTICK_STATE_INSERTED;
		memStickFatState = PSP_FAT_MEMORYSTICK_STATE_ASSIGNED;
	} else {
		memStickState = PSP_MEMORYSTICK_STATE_NOT_INSERTED;
		memStickFatState = PSP_FAT_MEMORYSTICK_STATE_UNASSIGNED;
	}

	memStickNeedsAssign = false;
}

void MemoryStick_Shutdown() {
	if (g_initialMemstickSizePromise) {
		g_initialMemstickSizePromise->BlockUntilReady();
	}
	delete g_initialMemstickSizePromise;
	g_initialMemstickSizePromise = nullptr;
}
