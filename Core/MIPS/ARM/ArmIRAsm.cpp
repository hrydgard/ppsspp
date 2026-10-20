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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM)

#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/ARM/ArmVfpuDot.h"
#include "Core/MemMap.h"
#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Core/Core.h"

std::vector<std::string> DisassembleArm2(const u8 *data, int size);

namespace MIPSComp {

using namespace ArmGen;
using namespace ArmIRJitConstants;

static const bool enableDisasm = false;

void ArmJitBackend::GenerateFixedCode(MIPSState *mipsState) {
	// This will be used as a writable scratch area, always 32-bit accessible.
	const u8 *start = AlignCodePage();
	if (DebugProfilerEnabled()) {
		ProtectMemoryPages(start, GetMemoryProtectPageSize(), MEM_PROT_READ | MEM_PROT_WRITE);
		hooks_.profilerPC = (uint32_t *)GetWritableCodePtr();
		Write32(0);
		hooks_.profilerStatus = (IRProfilerStatus *)GetWritableCodePtr();
		Write32(0);
	}

	const u8 *disasmStart = AlignCodePage();
	BeginWrite(GetMemoryProtectPageSize());

	// These are called with BL, so LR has to survive.
	restoreRoundingMode_ = AlignCode16();
	{
		VMRS(SCRATCH2);
		// Outside the JIT we run with round-to-nearest and flush-to-zero off.
		BIC(SCRATCH2, SCRATCH2, AssumeMakeOperand2((3 | 4) << 22));
		VMSR(SCRATCH2);
		B(R_LR);
	}

	// Must preserve SCRATCH1, since it may hold the PC. Destroys SCRATCH2 and R12.
	applyRoundingMode_ = AlignCode16();
	{
		LDR(SCRATCH2, CTXREG, offsetof(MIPSState, fcr31));

		TST(SCRATCH2, AssumeMakeOperand2(1 << 24));
		AND(SCRATCH2, SCRATCH2, Operand2(3));
		SetCC(CC_NEQ);
		ADD(SCRATCH2, SCRATCH2, Operand2(4));
		SetCC(CC_AL);

		// We can skip if the rounding mode is nearest (0) and flush is not set.
		// (as restoreRoundingMode cleared it out anyway)
		CMP(SCRATCH2, Operand2(0));
		FixupBranch skip = B_CC(CC_EQ);

		// MIPS Rounding Mode:       ARM Rounding Mode
		//   0: Round nearest        0
		//   1: Round to zero        3
		//   2: Round up (ceil)      1
		//   3: Round down (floor)   2
		AND(R12, SCRATCH2, Operand2(3));
		CMP(R12, Operand2(1));

		SetCC(CC_EQ); ADD(SCRATCH2, SCRATCH2, Operand2(2));
		SetCC(CC_GT); SUB(SCRATCH2, SCRATCH2, Operand2(1));
		SetCC(CC_AL);

		VMRS(R12);
		// Assume we're always in round-to-nearest mode beforehand.
		// But we need to clear flush to zero in this case anyway.
		BIC(R12, R12, AssumeMakeOperand2((3 | 4) << 22));
		ORR(R12, R12, Operand2(SCRATCH2, ST_LSL, 22));
		VMSR(R12);

		SetJumpTarget(skip);
		B(R_LR);
	}

	vfpuDot_ = GenerateArmVfpuDotCall(this, CTXREG, GenerateArmVfpuDotSpecial(this, CTXREG));

	hooks_.enterDispatcher = (IRNativeFuncNoArg)AlignCode16();

	// Nine registers, so we're misaligned by 4 for calls, which the SUB fixes.
	PUSH(9, R4, R5, R6, R7, R8, R9, R10, R11, R_LR);
	SUB(R_SP, R_SP, 4);
	VPUSH(D8, 8);

	// Fixed registers, these are always kept when in Jit context.
	MOVP2R(MEMBASEREG, mipsState->space->base);
	MOVP2R(CTXREG, mipsState);

	LoadStaticRegisters();
	WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	MovFromPC(SCRATCH1);
	WriteDebugPC(SCRATCH1);
	outerLoopPCInSCRATCH1_ = GetCodePtr();
	MovToPC(SCRATCH1);
	outerLoop_ = GetCodePtr();
		SaveStaticRegisters();  // Advance can change the downcount, so must save/restore
		RestoreRoundingMode(true);
		WriteDebugProfilerStatus(IRProfilerStatus::TIMER_ADVANCE);
		QuickCallFunctionR(R12, &CoreTiming::Advance, CTXREG);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		ApplyRoundingMode(true);
		LoadStaticRegisters();

		dispatcherCheckCoreState_ = GetCodePtr();

		MOVP2R(SCRATCH1, &coreState);
		LDR(SCRATCH1, SCRATCH1, 0);
		CMP(SCRATCH1, Operand2(0));
		FixupBranch badCoreState = B_CC(CC_NEQ);

		// Check downcount.
		CMP(DOWNCOUNTREG, Operand2(0));
		B_CC(CC_MI, outerLoop_);
		FixupBranch skipToRealDispatch = B();

		dispatcherPCInSCRATCH1_ = GetCodePtr();
		MovToPC(SCRATCH1);

		hooks_.dispatcher = GetCodePtr();

			CMP(DOWNCOUNTREG, Operand2(0));
			FixupBranch bail = B_CC(CC_MI);
			SetJumpTarget(skipToRealDispatch);

			dispatcherNoCheck_ = GetCodePtr();

			MovFromPC(SCRATCH1);
			WriteDebugPC(SCRATCH1);
			static_assert(Memory::BLOCK_SHADOW_ENTRY_MASK == 0x0FFFFFFC, "BICs below assume the 32-bit shadow mask");
			BIC(SCRATCH1, SCRATCH1, Operand2(0xF0, 4));   // &= 0x0FFFFFFF. Rotation is to the right, in 2-bit increments.
			BIC(SCRATCH1, SCRATCH1, Operand2(3));
			MOVP2R(SCRATCH2, mipsState->space->blockShadow);
			hooks_.dispatchFetch = GetCodePtr();
			LDR(SCRATCH1, SCRATCH2, SCRATCH1);
			// The entry is the block's offset into the code space, or 0 for none.
			CMP(SCRATCH1, Operand2(0));
			FixupBranch noBlock = B_CC(CC_EQ);
				MOVP2R(SCRATCH2, GetBasePtr());
				ADD(SCRATCH1, SCRATCH1, SCRATCH2);
				B(SCRATCH1);
			SetJumpTarget(noBlock);

			// No block found, let's jit.  We don't need to save static regs, they're all callee saved.
			// A replacement hook's block shadow entry jumps here too (GetBlockShadowHookValue()). JitAt
			// replaces it with the new block's entry, and destroying that block puts the hook value back.
			hooks_.blockShadowHook = GetCodePtr();
			RestoreRoundingMode(true);
			WriteDebugProfilerStatus(IRProfilerStatus::COMPILING);
			QuickCallFunctionR(R12, &MIPSComp::JitAt, CTXREG);
			WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
			ApplyRoundingMode(true);

			// Let's just dispatch again, we'll enter the block since we know it's there.
			B(dispatcherNoCheck_);

		SetJumpTarget(bail);

		MOVP2R(SCRATCH1, &coreState);
		LDR(SCRATCH1, SCRATCH1, 0);
		CMP(SCRATCH1, Operand2(0));
		B_CC(CC_EQ, outerLoop_);

	const uint8_t *quitLoop = GetCodePtr();
	SetJumpTarget(badCoreState);

	WriteDebugProfilerStatus(IRProfilerStatus::NOT_RUNNING);
	SaveStaticRegisters();
	RestoreRoundingMode(true);

	VPOP(D8, 8);
	ADD(R_SP, R_SP, 4);
	POP(9, R4, R5, R6, R7, R8, R9, R10, R11, R_PC);  // Returns

	hooks_.crashHandler = GetCodePtr();
	MOVP2R(SCRATCH1, &coreState);
	MOVI2R(SCRATCH2, CORE_RUNTIME_ERROR);
	STR(SCRATCH2, SCRATCH1, 0);
	B(quitLoop);

	// Leave this at the end, add more stuff above.
	if (enableDisasm) {
		std::vector<std::string> lines = DisassembleArm2(disasmStart, (int)(GetCodePtr() - disasmStart));
		for (auto s : lines) {
			INFO_LOG(Log::JIT, "%s", s.c_str());
		}
	}

	// Let's spare the pre-generated code from unprotect-reprotect.
	AlignCodePage();
	jitStartOffset_ = (int)(GetCodePtr() - start);
	// Don't forget to zap the instruction cache! This must stay at the end of this function.
	FlushIcache();
	EndWrite();
}

void ArmJitBackend::GetFixedCodeLabels(std::vector<IRNativeFixedCodeLabel> &labels) const {
	labels.push_back({ outerLoop_, "outerLoop", false });
	labels.push_back({ outerLoopPCInSCRATCH1_, "outerLoopPCInSCRATCH1", false });
	labels.push_back({ dispatcherCheckCoreState_, "dispatcherCheckCoreState", false });
	labels.push_back({ dispatcherPCInSCRATCH1_, "dispatcherPCInSCRATCH1", false });
	labels.push_back({ dispatcherNoCheck_, "dispatcherNoCheck", false });
	labels.push_back({ restoreRoundingMode_, "restoreRoundingMode", true });
	labels.push_back({ applyRoundingMode_, "applyRoundingMode", true });
	labels.push_back({ vfpuDot_, "vfpuDot", true });
}

} // namespace MIPSComp

#endif
