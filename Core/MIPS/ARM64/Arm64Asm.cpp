// Copyright (c) 2015- PPSSPP Project.

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


#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Common/MemoryUtil.h"
#include "Common/CPUDetect.h"
#include "Common/Arm64Emitter.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64Asm.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

using namespace Arm64Gen;

//static int temp32; // unused?

static const bool enableDebug = false;

//static bool enableStatistics = false; //unused?

//The standard ARM calling convention allocates the 16 ARM registers as:

// r15 is the program counter.
// r14 is the link register. (The BL instruction, used in a subroutine call, stores the return address in this register).
// r13 is the stack pointer. (The Push/Pop instructions in "Thumb" operating mode use this register only).
// r12 is the Intra-Procedure-call scratch register.
// r4 to r11: used to hold local variables.
// r0 to r3: used to hold argument values passed to a subroutine, and also hold results returned from a subroutine.

// Mappable registers:
//	 R2, R3, R4, R5, R6, R8, R11

// STATIC ALLOCATION ARM:
// R10 : MIPS state
// R11 : Memory base pointer.
// R7 :  Down counter
extern volatile CoreState coreState;

void ShowPC(u32 sp) {
	if (currentMIPS) {
		ERROR_LOG(JIT, "ShowPC : %08x  ArmSP : %08x", currentMIPS->pc, sp);
	} else {
		ERROR_LOG(JIT, "Universe corrupt?");
	}
}

void DisassembleArm(const u8 *data, int size);

// PLAN: no more block numbers - crazy opcodes just contain offset within
// dynarec buffer
// At this offset - 4, there is an int specifying the block number.

namespace MIPSComp {

using namespace Arm64JitConstants;

void Arm64Jit::GenerateFixedCode()
{

	// Uncomment if you want to see the output...
	// INFO_LOG(JIT, "THE DISASM ========================");
	// DisassembleArm(enterCode, GetCodePtr() - enterCode);
	// INFO_LOG(JIT, "END OF THE DISASM ========================");

	// Don't forget to zap the instruction cache!
	FlushIcache();
}

}  // namespace MIPSComp
