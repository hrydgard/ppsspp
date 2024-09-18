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

#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"

// This file contains compilation for exits.
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

void RiscVJitBackend::CompIR_Exit(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg exitReg = INVALID_REG;
	switch (inst.op) {
	case IROp::ExitToConst:
		FlushAll();
		WriteConstExit(inst.constant);
		break;

	case IROp::ExitToReg:
		exitReg = regs_.MapGPR(inst.src1);
		FlushAll();
		// TODO: If ever we don't read this back in dispatcherPCInSCRATCH1_, we should zero upper.
		MV(SCRATCH1, exitReg);
		QuickJ(R_RA, dispatcherPCInSCRATCH1_);
		break;

	case IROp::ExitToPC:
		FlushAll();
		QuickJ(R_RA, dispatcherCheckCoreState_);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_ExitIf(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg lhs = INVALID_REG;
	RiscVReg rhs = INVALID_REG;
	FixupBranch fixup;
	switch (inst.op) {
	case IROp::ExitToConstIfEq:
	case IROp::ExitToConstIfNeq:
		regs_.Map(inst);
		// We can't use SCRATCH1, which is destroyed by FlushAll()... but cheat and use R_RA.
		NormalizeSrc12(inst, &lhs, &rhs, R_RA, SCRATCH2, true);
		FlushAll();

		switch (inst.op) {
		case IROp::ExitToConstIfEq:
			fixup = BNE(lhs, rhs);
			break;

		case IROp::ExitToConstIfNeq:
			fixup = BEQ(lhs, rhs);
			break;

		default:
			INVALIDOP;
			break;
		}

		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfGtZ:
	case IROp::ExitToConstIfGeZ:
	case IROp::ExitToConstIfLtZ:
	case IROp::ExitToConstIfLeZ:
		regs_.Map(inst);
		NormalizeSrc1(inst, &lhs, SCRATCH2, true);
		FlushAll();

		switch (inst.op) {
		case IROp::ExitToConstIfGtZ:
			fixup = BGE(R_ZERO, lhs);
			break;

		case IROp::ExitToConstIfGeZ:
			fixup = BLT(lhs, R_ZERO);
			break;

		case IROp::ExitToConstIfLtZ:
			fixup = BGE(lhs, R_ZERO);
			break;

		case IROp::ExitToConstIfLeZ:
			fixup = BLT(R_ZERO, lhs);
			break;

		default:
			INVALIDOP;
			break;
		}

		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;

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
