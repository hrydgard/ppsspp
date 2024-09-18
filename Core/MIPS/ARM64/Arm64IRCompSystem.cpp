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

#include "ppsspp_config.h"
// In other words, PPSSPP_ARCH(ARM64) || DISASM_ALL.
#if PPSSPP_ARCH(ARM64) || (PPSSPP_PLATFORM(WINDOWS) && !defined(__LIBRETRO__))

#include "Common/Profiler/Profiler.h"
#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/ARM64/Arm64IRJit.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"

// This file contains compilation for basic PC/downcount accounting, syscalls, debug funcs, etc.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace Arm64Gen;
using namespace Arm64IRJitConstants;

void Arm64JitBackend::CompIR_Basic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Downcount:
		SUBI2R(DOWNCOUNTREG, DOWNCOUNTREG, (s64)(s32)inst.constant, SCRATCH1);
		break;

	case IROp::SetConst:
		regs_.SetGPRImm(inst.dest, inst.constant);
		break;

	case IROp::SetConstF:
	{
		regs_.Map(inst);
		float f;
		memcpy(&f, &inst.constant, sizeof(f));
		fp_.MOVI2F(regs_.F(inst.dest), f, SCRATCH1);
		break;
	}

	case IROp::SetPC:
		regs_.Map(inst);
		MovToPC(regs_.R(inst.src1));
		break;

	case IROp::SetPCConst:
		lastConstPC_ = inst.constant;
		MOVI2R(SCRATCH1, inst.constant);
		MovToPC(SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Breakpoint(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Breakpoint:
	{
		FlushAll();
		// Note: the constant could be a delay slot.
		MOVI2R(W0, inst.constant);
		QuickCallFunction(SCRATCH2_64, &IRRunBreakpoint);

		ptrdiff_t distance = dispatcherCheckCoreState_ - GetCodePointer();
		if (distance >= -0x100000 && distance < 0x100000) {
			CBNZ(W0, dispatcherCheckCoreState_);
		} else {
			FixupBranch keepOnKeepingOn = CBZ(W0);
			B(dispatcherCheckCoreState_);
			SetJumpTarget(keepOnKeepingOn);
		}
		break;
	}

	case IROp::MemoryCheck:
		if (regs_.IsGPRImm(inst.src1)) {
			uint32_t iaddr = regs_.GetGPRImm(inst.src1) + inst.constant;
			uint32_t checkedPC = lastConstPC_ + inst.dest;
			int size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			if (size == 0) {
				checkedPC += 4;
				size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			}
			bool isWrite = MIPSAnalyst::IsOpMemoryWrite(checkedPC);

			MemCheck check;
			if (CBreakPoints::GetMemCheckInRange(iaddr, size, &check)) {
				if (!(check.cond & MEMCHECK_READ) && !isWrite)
					break;
				if (!(check.cond & (MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE)) && isWrite)
					break;

				// We need to flush, or conditions and log expressions will see old register values.
				FlushAll();

				MOVI2R(W0, checkedPC);
				MOVI2R(W1, iaddr);
				QuickCallFunction(SCRATCH2_64, &IRRunMemCheck);

				ptrdiff_t distance = dispatcherCheckCoreState_ - GetCodePointer();
				if (distance >= -0x100000 && distance < 0x100000) {
					CBNZ(W0, dispatcherCheckCoreState_);
				} else {
					FixupBranch keepOnKeepingOn = CBZ(W0);
					B(dispatcherCheckCoreState_);
					SetJumpTarget(keepOnKeepingOn);
				}
			}
		} else {
			uint32_t checkedPC = lastConstPC_ + inst.dest;
			int size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			if (size == 0) {
				checkedPC += 4;
				size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			}
			bool isWrite = MIPSAnalyst::IsOpMemoryWrite(checkedPC);

			const auto memchecks = CBreakPoints::GetMemCheckRanges(isWrite);
			// We can trivially skip if there are no checks for this type (i.e. read vs write.)
			if (memchecks.empty())
				break;

			ARM64Reg addrBase = regs_.MapGPR(inst.src1);
			ADDI2R(SCRATCH1, addrBase, inst.constant, SCRATCH2);

			// We need to flush, or conditions and log expressions will see old register values.
			FlushAll();

			std::vector<FixupBranch> hitChecks;
			for (auto it : memchecks) {
				if (it.end != 0) {
					CMPI2R(SCRATCH1, it.start - size, SCRATCH2);
					MOVI2R(SCRATCH2, it.end);
					CCMP(SCRATCH1, SCRATCH2, 0xF, CC_HI);
					hitChecks.push_back(B(CC_LO));
				} else {
					CMPI2R(SCRATCH1, it.start, SCRATCH2);
					hitChecks.push_back(B(CC_EQ));
				}
			}

			FixupBranch noHits = B();

			// Okay, now land any hit here.
			for (auto &fixup : hitChecks)
				SetJumpTarget(fixup);
			hitChecks.clear();

			MOVI2R(W0, checkedPC);
			MOV(W1, SCRATCH1);
			QuickCallFunction(SCRATCH2_64, &IRRunMemCheck);

			ptrdiff_t distance = dispatcherCheckCoreState_ - GetCodePointer();
			if (distance >= -0x100000 && distance < 0x100000) {
				CBNZ(W0, dispatcherCheckCoreState_);
			} else {
				FixupBranch keepOnKeepingOn = CBZ(W0);
				B(dispatcherCheckCoreState_);
				SetJumpTarget(keepOnKeepingOn);
			}

			SetJumpTarget(noHits);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_System(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Syscall:
		FlushAll();
		SaveStaticRegisters();

		WriteDebugProfilerStatus(IRProfilerStatus::SYSCALL);
#ifdef USE_PROFILER
		// When profiling, we can't skip CallSyscall, since it times syscalls.
		MOVI2R(W0, inst.constant);
		QuickCallFunction(SCRATCH2_64, &CallSyscall);
#else
		// Skip the CallSyscall where possible.
		{
			MIPSOpcode op(inst.constant);
			void *quickFunc = GetQuickSyscallFunc(op);
			if (quickFunc) {
				MOVP2R(X0, GetSyscallFuncPointer(op));
				QuickCallFunction(SCRATCH2_64, (const u8 *)quickFunc);
			} else {
				MOVI2R(W0, inst.constant);
				QuickCallFunction(SCRATCH2_64, &CallSyscall);
			}
		}
#endif

		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();
		// This is always followed by an ExitToPC, where we check coreState.
		break;

	case IROp::CallReplacement:
		FlushAll();
		SaveStaticRegisters();
		WriteDebugProfilerStatus(IRProfilerStatus::REPLACEMENT);
		QuickCallFunction(SCRATCH2_64, GetReplacementFunc(inst.constant)->replaceFunc);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();

		// Absolute value the result and subtract.
		CMP(W0, 0);
		CSNEG(SCRATCH1, W0, W0, CC_PL);
		SUB(DOWNCOUNTREG, DOWNCOUNTREG, SCRATCH1);

		// W0 might be the mapped reg, but there's only one.
		// Set dest reg to the sign of the result.
		regs_.Map(inst);
		ASR(regs_.R(inst.dest), W0, 31);
		break;

	case IROp::Break:
		FlushAll();
		// This doesn't naturally have restore/apply around it.
		RestoreRoundingMode(true);
		SaveStaticRegisters();
		MovFromPC(W0);
		QuickCallFunction(SCRATCH2_64, &Core_Break);
		LoadStaticRegisters();
		ApplyRoundingMode(true);
		MovFromPC(SCRATCH1);
		ADDI2R(SCRATCH1, SCRATCH1, 4, SCRATCH2);
		B(dispatcherPCInSCRATCH1_);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Transfer(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::SetCtrlVFPU:
		regs_.SetGPRImm(IRREG_VFPU_CTRL_BASE + inst.dest, inst.constant);
		break;

	case IROp::SetCtrlVFPUReg:
		regs_.Map(inst);
		MOV(regs_.R(IRREG_VFPU_CTRL_BASE + inst.dest), regs_.R(inst.src1));
		break;

	case IROp::SetCtrlVFPUFReg:
		regs_.Map(inst);
		fp_.FMOV(regs_.R(IRREG_VFPU_CTRL_BASE + inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FpCondFromReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		MOV(regs_.R(IRREG_FPCOND), regs_.R(inst.src1));
		break;

	case IROp::FpCondToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		MOV(regs_.R(inst.dest), regs_.R(IRREG_FPCOND));
		break;

	case IROp::FpCtrlFromReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		ANDI2R(SCRATCH1, regs_.R(inst.src1), 0x0181FFFF, SCRATCH2);
		// Extract the new fpcond value.
		UBFX(regs_.R(IRREG_FPCOND), SCRATCH1, 23, 1);
		STR(INDEX_UNSIGNED, SCRATCH1, CTXREG, IRREG_FCR31 * 4);
		break;

	case IROp::FpCtrlToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		// Load fcr31 and clear the fpcond bit.
		LDR(INDEX_UNSIGNED, regs_.R(inst.dest), CTXREG, IRREG_FCR31 * 4);
		BFI(regs_.R(inst.dest), regs_.R(IRREG_FPCOND), 23, 1);
		// Also update mips->fcr31 while we're here.
		STR(INDEX_UNSIGNED, regs_.R(inst.dest), CTXREG, IRREG_FCR31 * 4);
		break;

	case IROp::VfpuCtrlToReg:
		regs_.Map(inst);
		MOV(regs_.R(inst.dest), regs_.R(IRREG_VFPU_CTRL_BASE + inst.src1));
		break;

	case IROp::FMovFromGPR:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
			fp_.MOVI2F(regs_.F(inst.dest), 0.0f, SCRATCH1);
		} else {
			regs_.Map(inst);
			fp_.FMOV(regs_.F(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::FMovToGPR:
		regs_.Map(inst);
		fp_.FMOV(regs_.R(inst.dest), regs_.F(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_ValidateAddress(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool isWrite = inst.src2 & 1;
	int alignment = 0;
	switch (inst.op) {
	case IROp::ValidateAddress8:
		alignment = 1;
		break;

	case IROp::ValidateAddress16:
		alignment = 2;
		break;

	case IROp::ValidateAddress32:
		alignment = 4;
		break;

	case IROp::ValidateAddress128:
		alignment = 16;
		break;

	default:
		INVALIDOP;
		break;
	}

	if (regs_.IsGPRMappedAsPointer(inst.src1)) {
		if (!jo.enablePointerify) {
			SUB(SCRATCH1_64, regs_.RPtr(inst.src1), MEMBASEREG);
			ADDI2R(SCRATCH1, SCRATCH1, inst.constant, SCRATCH2);
		} else {
			ADDI2R(SCRATCH1, regs_.R(inst.src1), inst.constant, SCRATCH2);
		}
	} else {
		regs_.Map(inst);
		ADDI2R(SCRATCH1, regs_.R(inst.src1), inst.constant, SCRATCH2);
	}
	ANDI2R(SCRATCH1, SCRATCH1, 0x3FFFFFFF, SCRATCH2);

	std::vector<FixupBranch> validJumps;

	FixupBranch unaligned;
	if (alignment == 2) {
		unaligned = TBNZ(SCRATCH1, 0);
	} else if (alignment != 1) {
		TSTI2R(SCRATCH1, alignment - 1, SCRATCH2);
		unaligned = B(CC_NEQ);
	}

	CMPI2R(SCRATCH1, PSP_GetUserMemoryEnd() - alignment, SCRATCH2);
	FixupBranch tooHighRAM = B(CC_HI);
	CMPI2R(SCRATCH1, PSP_GetKernelMemoryBase(), SCRATCH2);
	validJumps.push_back(B(CC_HS));

	CMPI2R(SCRATCH1, PSP_GetVidMemEnd() - alignment, SCRATCH2);
	FixupBranch tooHighVid = B(CC_HI);
	CMPI2R(SCRATCH1, PSP_GetVidMemBase(), SCRATCH2);
	validJumps.push_back(B(CC_HS));

	CMPI2R(SCRATCH1, PSP_GetScratchpadMemoryEnd() - alignment, SCRATCH2);
	FixupBranch tooHighScratch = B(CC_HI);
	CMPI2R(SCRATCH1, PSP_GetScratchpadMemoryBase(), SCRATCH2);
	validJumps.push_back(B(CC_HS));

	if (alignment != 1)
		SetJumpTarget(unaligned);
	SetJumpTarget(tooHighRAM);
	SetJumpTarget(tooHighVid);
	SetJumpTarget(tooHighScratch);

	// If we got here, something unusual and bad happened, so we'll always go back to the dispatcher.
	// Because of that, we can avoid flushing outside this case.
	auto regsCopy = regs_;
	regsCopy.FlushAll();

	// Ignores the return value, always returns to the dispatcher.
	// Otherwise would need a thunk to restore regs.
	MOV(W0, SCRATCH1);
	MOVI2R(W1, alignment);
	MOVI2R(W2, isWrite ? 1 : 0);
	QuickCallFunction(SCRATCH2, &ReportBadAddress);
	B(dispatcherCheckCoreState_);

	for (FixupBranch &b : validJumps)
		SetJumpTarget(b);
}

} // namespace MIPSComp

#endif
