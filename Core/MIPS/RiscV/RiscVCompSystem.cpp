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

#include "Common/Profiler/Profiler.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"

// This file contains compilation for basic PC/downcount accounting, syscalls, debug funcs, etc.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace RiscVGen;
using namespace RiscVJitConstants;

void RiscVJitBackend::CompIR_Basic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::SetConst:
		// Sign extend all constants.  We get 0xFFFFFFFF sometimes, and it's more work to truncate.
		// The register only holds 32 bits in the end anyway.
		regs_.SetGPRImm(inst.dest, (int32_t)inst.constant);
		break;

	case IROp::SetConstF:
		regs_.Map(inst);
		if (inst.constant == 0)
			FCVT(FConv::S, FConv::W, regs_.F(inst.dest), R_ZERO);
		else
			QuickFLI(32, regs_.F(inst.dest), inst.constant, SCRATCH1);
		break;

	case IROp::Downcount:
		if (inst.constant <= 2048) {
			ADDI(DOWNCOUNTREG, DOWNCOUNTREG, -(s32)inst.constant);
		} else {
			LI(SCRATCH1, inst.constant, SCRATCH2);
			SUB(DOWNCOUNTREG, DOWNCOUNTREG, SCRATCH1);
		}
		break;

	case IROp::SetPC:
		regs_.Map(inst);
		MovToPC(regs_.R(inst.src1));
		break;

	case IROp::SetPCConst:
		LI(SCRATCH1, inst.constant, SCRATCH2);
		MovToPC(SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Transfer(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::SetCtrlVFPU:
		regs_.SetGPRImm(IRREG_VFPU_CTRL_BASE + inst.dest, inst.constant);
		break;

	case IROp::SetCtrlVFPUReg:
		regs_.Map(inst);
		MV(regs_.R(IRREG_VFPU_CTRL_BASE + inst.dest), regs_.R(inst.src1));
		regs_.MarkGPRDirty(IRREG_VFPU_CTRL_BASE + inst.dest, regs_.IsNormalized32(inst.src1));
		break;

	case IROp::SetCtrlVFPUFReg:
		regs_.Map(inst);
		FMV(FMv::X, FMv::W, regs_.R(IRREG_VFPU_CTRL_BASE + inst.dest), regs_.F(inst.src1));
		regs_.MarkGPRDirty(IRREG_VFPU_CTRL_BASE + inst.dest, true);
		break;

	case IROp::FpCondFromReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		MV(regs_.R(IRREG_FPCOND), regs_.R(inst.src1));
		break;

	case IROp::FpCondToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		MV(regs_.R(inst.dest), regs_.R(IRREG_FPCOND));
		regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(IRREG_FPCOND));
		break;

	case IROp::FpCtrlFromReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		LI(SCRATCH1, 0x0181FFFF);
		AND(SCRATCH1, regs_.R(inst.src1), SCRATCH1);
		// Extract the new fpcond value.
		if (cpu_info.RiscV_Zbs) {
			BEXTI(regs_.R(IRREG_FPCOND), SCRATCH1, 23);
		} else {
			SRLI(regs_.R(IRREG_FPCOND), SCRATCH1, 23);
			ANDI(regs_.R(IRREG_FPCOND), regs_.R(IRREG_FPCOND), 1);
		}
		SW(SCRATCH1, CTXREG, IRREG_FCR31 * 4);
		regs_.MarkGPRDirty(IRREG_FPCOND, true);
		break;

	case IROp::FpCtrlToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		// Load fcr31 and clear the fpcond bit.
		LW(SCRATCH1, CTXREG, IRREG_FCR31 * 4);
		if (cpu_info.RiscV_Zbs) {
			BCLRI(SCRATCH1, SCRATCH1, 23);
		} else {
			LI(SCRATCH2, ~(1 << 23));
			AND(SCRATCH1, SCRATCH1, SCRATCH2);
		}

		// Now get the correct fpcond bit.
		ANDI(SCRATCH2, regs_.R(IRREG_FPCOND), 1);
		SLLI(SCRATCH2, SCRATCH2, 23);
		OR(regs_.R(inst.dest), SCRATCH1, SCRATCH2);

		// Also update mips->fcr31 while we're here.
		SW(regs_.R(inst.dest), CTXREG, IRREG_FCR31 * 4);
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::VfpuCtrlToReg:
		regs_.Map(inst);
		MV(regs_.R(inst.dest), regs_.R(IRREG_VFPU_CTRL_BASE + inst.src1));
		regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(IRREG_VFPU_CTRL_BASE + inst.src1));
		break;

	case IROp::FMovFromGPR:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
			FCVT(FConv::S, FConv::W, regs_.F(inst.dest), R_ZERO);
		} else {
			regs_.Map(inst);
			FMV(FMv::W, FMv::X, regs_.F(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::FMovToGPR:
		regs_.Map(inst);
		FMV(FMv::X, FMv::W, regs_.R(inst.dest), regs_.F(inst.src1));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_System(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Syscall:
		FlushAll();
		SaveStaticRegisters();

		WriteDebugProfilerStatus(IRProfilerStatus::SYSCALL);
#ifdef USE_PROFILER
		// When profiling, we can't skip CallSyscall, since it times syscalls.
		LI(X10, (int32_t)inst.constant);
		QuickCallFunction(&CallSyscall, SCRATCH2);
#else
		// Skip the CallSyscall where possible.
		{
			MIPSOpcode op(inst.constant);
			void *quickFunc = GetQuickSyscallFunc(op);
			if (quickFunc) {
				LI(X10, (uintptr_t)GetSyscallFuncPointer(op));
				QuickCallFunction((const u8 *)quickFunc, SCRATCH2);
			} else {
				LI(X10, (int32_t)inst.constant);
				QuickCallFunction(&CallSyscall, SCRATCH2);
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
		QuickCallFunction(GetReplacementFunc(inst.constant)->replaceFunc, SCRATCH2);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		LoadStaticRegisters();

		regs_.Map(inst);
		SRAIW(regs_.R(inst.dest), X10, 31);

		// Absolute value trick: if neg, abs(x) == (x ^ -1) + 1.
		XOR(X10, X10, regs_.R(inst.dest));
		SUBW(X10, X10, regs_.R(inst.dest));
		SUB(DOWNCOUNTREG, DOWNCOUNTREG, X10);
		break;

	case IROp::Break:
		FlushAll();
		// This doesn't naturally have restore/apply around it.
		RestoreRoundingMode(true);
		SaveStaticRegisters();
		MovFromPC(X10);
		QuickCallFunction(&Core_Break, SCRATCH2);
		LoadStaticRegisters();
		ApplyRoundingMode(true);
		MovFromPC(SCRATCH1);
		ADDI(SCRATCH1, SCRATCH1, 4);
		QuickJ(R_RA, dispatcherPCInSCRATCH1_);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Breakpoint(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Breakpoint:
	case IROp::MemoryCheck:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_ValidateAddress(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::ValidateAddress8:
	case IROp::ValidateAddress16:
	case IROp::ValidateAddress32:
	case IROp::ValidateAddress128:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
