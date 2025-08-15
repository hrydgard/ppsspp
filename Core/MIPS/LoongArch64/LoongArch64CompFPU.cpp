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
#include "Core/MIPS/LoongArch64/LoongArch64Jit.h"
#include "Core/MIPS/LoongArch64/LoongArch64RegCache.h"

// This file contains compilation for floating point related instructions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace LoongArch64Gen;
using namespace LoongArch64JitConstants;

void LoongArch64JitBackend::CompIR_FArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FAdd:
		regs_.Map(inst);
		FADD_S(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FSub:
		regs_.Map(inst);
		FSUB_S(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FMul:
		regs_.Map(inst);
		// We'll assume everyone will make it such that 0 * infinity = NAN properly.
		// See blame on this comment if that proves untrue.
		FMUL_S(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FDiv:
		regs_.Map(inst);
		FDIV_S(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FSqrt:
		regs_.Map(inst);
		FSQRT_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FNeg:
		regs_.Map(inst);
		FNEG_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_FCondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.Map(inst);
	FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CUN);
	MOVCF2GR(SCRATCH1, FCC0);
	FixupBranch unordered = BNEZ(SCRATCH1);

	switch (inst.op) {
	case IROp::FMin:
		FMIN_S(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FMax:
		FMAX_S(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	default:
		INVALIDOP;
		break;
	}

	FixupBranch ordererDone = B();
	SetJumpTarget(unordered);

	MOVFR2GR_S(SCRATCH1, regs_.F(inst.src1));
	MOVFR2GR_S(SCRATCH2, regs_.F(inst.src2));

	// If both are negative, we flip the comparison (not two's compliment.)
	// We cheat and use RA...
	AND(R_RA, SCRATCH1, SCRATCH2);
	SRLI_W(R_RA, R_RA, 31);

	LoongArch64Reg isSrc1LowerReg = regs_.GetAndLockTempGPR();
	SLT(isSrc1LowerReg, SCRATCH1, SCRATCH2);
	// Flip the flag (to reverse the min/max) based on if both were negative.
	XOR(isSrc1LowerReg, isSrc1LowerReg, R_RA);
	FixupBranch useSrc1;
	switch (inst.op) {
	case IROp::FMin:
		useSrc1 = BNEZ(isSrc1LowerReg);
		break;

	case IROp::FMax:
		useSrc1 = BEQZ(isSrc1LowerReg);
		break;

	default:
		INVALIDOP;
		break;
	}
	MOVE(SCRATCH1, SCRATCH2);
	SetJumpTarget(useSrc1);

	MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);

	SetJumpTarget(ordererDone);
}

void LoongArch64JitBackend::CompIR_FAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			FMOV_S(regs_.F(inst.dest), regs_.F(inst.src1));
		}
		break;

	case IROp::FAbs:
		regs_.Map(inst);
		FABS_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FSign:
	{
		regs_.Map(inst);
		// Check if it's negative zero, either 0x20/0x200 is zero.
		FCLASS_S(SCRATCHF1, regs_.F(inst.src1));
		MOVFR2GR_S(SCRATCH1, SCRATCHF1);
		ANDI(SCRATCH1, SCRATCH1, 0x220);
		SLTUI(SCRATCH1, SCRATCH1, 1);
		// Okay, it's zero if zero, 1 otherwise.  Convert 1 to a constant 1.0.
		// Probably non-zero is the common case, so we make that the straight line.
		FixupBranch skipOne = BEQZ(SCRATCH1);
		LI(SCRATCH1, 1.0f);

		// Now we just need the sign from it.
		MOVFR2GR_S(SCRATCH2, regs_.F(inst.src1));
		// Use a wall to isolate the sign, and combine.
		SRAI_W(SCRATCH2, SCRATCH2, 31);
		SLLI_W(SCRATCH2, SCRATCH2, 31);
		OR(SCRATCH1, SCRATCH1, SCRATCH2);

		SetJumpTarget(skipOne);
		MOVGR2FR_W(regs_.F(inst.dest), SCRATCH1);
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_FRound(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.Map(inst);
	// FTINT* instruction will convert NAN to zero, tested on 3A6000.
	QuickFLI(32, SCRATCHF1, (uint32_t)0x7fffffffl, SCRATCH1);
	FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src1), LoongArch64Fcond::CUN);

	switch (inst.op) {
	case IROp::FRound:
		FTINTRNE_W_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FTrunc:
		FTINTRZ_W_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FCeil:
		FTINTRP_W_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FFloor:
		FTINTRM_W_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}

	// Switch to INT_MAX if it was NAN.
	FSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF1, FCC0);
}

void LoongArch64JitBackend::CompIR_FCvt(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FCvtWS:
		CompIR_Generic(inst);
		break;

	case IROp::FCvtSW:
		regs_.Map(inst);
		FFINT_S_W(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FCvtScaledWS:
		regs_.Map(inst);
		// Prepare for the NAN result
		QuickFLI(32, SCRATCHF1, (uint32_t)(0x7FFFFFFF), SCRATCH1);
		// Prepare the multiplier.
		QuickFLI(32, SCRATCHF1, (float)(1UL << (inst.src2 & 0x1F)), SCRATCH1);

		switch (inst.src2 >> 6) {
		case 0: // RNE
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src1), LoongArch64Fcond::CUN);
			FMUL_S(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
			FTINTRNE_W_S(regs_.F(inst.dest), regs_.F(inst.dest));
			FSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2, FCC0);
			break;
		case 1: // RZ
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src1), LoongArch64Fcond::CUN);
			FMUL_S(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
			FTINTRZ_W_S(regs_.F(inst.dest), regs_.F(inst.dest));
			FSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2, FCC0);
			break;
		case 2: // RP
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src1), LoongArch64Fcond::CUN);
			FMUL_S(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
			FTINTRP_W_S(regs_.F(inst.dest), regs_.F(inst.dest));
			FSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2, FCC0);
			break;
		case 3: // RM
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src1), LoongArch64Fcond::CUN);
			FMUL_S(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
			FTINTRM_W_S(regs_.F(inst.dest), regs_.F(inst.dest));
			FSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2, FCC0);
			break;
		default:
			_assert_msg_(false, "Invalid rounding mode for FCvtScaledWS");
		}

		break;

	case IROp::FCvtScaledSW:
		regs_.Map(inst);
		FFINT_S_W(regs_.F(inst.dest), regs_.F(inst.src1));

		// Pre-divide so we can avoid any actual divide.
		QuickFLI(32, SCRATCHF1, 1.0f / (1UL << (inst.src2 & 0x1F)), SCRATCH1);
		FMUL_S(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_FSat(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FSat0_1:
		regs_.Map(inst);
		QuickFLI(32, SCRATCHF1, (float)1.0f, SCRATCH1);
		// Check whether FMAX takes the larger of the two zeros, which is what we want.
		QuickFLI(32, SCRATCHF2, (float)0.0f, SCRATCH1);

		FMIN_S(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
		FMAX_S(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2);
		break;

	case IROp::FSatMinus1_1:
		regs_.Map(inst);
		QuickFLI(32, SCRATCHF1, (float)1.0f, SCRATCH1);
		FNEG_S(SCRATCHF2, SCRATCHF1);

		FMIN_S(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
		FMAX_S(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_FCompare(IRInst inst) {
	CONDITIONAL_DISABLE;

	constexpr IRReg IRREG_VFPU_CC = IRREG_VFPU_CTRL_BASE + VFPU_CTRL_CC;

	switch (inst.op) {
	case IROp::FCmp:
		switch (inst.dest) {
		case IRFpCompareMode::False:
			regs_.SetGPRImm(IRREG_FPCOND, 0);
			break;

		case IRFpCompareMode::EitherUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CUN);
			MOVCF2GR(regs_.R(IRREG_FPCOND), FCC0);
			regs_.MarkGPRDirty(IRREG_FPCOND, true);
			break;

		case IRFpCompareMode::EqualOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CEQ);
			MOVCF2GR(regs_.R(IRREG_FPCOND), FCC0);
			regs_.MarkGPRDirty(IRREG_FPCOND, true);
			break;

		case IRFpCompareMode::EqualUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CUEQ);
			MOVCF2GR(regs_.R(IRREG_FPCOND), FCC0);
			regs_.MarkGPRDirty(IRREG_FPCOND, true);
			break;

		case IRFpCompareMode::LessEqualOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CLE);
			MOVCF2GR(regs_.R(IRREG_FPCOND), FCC0);
			regs_.MarkGPRDirty(IRREG_FPCOND, true);
			break;

		case IRFpCompareMode::LessEqualUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CULE);
			MOVCF2GR(regs_.R(IRREG_FPCOND), FCC0);
			regs_.MarkGPRDirty(IRREG_FPCOND, true);
			break;

		case IRFpCompareMode::LessOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CLT);
			MOVCF2GR(regs_.R(IRREG_FPCOND), FCC0);
			regs_.MarkGPRDirty(IRREG_FPCOND, true);
			break;

		case IRFpCompareMode::LessUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CULT);
			MOVCF2GR(regs_.R(IRREG_FPCOND), FCC0);
			regs_.MarkGPRDirty(IRREG_FPCOND, true);
			break;

		default:
			_assert_msg_(false, "Unexpected IRFpCompareMode %d", inst.dest);
		}
		break;

	case IROp::FCmovVfpuCC:
		regs_.MapWithExtra(inst, { { 'G', IRREG_VFPU_CC, 1, MIPSMap::INIT } });
		if ((inst.src2 & 0xF) == 0) {
			ANDI(SCRATCH1, regs_.R(IRREG_VFPU_CC), 1);
		} else {
			BSTRPICK_D(SCRATCH1, regs_.R(IRREG_VFPU_CC), inst.src2 & 0xF, inst.src2 & 0xF);
		}
		if ((inst.src2 >> 7) & 1) {
			FixupBranch skip = BEQZ(SCRATCH1);
			FMOV_S(regs_.F(inst.dest), regs_.F(inst.src1));
			SetJumpTarget(skip);
		} else {
			FixupBranch skip = BNEZ(SCRATCH1);
			FMOV_S(regs_.F(inst.dest), regs_.F(inst.src1));
			SetJumpTarget(skip);
		}
		break;

	case IROp::FCmpVfpuBit:
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);

		switch (VCondition(inst.dest & 0xF)) {
		case VC_EQ:
			regs_.Map(inst);
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CEQ);
			MOVCF2GR(SCRATCH1, FCC0);
			break;
		case VC_NE:
			regs_.Map(inst);
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CNE);
			MOVCF2GR(SCRATCH1, FCC0);
			break;
		case VC_LT:
			regs_.Map(inst);
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CLT);
			MOVCF2GR(SCRATCH1, FCC0);
			break;
		case VC_LE:
			regs_.Map(inst);
			FCMP_COND_S(FCC0, regs_.F(inst.src1), regs_.F(inst.src2), LoongArch64Fcond::CLE);
			MOVCF2GR(SCRATCH1, FCC0);
			break;
		case VC_GT:
			regs_.Map(inst);
			FCMP_COND_S(FCC0, regs_.F(inst.src2), regs_.F(inst.src1), LoongArch64Fcond::CLT);
			MOVCF2GR(SCRATCH1, FCC0);
			break;
		case VC_GE:
			regs_.Map(inst);
			FCMP_COND_S(FCC0, regs_.F(inst.src2), regs_.F(inst.src1), LoongArch64Fcond::CLE);
			MOVCF2GR(SCRATCH1, FCC0);
			break;
		case VC_EZ:
		case VC_NZ:
			regs_.MapFPR(inst.src1);
			// Zero is either 0x20 or 0x200.
			FCLASS_S(SCRATCHF1, regs_.F(inst.src1));
			MOVFR2GR_S(SCRATCH1, SCRATCHF1);
			ANDI(SCRATCH1, SCRATCH1, 0x220);
			if ((inst.dest & 4) == 0)
				SLTU(SCRATCH1, R_ZERO, SCRATCH1);
			else
				SLTUI(SCRATCH1, SCRATCH1, 1);
			break;
		case VC_EN:
		case VC_NN:
			regs_.MapFPR(inst.src1);
			// NAN is either 0x1 or 0x2.
			FCLASS_S(SCRATCHF1, regs_.F(inst.src1));
			MOVFR2GR_S(SCRATCH1, SCRATCHF1);
			ANDI(SCRATCH1, SCRATCH1, 0x3);
			if ((inst.dest & 4) == 0)
				SLTU(SCRATCH1, R_ZERO, SCRATCH1);
			else
				SLTUI(SCRATCH1, SCRATCH1, 1);
			break;
		case VC_EI:
		case VC_NI:
			regs_.MapFPR(inst.src1);
			// Infinity is either 0x40 or 0x04.
			FCLASS_S(SCRATCHF1, regs_.F(inst.src1));
			MOVFR2GR_S(SCRATCH1, SCRATCHF1);
			ANDI(SCRATCH1, SCRATCH1, 0x44);
			if ((inst.dest & 4) == 0)
				SLTU(SCRATCH1, R_ZERO, SCRATCH1);
			else
				SLTUI(SCRATCH1, SCRATCH1, 1);
			break;
		case VC_ES:
		case VC_NS:
			regs_.MapFPR(inst.src1);
			// Infinity is either 0x40 or 0x04, NAN is either 0x1 or 0x2.
			FCLASS_S(SCRATCHF1, regs_.F(inst.src1));
			MOVFR2GR_S(SCRATCH1, SCRATCHF1);
			ANDI(SCRATCH1, SCRATCH1, 0x47);
			if ((inst.dest & 4) == 0)
				SLTU(SCRATCH1, R_ZERO, SCRATCH1);
			else
				SLTUI(SCRATCH1, SCRATCH1, 1);
			break;
		case VC_TR:
			LI(SCRATCH1, 1);
			break;
		case VC_FL:
			LI(SCRATCH1, 0);
			break;
		}

		ANDI(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), ~(1 << (inst.dest >> 4)));
		if ((inst.dest >> 4) != 0)
			SLLI_D(SCRATCH1, SCRATCH1, inst.dest >> 4);
		OR(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), SCRATCH1);
		break;

	case IROp::FCmpVfpuAggregate:
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);
		if (inst.dest == 1) {
			ANDI(SCRATCH1, regs_.R(IRREG_VFPU_CC), inst.dest);
			// Negate so 1 becomes all bits set and zero stays zero, then mask to 0x30.
			SUB_D(SCRATCH1, R_ZERO, SCRATCH1);
			ANDI(SCRATCH1, SCRATCH1, 0x30);

			// Reject the old any/all bits and replace them with our own.
			ANDI(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), ~0x30);
			OR(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), SCRATCH1);
		} else {
			ANDI(SCRATCH1, regs_.R(IRREG_VFPU_CC), inst.dest);
			FixupBranch skipZero = BEQZ(SCRATCH1);

			// To compare to inst.dest for "all", let's simply subtract it and compare to zero.
			ADDI_D(SCRATCH1, SCRATCH1, -inst.dest);
			SLTUI(SCRATCH1, SCRATCH1, 1);
			// Now we combine with the "any" bit.
			SLLI_D(SCRATCH1, SCRATCH1, 5);
			ORI(SCRATCH1, SCRATCH1, 0x10);

			SetJumpTarget(skipZero);

			// Reject the old any/all bits and replace them with our own.
			ANDI(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), ~0x30);
			OR(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), SCRATCH1);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_RoundingMode(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::RestoreRoundingMode:
		RestoreRoundingMode();
		break;

	case IROp::ApplyRoundingMode:
		ApplyRoundingMode();
		break;

	case IROp::UpdateRoundingMode:
		// Do nothing, we don't use any instructions that need updating the rounding mode.
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_FSpecial(IRInst inst) {
	CONDITIONAL_DISABLE;

	auto callFuncF_F = [&](float (*func)(float)) {
		regs_.FlushBeforeCall();
		WriteDebugProfilerStatus(IRProfilerStatus::MATH_HELPER);

		// It might be in a non-volatile register.
		// TODO: May have to handle a transfer if SIMD here.
		if (regs_.IsFPRMapped(inst.src1)) {
			int lane = regs_.GetFPRLane(inst.src1);
			if (lane == 0)
				FMOV_S(F0, regs_.F(inst.src1));
			else
				VREPLVEI_W(V0, regs_.V(inst.src1), lane);
		} else {
			int offset = offsetof(MIPSState, f) + inst.src1 * 4;
			FLD_S(F0, CTXREG, offset);
		}
		QuickCallFunction(func, SCRATCH1);

		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		// If it's already F0, we're done - MapReg doesn't actually overwrite the reg in that case.
		if (regs_.F(inst.dest) != F0) {
			FMOV_S(regs_.F(inst.dest), F0);
		}

		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
	};

	switch (inst.op) {
	case IROp::FSin:
		callFuncF_F(&vfpu_sin);
		break;

	case IROp::FCos:
		callFuncF_F(&vfpu_cos);
		break;

	case IROp::FRSqrt:
		regs_.Map(inst);
		FRSQRT_S(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FRecip:
		regs_.Map(inst);
		FRECIP_S(regs_.F(inst.dest), regs_.F(inst.src1));
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