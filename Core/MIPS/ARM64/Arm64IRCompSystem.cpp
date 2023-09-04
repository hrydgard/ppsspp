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
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
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
	case IROp::MemoryCheck:
		CompIR_Generic(inst);
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

		LoadStaticRegisters();
		// This is always followed by an ExitToPC, where we check coreState.
		break;

	case IROp::CallReplacement:
		FlushAll();
		SaveStaticRegisters();
		QuickCallFunction(SCRATCH2_64, GetReplacementFunc(inst.constant)->replaceFunc);
		LoadStaticRegisters();
		SUB(DOWNCOUNTREG, DOWNCOUNTREG, W0);
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
}

} // namespace MIPSComp

#endif
