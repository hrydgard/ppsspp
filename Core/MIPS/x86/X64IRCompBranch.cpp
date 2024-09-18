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

#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

// This file contains compilation for exits.
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

void X64JitBackend::CompIR_Exit(IRInst inst) {
	CONDITIONAL_DISABLE;

	X64Reg exitReg = INVALID_REG;
	switch (inst.op) {
	case IROp::ExitToConst:
		FlushAll();
		WriteConstExit(inst.constant);
		break;

	case IROp::ExitToReg:
		exitReg = regs_.MapGPR(inst.src1);
		FlushAll();
		MOV(32, R(SCRATCH1), R(exitReg));
		JMP(dispatcherPCInSCRATCH1_, true);
		break;

	case IROp::ExitToPC:
		FlushAll();
		JMP(dispatcherCheckCoreState_, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_ExitIf(IRInst inst) {
	CONDITIONAL_DISABLE;

	X64Reg lhs = INVALID_REG;
	X64Reg rhs = INVALID_REG;
	FixupBranch fixup;
	switch (inst.op) {
	case IROp::ExitToConstIfEq:
	case IROp::ExitToConstIfNeq:
		regs_.Map(inst);
		lhs = regs_.RX(inst.src1);
		rhs = regs_.RX(inst.src2);
		// This won't change those regs, intentionally.  It might affect flags, though.
		FlushAll();

		CMP(32, R(lhs), R(rhs));
		switch (inst.op) {
		case IROp::ExitToConstIfEq:
			fixup = J_CC(CC_NE);
			break;

		case IROp::ExitToConstIfNeq:
			fixup = J_CC(CC_E);
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
		lhs = regs_.RX(inst.src1);
		FlushAll();

		CMP(32, R(lhs), Imm32(0));
		switch (inst.op) {
		case IROp::ExitToConstIfGtZ:
			fixup = J_CC(CC_LE, lhs);
			break;

		case IROp::ExitToConstIfGeZ:
			fixup = J_CC(CC_L, lhs);
			break;

		case IROp::ExitToConstIfLtZ:
			fixup = J_CC(CC_GE, lhs);
			break;

		case IROp::ExitToConstIfLeZ:
			fixup = J_CC(CC_G, lhs);
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

#endif
