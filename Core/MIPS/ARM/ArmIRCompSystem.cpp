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

#include "Common/Profiler/Profiler.h"
#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"

// This file contains compilation for basic PC/downcount accounting, syscalls, debug funcs, etc.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace ArmGen;
using namespace ArmIRJitConstants;

void ArmJitBackend::CompIR_Basic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Downcount:
		SUBI2R(DOWNCOUNTREG, DOWNCOUNTREG, inst.constant, SCRATCH1);
		break;

	case IROp::SetConst:
		regs_.SetGPRImm(inst.dest, inst.constant);
		break;

	case IROp::SetConstF:
	{
		regs_.Map(inst);
		float f;
		memcpy(&f, &inst.constant, sizeof(f));
		MOVI2F(regs_.F(inst.dest), f, SCRATCH1);
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

void ArmJitBackend::CompIR_Breakpoint(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Breakpoint:
		FlushAll();
		// Note: the constant could be a delay slot.
		MOVI2R(R0, inst.constant);
		QuickCallFunction(R12, &IRRunBreakpoint);
		CMP(R0, Operand2(0));
		B_CC(CC_NEQ, dispatcherCheckCoreState_);
		break;

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
			if (g_breakpoints.GetMemCheckInRange(iaddr, size, &check)) {
				if (!(check.cond & MEMCHECK_READ) && !isWrite)
					break;
				if (!(check.cond & (MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE)) && isWrite)
					break;

				// We need to flush, or conditions and log expressions will see old register values.
				FlushAll();

				MOVI2R(R0, checkedPC);
				MOVI2R(R1, iaddr);
				QuickCallFunction(R12, &IRRunMemCheck);
				CMP(R0, Operand2(0));
				B_CC(CC_NEQ, dispatcherCheckCoreState_);
			}
		} else {
			uint32_t checkedPC = lastConstPC_ + inst.dest;
			int size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			if (size == 0) {
				checkedPC += 4;
				size = MIPSAnalyst::OpMemoryAccessSize(checkedPC);
			}
			bool isWrite = MIPSAnalyst::IsOpMemoryWrite(checkedPC);

			const auto memchecks = g_breakpoints.GetMemCheckRanges(isWrite);
			// We can trivially skip if there are no checks for this type (i.e. read vs write.)
			if (memchecks.empty())
				break;

			// We need to flush, or conditions and log expressions will see old register values.
			// The flush may use SCRATCH1, so read the address after it. R12 is free after it too.
			FlushAll();
			LDR(SCRATCH1, CTXREG, inst.src1 * 4);
			ADDI2R(SCRATCH1, SCRATCH1, inst.constant, SCRATCH2);

			std::vector<FixupBranch> hitChecks;
			for (auto it : memchecks) {
				if (it.end != 0) {
					// A hit when start - size < addr < end, as an unsigned range check.
					uint32_t lo = it.start - size + 1;
					SUBI2R(R12, SCRATCH1, lo, SCRATCH2);
					CMPI2R(R12, it.end - lo, SCRATCH2);
					hitChecks.push_back(B_CC(CC_LO));
				} else {
					CMPI2R(SCRATCH1, it.start, SCRATCH2);
					hitChecks.push_back(B_CC(CC_EQ));
				}
			}

			FixupBranch noHits = B();

			// Okay, now land any hit here.
			for (auto &fixup : hitChecks)
				SetJumpTarget(fixup);
			hitChecks.clear();

			MOV(R1, SCRATCH1);
			MOVI2R(R0, checkedPC);
			QuickCallFunction(R12, &IRRunMemCheck);
			CMP(R0, Operand2(0));
			B_CC(CC_NEQ, dispatcherCheckCoreState_);

			SetJumpTarget(noHits);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_System(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Syscall:
		FlushAll();
		SaveStaticRegisters();

		WriteDebugProfilerStatus(IRProfilerStatus::SYSCALL);
#ifdef USE_PROFILER
		// When profiling, we can't skip CallSyscall, since it times syscalls.
		MOVI2R(R0, inst.constant);
		QuickCallFunction(R12, &CallSyscall);
#else
		// Skip the CallSyscall where possible.
		{
			MIPSOpcode op(inst.constant);
			const HLEFunction *func = GetSyscallFunctionData(op, 0);
			if (func) {
				void *quickFunc = GetQuickSyscallFunc(func, op);
				if (quickFunc) {
					MOVP2R(R0, func);
					QuickCallFunction(R12, (const u8 *)quickFunc);
				} else {
					MOVI2R(R0, inst.constant);
					QuickCallFunction(R12, &CallSyscall);
				}
			} else {
				// Shouldn't get here.
				MOVI2R(R0, 0);
				QuickCallFunction(R12, &CallSyscallUnresolvedAtPC);
			}
		}
#endif

		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();
		// This is always followed by an ExitToPC, where we check coreState.
		break;

	case IROp::SyscallUnresolved:
		FlushAll();
		SaveStaticRegisters();
		WriteDebugProfilerStatus(IRProfilerStatus::SYSCALL);
		MOVI2R(R0, inst.constant);
		QuickCallFunction(R12, &CallSyscallUnresolvedAtPC);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();
		break;

	case IROp::CallReplacement:
		FlushAll();
		SaveStaticRegisters();
		WriteDebugProfilerStatus(IRProfilerStatus::REPLACEMENT);
		QuickCallFunction(R12, GetReplacementFunc(inst.constant)->replaceFunc);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();

		// Absolute value the result and subtract.
		CMP(R0, Operand2(0));
		SetCC(CC_MI);
		RSB(SCRATCH2, R0, Operand2(0));
		SetCC(CC_PL);
		MOV(SCRATCH2, R0);
		SetCC(CC_AL);
		SUB(DOWNCOUNTREG, DOWNCOUNTREG, SCRATCH2);

		// Everything was flushed, so this can't touch R0.
		// Set dest reg to the sign of the result.
		regs_.Map(inst);
		ASR(regs_.R(inst.dest), R0, 31);
		break;

	case IROp::Break:
		FlushAll();
		// This doesn't naturally have restore/apply around it.
		RestoreRoundingMode(true);
		SaveStaticRegisters();
		MovFromPC(R0);
		QuickCallFunction(R12, &Core_BreakException);
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

void ArmJitBackend::CompIR_Transfer(IRInst inst) {
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
		VMOV(regs_.R(IRREG_VFPU_CTRL_BASE + inst.dest), regs_.F(inst.src1));
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
		STR(SCRATCH1, CTXREG, IRREG_FCR31 * 4);
		break;

	case IROp::FpCtrlToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		// Load fcr31 and clear the fpcond bit.
		LDR(regs_.R(inst.dest), CTXREG, IRREG_FCR31 * 4);
		BFI(regs_.R(inst.dest), regs_.R(IRREG_FPCOND), 23, 1);
		// Also update mips->fcr31 while we're here.
		STR(regs_.R(inst.dest), CTXREG, IRREG_FCR31 * 4);
		break;

	case IROp::VfpuCtrlToReg:
		regs_.Map(inst);
		MOV(regs_.R(inst.dest), regs_.R(IRREG_VFPU_CTRL_BASE + inst.src1));
		break;

	case IROp::FMovFromGPR:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
			MOVI2F(regs_.F(inst.dest), 0.0f, SCRATCH1);
		} else {
			regs_.Map(inst);
			VMOV(regs_.F(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::FMovToGPR:
		regs_.Map(inst);
		VMOV(regs_.R(inst.dest), regs_.F(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_ValidateAddress(IRInst inst) {
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

	regs_.Map(inst);
	ADDI2R(SCRATCH1, regs_.R(inst.src1), inst.constant, SCRATCH2);
	BIC(SCRATCH1, SCRATCH1, Operand2(0xC0, 4));

	std::vector<FixupBranch> validJumps;

	FixupBranch unaligned;
	if (alignment != 1) {
		TST(SCRATCH1, Operand2(alignment - 1));
		unaligned = B_CC(CC_NEQ);
	}

	CMPI2R(SCRATCH1, PSP_GetUserMemoryEnd() - alignment, SCRATCH2);
	FixupBranch tooHighRAM = B_CC(CC_HI);
	CMPI2R(SCRATCH1, PSP_GetKernelMemoryBase(), SCRATCH2);
	validJumps.push_back(B_CC(CC_HS));

	CMPI2R(SCRATCH1, PSP_GetVidMemEnd() - alignment, SCRATCH2);
	FixupBranch tooHighVid = B_CC(CC_HI);
	CMPI2R(SCRATCH1, PSP_GetVidMemBase(), SCRATCH2);
	validJumps.push_back(B_CC(CC_HS));

	CMPI2R(SCRATCH1, PSP_GetScratchpadMemoryEnd() - alignment, SCRATCH2);
	FixupBranch tooHighScratch = B_CC(CC_HI);
	CMPI2R(SCRATCH1, PSP_GetScratchpadMemoryBase(), SCRATCH2);
	validJumps.push_back(B_CC(CC_HS));

	if (alignment != 1)
		SetJumpTarget(unaligned);
	SetJumpTarget(tooHighRAM);
	SetJumpTarget(tooHighVid);
	SetJumpTarget(tooHighScratch);

	// If we got here, something unusual and bad happened, so we'll always go back to the dispatcher.
	// Because of that, we can avoid flushing outside this case. The flush may use SCRATCH1, so the
	// address waits on the stack (two regs, to keep it aligned for the call).
	PUSH(2, SCRATCH1, SCRATCH2);
	auto regsCopy = regs_;
	regsCopy.FlushAll();
	POP(2, SCRATCH1, SCRATCH2);

	// Ignores the return value, always returns to the dispatcher.
	// Otherwise would need a thunk to restore regs.
	MOVI2R(R1, alignment);
	MOVI2R(R2, isWrite ? 1 : 0);
	QuickCallFunction(R12, &ReportBadAddress);
	B(dispatcherCheckCoreState_);

	for (FixupBranch &b : validJumps)
		SetJumpTarget(b);
}

} // namespace MIPSComp

#endif
