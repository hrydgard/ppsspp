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

#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"

// This file contains compilation for exits.
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

void ArmJitBackend::CompIR_Exit(IRInst inst) {
	CONDITIONAL_DISABLE;

	ARMReg exitReg = INVALID_REG;
	switch (inst.op) {
	case IROp::ExitToConst:
		FlushAll();
		WriteConstExit(inst.constant);
		break;

	case IROp::ExitToReg:
		exitReg = regs_.MapGPR(inst.src1);
		FlushAll();
		MOV(SCRATCH1, exitReg);
		B(dispatcherPCInSCRATCH1_);
		break;

	case IROp::ExitToPC:
		FlushAll();
		B(dispatcherCheckCoreState_);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_ExitIf(IRInst inst) {
	CONDITIONAL_DISABLE;

	ARMReg lhs = INVALID_REG;
	ARMReg rhs = INVALID_REG;
	FixupBranch fixup;
	switch (inst.op) {
	case IROp::ExitToConstIfEq:
	case IROp::ExitToConstIfNeq:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			lhs = regs_.MapGPR(inst.src2);
			FlushAll();
			CMP(lhs, Operand2(0));
		} else if (regs_.IsGPRImm(inst.src2) && regs_.GetGPRImm(inst.src2) == 0) {
			lhs = regs_.MapGPR(inst.src1);
			FlushAll();
			CMP(lhs, Operand2(0));
		} else {
			regs_.Map(inst);
			lhs = regs_.R(inst.src1);
			rhs = regs_.R(inst.src2);
			FlushAll();
			CMP(lhs, rhs);
		}

		fixup = B_CC(inst.op == IROp::ExitToConstIfEq ? CC_NEQ : CC_EQ);
		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfGtZ:
	case IROp::ExitToConstIfGeZ:
	case IROp::ExitToConstIfLtZ:
	case IROp::ExitToConstIfLeZ:
	{
		lhs = regs_.MapGPR(inst.src1);
		FlushAll();
		CMP(lhs, Operand2(0));
		// Branch over the exit when the condition fails.
		CCFlags skip = CC_AL;
		switch (inst.op) {
		case IROp::ExitToConstIfGtZ: skip = CC_LE; break;
		case IROp::ExitToConstIfGeZ: skip = CC_LT; break;
		case IROp::ExitToConstIfLtZ: skip = CC_GE; break;
		case IROp::ExitToConstIfLeZ: skip = CC_GT; break;
		default: break;
		}
		fixup = B_CC(skip);
		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;
	}

	case IROp::ExitToConstIfFpTrue:
	case IROp::ExitToConstIfFpFalse:
		// Note: not used.
		DISABLE;
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
