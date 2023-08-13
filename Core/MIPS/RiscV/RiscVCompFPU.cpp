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

void RiscVJitBackend::CompIR_FArith(IRInst inst) {
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
		// We'll assume everyone will make it such that 0 * infinity = NAN properly.
		// See blame on this comment if that proves untrue.
		FMUL(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
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

void RiscVJitBackend::CompIR_FCondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;
	if (inst.op != IROp::FMin && inst.op != IROp::FMax)
		INVALIDOP;
	bool maxCondition = inst.op == IROp::FMax;

	// FMin and FMax are used by VFPU and handle NAN/INF as just a larger exponent.
	fpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
	FCLASS(32, SCRATCH1, fpr.R(inst.src1));
	FCLASS(32, SCRATCH2, fpr.R(inst.src2));

	// If either side is a NAN, it needs to participate in the comparison.
	OR(SCRATCH1, SCRATCH1, SCRATCH2);
	// NAN is either 0x100 or 0x200.
	ANDI(SCRATCH1, SCRATCH1, 0x300);
	FixupBranch useNormalCond = BEQ(SCRATCH1, R_ZERO);

	// Time to use bits... classify won't help because it ignores -NAN.
	FMV(FMv::X, FMv::W, SCRATCH1, fpr.R(inst.src1));
	FMV(FMv::X, FMv::W, SCRATCH2, fpr.R(inst.src2));

	// If both are negative, we flip the comparison (not two's compliment.)
	// We cheat and use RA...
	AND(R_RA, SCRATCH1, SCRATCH2);
	SRLIW(R_RA, R_RA, 31);

	if (cpu_info.RiscV_Zbb) {
		FixupBranch swapCompare = BNE(R_RA, R_ZERO);
		if (maxCondition)
			MAX(SCRATCH1, SCRATCH1, SCRATCH2);
		else
			MIN(SCRATCH1, SCRATCH1, SCRATCH2);
		FixupBranch skipSwapCompare = J();
		SetJumpTarget(swapCompare);
		if (maxCondition)
			MIN(SCRATCH1, SCRATCH1, SCRATCH2);
		else
			MAX(SCRATCH1, SCRATCH1, SCRATCH2);
		SetJumpTarget(skipSwapCompare);
	} else {
		RiscVReg isSrc1LowerReg = gpr.GetAndLockTempR();
		gpr.ReleaseSpillLocksAndDiscardTemps();

		SLT(isSrc1LowerReg, SCRATCH1, SCRATCH2);
		// Flip the flag (to reverse the min/max) based on if both were negative.
		XOR(isSrc1LowerReg, isSrc1LowerReg, R_RA);
		FixupBranch useSrc1;
		if (maxCondition)
			useSrc1 = BEQ(isSrc1LowerReg, R_ZERO);
		else
			useSrc1 = BNE(isSrc1LowerReg, R_ZERO);
		MV(SCRATCH1, SCRATCH2);
		SetJumpTarget(useSrc1);
	}

	FMV(FMv::W, FMv::X, fpr.R(inst.dest), SCRATCH1);
	FixupBranch finish = J();

	SetJumpTarget(useNormalCond);
	if (maxCondition)
		FMAX(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
	else
		FMIN(32, fpr.R(inst.dest), fpr.R(inst.src1), fpr.R(inst.src2));
	SetJumpTarget(finish);
}

void RiscVJitBackend::CompIR_FAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMov:
		if (inst.dest != inst.src1) {
			fpr.MapDirtyIn(inst.dest, inst.src1);
			FMV(32, fpr.R(inst.dest), fpr.R(inst.src1));
		}
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

void RiscVJitBackend::CompIR_FRound(IRInst inst) {
	CONDITIONAL_DISABLE;

	// TODO: If this is followed by a GPR transfer, might want to combine.
	fpr.MapDirtyIn(inst.dest, inst.src1);

	switch (inst.op) {
	case IROp::FRound:
		FCVT(FConv::W, FConv::S, SCRATCH1, fpr.R(inst.src1), Round::NEAREST_EVEN);
		break;

	case IROp::FTrunc:
		FCVT(FConv::W, FConv::S, SCRATCH1, fpr.R(inst.src1), Round::TOZERO);
		break;

	case IROp::FCeil:
		FCVT(FConv::W, FConv::S, SCRATCH1, fpr.R(inst.src1), Round::UP);
		break;

	case IROp::FFloor:
		FCVT(FConv::W, FConv::S, SCRATCH1, fpr.R(inst.src1), Round::DOWN);
		break;

	default:
		INVALIDOP;
		break;
	}

	FMV(FMv::W, FMv::X, fpr.R(inst.dest), SCRATCH1);
}

void RiscVJitBackend::CompIR_FCvt(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg tempReg = INVALID_REG;
	switch (inst.op) {
	case IROp::FCvtWS:
		CompIR_Generic(inst);
		break;

	case IROp::FCvtSW:
		// TODO: This is probably proceeded by a GPR transfer, might be ideal to combine.
		fpr.MapDirtyIn(inst.dest, inst.src1);
		FMV(FMv::X, FMv::W, SCRATCH1, fpr.R(inst.src1));
		FCVT(FConv::S, FConv::W, fpr.R(inst.dest), SCRATCH1);
		break;

	case IROp::FCvtScaledWS:
		if (cpu_info.RiscV_D) {
			Round rm = Round::NEAREST_EVEN;
			switch (inst.src2 >> 6) {
			case 0: rm = Round::NEAREST_EVEN; break;
			case 1: rm = Round::TOZERO; break;
			case 2: rm = Round::UP; break;
			case 3: rm = Round::DOWN; break;
			}

			tempReg = fpr.MapDirtyInTemp(inst.dest, inst.src1);
			// Prepare the double src1 and the multiplier.
			FCVT(FConv::D, FConv::S, fpr.R(inst.dest), fpr.R(inst.src1));
			LI(SCRATCH1, 1UL << (inst.src2 & 0x1F));
			FCVT(FConv::D, FConv::WU, tempReg, SCRATCH1, rm);

			FMUL(64, fpr.R(inst.dest), fpr.R(inst.dest), tempReg, rm);
			// NAN and clamping should all be correct.
			FCVT(FConv::W, FConv::D, SCRATCH1, fpr.R(inst.dest), rm);
			// TODO: Could combine with a transfer, often is one...
			FMV(FMv::W, FMv::X, fpr.R(inst.dest), SCRATCH1);
		} else {
			CompIR_Generic(inst);
		}
		break;

	case IROp::FCvtScaledSW:
		// TODO: This is probably proceeded by a GPR transfer, might be ideal to combine.
		tempReg = fpr.MapDirtyInTemp(inst.dest, inst.src1);
		FMV(FMv::X, FMv::W, SCRATCH1, fpr.R(inst.src1));
		FCVT(FConv::S, FConv::W, fpr.R(inst.dest), SCRATCH1);

		// Pre-divide so we can avoid any actual divide.
		LI(SCRATCH1, 1.0f / (1UL << (inst.src2 & 0x1F)));
		FMV(FMv::W, FMv::X, tempReg, SCRATCH1);
		FMUL(32, fpr.R(inst.dest), fpr.R(inst.dest), tempReg);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_FSat(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg tempReg = INVALID_REG;
	FixupBranch skipLower;
	FixupBranch finishLower;
	FixupBranch skipHigher;
	switch (inst.op) {
	case IROp::FSat0_1:
		tempReg = fpr.MapDirtyInTemp(inst.dest, inst.src1);
		if (inst.dest != inst.src1)
			FMV(32, fpr.R(inst.dest), fpr.R(inst.src1));

		// First, set SCRATCH1 = clamp to zero, SCRATCH2 = clamp to one.
		FCVT(FConv::S, FConv::W, tempReg, R_ZERO);
		// FLE here is intentional to convert -0.0 to +0.0.
		FLE(32, SCRATCH1, fpr.R(inst.src1), tempReg);
		LI(SCRATCH2, 1.0f);
		FMV(FMv::W, FMv::X, tempReg, SCRATCH2);
		FLT(32, SCRATCH2, tempReg, fpr.R(inst.src1));

		skipLower = BEQ(SCRATCH1, R_ZERO);
		FCVT(FConv::S, FConv::W, fpr.R(inst.dest), R_ZERO);
		finishLower = J();

		SetJumpTarget(skipLower);
		skipHigher = BEQ(SCRATCH2, R_ZERO);
		// Still has 1.0 in it.
		FMV(32, fpr.R(inst.dest), tempReg);

		SetJumpTarget(finishLower);
		SetJumpTarget(skipHigher);
		break;

	case IROp::FSatMinus1_1:
		tempReg = fpr.MapDirtyInTemp(inst.dest, inst.src1);
		if (inst.dest != inst.src1)
			FMV(32, fpr.R(inst.dest), fpr.R(inst.src1));

		// First, set SCRATCH1 = clamp to negative, SCRATCH2 = clamp to positive.
		LI(SCRATCH2, -1.0f);
		FMV(FMv::W, FMv::X, tempReg, SCRATCH2);
		FLT(32, SCRATCH1, fpr.R(inst.src1), tempReg);
		FNEG(32, tempReg, tempReg);
		FLT(32, SCRATCH2, tempReg, fpr.R(inst.src1));

		// But we can actually do one branch, using sign-injection to keep the original sign.
		OR(SCRATCH1, SCRATCH1, SCRATCH2);

		skipLower = BEQ(SCRATCH1, R_ZERO);
		FSGNJ(32, fpr.R(inst.dest), tempReg, fpr.R(inst.dest));
		SetJumpTarget(skipLower);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_FCompare(IRInst inst) {
	CONDITIONAL_DISABLE;

	constexpr IRRegIndex IRREG_VFPUL_CC = IRREG_VFPU_CTRL_BASE + VFPU_CTRL_CC;

	switch (inst.op) {
	case IROp::FCmp:
		switch (inst.dest) {
		case IRFpCompareMode::False:
			gpr.SetImm(IRREG_FPCOND, 0);
			break;

		case IRFpCompareMode::EitherUnordered:
			fpr.MapInIn(inst.src1, inst.src2);
			gpr.MapReg(IRREG_FPCOND, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			FCLASS(32, SCRATCH1, fpr.R(inst.src1));
			FCLASS(32, SCRATCH2, fpr.R(inst.src2));
			OR(SCRATCH1, SCRATCH1, SCRATCH2);
			// NAN is 0x100 or 0x200.
			ANDI(SCRATCH1, SCRATCH1, 0x300);
			SNEZ(gpr.R(IRREG_FPCOND), SCRATCH1);
			break;

		case IRFpCompareMode::EqualOrdered:
			fpr.MapInIn(inst.src1, inst.src2);
			gpr.MapReg(IRREG_FPCOND, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			FEQ(32, gpr.R(IRREG_FPCOND), fpr.R(inst.src1), fpr.R(inst.src2));
			break;

		case IRFpCompareMode::EqualUnordered:
			fpr.MapInIn(inst.src1, inst.src2);
			gpr.MapReg(IRREG_FPCOND, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			FEQ(32, gpr.R(IRREG_FPCOND), fpr.R(inst.src1), fpr.R(inst.src2));

			// Now let's just OR in the unordered check.
			FCLASS(32, SCRATCH1, fpr.R(inst.src1));
			FCLASS(32, SCRATCH2, fpr.R(inst.src2));
			OR(SCRATCH1, SCRATCH1, SCRATCH2);
			// NAN is 0x100 or 0x200.
			ANDI(SCRATCH1, SCRATCH1, 0x300);
			SNEZ(SCRATCH1, SCRATCH1);
			OR(gpr.R(IRREG_FPCOND), gpr.R(IRREG_FPCOND), SCRATCH1);
			break;

		case IRFpCompareMode::LessEqualOrdered:
			fpr.MapInIn(inst.src1, inst.src2);
			gpr.MapReg(IRREG_FPCOND, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			FLE(32, gpr.R(IRREG_FPCOND), fpr.R(inst.src1), fpr.R(inst.src2));
			break;

		case IRFpCompareMode::LessEqualUnordered:
			fpr.MapInIn(inst.src1, inst.src2);
			gpr.MapReg(IRREG_FPCOND, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			FLT(32, gpr.R(IRREG_FPCOND), fpr.R(inst.src2), fpr.R(inst.src1));
			SEQZ(gpr.R(IRREG_FPCOND), gpr.R(IRREG_FPCOND));
			break;

		case IRFpCompareMode::LessOrdered:
			fpr.MapInIn(inst.src1, inst.src2);
			gpr.MapReg(IRREG_FPCOND, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			FLT(32, gpr.R(IRREG_FPCOND), fpr.R(inst.src1), fpr.R(inst.src2));
			break;

		case IRFpCompareMode::LessUnordered:
			fpr.MapInIn(inst.src1, inst.src2);
			gpr.MapReg(IRREG_FPCOND, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			FLE(32, gpr.R(IRREG_FPCOND), fpr.R(inst.src2), fpr.R(inst.src1));
			SEQZ(gpr.R(IRREG_FPCOND), gpr.R(IRREG_FPCOND));
			break;
		}
		break;

	case IROp::FCmovVfpuCC:
		gpr.MapReg(IRREG_VFPUL_CC);
		fpr.MapDirtyIn(inst.dest, inst.src1, false);
		if ((inst.src2 & 0xF) == 0) {
			ANDI(SCRATCH1, gpr.R(IRREG_VFPUL_CC), 1);
		} else if (cpu_info.RiscV_Zbs) {
			BEXTI(SCRATCH1, gpr.R(IRREG_VFPUL_CC), inst.src2 & 0xF);
		} else {
			SRLI(SCRATCH1, gpr.R(IRREG_VFPUL_CC), inst.src2 & 0xF);
			ANDI(SCRATCH1, SCRATCH1, 1);
		}
		if ((inst.src2 >> 7) & 1) {
			FixupBranch skip = BEQ(SCRATCH1, R_ZERO);
			FMV(32, fpr.R(inst.dest), fpr.R(inst.src1));
			SetJumpTarget(skip);
		} else {
			FixupBranch skip = BNE(SCRATCH1, R_ZERO);
			FMV(32, fpr.R(inst.dest), fpr.R(inst.src1));
			SetJumpTarget(skip);
		}
		break;

	case IROp::FCmpVfpuBit:
		gpr.MapReg(IRREG_VFPUL_CC, MIPSMap::DIRTY);

		switch (VCondition(inst.dest & 0xF)) {
		case VC_EQ:
			fpr.MapInIn(inst.src1, inst.src2);
			FEQ(32, SCRATCH1, fpr.R(inst.src1), fpr.R(inst.src2));
			break;
		case VC_NE:
			fpr.MapInIn(inst.src1, inst.src2);
			// We could almost negate FEQ, except NAN != NAN.
			// Anything != NAN is false and NAN != NAN is within that, so we only check one side.
			FCLASS(32, SCRATCH2, fpr.R(inst.src2));
			// NAN is 0x100 or 0x200.
			ANDI(SCRATCH2, SCRATCH2, 0x300);
			SNEZ(SCRATCH2, SCRATCH2);

			FEQ(32, SCRATCH1, fpr.R(inst.src1), fpr.R(inst.src2));
			SEQZ(SCRATCH1, SCRATCH1);
			// Just OR in whether that side was a NAN so it's always not equal.
			OR(SCRATCH1, SCRATCH1, SCRATCH2);
			break;
		case VC_LT:
			fpr.MapInIn(inst.src1, inst.src2);
			FLT(32, SCRATCH1, fpr.R(inst.src1), fpr.R(inst.src2));
			break;
		case VC_LE:
			fpr.MapInIn(inst.src1, inst.src2);
			FLE(32, SCRATCH1, fpr.R(inst.src1), fpr.R(inst.src2));
			break;
		case VC_GT:
			fpr.MapInIn(inst.src1, inst.src2);
			FLT(32, SCRATCH1, fpr.R(inst.src2), fpr.R(inst.src1));
			break;
		case VC_GE:
			fpr.MapInIn(inst.src1, inst.src2);
			FLE(32, SCRATCH1, fpr.R(inst.src2), fpr.R(inst.src1));
			break;
		case VC_EZ:
		case VC_NZ:
			fpr.MapReg(inst.src1);
			// Zero is either 0x10 or 0x08.
			FCLASS(32, SCRATCH1, fpr.R(inst.src1));
			ANDI(SCRATCH1, SCRATCH1, 0x18);
			if ((inst.dest & 4) == 0)
				SNEZ(SCRATCH1, SCRATCH1);
			else
				SEQZ(SCRATCH1, SCRATCH1);
			break;
		case VC_EN:
		case VC_NN:
			fpr.MapReg(inst.src1);
			// NAN is either 0x100 or 0x200.
			FCLASS(32, SCRATCH1, fpr.R(inst.src1));
			ANDI(SCRATCH1, SCRATCH1, 0x300);
			if ((inst.dest & 4) == 0)
				SNEZ(SCRATCH1, SCRATCH1);
			else
				SEQZ(SCRATCH1, SCRATCH1);
			break;
		case VC_EI:
		case VC_NI:
			fpr.MapReg(inst.src1);
			// Infinity is either 0x80 or 0x01.
			FCLASS(32, SCRATCH1, fpr.R(inst.src1));
			ANDI(SCRATCH1, SCRATCH1, 0x81);
			if ((inst.dest & 4) == 0)
				SNEZ(SCRATCH1, SCRATCH1);
			else
				SEQZ(SCRATCH1, SCRATCH1);
			break;
		case VC_ES:
		case VC_NS:
			fpr.MapReg(inst.src1);
			// Infinity is either 0x80 or 0x01, NAN is either 0x100 or 0x200.
			FCLASS(32, SCRATCH1, fpr.R(inst.src1));
			ANDI(SCRATCH1, SCRATCH1, 0x381);
			if ((inst.dest & 4) == 0)
				SNEZ(SCRATCH1, SCRATCH1);
			else
				SEQZ(SCRATCH1, SCRATCH1);
			break;
		case VC_TR:
			LI(SCRATCH1, 1);
			break;
		case VC_FL:
			LI(SCRATCH1, 0);
			break;
		}

		ANDI(gpr.R(IRREG_VFPUL_CC), gpr.R(IRREG_VFPUL_CC), ~(1 << (inst.dest >> 4)));
		if ((inst.dest >> 4) != 0)
			SLLI(SCRATCH1, SCRATCH1, inst.dest >> 4);
		OR(gpr.R(IRREG_VFPUL_CC), gpr.R(IRREG_VFPUL_CC), SCRATCH1);
		break;

	case IROp::FCmpVfpuAggregate:
		gpr.MapReg(IRREG_VFPUL_CC, MIPSMap::DIRTY);
		ANDI(SCRATCH1, gpr.R(IRREG_VFPUL_CC), inst.dest);
		// This is the "any bit", easy.
		SNEZ(SCRATCH2, SCRATCH1);
		// To compare to inst.dest for "all", let's simply subtract it and compare to zero.
		ADDI(SCRATCH1, SCRATCH1, -inst.dest);
		SEQZ(SCRATCH1, SCRATCH1);
		// Now we combine those together.
		SLLI(SCRATCH1, SCRATCH1, 5);
		SLLI(SCRATCH2, SCRATCH2, 4);
		OR(SCRATCH1, SCRATCH1, SCRATCH2);

		// Reject those any/all bits and replace them with our own.
		ANDI(gpr.R(IRREG_VFPUL_CC), gpr.R(IRREG_VFPUL_CC), ~0x30);
		OR(gpr.R(IRREG_VFPUL_CC), gpr.R(IRREG_VFPUL_CC), SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_RoundingMode(IRInst inst) {
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

void RiscVJitBackend::CompIR_FSpecial(IRInst inst) {
	CONDITIONAL_DISABLE;

#ifdef __riscv_float_abi_soft
#error Currently hard float is required.
#endif

	auto callFuncF_F = [&](float (*func)(float)){
		gpr.FlushBeforeCall();
		fpr.FlushBeforeCall();
		// It might be in a non-volatile register.
		if (fpr.IsMapped(inst.src1)) {
			FMV(32, F10, fpr.R(inst.src1));
		} else {
			int offset = offsetof(MIPSState, f) + inst.src1 * 4;
			FL(32, F10, CTXREG, offset);
		}
		QuickCallFunction(func, SCRATCH1);

		fpr.MapReg(inst.dest, MIPSMap::NOINIT);
		// If it's already F10, we're done - MapReg doesn't actually overwrite the reg in that case.
		if (fpr.R(inst.dest) != F10) {
			FMV(32, fpr.R(inst.dest), F10);
		}
	};

	RiscVReg tempReg = INVALID_REG;
	switch (inst.op) {
	case IROp::FSin:
		callFuncF_F(&vfpu_sin);
		break;

	case IROp::FCos:
		callFuncF_F(&vfpu_cos);
		break;

	case IROp::FRSqrt:
		tempReg = fpr.MapDirtyInTemp(inst.dest, inst.src1);
		FSQRT(32, fpr.R(inst.dest), fpr.R(inst.src1));

		// Ugh, we can't really avoid a temp here.  Probably not worth a permanent one.
		LI(SCRATCH1, 1.0f);
		FMV(FMv::W, FMv::X, tempReg, SCRATCH1);
		FDIV(32, fpr.R(inst.dest), tempReg, fpr.R(inst.dest));
		break;

	case IROp::FRecip:
		if (inst.dest != inst.src1) {
			// This is the easy case.
			fpr.MapDirtyIn(inst.dest, inst.src1);
			LI(SCRATCH1, 1.0f);
			FMV(FMv::W, FMv::X, fpr.R(inst.dest), SCRATCH1);
			FDIV(32, fpr.R(inst.dest), fpr.R(inst.dest), fpr.R(inst.src1));
		} else {
			tempReg = fpr.MapDirtyInTemp(inst.dest, inst.src1);
			LI(SCRATCH1, 1.0f);
			FMV(FMv::W, FMv::X, tempReg, SCRATCH1);
			FDIV(32, fpr.R(inst.dest), tempReg, fpr.R(inst.src1));
		}
		break;

	case IROp::FAsin:
		callFuncF_F(&vfpu_asin);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
