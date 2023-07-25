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

// This file contains compilation for vector instructions.
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

void RiscVJit::CompIR_VecAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Init:
	case IROp::Vec4Shuffle:
	case IROp::Vec4Mov:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_VecArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Add:
	case IROp::Vec4Sub:
	case IROp::Vec4Mul:
	case IROp::Vec4Div:
	case IROp::Vec4Scale:
	case IROp::Vec4Neg:
	case IROp::Vec4Abs:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_VecHoriz(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Dot:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec2Unpack16To31:
	case IROp::Vec2Unpack16To32:
	case IROp::Vec4Unpack8To32:
	case IROp::Vec4DuplicateUpperBitsAndShift1:
	case IROp::Vec4Pack31To8:
	case IROp::Vec4Pack32To8:
	case IROp::Vec2Pack31To16:
	case IROp::Vec2Pack32To16:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_VecClamp(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4ClampToZero:
	case IROp::Vec2ClampToZero:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
