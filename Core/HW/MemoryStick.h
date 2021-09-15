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
void MemoryStick_Shutdown();
void MemoryStick_DoState(PointerWrap &p);
MemStickState MemoryStick_State();
MemStickFatState MemoryStick_FatState();

void MemoryStick_SetState(MemStickState state);
void MemoryStick_SetFatState(MemStickFatState state);

u64 MemoryStick_SectorSize();
u64 MemoryStick_FreeSpace();
void MemoryStick_NotifyWrite();
