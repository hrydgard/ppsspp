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

// This file contains compilation for floating point related instructions.
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

void X64JitBackend::CompIR_FArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FAdd:
	case IROp::FSub:
	case IROp::FMul:
	case IROp::FDiv:
	case IROp::FSqrt:
	case IROp::FNeg:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMov:
	case IROp::FAbs:
	case IROp::FSign:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FCompare(IRInst inst) {
	CONDITIONAL_DISABLE;

	constexpr IRReg IRREG_VFPU_CC = IRREG_VFPU_CTRL_BASE + VFPU_CTRL_CC;

	switch (inst.op) {
	case IROp::FCmp:
	case IROp::FCmovVfpuCC:
	case IROp::FCmpVfpuBit:
	case IROp::FCmpVfpuAggregate:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FCondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMin:
	case IROp::FMax:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FCvt(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FCvtWS:
	case IROp::FCvtSW:
	case IROp::FCvtScaledWS:
	case IROp::FCvtScaledSW:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FRound(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FRound:
	case IROp::FTrunc:
	case IROp::FCeil:
	case IROp::FFloor:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FSat(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FSat0_1:
	case IROp::FSatMinus1_1:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FSpecial(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FSin:
	case IROp::FCos:
	case IROp::FRSqrt:
	case IROp::FRecip:
	case IROp::FAsin:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_RoundingMode(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::RestoreRoundingMode:
		RestoreRoundingMode();
		break;

	case IROp::ApplyRoundingMode:
		ApplyRoundingMode();
		break;

	case IROp::UpdateRoundingMode:
		// TODO: We might want to do something here?
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
