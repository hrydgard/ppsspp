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

// This file contains compilation for floating point related instructions.
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

void RiscVJit::CompIR_FArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FAdd:
		fpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
		FADD(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
		break;

	case IROp::FSub:
		fpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
		FSUB(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
		break;

	case IROp::FMul:
		fpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
		// TODO: If FMUL consistently produces NAN across chip vendors, we can skip this.
		// Luckily this does match the RISC-V canonical NAN.
		if (inst.src1 != inst.src2) {
			// These will output 0x80/0x01 if infinity, 0x10/0x80 if zero.
			// We need to check if one is infinity and the other zero.

			// First, try inf * zero.
			FCLASS(32, SCRATCH1, fpr.R(inst.src1));
			FCLASS(32, SCRATCH2, fpr.R(inst.src2));
			ANDI(R_RA, SCRATCH1, 0x81);
			FixupBranch lhsNotInf = BEQ(R_RA, R_ZERO);
			ANDI(R_RA, SCRATCH2, 0x18);
			FixupBranch infZero = BNE(R_RA, R_ZERO);

			// Okay, what about the other order?
			SetJumpTarget(lhsNotInf);
			ANDI(R_RA, SCRATCH1, 0x18);
			FixupBranch lhsNotZero = BEQ(R_RA, R_ZERO);
			ANDI(R_RA, SCRATCH2, 0x81);
			FixupBranch zeroInf = BNE(R_RA, R_ZERO);

			// Nope, all good.
			SetJumpTarget(lhsNotZero);
			FMUL(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
			FixupBranch skip = J();

			SetJumpTarget(infZero);
			SetJumpTarget(zeroInf);
			LI(SCRATCH1, 0x7FC00000);
			FMV(FMv::W, FMv::X, fpr.R(inst.dest), SCRATCH1);

			SetJumpTarget(skip);
		} else {
			FMUL(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
		}
		break;

	case IROp::FDiv:
		fpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
		FDIV(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
		break;

	case IROp::FSqrt:
		fpr.MapDirtyIn(inst.dest, inst.src1);
		FSQRT(32, fpr.R(inst.dest), fpr.R(inst.src1));
		break;

	case IROp::FNeg:
		fpr.MapDirtyIn(inst.dest, inst.src1);
		FNEG(32, fpr.R(inst.dest), fpr.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_FCondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMin:
	case IROp::FMax:
		// TODO: These are tricky, have to handle order correctly.
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_FAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMov:
		fpr.MapDirtyIn(inst.dest, inst.src1);
		FMV(32, fpr.R(inst.dest), fpr.R(inst.src1));
		break;

	case IROp::FAbs:
		fpr.MapDirtyIn(inst.dest, inst.src1);
		FABS(32, fpr.R(inst.dest), fpr.R(inst.src1));
		break;

	case IROp::FSign:
	{
		fpr.MapDirtyIn(inst.dest, inst.src1);
		// Check if it's negative zero, either 0x10/0x08 is zero.
		FCLASS(32, SCRATCH1, fpr.R(inst.src1));
		ANDI(SCRATCH1, SCRATCH1, 0x18);
		SEQZ(SCRATCH1, SCRATCH1);
		// Okay, it's zero if zero, 1 otherwise.  Convert 1 to a constant 1.0.
		// Probably non-zero is the common case, so we make that the straight line.
		FixupBranch skipOne = BEQ(SCRATCH1, R_ZERO);
		LI(SCRATCH1, 1.0f);

		// Now we just need the sign from it.
		FMV(FMv::X, FMv::W, SCRATCH2, fpr.R(inst.src1));
		// Use a wall to isolate the sign, and combine.
		SRAIW(SCRATCH2, SCRATCH2, 31);
		SLLIW(SCRATCH2, SCRATCH2, 31);
		OR(SCRATCH1, SCRATCH1, SCRATCH2);

		SetJumpTarget(skipOne);
		FMV(FMv::W, FMv::X, fpr.R(inst.dest), SCRATCH1);
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_FRound(IRInst inst) {
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

void RiscVJit::CompIR_FCvt(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FCvtWS:
	case IROp::FCvtScaledWS:
	case IROp::FCvtSW:
	case IROp::FCvtScaledSW:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_FSat(IRInst inst) {
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

void RiscVJit::CompIR_FCompare(IRInst inst) {
	CONDITIONAL_DISABLE;

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

void RiscVJit::CompIR_RoundingMode(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::RestoreRoundingMode:
		RestoreRoundingMode();
		break;

	case IROp::ApplyRoundingMode:
		ApplyRoundingMode();
		break;

	case IROp::UpdateRoundingMode:
		// We don't need to do anything, instructions allow a "dynamic" rounding mode.
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_FSpecial(IRInst inst) {
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

} // namespace MIPSComp
