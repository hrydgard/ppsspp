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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM64)

#include "base/logging.h"

#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"
#include "Core/CoreTiming.h"
#include "Common/MemoryUtil.h"
#include "Common/CPUDetect.h"
#include "Common/Arm64Emitter.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

using namespace Arm64Gen;

//static int temp32; // unused?

static const bool enableDebug = false;
static const bool enableDisasm = false;

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

// So: Scratch registers: x16, x17
// Mappable registers in priority order:
//	 x19, x20, x21, x22, x23, (x24, x25, x26, x27, x28), x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x0, x1,
// That's a whole lot of registers so we might be able to statically allocate a bunch of common MIPS registers.
// We should put statically allocated registers in the 7 callee-save regs that are left over after the system regs (x19-x25), so we don't have to bother with
// saving them when we call out of the JIT. We will perform regular dynamic register allocation in the rest (x0-x15)

// STATIC ALLOCATION ARM64 (these are all callee-save registers):
// x23 : Down counter
// x24 : PC save on JR with non-nice delay slot (to be eliminated later?)
// x25 : MSR/MRS temporary (to be eliminated later)
// x26 : JIT base reg
// x27 : MIPS state (Could eliminate by placing the MIPS state right at the memory base)
// x28 : Memory base pointer.

extern volatile CoreState coreState;

void ShowPC(u32 downcount, void *membase, void *jitbase) {
	static int count = 0;
	if (currentMIPS) {
		ELOG("ShowPC : %08x  Downcount : %08x %d %p %p", currentMIPS->pc, downcount, count, membase, jitbase);
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

void Arm64Jit::GenerateFixedCode(const JitOptions &jo) {
	const u8 *start = AlignCodePage();
	BeginWrite();

	if (jo.useStaticAlloc) {
		saveStaticRegisters = AlignCode16();
		STR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		gpr.EmitSaveStaticRegisters();
		RET();

		loadStaticRegisters = AlignCode16();
		gpr.EmitLoadStaticRegisters();
		LDR(INDEX_UNSIGNED, DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		RET();

		start = saveStaticRegisters;
	} else {
		saveStaticRegisters = nullptr;
		loadStaticRegisters = nullptr;
	}

	restoreRoundingMode = AlignCode16(); {
		MRS(SCRATCH2_64, FIELD_FPCR);
		// We are not in flush-to-zero mode outside the JIT, so let's turn it off.
		uint32_t mask = ~(4 << 22);
		// Assume we're always in round-to-nearest mode beforehand.
		mask &= ~(3 << 22);
		ANDI2R(SCRATCH2, SCRATCH2, mask);
		_MSR(FIELD_FPCR, SCRATCH2_64);
		RET();
	}

	applyRoundingMode = AlignCode16(); {
		LDR(INDEX_UNSIGNED, SCRATCH2, CTXREG, offsetof(MIPSState, fcr31));
		TSTI2R(SCRATCH2, 1 << 24);
		ANDI2R(SCRATCH2, SCRATCH2, 3);
		FixupBranch skip1 = B(CC_EQ);
		ADDI2R(SCRATCH2, SCRATCH2, 4);
		SetJumpTarget(skip1);

		// We can skip if the rounding mode is nearest (0) and flush is not set.
		// (as restoreRoundingMode cleared it out anyway)
		CMPI2R(SCRATCH2, 0);
		FixupBranch skip = B(CC_EQ);

		// MIPS Rounding Mode:       ARM Rounding Mode
		//   0: Round nearest        0
		//   1: Round to zero        3
		//   2: Round up (ceil)      1
		//   3: Round down (floor)   2
		ANDI2R(SCRATCH1, SCRATCH2, 3);
		CMPI2R(SCRATCH1, 1);

		FixupBranch skipadd = B(CC_NEQ);
		ADDI2R(SCRATCH2, SCRATCH2, 2);
		SetJumpTarget(skipadd);
		FixupBranch skipsub = B(CC_LE);
		SUBI2R(SCRATCH2, SCRATCH2, 1);
		SetJumpTarget(skipsub);

		// Actually change the system FPCR register
		MRS(SCRATCH1_64, FIELD_FPCR);
		// Clear both flush-to-zero and rounding before re-setting them.
		ANDI2R(SCRATCH1, SCRATCH1, ~((4 | 3) << 22));
		ORR(SCRATCH1, SCRATCH1, SCRATCH2, ArithOption(SCRATCH2, ST_LSL, 22));
		_MSR(FIELD_FPCR, SCRATCH1_64);

		SetJumpTarget(skip);
		RET();
	}

	updateRoundingMode = AlignCode16(); {
		LDR(INDEX_UNSIGNED, SCRATCH2, CTXREG, offsetof(MIPSState, fcr31));

		// Set SCRATCH2 to FZ:RM (FZ is bit 24, and RM are lowest 2 bits.)
		TSTI2R(SCRATCH2, 1 << 24);
		ANDI2R(SCRATCH2, SCRATCH2, 3);
		FixupBranch skip = B(CC_EQ);
		ADDI2R(SCRATCH2, SCRATCH2, 4);
		SetJumpTarget(skip);

		// Let's update js.currentRoundingFunc with the right convertS0ToSCRATCH1 func.
		MOVP2R(SCRATCH1_64, convertS0ToSCRATCH1);
		LSL(SCRATCH2, SCRATCH2, 3);
		LDR(SCRATCH2_64, SCRATCH1_64, SCRATCH2);
		MOVP2R(SCRATCH1_64, &js.currentRoundingFunc);
		STR(INDEX_UNSIGNED, SCRATCH2_64, SCRATCH1_64, 0);
		RET();
	}

	enterDispatcher = AlignCode16();

	uint32_t regs_to_save = Arm64Gen::ALL_CALLEE_SAVED;
	uint32_t regs_to_save_fp = Arm64Gen::ALL_CALLEE_SAVED_FP;
	fp.ABI_PushRegisters(regs_to_save, regs_to_save_fp);

	// Fixed registers, these are always kept when in Jit context.
	MOVP2R(MEMBASEREG, Memory::base);
	MOVP2R(CTXREG, mips_);
	MOVP2R(JITBASEREG, GetBasePtr());

	LoadStaticRegisters();
	MovFromPC(SCRATCH1);
	outerLoopPCInSCRATCH1 = GetCodePtr();
	MovToPC(SCRATCH1);
	outerLoop = GetCodePtr();
		SaveStaticRegisters();  // Advance can change the downcount, so must save/restore
		RestoreRoundingMode(true);
		QuickCallFunction(SCRATCH1_64, &CoreTiming::Advance);
		ApplyRoundingMode(true);
		LoadStaticRegisters();
		FixupBranch skipToCoreStateCheck = B();  //skip the downcount check

		dispatcherCheckCoreState = GetCodePtr();

		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bailCoreState = B(CC_MI);

		SetJumpTarget(skipToCoreStateCheck);

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

			SetJumpTarget(skipToRealDispatch2);

			dispatcherNoCheck = GetCodePtr();

			// Debug
			if (enableDebug) {
				MOV(W0, DOWNCOUNTREG);
				MOV(X1, MEMBASEREG);
				MOV(X2, JITBASEREG);
				QuickCallFunction(SCRATCH1_64, (void *)&ShowPC);
			}

			LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, pc));
#ifdef MASKED_PSP_MEMORY
			ANDI2R(SCRATCH1, SCRATCH1, 0x3FFFFFFF);
#endif
			LDR(SCRATCH1, MEMBASEREG, SCRATCH1_64);
			LSR(SCRATCH2, SCRATCH1, 24);   // or UBFX(SCRATCH2, SCRATCH1, 24, 8)
			ANDI2R(SCRATCH1, SCRATCH1, 0x00FFFFFF);
			CMP(SCRATCH2, MIPS_EMUHACK_OPCODE >> 24);
			FixupBranch skipJump = B(CC_NEQ);
				ADD(SCRATCH1_64, JITBASEREG, SCRATCH1_64);
				BR(SCRATCH1_64);
			SetJumpTarget(skipJump);

			// No block found, let's jit. I don't think we actually need to save static regs that are in callee-save regs here but whatever.
			// Also, rounding mode gotta be irrelevant here..
			SaveStaticRegisters();
			RestoreRoundingMode(true);
			QuickCallFunction(SCRATCH1_64, (void *)&MIPSComp::JitAt);
			ApplyRoundingMode(true);
			LoadStaticRegisters();

			B(dispatcherNoCheck); // no point in special casing this

		SetJumpTarget(bail);
		SetJumpTarget(bailCoreState);

		MOVP2R(SCRATCH1_64, &coreState);
		LDR(INDEX_UNSIGNED, SCRATCH1, SCRATCH1_64, 0);
		CMP(SCRATCH1, 0);
		B(CC_EQ, outerLoop);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);

	SaveStaticRegisters();
	RestoreRoundingMode(true);

	fp.ABI_PopRegisters(regs_to_save, regs_to_save_fp);

	RET();

	crashHandler = GetCodePtr();
	MOVP2R(SCRATCH1_64, &coreState);
	MOVI2R(SCRATCH2, CORE_ERROR);
	STR(INDEX_UNSIGNED, SCRATCH2, SCRATCH1_64, 0);
	B(quitLoop);

	// Generate some integer conversion funcs.
	// MIPS order!
	static const RoundingMode roundModes[8] = { ROUND_N, ROUND_Z, ROUND_P, ROUND_M, ROUND_N, ROUND_Z, ROUND_P, ROUND_M };
	for (size_t i = 0; i < ARRAY_SIZE(roundModes); ++i) {
		convertS0ToSCRATCH1[i] = AlignCode16();

		fp.FCMP(S0, S0);  // Detect NaN
		fp.FCVTS(S0, S0, roundModes[i]);
		FixupBranch skip = B(CC_VC);
		MOVI2R(SCRATCH2, 0x7FFFFFFF);
		fp.FMOV(S0, SCRATCH2);
		SetJumpTarget(skip);

		RET();
	}

	// Leave this at the end, add more stuff above.
	if (enableDisasm) {
		std::vector<std::string> lines = DisassembleArm64(start, GetCodePtr() - start);
		for (auto s : lines) {
			INFO_LOG(JIT, "%s", s.c_str());
		}
	}

	// Let's spare the pre-generated code from unprotect-reprotect.
	AlignCodePage();
	jitStartOffset = (int)(GetCodePtr() - start);
	// Don't forget to zap the instruction cache! This must stay at the end of this function.
	FlushIcache();
	EndWrite();
}

}  // namespace MIPSComp

#endif // PPSSPP_ARCH(ARM64)
