#include "MemoryStick.h"

// MS and FatMS states.
static MemStickState memStickState = PSP_MEMORYSTICK_STATE_DRIVER_READY;
static MemStickFatState memStickFatState = PSP_FAT_MEMORYSTICK_STATE_ASSIGNED;

MemStickState MemoryStick_State()
{
	return memStickState;
}

MemStickFatState MemoryStick_FatState()
{
	return memStickFatState;
}

u64 MemoryStick_SectorSize()
{
	return 32 * 1024; // 32KB
}

u64 MemoryStick_FreeSpace()
{
	return 1ULL * 1024 * 1024 * 1024; // 1GB
}