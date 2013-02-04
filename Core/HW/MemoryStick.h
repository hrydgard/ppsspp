#include "../../Globals.h"

class PointerWrap;

// mscmhc0 states
enum MemStickState {
	PSP_MEMORYSTICK_STATE_DRIVER_READY = 1,
	PSP_MEMORYSTICK_STATE_DRIVER_BUSY = 2,
	PSP_MEMORYSTICK_STATE_DEVICE_INSERTED = 4,
	PSP_MEMORYSTICK_STATE_DEVICE_REMOVED = 8,
};

// memstick FAT states.
enum MemStickFatState {
	PSP_FAT_MEMORYSTICK_STATE_UNASSIGNED = 0,
	PSP_FAT_MEMORYSTICK_STATE_ASSIGNED = 1,
};

void MemoryStick_DoState(PointerWrap &p);
MemStickState MemoryStick_State();
MemStickFatState MemoryStick_FatState();

void MemoryStick_SetFatState(MemStickFatState state);

u64 MemoryStick_SectorSize();
u64 MemoryStick_FreeSpace();