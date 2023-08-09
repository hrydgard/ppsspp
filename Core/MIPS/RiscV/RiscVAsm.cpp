// Copyright (c) 2023- PPSSPP Project.

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

#include "Common/Log.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/System.h"

namespace MIPSComp {

using namespace RiscVGen;
using namespace RiscVJitConstants;

static const bool enableDebug = false;
static const bool enableDisasm = false;

static void ShowPC(u32 downcount, void *membase, void *jitbase) {
	static int count = 0;
	if (currentMIPS) {
		ERROR_LOG(JIT, "[%08x] ShowPC  Downcount : %08x %d %p %p", currentMIPS->pc, downcount, count, membase, jitbase);
	} else {
		ERROR_LOG(JIT, "Universe corrupt?");
	}
	//if (count > 2000)
	//	exit(0);
	count++;
}

void RiscVJitBackend::GenerateFixedCode(MIPSState *mipsState) {
	BeginWrite(GetMemoryProtectPageSize());
	const u8 *start = AlignCodePage();

	if (jo.useStaticAlloc) {
		saveStaticRegisters_ = AlignCode16();
		SW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		gpr.EmitSaveStaticRegisters();
		RET();

		loadStaticRegisters_ = AlignCode16();
		gpr.EmitLoadStaticRegisters();
		LW(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		RET();

		start = saveStaticRegisters_;
	} else {
		saveStaticRegisters_ = nullptr;
		loadStaticRegisters_ = nullptr;
	}

	applyRoundingMode_ = AlignCode16();
	{
		// Not sure if RISC-V has any flush to zero capability?  Leaving it off for now...
		LWU(SCRATCH2, CTXREG, offsetof(MIPSState, fcr31));

		// We can skip if the rounding mode is nearest (0) and flush is not set.
		// (as restoreRoundingMode cleared it out anyway)
		FixupBranch skip = BEQ(SCRATCH2, R_ZERO);

		// MIPS Rounding Mode:       RISC-V
		//   0: Round nearest        0
		//   1: Round to zero        1
		//   2: Round up (ceil)      3
		//   3: Round down (floor)   2
		if (cpu_info.RiscV_Zbs) {
			BEXTI(SCRATCH1, SCRATCH2, 1);
		} else {
			ANDI(SCRATCH1, SCRATCH2, 2);
			SRLI(SCRATCH1, SCRATCH1, 1);
		}
		// Swap the lowest bit by the second bit.
		XOR(SCRATCH2, SCRATCH2, SCRATCH1);

		FSRM(SCRATCH2);

		SetJumpTarget(skip);
		RET();
	}

	hooks_.enterDispatcher = (IRNativeFuncNoArg)AlignCode16();

	// Start by saving some regs on the stack.  There are 12 GPs and 12 FPs we want.
	// Note: we leave R_SP as, well, SP, so it doesn't need to be saved.
	_assert_msg_(cpu_info.Mode64bit, "RiscVAsm currently assumes RV64, not RV32 or RV128");
	static constexpr RiscVReg regs_to_save[]{ R_RA, X8, X9, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27 };
	// TODO: Maybe we shouldn't regalloc all of these?  Is it worth it?
	static constexpr RiscVReg regs_to_save_fp[]{ F8, F9, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27 };
	int saveSize = (XLEN / 8) * (int)(ARRAY_SIZE(regs_to_save) + ARRAY_SIZE(regs_to_save_fp));
	if (saveSize & 0xF)
		saveSize += 8;
	_assert_msg_((saveSize & 0xF) == 0, "Stack must be kept aligned");
	int saveOffset = 0;
	ADDI(R_SP, R_SP, -saveSize);
	for (RiscVReg r : regs_to_save) {
		SD(r, R_SP, saveOffset);
		saveOffset += XLEN / 8;
	}
	for (RiscVReg r : regs_to_save_fp) {
		FS(64, r, R_SP, saveOffset);
		saveOffset += XLEN / 8;
	}
	_assert_(saveOffset <= saveSize);

	// Fixed registers, these are always kept when in Jit context.
	LI(MEMBASEREG, Memory::base, SCRATCH1);
	LI(CTXREG, mipsState, SCRATCH1);
	LI(JITBASEREG, GetBasePtr(), SCRATCH1);

	LoadStaticRegisters();
	MovFromPC(SCRATCH1);
	outerLoopPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);
	outerLoop_ = GetCodePtr();
	// Advance can change the downcount (or thread), so must save/restore around it.
	SaveStaticRegisters();
	RestoreRoundingMode(true);
	QuickCallFunction(&CoreTiming::Advance, X7);
	ApplyRoundingMode(true);
	LoadStaticRegisters();

	dispatcherCheckCoreState_ = GetCodePtr();
	LI(SCRATCH1, &coreState, SCRATCH2);
	LW(SCRATCH1, SCRATCH1, 0);
	FixupBranch badCoreState = BNE(SCRATCH1, R_ZERO);

	// We just checked coreState, so go to advance if downcount is negative.
	BLT(DOWNCOUNTREG, R_ZERO, outerLoop_);
	FixupBranch skipToRealDispatch = J();

	dispatcherPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);

	hooks_.dispatcher = GetCodePtr();
	FixupBranch bail = BLT(DOWNCOUNTREG, R_ZERO);
	SetJumpTarget(skipToRealDispatch);

	dispatcherNoCheck_ = GetCodePtr();

	// Debug
	if (enableDebug) {
		MV(X10, DOWNCOUNTREG);
		MV(X11, MEMBASEREG);
		MV(X12, JITBASEREG);
		QuickCallFunction(&ShowPC, X7);
	}

	LWU(SCRATCH1, CTXREG, offsetof(MIPSState, pc));
#ifdef MASKED_PSP_MEMORY
	LI(SCRATCH2, 0x3FFFFFFF);
	AND(SCRATCH1, SCRATCH1, SCRATCH2);
#endif
	ADD(SCRATCH1, SCRATCH1, MEMBASEREG);
	hooks_.dispatchFetch = GetCodePtr();
	LWU(SCRATCH1, SCRATCH1, 0);
	SRLI(SCRATCH2, SCRATCH1, 24);
	// We're in other words comparing to the top 8 bits of MIPS_EMUHACK_OPCODE by subtracting.
	ADDI(SCRATCH2, SCRATCH2, -(MIPS_EMUHACK_OPCODE >> 24));
	FixupBranch needsCompile = BNE(SCRATCH2, R_ZERO);
	// Use a wall to mask by 0x00FFFFFF and extract the block jit offset.
	SLLI(SCRATCH1, SCRATCH1, XLEN - 24);
	SRLI(SCRATCH1, SCRATCH1, XLEN - 24);
	ADD(SCRATCH1, JITBASEREG, SCRATCH1);
	JR(SCRATCH1);
	SetJumpTarget(needsCompile);

	// No block found, let's jit.  We don't need to save static regs, they're all callee saved.
	RestoreRoundingMode(true);
	QuickCallFunction(&MIPSComp::JitAt, X7);
	ApplyRoundingMode(true);

	// Try again, the block index should be set now.
	J(dispatcherNoCheck_);

	SetJumpTarget(bail);

	LI(SCRATCH1, &coreState, SCRATCH2);
	LW(SCRATCH1, SCRATCH1, 0);
	BEQ(SCRATCH1, R_ZERO, outerLoop_);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);

	SaveStaticRegisters();
	RestoreRoundingMode(true);

	_assert_msg_(cpu_info.Mode64bit, "RiscVAsm currently assumes RV64, not RV32 or RV128");
	saveOffset = 0;
	for (RiscVReg r : regs_to_save) {
		LD(r, R_SP, saveOffset);
		saveOffset += XLEN / 8;
	}
	for (RiscVReg r : regs_to_save_fp) {
		FL(64, r, R_SP, saveOffset);
		saveOffset += XLEN / 8;
	}
	ADDI(R_SP, R_SP, saveSize);

	RET();

	hooks_.crashHandler = GetCodePtr();
	LI(SCRATCH1, &coreState, SCRATCH2);
	LI(SCRATCH2, CORE_RUNTIME_ERROR);
	SW(SCRATCH2, SCRATCH1, 0);
	J(quitLoop);

	// Leave this at the end, add more stuff above.
	if (enableDisasm) {
#if PPSSPP_ARCH(RISCV64)
		std::vector<std::string> lines = DisassembleRV64(start, GetCodePtr() - start);
		for (auto s : lines) {
			INFO_LOG(JIT, "%s", s.c_str());
		}
#endif
	}

	// Let's spare the pre-generated code from unprotect-reprotect.
	AlignCodePage();
	jitStartOffset_ = (int)(GetCodePtr() - start);
	// Don't forget to zap the instruction cache! This must stay at the end of this function.
	FlushIcache();
	EndWrite();
}

} // namespace MIPSComp
