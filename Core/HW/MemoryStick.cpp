#include "Common/ChunkFile.h"
#include "Core/CoreTiming.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HW/MemoryStick.h"
#include "Core/System.h"

// MS and FatMS states.
static MemStickState memStickState;
static MemStickFatState memStickFatState;
static u64 memStickSize;
static bool memStickNeedsAssign = false;
static u64 memStickInsertedAt = 0;

void MemoryStick_DoState(PointerWrap &p) {
	auto s = p.Section("MemoryStick", 1, 3);
	if (!s)
		return;

	p.Do(memStickState);
	p.Do(memStickFatState);
	if (s >= 2)
		p.Do(memStickSize);
	else
		memStickSize = 1ULL * 1024 * 1024 * 1024;
	if (s >= 3) {
		p.Do(memStickNeedsAssign);
		p.Do(memStickInsertedAt);
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
	u64 freeSpace = pspFileSystem.FreeSpace("ms0:/");
	if (freeSpace < memStickSize)
		return freeSpace;
	return memStickSize;
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

	// Harry Potter and the Goblet of Fire has a bug where it can't handle certain amounts
	// of free space due to incorrect 32-bit math.
	// We use 9GB here, which does not trigger the bug, as a cap for the max free space.
	memStickSize = 9ULL * 1024 * 1024 * 1024; // 9GB
	memStickNeedsAssign = false;
}
