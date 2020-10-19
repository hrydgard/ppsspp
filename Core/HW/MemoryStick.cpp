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
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/Compatibility.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HW/MemoryStick.h"
#include "Core/System.h"

// MS and FatMS states.
static MemStickState memStickState;
static MemStickFatState memStickFatState;
static bool memStickNeedsAssign = false;
static u64 memStickInsertedAt = 0;

const u64 normalMemstickSize = 9ULL * 1024 * 1024 * 1024;
const u64 smallMemstickSize = 1ULL * 1024 * 1024 * 1024;

void MemoryStick_DoState(PointerWrap &p) {
	auto s = p.Section("MemoryStick", 1, 4);
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

u64 MemoryStick_FreeSpace() {
	u64 realFreeSpace = pspFileSystem.FreeSpace("ms0:/");

	// Cap the memory stick size to avoid math errors when old games get sizes that were
	// not planned for back then (even though 2GB cards were available.)
	// We have a compat setting to make it even smaller for Harry Potter : Goblet of Fire, see #13266.
	const u64 memStickSize = PSP_CoreParameter().compat.flags().ReportSmallMemstick ? smallMemstickSize : (u64)g_Config.iMemStickSizeGB * 1024 * 1024 * 1024;
	// Assume the memory stick is only used to store savedata.
	u64 usedSpace = pspFileSystem.getDirSize("ms0:/PSP/SAVEDATA/");
	u64 simulatedFreeSpace = 0;
	if (usedSpace < memStickSize) {
		simulatedFreeSpace = memStickSize - usedSpace;
	}

	return std::min(simulatedFreeSpace, realFreeSpace);
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
