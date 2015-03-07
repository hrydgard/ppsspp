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

#include "base/logging.h"

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

static const bool enableDebug = true;

//static bool enableStatistics = false; //unused?


// ARM64 calling conventions
// Standard: http://infocenter.arm.com/help/topic/com.arm.doc.ihi0055b/IHI0055B_aapcs64.pdf
// Apple: https://developer.apple.com/library/ios/documentation/Xcode/Conceptual/iPhoneOSABIReference/Articles/ARM64FunctionCallingConventions.html

// Summary:
// ===========
// SP ("x31") is not a GPR so irrelevant.
// x0-x7: 8 parameter/result registers
// x8: "Indirect result location register" (points to struct return values? I think we can map this)
// x9-x15: 7 temporary registers (no need to save)
// x16: temporary register/procedure call scratch register 1
// x17: temporary register/procedure call scratch register 2
// x18: unavailable (reserved for use by the OS or linker or whatever - iOS, for example, uses it)
// x19-x28: 10 callee-saved registers
// x29: the frame pointer register
// x30: link register for procedure calls

// r15 is the program counter.
// r14 is the link register. (The BL instruction, used in a subroutine call, stores the return address in this register).
// r13 is the stack pointer. (The Push/Pop instructions in "Thumb" operating mode use this register only).
// r12 is the Intra-Procedure-call scratch register.
// r4 to r11: used to hold local variables.
// r0 to r3: used to hold argument values passed to a subroutine, and also hold results returned from a subroutine.

// So: Scratch registers: x16, x17
// Mappable registers in priority order:
//	 x19, x20, x21, x22, x23, x24, x25, x27, x28, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x0, x1,
// That's a whole lot of registers so we might be able to statically allocate a bunch of common MIPS registers.
// We should put statically allocated registers in the 7 callee-save regs that are left over after the system regs (x19-x25), so we don't have to bother with
// saving them when we call out of the JIT. We will perform regular dynamic register allocation in the rest (x0-x15)

// STATIC ALLOCATION ARM64 (these are all callee-save registers):
// x25 : MSR/MRS temporary (to be eliminated later)
// x26 : JIT base reg
// x27 : MIPS state (Could eliminate by placing the MIPS state right at the memory base)
// x28 : Memory base pointer.
// x29 : Down counter

extern volatile CoreState coreState;

void ShowPC(u32 sp) {
	static int count = 0;
	if (currentMIPS) {
		ELOG("ShowPC : %08x  ArmSP : %08x %d", currentMIPS->pc, sp, count);
	} else {
		ELOG("Universe corrupt?");
	}
	//if (count > 2000)
	//	exit(0);
	count++;
}

void DisassembleArm(const u8 *data, int size);

// PLAN: no more block numbers - crazy opcodes just contain offset within
// dynarec buffer
// At this offset - 4, there is an int specifying the block number.

namespace MIPSComp {

using namespace Arm64JitConstants;

void Arm64Jit::GenerateFixedCode() {
	enterCode = AlignCode16();

	const u32 ALL_CALLEE_SAVED = 0x7FF80000;
	BitSet32 regs_to_save(ALL_CALLEE_SAVED);
	enterCode = GetCodePtr();

	ABI_PushRegisters(regs_to_save);

	// Fixed registers, these are always kept when in Jit context.
	// R8 is used to hold flags during delay slots. Not always needed.
	// R13 cannot be used as it's the stack pointer.
	// TODO: Consider statically allocating:
	//   * r2-r4
	// Really starting to run low on registers already though...

	// R11, R10, R9
	MOVP2R(MEMBASEREG, Memory::base);
	MOVP2R(CTXREG, mips_);
	MOVP2R(JITBASEREG, GetBasePtr());

	// TODO: Preserve ASIMD registers

	RestoreDowncount();
	MovFromPC(SCRATCH1);
	outerLoopPCInSCRATCH1 = GetCodePtr();
	MovToPC(SCRATCH1);
	outerLoop = GetCodePtr();
		SaveDowncount();
		RestoreRoundingMode(true);
		QuickCallFunction(SCRATCH1_64, &CoreTiming::Advance);
		ApplyRoundingMode(true);
		RestoreDowncount();
		FixupBranch skipToRealDispatch = B(); //skip the sync and compare first time

		dispatcherCheckCoreState = GetCodePtr();

		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bailCoreState = B(CC_MI);

		MOVP2R(SCRATCH1_64, &coreState);
		LDR(INDEX_UNSIGNED, SCRATCH1, SCRATCH1_64, 0);
		CMP(SCRATCH1, 0);
		FixupBranch badCoreState = B(CC_NEQ);
		FixupBranch skipToRealDispatch2 = B(); //skip the sync and compare first time

		dispatcherPCInSCRATCH1 = GetCodePtr();
		// TODO: Do we always need to write PC to RAM here?
		MovToPC(SCRATCH1);

		// At this point : flags = EQ. Fine for the next check, no need to jump over it.
		dispatcher = GetCodePtr();

			// The result of slice decrementation should be in flags if somebody jumped here
			// IMPORTANT - We jump on negative, not carry!!!
			FixupBranch bail = B(CC_MI);

			SetJumpTarget(skipToRealDispatch);
			SetJumpTarget(skipToRealDispatch2);

			dispatcherNoCheck = GetCodePtr();

			// Debug
			if (enableDebug) {
				QuickCallFunction(SCRATCH1, (void *)&ShowPC);
			}

			LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, pc));
			LDR(SCRATCH1, MEMBASEREG, SCRATCH1_64);
			ANDI2R(SCRATCH2, SCRATCH1, 0xFF000000);   // rotation is to the right, in 2-bit increments.
			ANDI2R(SCRATCH1, SCRATCH1, 0x00FFFFFF);  // TODO: Replace this and the next op by a bit field extract
			LSR(SCRATCH2, SCRATCH2, 24);
			CMP(SCRATCH2, MIPS_EMUHACK_OPCODE>>24);
			FixupBranch skipJump = B(CC_NEQ);
				// IDEA - we have 26 bits, why not just use offsets from base of code?
				// Another idea: Shift the bloc number left by two in the op, this would let us do
				// LDR(R0, R9, R0); here, replacing the next instructions.
#ifdef IOS
				// On iOS, R9 (JITBASEREG) is volatile.  We have to reload it.
				MOVI2R(JITBASEREG, (uintptr_t)GetBasePtr());
#endif
				ADD(SCRATCH1_64, SCRATCH1_64, JITBASEREG);
				BR(SCRATCH1_64);
			SetJumpTarget(skipJump);
			// No block found, let's jit
			SaveDowncount();
			RestoreRoundingMode(true);
			QuickCallFunction(SCRATCH1_64, (void *)&MIPSComp::JitAt);
			ApplyRoundingMode(true);
			RestoreDowncount();

			B(dispatcherNoCheck); // no point in special casing this

		SetJumpTarget(bail);
		SetJumpTarget(bailCoreState);

		MOVP2R(SCRATCH1_64, &coreState);
		LDR(INDEX_UNSIGNED, SCRATCH1, SCRATCH1_64, 0);
		CMP(SCRATCH1, 0);
		B(CC_EQ, outerLoop);

	SetJumpTarget(badCoreState);
	breakpointBailout = GetCodePtr();

	// TODO: Restore ASIMD registers

	SaveDowncount();
	RestoreRoundingMode(true);

	ABI_PopRegisters(regs_to_save);

	INFO_LOG(JIT, "THE DISASM : %p ========================", enterCode);
	std::vector<std::string> lines = DisassembleArm64(enterCode, GetCodePtr() - enterCode);
	for (auto s : lines) {
		INFO_LOG(JIT, "%s", s.c_str());
	}
	INFO_LOG(JIT, "END OF THE DISASM : %p ========================", GetCodePtr());

	// Don't forget to zap the instruction cache!
	FlushIcache();
}

}  // namespace MIPSComp
