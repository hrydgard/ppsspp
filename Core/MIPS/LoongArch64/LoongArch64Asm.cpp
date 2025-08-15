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
#include "Core/MIPS/LoongArch64/LoongArch64Jit.h"
#include "Core/MIPS/LoongArch64/LoongArch64RegCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/Core.h"

namespace MIPSComp {

using namespace LoongArch64Gen;
using namespace LoongArch64JitConstants;

static const bool enableDebug = false;
static const bool enableDisasm = false;

static void ShowPC(u32 downcount, void *membase, void *jitbase) {
	static int count = 0;
	if (currentMIPS) {
		ERROR_LOG(Log::JIT, "[%08x] ShowPC Downcount : %08x %d %p %p", currentMIPS->pc, downcount, count, membase, jitbase);
	} else {
		ERROR_LOG(Log::JIT, "Universe corrupt?");
	}
	count++;
}

void LoongArch64JitBackend::GenerateFixedCode(MIPSState *mipsState) {
	// This will be used as a writable scratch area, always 32-bit accessible.
	const u8 *start = AlignCodePage();
	if (DebugProfilerEnabled()) {
		ProtectMemoryPages(start, GetMemoryProtectPageSize(), MEM_PROT_READ | MEM_PROT_WRITE);
		hooks_.profilerPC = (uint32_t *)GetWritableCodePtr();
		*hooks_.profilerPC = 0;
		hooks_.profilerStatus = (IRProfilerStatus *)GetWritableCodePtr() + 1;
		*hooks_.profilerStatus = IRProfilerStatus::NOT_RUNNING;
		SetCodePointer(GetCodePtr() + sizeof(uint32_t) * 2, GetWritableCodePtr() + sizeof(uint32_t) * 2);
	}

	const u8 *disasmStart = AlignCodePage();
	BeginWrite(GetMemoryProtectPageSize());
	if (jo.useStaticAlloc) {
		saveStaticRegisters_ = AlignCode16();
		ST_W(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		regs_.EmitSaveStaticRegisters();
		RET();

		loadStaticRegisters_ = AlignCode16();
		regs_.EmitLoadStaticRegisters();
		LD_W(DOWNCOUNTREG, CTXREG, offsetof(MIPSState, downcount));
		RET();
	} else {
		saveStaticRegisters_ = nullptr;
		loadStaticRegisters_ = nullptr;
	}

	applyRoundingMode_ = AlignCode16();
	{
		// LoongArch64 does not have any flush to zero capability, so leaving it off
		LD_WU(SCRATCH2, CTXREG, offsetof(MIPSState, fcr31));
		// We have to do this because otherwise, FMUL will output inaccurate results,
		// which cause some game going into infinite loop, for example PATAPON.
		ANDI(SCRATCH2, SCRATCH2, 3);
		SLLI_D(SCRATCH2, SCRATCH2, 8);

		// We can skip if the rounding mode is nearest (0) and flush is not set.
		// (as restoreRoundingMode cleared it out anyway)
		FixupBranch skip = BEQZ(SCRATCH2);

		// MIPS Rounding Mode:       LoongArch64
		//   0: Round nearest        0 RNE
		//   1: Round to zero        1 RZ
		//   2: Round up (ceil)      2 RP
		//   3: Round down (floor)   3 RM
		MOVGR2FCSR(FCSR3, SCRATCH2);

		SetJumpTarget(skip);
		RET();
	}

	hooks_.enterDispatcher = (IRNativeFuncNoArg)AlignCode16();

	// Start by saving some regs on the stack.  There are 11 GPs and 8 FPs we want.
	// Note: we leave R_SP as, well, SP, so it doesn't need to be saved.
	static constexpr LoongArch64Reg regs_to_save[]{ R_RA, R22, R23, R24, R25, R26, R27, R28, R29, R30, R31 };
	// TODO: Maybe we shouldn't regalloc all of these?  Is it worth it?
	static constexpr LoongArch64Reg regs_to_save_fp[]{ F24, F25, F26, F27, F28, F29, F30, F31 };
	int saveSize = (64 / 8) * (int)(ARRAY_SIZE(regs_to_save) + ARRAY_SIZE(regs_to_save_fp));
	if (saveSize & 0xF)
		saveSize += 8;
	_assert_msg_((saveSize & 0xF) == 0, "Stack must be kept aligned");
	int saveOffset = 0;
	ADDI_D(R_SP, R_SP, -saveSize);
	for (LoongArch64Reg r : regs_to_save) {
		ST_D(r, R_SP, saveOffset);
		saveOffset += 64 / 8;
	}
	for (LoongArch64Reg r : regs_to_save_fp) {
		FST_D(r, R_SP, saveOffset);
		saveOffset += 64 / 8;
	}
	_assert_(saveOffset <= saveSize);

	// Fixed registers, these are always kept when in Jit context.
	LI(MEMBASEREG, Memory::base);
	LI(CTXREG, mipsState);
	LI(JITBASEREG, GetBasePtr() - MIPS_EMUHACK_OPCODE);

	LoadStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	MovFromPC(SCRATCH1);
	WriteDebugPC(SCRATCH1);
	outerLoopPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);
	outerLoop_ = GetCodePtr();
	// Advance can change the downcount (or thread), so must save/restore around it.
	SaveStaticRegisters();
	RestoreRoundingMode(true);
	WriteDebugProfilerStatus(IRProfilerStatus::TIMER_ADVANCE);
	QuickCallFunction(&CoreTiming::Advance, R20);
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	ApplyRoundingMode(true);
	LoadStaticRegisters();

	dispatcherCheckCoreState_ = GetCodePtr();
	LI(SCRATCH1, &coreState);
	LD_W(SCRATCH1, SCRATCH1, 0);
	FixupBranch badCoreState = BNEZ(SCRATCH1);

	// We just checked coreState, so go to advance if downcount is negative.
	BLT(DOWNCOUNTREG, R_ZERO, outerLoop_);
	FixupBranch skipToRealDispatch = B();

	dispatcherPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);

	hooks_.dispatcher = GetCodePtr();
	FixupBranch bail = BLT(DOWNCOUNTREG, R_ZERO);
	SetJumpTarget(skipToRealDispatch);

	dispatcherNoCheck_ = GetCodePtr();

	// Debug
	if (enableDebug) {
		MOVE(R4, DOWNCOUNTREG);
		MOVE(R5, MEMBASEREG);
		MOVE(R6, JITBASEREG);
		QuickCallFunction(&ShowPC, R20);
	}

	LD_WU(SCRATCH1, CTXREG, offsetof(MIPSState, pc));
	WriteDebugPC(SCRATCH1);
#ifdef MASKED_PSP_MEMORY
	LI(SCRATCH2, 0x3FFFFFFF);
	AND(SCRATCH1, SCRATCH1, SCRATCH2);
#endif
	ADD_D(SCRATCH1, SCRATCH1, MEMBASEREG);
	hooks_.dispatchFetch = GetCodePtr();
	LD_WU(SCRATCH1, SCRATCH1, 0);
	SRLI_D(SCRATCH2, SCRATCH1, 24);
	// We're in other words comparing to the top 8 bits of MIPS_EMUHACK_OPCODE by subtracting.
	ADDI_D(SCRATCH2, SCRATCH2, -(MIPS_EMUHACK_OPCODE >> 24));
	FixupBranch needsCompile = BNEZ(SCRATCH2);
	// No need to mask, JITBASEREG has already accounted for the upper bits.
	ADD_D(SCRATCH1, JITBASEREG, SCRATCH1);
	JR(SCRATCH1);
	SetJumpTarget(needsCompile);

	// No block found, let's jit.  We don't need to save static regs, they're all callee saved.
	RestoreRoundingMode(true);
	WriteDebugProfilerStatus(IRProfilerStatus::COMPILING);
	QuickCallFunction(&MIPSComp::JitAt, R20);
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	ApplyRoundingMode(true);

	// Try again, the block index should be set now.
	B(dispatcherNoCheck_);

	SetJumpTarget(bail);

	LI(SCRATCH1, &coreState);
	LD_W(SCRATCH1, SCRATCH1, 0);
	BEQZ(SCRATCH1, outerLoop_);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);

	WriteDebugProfilerStatus(IRProfilerStatus::NOT_RUNNING);
	SaveStaticRegisters();
	RestoreRoundingMode(true);

	saveOffset = 0;
	for (LoongArch64Reg r : regs_to_save) {
		LD_D(r, R_SP, saveOffset);
		saveOffset += 64 / 8;
	}
	for (LoongArch64Reg r : regs_to_save_fp) {
		FLD_D(r, R_SP, saveOffset);
		saveOffset += 64 / 8;
	}
	ADDI_D(R_SP, R_SP, saveSize);

	RET();

	hooks_.crashHandler = GetCodePtr();
	LI(SCRATCH1, &coreState);
	LI(SCRATCH2, CORE_RUNTIME_ERROR);
	ST_W(SCRATCH2, SCRATCH1, 0);
	B(quitLoop);

	// Leave this at the end, add more stuff above.
	if (enableDisasm) {
#if PPSSPP_ARCH(LOONGARCH64)
		std::vector<std::string> lines = DisassembleLA64(start, GetCodePtr() - start);
		for (auto s : lines) {
			INFO_LOG(Log::JIT, "%s", s.c_str());
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
