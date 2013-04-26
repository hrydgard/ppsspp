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


#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "MemoryUtil.h"

#include "ArmEmitter.h"
#include "ArmJit.h"
#include "../JitCommon/JitCommon.h"
#include "ArmAsm.h"

using namespace ArmGen;

//static int temp32; // unused?

//TODO - make an option
//#if _DEBUG
static bool enableDebug = false; 
//#else
//		bool enableDebug = false; 
//#endif

//static bool enableStatistics = false; //unused?

//The standard ARM calling convention allocates the 16 ARM registers as:

// r15 is the program counter.
// r14 is the link register. (The BL instruction, used in a subroutine call, stores the return address in this register).
// r13 is the stack pointer. (The Push/Pop instructions in "Thumb" operating mode use this register only).
// r12 is the Intra-Procedure-call scratch register.
// r4 to r11: used to hold local variables.
// r0 to r3: used to hold argument values passed to a subroutine, and also hold results returned from a subroutine.

//	 R2, R3, R4, R5, R6, R7, R8, R10, R11	 // omitting R9?


// STATIC ALLOCATION ARM:
// R11 : Memory base pointer.
// R10 : MIPS state
// R9 : 
extern volatile CoreState coreState;

void JitAt()
{
	MIPSComp::jit->Compile(currentMIPS->pc);
}

/*
double testD;

u32 DoubleTest(u32 sp) {
	volatile double local = 1.0;
	testD += local;
	return (u32)(&local);
}

void ShowPC(u32 sp) {
	if (currentMIPS) {
		ERROR_LOG(HLE, "ShowPC : %08x  ArmSP : %08x", currentMIPS->pc, sp);
	} else {
		ERROR_LOG(HLE, "Universe corrupt?");
	}
}*/

void DisassembleArm(const u8 *data, int size);

// PLAN: no more block numbers - crazy opcodes just contain offset within
// dynarec buffer
// At this offset - 4, there is an int specifying the block number.

namespace MIPSComp {

void Jit::GenerateFixedCode()
{
	enterCode = AlignCode16();

	INFO_LOG(HLE, "Base: %08x", (u32)Memory::base);

	SetCC(CC_AL);

	PUSH(9, R4, R5, R6, R7, R8, R9, R10, R11, _LR);
	// Take care to 8-byte align stack for function calls.
	// We are misaligned here because of an odd number of args for PUSH.
	// It's not like x86 where you need to account for an extra 4 bytes
	// consumed by CALL.
	SUB(_SP, _SP, 4);
	// Now we are correctly aligned and plan to stay that way.

	// TODO: R12 should be usable for regalloc but will get thrashed by C code.

	// Fixed registers, these are always kept when in Jit context.
	// R8 is used to hold flags during delay slots. Not always needed.
	// R13 cannot be used as it's the stack pointer.
	// TODO: Consider statically allocating:
	//   * r2-r4
	// Really starting to run low on registers already though...

	MOVI2R(R11, (u32)Memory::base);
	MOVI2R(R10, (u32)mips_);
	MOVI2R(R9, (u32)GetBasePtr());

	MovFromPC(R0);
	outerLoopPCInR0 = GetCodePtr();
	MovToPC(R0);
	outerLoop = GetCodePtr();
		QuickCallFunction(R0, (void *)&CoreTiming::Advance);
		FixupBranch skipToRealDispatch = B(); //skip the sync and compare first time

		dispatcherCheckCoreState = GetCodePtr();

		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bailCoreState = B_CC(CC_MI);

		MOVI2R(R0, (u32)&coreState);
		LDR(R0, R0);
		CMP(R0, 0);
		FixupBranch badCoreState = B_CC(CC_NEQ);
		FixupBranch skipToRealDispatch2 = B(); //skip the sync and compare first time

		dispatcherPCInR0 = GetCodePtr();
		MovToPC(R0);

		// At this point : flags = EQ. Fine for the next check, no need to jump over it.
		dispatcher = GetCodePtr();

			// The result of slice decrementation should be in flags if somebody jumped here
			// IMPORTANT - We jump on negative, not carry!!!
			FixupBranch bail = B_CC(CC_MI);

			SetJumpTarget(skipToRealDispatch);
			SetJumpTarget(skipToRealDispatch2);

			dispatcherNoCheck = GetCodePtr();

			// Debug
			// MOV(R0, R13);
			// QuickCallFunction(R1, (void *)&ShowPC);

			LDR(R0, CTXREG, offsetof(MIPSState, pc));
			BIC(R0, R0, Operand2(0xC0, 4));   // &= 0x3FFFFFFF
			LDR(R0, R11, R0);
			AND(R1, R0, Operand2(0xFC, 4));   // rotation is to the right, in 2-bit increments.
			BIC(R0, R0, Operand2(0xFC, 4));
			CMP(R1, Operand2(MIPS_EMUHACK_OPCODE >> 24, 4));
			SetCC(CC_EQ);
				// IDEA - we have 26 bits, why not just use offsets from base of code?
				// Another idea: Shift the bloc number left by two in the op, this would let us do
				// LDR(R0, R9, R0); here, replacing the next instructions.
#ifdef IOS
				// TODO: Fix me, I'm ugly.
				MOVI2R(R9, (u32)GetBasePtr());
#endif
				ADD(R0, R0, R9);
				B(R0);
			SetCC(CC_AL);

			//Ok, no block, let's jit
			QuickCallFunction(R2, (void *)&JitAt);

			B(dispatcherNoCheck); // no point in special casing this

		SetJumpTarget(bail);
		SetJumpTarget(bailCoreState);

		MOVI2R(R0, (u32)&coreState);
		LDR(R0, R0);
		CMP(R0, 0);
		B_CC(CC_EQ, outerLoop);

	SetJumpTarget(badCoreState);

	breakpointBailout = GetCodePtr();

	ADD(_SP, _SP, 4);

	POP(9, R4, R5, R6, R7, R8, R9, R10, R11, _PC);  // Returns

	// Uncomment if you want to see the output...
	// INFO_LOG(HLE, "THE DISASM ========================");
	// DisassembleArm(enterCode, GetCodePtr() - enterCode);
	// INFO_LOG(HLE, "END OF THE DISASM ========================");

	// Don't forget to zap the instruction cache!
	FlushLitPool();
	FlushIcache();
}

}  // namespace
