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
#include "Core/MIPS/MIPSTables.h"
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

void RiscVJit::CompIR_Basic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::SetConst:
		// Sign extend all constants.  We get 0xFFFFFFFF sometimes, and it's more work to truncate.
		// The register only holds 32 bits in the end anyway.
		gpr.SetImm(inst.dest, (int32_t)inst.constant);
		break;

	case IROp::SetConstF:
		CompIR_Generic(inst);
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

void RiscVJit::CompIR_Transfer(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::SetCtrlVFPU:
	case IROp::SetCtrlVFPUReg:
	case IROp::SetCtrlVFPUFReg:
	case IROp::FpCondToReg:
	case IROp::VfpuCtrlToReg:
	case IROp::FMovFromGPR:
	case IROp::FMovToGPR:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_System(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Interpret:
		// IR protects us against this being a branching instruction (well, hopefully.)
		FlushAll();
		SaveStaticRegisters();
		LI(X10, (int32_t)inst.constant);
		QuickCallFunction((const u8 *)MIPSGetInterpretFunc(MIPSOpcode(inst.constant)));
		LoadStaticRegisters();
		break;

	case IROp::Syscall:
		FlushAll();
		SaveStaticRegisters();

#ifdef USE_PROFILER
		// When profiling, we can't skip CallSyscall, since it times syscalls.
		LI(X10, (int32_t)inst.constant);
		QuickCallFunction(&CallSyscall);
#else
		// Skip the CallSyscall where possible.
		{
			MIPSOpcode op(inst.constant);
			void *quickFunc = GetQuickSyscallFunc(op);
			if (quickFunc) {
				LI(X10, (uintptr_t)GetSyscallFuncPointer(op));
				QuickCallFunction((const u8 *)quickFunc);
			} else {
				LI(X10, (int32_t)inst.constant);
				QuickCallFunction(&CallSyscall);
			}
		}
#endif

		LoadStaticRegisters();
		// This is always followed by an ExitToPC, where we check coreState.
		break;

	case IROp::CallReplacement:
		FlushAll();
		SaveStaticRegisters();
		QuickCallFunction(GetReplacementFunc(inst.constant)->replaceFunc);
		LoadStaticRegisters();
		SUB(DOWNCOUNTREG, DOWNCOUNTREG, X10);
		break;

	case IROp::Break:
		FlushAll();
		// This doesn't naturally have restore/apply around it.
		RestoreRoundingMode(true);
		SaveStaticRegisters();
		MovFromPC(X10);
		QuickCallFunction(&Core_Break);
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

void RiscVJit::CompIR_Breakpoint(IRInst inst) {
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

void RiscVJit::CompIR_ValidateAddress(IRInst inst) {
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
