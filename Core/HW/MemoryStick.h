#pragma once

#include "Common/CommonTypes.h"

class PointerWrap;

// mscmhc0 states (status of the card.)
enum MemStickState {
	PSP_MEMORYSTICK_STATE_INSERTED        = 1,
	PSP_MEMORYSTICK_STATE_NOT_INSERTED    = 2,
};

// memstick FAT states (status of mounting.)
enum MemStickFatState {
	PSP_FAT_MEMORYSTICK_STATE_UNASSIGNED = 0,
	PSP_FAT_MEMORYSTICK_STATE_ASSIGNED   = 1,
};

enum MemStickDriverState {
	PSP_MEMORYSTICK_STATE_DRIVER_READY    = 1,
	PSP_MEMORYSTICK_STATE_DRIVER_BUSY     = 2,
	PSP_MEMORYSTICK_STATE_DEVICE_INSERTED = 4,
	PSP_MEMORYSTICK_STATE_DEVICE_REMOVED  = 8,
};

void MemoryStick_Init();
void MemoryStick_DoState(PointerWrap &p);
MemStickState MemoryStick_State();
MemStickFatState MemoryStick_FatState();

void MemoryStick_SetState(MemStickState state);
void MemoryStick_SetFatState(MemStickFatState state);

u64 MemoryStick_SectorSize();
u64 MemoryStick_FreeSpace();
