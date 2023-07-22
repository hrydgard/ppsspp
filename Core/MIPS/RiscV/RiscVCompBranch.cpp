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

#include "Core/MemMap.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"

// This file contains compilation for exits, syscalls, and related.
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

void RiscVJit::CompIR_Exit(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg exitReg = INVALID_REG;
	switch (inst.op) {
	case IROp::ExitToConst:
		FlushAll();
		LI(SCRATCH1, inst.constant);
		QuickJ(R_RA, dispatcherPCInSCRATCH1_);
		break;

	case IROp::ExitToReg:
		exitReg = gpr.MapReg(inst.src1);
		FlushAll();
		// TODO: If ever we don't read this back in dispatcherPCInSCRATCH1_, we should zero upper.
		MV(SCRATCH1, exitReg);
		QuickJ(R_RA, dispatcherPCInSCRATCH1_);
		break;

	case IROp::ExitToPC:
		FlushAll();
		QuickJ(R_RA, dispatcher_);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_ExitIf(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg lhs = INVALID_REG;
	RiscVReg rhs = INVALID_REG;
	FixupBranch fixup;
	switch (inst.op) {
	case IROp::ExitToConstIfEq:
	case IROp::ExitToConstIfNeq:
		gpr.MapInIn(inst.src1, inst.src2);
		lhs = gpr.R(inst.src1);
		rhs = gpr.R(inst.src2);
		FlushAll();

		// For proper compare, we must sign extend so they both match or don't match.
		// But don't change pointers, in case one is SP (happens in LittleBigPlanet.)
		if (XLEN >= 64 && gpr.IsMappedAsPointer(inst.src1)) {
			ADDIW(SCRATCH1, lhs, 0);
			lhs = SCRATCH1;
		} else if (XLEN >= 64 && lhs != R_ZERO) {
			ADDIW(lhs, lhs, 0);
		}
		if (XLEN >= 64 && gpr.IsMappedAsPointer(inst.src2)) {
			ADDIW(SCRATCH2, rhs, 0);
			rhs = SCRATCH2;
		} else if (XLEN >= 64 && rhs != R_ZERO) {
			ADDIW(rhs, rhs, 0);
		}

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

		LI(SCRATCH1, inst.constant);
		QuickJ(R_RA, dispatcherPCInSCRATCH1_);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfGtZ:
	case IROp::ExitToConstIfGeZ:
	case IROp::ExitToConstIfLtZ:
	case IROp::ExitToConstIfLeZ:
		lhs = gpr.MapReg(inst.src1);
		FlushAll();

		// For proper compare, we must sign extend.
		if (XLEN >= 64 && gpr.IsMappedAsPointer(inst.src1)) {
			ADDIW(SCRATCH1, lhs, 0);
			lhs = SCRATCH1;
		} else if (XLEN >= 64 && lhs != R_ZERO) {
			ADDIW(lhs, lhs, 0);
		}

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

		LI(SCRATCH1, inst.constant);
		QuickJ(R_RA, dispatcherPCInSCRATCH1_);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfFpTrue:
	case IROp::ExitToConstIfFpFalse:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
