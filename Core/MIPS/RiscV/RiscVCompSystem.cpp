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
		gpr.SetGPRImm(inst.dest, (int32_t)inst.constant);
		break;

	case IROp::SetConstF:
		fpr.MapReg(inst.dest, MIPSMap::NOINIT);
		if (inst.constant == 0) {
			FCVT(FConv::S, FConv::W, fpr.R(inst.dest), R_ZERO);
		} else {
			// TODO: In the future, could use FLI if it's approved.
			// Also, is FCVT faster?
			LI(SCRATCH1, (int32_t)inst.constant);
			FMV(FMv::W, FMv::X, fpr.R(inst.dest), SCRATCH1);
		}
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
		gpr.MapIn(inst.src1);
		MovToPC(gpr.R(inst.src1));
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
		gpr.SetGPRImm(IRREG_VFPU_CTRL_BASE + inst.dest, (int32_t)inst.constant);
		break;

	case IROp::SetCtrlVFPUReg:
		gpr.MapDirtyIn(IRREG_VFPU_CTRL_BASE + inst.dest, inst.src1);
		MV(gpr.R(IRREG_VFPU_CTRL_BASE + inst.dest), gpr.R(inst.src1));
		gpr.MarkGPRDirty(IRREG_VFPU_CTRL_BASE + inst.dest, gpr.IsNormalized32(inst.src1));
		break;

	case IROp::SetCtrlVFPUFReg:
		gpr.MapReg(IRREG_VFPU_CTRL_BASE + inst.dest, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
		fpr.MapReg(inst.src1);
		FMV(FMv::X, FMv::W, gpr.R(IRREG_VFPU_CTRL_BASE + inst.dest), fpr.R(inst.src1));
		break;

	case IROp::FpCondFromReg:
		gpr.MapDirtyIn(IRREG_FPCOND, inst.src1);
		MV(gpr.R(IRREG_FPCOND), gpr.R(inst.src1));
		break;

	case IROp::FpCondToReg:
		gpr.MapDirtyIn(inst.dest, IRREG_FPCOND);
		MV(gpr.R(inst.dest), gpr.R(IRREG_FPCOND));
		gpr.MarkGPRDirty(inst.dest, gpr.IsNormalized32(IRREG_FPCOND));
		break;

	case IROp::FpCtrlFromReg:
		gpr.MapDirtyIn(IRREG_FPCOND, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
		LI(SCRATCH1, 0x0181FFFF);
		AND(SCRATCH1, gpr.R(inst.src1), SCRATCH1);
		// Extract the new fpcond value.
		if (cpu_info.RiscV_Zbs) {
			BEXTI(gpr.R(IRREG_FPCOND), SCRATCH1, 23);
		} else {
			SRLI(gpr.R(IRREG_FPCOND), SCRATCH1, 23);
			ANDI(gpr.R(IRREG_FPCOND), gpr.R(IRREG_FPCOND), 1);
		}
		SW(SCRATCH1, CTXREG, IRREG_FCR31 * 4);
		break;

	case IROp::FpCtrlToReg:
		gpr.MapDirtyIn(inst.dest, IRREG_FPCOND, MapType::AVOID_LOAD_MARK_NORM32);
		// Load fcr31 and clear the fpcond bit.
		LW(SCRATCH1, CTXREG, IRREG_FCR31 * 4);
		if (cpu_info.RiscV_Zbs) {
			BCLRI(SCRATCH1, SCRATCH1, 23);
		} else {
			LI(SCRATCH2, ~(1 << 23));
			AND(SCRATCH1, SCRATCH1, SCRATCH2);
		}

		// Now get the correct fpcond bit.
		ANDI(SCRATCH2, gpr.R(IRREG_FPCOND), 1);
		SLLI(SCRATCH2, SCRATCH2, 23);
		OR(gpr.R(inst.dest), SCRATCH1, SCRATCH2);

		// Also update mips->fcr31 while we're here.
		SW(gpr.R(inst.dest), CTXREG, IRREG_FCR31 * 4);
		break;

	case IROp::VfpuCtrlToReg:
		gpr.MapDirtyIn(inst.dest, IRREG_VFPU_CTRL_BASE + inst.src1);
		MV(gpr.R(inst.dest), gpr.R(IRREG_VFPU_CTRL_BASE + inst.src1));
		gpr.MarkGPRDirty(inst.dest, gpr.IsNormalized32(IRREG_VFPU_CTRL_BASE + inst.src1));
		break;

	case IROp::FMovFromGPR:
		fpr.MapReg(inst.dest, MIPSMap::NOINIT);
		if (gpr.IsGPRImm(inst.src1) && gpr.GetGPRImm(inst.src1) == 0) {
			FCVT(FConv::S, FConv::W, fpr.R(inst.dest), R_ZERO);
		} else {
			gpr.MapReg(inst.src1);
			FMV(FMv::W, FMv::X, fpr.R(inst.dest), gpr.R(inst.src1));
		}
		break;

	case IROp::FMovToGPR:
		gpr.MapReg(inst.dest, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
		fpr.MapReg(inst.src1);
		FMV(FMv::X, FMv::W, gpr.R(inst.dest), fpr.R(inst.src1));
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

		LoadStaticRegisters();
		// This is always followed by an ExitToPC, where we check coreState.
		break;

	case IROp::CallReplacement:
		FlushAll();
		SaveStaticRegisters();
		QuickCallFunction(GetReplacementFunc(inst.constant)->replaceFunc, SCRATCH2);
		LoadStaticRegisters();
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
