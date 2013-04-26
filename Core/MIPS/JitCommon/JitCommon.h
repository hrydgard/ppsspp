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

#include "Common/Common.h"

#if defined(ARM)
#include "../ARM/ArmJit.h"
#else
#include "../x86/Jit.h"
#endif

// Unlike on the PPC, opcode 0 is not unused and thus we have to choose another fake
// opcode to represent JIT blocks and other emu hacks.
// I've chosen 0x68000000.

#define MIPS_EMUHACK_OPCODE 0x68000000
#define MIPS_EMUHACK_MASK 0xFC000000
#define MIPS_EMUHACK_VALUE_MASK 0x03FFFFFF

#define MIPS_IS_EMUHACK(op) (((op) & 0xFC000000) == MIPS_EMUHACK_OPCODE)  // masks away the subop


// There are 2 bits available for sub-opcodes, 0x03000000.
#define EMUOP_RUNBLOCK 0   // Runs a JIT block
#define EMUOP_RETKERNEL 1  // Returns to the simulated PSP kernel from a thread

namespace MIPSComp {
extern Jit *jit;
}
