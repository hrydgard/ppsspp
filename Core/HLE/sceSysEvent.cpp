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

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MemMap.h"

// Provide a minimal PspSysEventHandler for ME startup code.
// It uses a fixed kernel address and the name "SceMeRpc".
static u32 sceKernelReferSysEventHandler() {
	const u32 handlerAddr = 0x88000100;
	const u32 nameAddr = handlerAddr + 0x40; // name string after struct

	// Write name string "SceMeRpc\0"
	Memory::Write_U32(0x4D656353, nameAddr);     // "SceM" (little-endian: 'S','c','e','M')
	Memory::Write_U32(0x63705265, nameAddr + 4); // "eRpc" (little-endian: 'e','R','p','c')
	Memory::Write_U8(0, nameAddr + 8);           // null terminator

	// Write the fixed handler record.
	Memory::Write_U32(64, handlerAddr);          // size
	Memory::Write_U32(nameAddr, handlerAddr + 4); // name pointer
	Memory::Write_U32(0xFFFF00, handlerAddr + 8); // type_mask
	Memory::Write_U32(0, handlerAddr + 12);       // handler (will be patched by kinit)
	Memory::Write_U32(0, handlerAddr + 16);       // r28
	Memory::Write_U32(0, handlerAddr + 20);       // busy
	Memory::Write_U32(0, handlerAddr + 24);       // next = NULL (end of list)

	return handlerAddr;
}

static u32 sceKernelRegisterSysEventHandler(u32 handler) { return 0; }
static u32 sceKernelUnregisterSysEventHandler(u32 handler) { return 0; }

const HLEFunction sceSysEventForKernel[] = {
	{0X68D55505, &WrapU_V<sceKernelReferSysEventHandler>,           "sceKernelReferSysEventHandler",           'x', ""   },
	{0XCD9E4BB5, &WrapU_U<sceKernelRegisterSysEventHandler>,        "sceKernelRegisterSysEventHandler",        'x', "x"  },
	{0XD7D3FDCD, &WrapU_U<sceKernelUnregisterSysEventHandler>,      "sceKernelUnregisterSysEventHandler",      'x', "x"  },
};

void Register_sceSysEventForKernel() {
	RegisterHLEModule("sceSysEventForKernel", ARRAY_SIZE(sceSysEventForKernel), sceSysEventForKernel);
}
