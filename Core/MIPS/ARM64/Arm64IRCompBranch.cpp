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

#include "Core/MIPS/ARM64/Arm64IRJit.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"

// This file contains compilation for exits.
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

void Arm64JitBackend::CompIR_Exit(IRInst inst) {
	CONDITIONAL_DISABLE;

	ARM64Reg exitReg = INVALID_REG;
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

void Arm64JitBackend::CompIR_ExitIf(IRInst inst) {
	CONDITIONAL_DISABLE;

	ARM64Reg lhs = INVALID_REG;
	ARM64Reg rhs = INVALID_REG;
	FixupBranch fixup;
	switch (inst.op) {
	case IROp::ExitToConstIfEq:
	case IROp::ExitToConstIfNeq:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			lhs = regs_.MapGPR(inst.src2);
			FlushAll();

			if (inst.op == IROp::ExitToConstIfEq)
				fixup = CBNZ(lhs);
			else if (inst.op == IROp::ExitToConstIfNeq)
				fixup = CBZ(lhs);
			else
				_assert_(false);
		} else if (regs_.IsGPRImm(inst.src2) && regs_.GetGPRImm(inst.src2) == 0) {
			lhs = regs_.MapGPR(inst.src1);
			FlushAll();

			if (inst.op == IROp::ExitToConstIfEq)
				fixup = CBNZ(lhs);
			else if (inst.op == IROp::ExitToConstIfNeq)
				fixup = CBZ(lhs);
			else
				_assert_(false);
		} else {
			regs_.Map(inst);
			lhs = regs_.R(inst.src1);
			rhs = regs_.R(inst.src2);
			FlushAll();

			CMP(lhs, rhs);
			if (inst.op == IROp::ExitToConstIfEq)
				fixup = B(CC_NEQ);
			else if (inst.op == IROp::ExitToConstIfNeq)
				fixup = B(CC_EQ);
			else
				_assert_(false);
		}

		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfGtZ:
		lhs = regs_.MapGPR(inst.src1);
		FlushAll();
		CMP(lhs, 0);
		fixup = B(CC_LE);
		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfGeZ:
		// In other words, exit if sign bit is 0.
		lhs = regs_.MapGPR(inst.src1);
		FlushAll();
		fixup = TBNZ(lhs, 31);
		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfLtZ:
		// In other words, exit if sign bit is 1.
		lhs = regs_.MapGPR(inst.src1);
		FlushAll();
		fixup = TBZ(lhs, 31);
		WriteConstExit(inst.constant);
		SetJumpTarget(fixup);
		break;

	case IROp::ExitToConstIfLeZ:
		lhs = regs_.MapGPR(inst.src1);
		FlushAll();
		CMP(lhs, 0);
		fixup = B(CC_GT);
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

#endif
