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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include "Common/Profiler/Profiler.h"
#include "Core/Core.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

// This file contains compilation for basic PC/downcount accounting, syscalls, debug funcs, etc.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace Gen;
using namespace X64IRJitConstants;

void X64JitBackend::CompIR_Basic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Downcount:
		//ADD(32, R(DOWNCOUNTREG), R(DOWNCOUNTREG), Imm32(-(s32)inst.constant));
		SUB(32, MDisp(CTXREG, downcountOffset), Imm32((s32)inst.constant));
		break;

	case IROp::SetConst:
		regs_.SetGPRImm(inst.dest, inst.constant);
		break;

	case IROp::SetConstF:
		regs_.Map(inst);
		if (inst.constant == 0) {
			XORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
		} else {
			MOV(32, R(SCRATCH1), Imm32(inst.constant));
			MOVD_xmm(regs_.FX(inst.dest), R(SCRATCH1));
		}
		break;

	case IROp::SetPC:
	case IROp::SetPCConst:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Breakpoint(IRInst inst) {
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

void X64JitBackend::CompIR_System(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Syscall:
	case IROp::CallReplacement:
	case IROp::Break:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Transfer(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::SetCtrlVFPU:
	case IROp::SetCtrlVFPUReg:
	case IROp::SetCtrlVFPUFReg:
		CompIR_Generic(inst);
		break;

	case IROp::FpCondFromReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		MOV(32, regs_.R(IRREG_FPCOND), regs_.R(inst.src1));
		break;

	case IROp::FpCondToReg:
		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::INIT } });
		MOV(32, regs_.R(inst.dest), regs_.R(IRREG_FPCOND));
		break;

	case IROp::FpCtrlFromReg:
	case IROp::FpCtrlToReg:
	case IROp::VfpuCtrlToReg:
		CompIR_Generic(inst);
		break;

	case IROp::FMovFromGPR:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
			XORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
		} else {
			regs_.Map(inst);
			MOVD_xmm(regs_.FX(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::FMovToGPR:
		regs_.Map(inst);
		MOVD_xmm(regs_.R(inst.dest), regs_.FX(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_ValidateAddress(IRInst inst) {
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

#endif
