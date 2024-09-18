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

#ifndef offsetof
#include <cstddef>
#endif

#include "Core/MIPS/ARM64/Arm64IRJit.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"

// This file contains compilation for floating point related instructions.
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

void Arm64JitBackend::CompIR_FArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FAdd:
		regs_.Map(inst);
		fp_.FADD(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FSub:
		regs_.Map(inst);
		fp_.FSUB(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FMul:
		regs_.Map(inst);
		fp_.FMUL(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FDiv:
		regs_.Map(inst);
		fp_.FDIV(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FSqrt:
		regs_.Map(inst);
		fp_.FSQRT(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FNeg:
		regs_.Map(inst);
		fp_.FNEG(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_FAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			fp_.FMOV(regs_.F(inst.dest), regs_.F(inst.src1));
		}
		break;

	case IROp::FAbs:
		regs_.Map(inst);
		fp_.FABS(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FSign:
		regs_.Map(inst);
		// We'll need this flag later.  Vector could use a temp and FCMEQ.
		fp_.FCMP(regs_.F(inst.src1));

		fp_.MOVI2FDUP(EncodeRegToDouble(SCRATCHF1), 1.0f);
		// Invert 0x80000000 -> 0x7FFFFFFF as a mask for sign.
		fp_.MVNI(32, EncodeRegToDouble(SCRATCHF2), 0x80, 24);
		// Keep the sign bit in dest, replace all other bits from 1.0f.
		if (inst.dest != inst.src1)
			fp_.FMOV(regs_.FD(inst.dest), regs_.FD(inst.src1));
		fp_.BIT(regs_.FD(inst.dest), EncodeRegToDouble(SCRATCHF1), EncodeRegToDouble(SCRATCHF2));

		// It's later now, let's replace with zero if that FCmp was EQ to zero.
		fp_.MOVI2FDUP(EncodeRegToDouble(SCRATCHF1), 0.0f);
		fp_.FCSEL(regs_.F(inst.dest), SCRATCHF1, regs_.F(inst.dest), CC_EQ);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_FCompare(IRInst inst) {
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
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(regs_.R(IRREG_FPCOND), CC_VS);
			break;

		case IRFpCompareMode::EqualOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(regs_.R(IRREG_FPCOND), CC_EQ);
			break;

		case IRFpCompareMode::EqualUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(regs_.R(IRREG_FPCOND), CC_EQ);
			// If ordered, use the above result.  If unordered, use ZR+1 (being 1.)
			CSINC(regs_.R(IRREG_FPCOND), regs_.R(IRREG_FPCOND), WZR, CC_VC);
			break;

		case IRFpCompareMode::LessEqualOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(regs_.R(IRREG_FPCOND), CC_LS);
			break;

		case IRFpCompareMode::LessEqualUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(regs_.R(IRREG_FPCOND), CC_LE);
			break;

		case IRFpCompareMode::LessOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(regs_.R(IRREG_FPCOND), CC_LO);
			break;

		case IRFpCompareMode::LessUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(regs_.R(IRREG_FPCOND), CC_LT);
			break;

		default:
			_assert_msg_(false, "Unexpected IRFpCompareMode %d", inst.dest);
		}
		break;

	case IROp::FCmovVfpuCC:
		regs_.MapWithExtra(inst, { { 'G', IRREG_VFPU_CC, 1, MIPSMap::INIT } });
		TSTI2R(regs_.R(IRREG_VFPU_CC), 1ULL << (inst.src2 & 0xF));
		if ((inst.src2 >> 7) & 1) {
			fp_.FCSEL(regs_.F(inst.dest), regs_.F(inst.dest), regs_.F(inst.src1), CC_EQ);
		} else {
			fp_.FCSEL(regs_.F(inst.dest), regs_.F(inst.dest), regs_.F(inst.src1), CC_NEQ);
		}
		break;

	case IROp::FCmpVfpuBit:
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);

		switch (VCondition(inst.dest & 0xF)) {
		case VC_EQ:
			regs_.Map(inst);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(SCRATCH1, CC_EQ);
			break;
		case VC_NE:
			regs_.Map(inst);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(SCRATCH1, CC_NEQ);
			break;
		case VC_LT:
			regs_.Map(inst);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(SCRATCH1, CC_LO);
			break;
		case VC_LE:
			regs_.Map(inst);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(SCRATCH1, CC_LS);
			break;
		case VC_GT:
			regs_.Map(inst);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(SCRATCH1, CC_GT);
			break;
		case VC_GE:
			regs_.Map(inst);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			CSET(SCRATCH1, CC_GE);
			break;
		case VC_EZ:
			regs_.MapFPR(inst.src1);
			fp_.FCMP(regs_.F(inst.src1));
			CSET(SCRATCH1, CC_EQ);
			break;
		case VC_NZ:
			regs_.MapFPR(inst.src1);
			fp_.FCMP(regs_.F(inst.src1));
			CSET(SCRATCH1, CC_NEQ);
			break;
		case VC_EN:
			regs_.MapFPR(inst.src1);
			fp_.FCMP(regs_.F(inst.src1));
			CSET(SCRATCH1, CC_VS);
			break;
		case VC_NN:
			regs_.MapFPR(inst.src1);
			fp_.FCMP(regs_.F(inst.src1));
			CSET(SCRATCH1, CC_VC);
			break;
		case VC_EI:
			regs_.MapFPR(inst.src1);
			// Compare abs(f) >= Infinity.  Could use FACGE for vector.
			MOVI2R(SCRATCH1, 0x7F800000);
			fp_.FMOV(SCRATCHF2, SCRATCH1);
			fp_.FABS(SCRATCHF1, regs_.F(inst.src1));
			fp_.FCMP(SCRATCHF1, SCRATCHF2);
			CSET(SCRATCH1, CC_GE);
			break;
		case VC_NI:
			regs_.MapFPR(inst.src1);
			// Compare abs(f) < Infinity.
			MOVI2R(SCRATCH1, 0x7F800000);
			fp_.FMOV(SCRATCHF2, SCRATCH1);
			fp_.FABS(SCRATCHF1, regs_.F(inst.src1));
			fp_.FCMP(SCRATCHF1, SCRATCHF2);
			// Less than or NAN.
			CSET(SCRATCH1, CC_LT);
			break;
		case VC_ES:
			regs_.MapFPR(inst.src1);
			// Compare abs(f) < Infinity.
			MOVI2R(SCRATCH1, 0x7F800000);
			fp_.FMOV(SCRATCHF2, SCRATCH1);
			fp_.FABS(SCRATCHF1, regs_.F(inst.src1));
			fp_.FCMP(SCRATCHF1, SCRATCHF2);
			// Greater than or equal to Infinity, or NAN.
			CSET(SCRATCH1, CC_HS);
			break;
		case VC_NS:
			regs_.MapFPR(inst.src1);
			// Compare abs(f) < Infinity.
			MOVI2R(SCRATCH1, 0x7F800000);
			fp_.FMOV(SCRATCHF2, SCRATCH1);
			fp_.FABS(SCRATCHF1, regs_.F(inst.src1));
			fp_.FCMP(SCRATCHF1, SCRATCHF2);
			// Less than Infinity, but not NAN.
			CSET(SCRATCH1, CC_LO);
			break;
		case VC_TR:
			MOVI2R(SCRATCH1, 1);
			break;
		case VC_FL:
			MOVI2R(SCRATCH1, 0);
			break;
		}

		BFI(regs_.R(IRREG_VFPU_CC), SCRATCH1, inst.dest >> 4, 1);
		break;

	case IROp::FCmpVfpuAggregate:
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);
		if (inst.dest == 1) {
			// Just replicate the lowest bit to the others.
			BFI(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), 4, 1);
			BFI(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), 5, 1);
		} else {
			MOVI2R(SCRATCH1, inst.dest);
			// Grab the any bit.
			TST(regs_.R(IRREG_VFPU_CC), SCRATCH1);
			CSET(SCRATCH2, CC_NEQ);
			// Now the all bit, by clearing our mask to zero.
			BICS(WZR, SCRATCH1, regs_.R(IRREG_VFPU_CC));
			CSET(SCRATCH1, CC_EQ);

			// Insert the bits into place.
			BFI(regs_.R(IRREG_VFPU_CC), SCRATCH2, 4, 1);
			BFI(regs_.R(IRREG_VFPU_CC), SCRATCH1, 5, 1);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_FCondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	// For Vec4, we could basically just ORR FCMPGE/FCMPLE together, but overlap is trickier.
	regs_.Map(inst);
	fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src2));
	FixupBranch unordered = B(CC_VS);

	switch (inst.op) {
	case IROp::FMin:
		fp_.FMIN(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FMax:
		fp_.FMAX(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	default:
		INVALIDOP;
		break;
	}

	FixupBranch orderedDone = B();

	// Not sure if this path is fast, trying to optimize it to be small but correct.
	// Probably an uncommon path.
	SetJumpTarget(unordered);
	fp_.AND(EncodeRegToDouble(SCRATCHF1), regs_.FD(inst.src1), regs_.FD(inst.src2));
	// SCRATCHF1 = 0xFFFFFFFF if sign bit set on both, 0x00000000 otherwise.
	fp_.CMLT(32, EncodeRegToDouble(SCRATCHF1), EncodeRegToDouble(SCRATCHF1));

	switch (inst.op) {
	case IROp::FMin:
		fp_.SMAX(32, EncodeRegToDouble(SCRATCHF2), regs_.FD(inst.src1), regs_.FD(inst.src2));
		fp_.SMIN(32, regs_.FD(inst.dest), regs_.FD(inst.src1), regs_.FD(inst.src2));
		break;

	case IROp::FMax:
		fp_.SMIN(32, EncodeRegToDouble(SCRATCHF2), regs_.FD(inst.src1), regs_.FD(inst.src2));
		fp_.SMAX(32, regs_.FD(inst.dest), regs_.FD(inst.src1), regs_.FD(inst.src2));
		break;

	default:
		INVALIDOP;
		break;
	}
	// Replace dest with SCRATCHF2 if both were less than zero.
	fp_.BIT(regs_.FD(inst.dest), EncodeRegToDouble(SCRATCHF2), EncodeRegToDouble(SCRATCHF1));

	SetJumpTarget(orderedDone);
}

void Arm64JitBackend::CompIR_FCvt(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FCvtWS:
		// TODO: Unfortunately, we don't currently have the hasSetRounding flag, could skip lookup.
		regs_.Map(inst);
		fp_.FMOV(S0, regs_.F(inst.src1));

		MOVP2R(SCRATCH1_64, &currentRoundingFunc_);
		LDR(INDEX_UNSIGNED, SCRATCH1_64, SCRATCH1_64, 0);
		BLR(SCRATCH1_64);

		fp_.FMOV(regs_.F(inst.dest), S0);
		break;

	case IROp::FCvtSW:
		regs_.Map(inst);
		fp_.SCVTF(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FCvtScaledWS:
		if (IRRoundMode(inst.src2 >> 6) == IRRoundMode::CAST_1) {
			regs_.Map(inst);
			// NAN would convert to zero, so detect it specifically and replace with 0x7FFFFFFF.
			fp_.MVNI(32, EncodeRegToDouble(SCRATCHF2), 0x80, 24);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src1));
			fp_.FCVTZS(regs_.F(inst.dest), regs_.F(inst.src1), inst.src2 & 0x1F);
			fp_.FCSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2, CC_VC);
		} else {
			RoundingMode rm;
			switch (IRRoundMode(inst.src2 >> 6)) {
			case IRRoundMode::RINT_0: rm = RoundingMode::ROUND_N; break;
			case IRRoundMode::CEIL_2: rm = RoundingMode::ROUND_P; break;
			case IRRoundMode::FLOOR_3: rm = RoundingMode::ROUND_M; break;
			default:
				_assert_msg_(false, "Invalid rounding mode for FCvtScaledWS");
			}

			// Unfortunately, only Z has a direct scaled instruction.
			// We'll have to multiply.
			regs_.Map(inst);
			fp_.MOVI2F(SCRATCHF1, (float)(1UL << (inst.src2 & 0x1F)), SCRATCH1);
			// This is for the NAN result.
			fp_.MVNI(32, EncodeRegToDouble(SCRATCHF2), 0x80, 24);
			fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src1));
			fp_.FMUL(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
			fp_.FCVTS(regs_.F(inst.dest), regs_.F(inst.dest), rm);
			fp_.FCSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2, CC_VC);
		}
		break;

	case IROp::FCvtScaledSW:
		// TODO: This is probably proceeded by a GPR transfer, might be ideal to combine.
		regs_.Map(inst);
		fp_.SCVTF(regs_.F(inst.dest), regs_.F(inst.src1), inst.src2 & 0x1F);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_FRound(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.Map(inst);
	// Invert 0x80000000 -> 0x7FFFFFFF for the NAN result.
	fp_.MVNI(32, EncodeRegToDouble(SCRATCHF1), 0x80, 24);
	fp_.FCMP(regs_.F(inst.src1), regs_.F(inst.src1));

	// Luckily, these already saturate.
	switch (inst.op) {
	case IROp::FRound:
		fp_.FCVTS(regs_.F(inst.dest), regs_.F(inst.src1), ROUND_N);
		break;

	case IROp::FTrunc:
		fp_.FCVTS(regs_.F(inst.dest), regs_.F(inst.src1), ROUND_Z);
		break;

	case IROp::FCeil:
		fp_.FCVTS(regs_.F(inst.dest), regs_.F(inst.src1), ROUND_P);
		break;

	case IROp::FFloor:
		fp_.FCVTS(regs_.F(inst.dest), regs_.F(inst.src1), ROUND_M);
		break;

	default:
		INVALIDOP;
		break;
	}

	// Switch to INT_MAX if it was NAN.
	fp_.FCSEL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF1, CC_VC);
}

void Arm64JitBackend::CompIR_FSat(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FSat0_1:
		regs_.Map(inst);
		fp_.MOVI2F(SCRATCHF1, 1.0f);
		// Note that FMAX takes the larger of the two zeros, which is what we want.
		fp_.MOVI2F(SCRATCHF2, 0.0f);

		fp_.FMIN(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
		fp_.FMAX(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2);
		break;

	case IROp::FSatMinus1_1:
		regs_.Map(inst);
		fp_.MOVI2F(SCRATCHF1, 1.0f);
		fp_.FNEG(SCRATCHF2, SCRATCHF1);

		fp_.FMIN(regs_.F(inst.dest), regs_.F(inst.src1), SCRATCHF1);
		fp_.FMAX(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF2);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_FSpecial(IRInst inst) {
	CONDITIONAL_DISABLE;

	auto callFuncF_F = [&](float (*func)(float)) {
		regs_.FlushBeforeCall();
		WriteDebugProfilerStatus(IRProfilerStatus::MATH_HELPER);

		// It might be in a non-volatile register.
		// TODO: May have to handle a transfer if SIMD here.
		if (regs_.IsFPRMapped(inst.src1)) {
			int lane = regs_.GetFPRLane(inst.src1);
			if (lane == 0)
				fp_.FMOV(S0, regs_.F(inst.src1));
			else
				fp_.DUP(32, Q0, regs_.F(inst.src1), lane);
		} else {
			int offset = offsetof(MIPSState, f) + inst.src1 * 4;
			fp_.LDR(32, INDEX_UNSIGNED, S0, CTXREG, offset);
		}
		QuickCallFunction(SCRATCH2_64, func);

		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		// If it's already F10, we're done - MapReg doesn't actually overwrite the reg in that case.
		if (regs_.F(inst.dest) != S0) {
			fp_.FMOV(regs_.F(inst.dest), S0);
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
		fp_.MOVI2F(SCRATCHF1, 1.0f);
		fp_.FSQRT(regs_.F(inst.dest), regs_.F(inst.src1));
		fp_.FDIV(regs_.F(inst.dest), SCRATCHF1, regs_.F(inst.dest));
		break;

	case IROp::FRecip:
		regs_.Map(inst);
		fp_.MOVI2F(SCRATCHF1, 1.0f);
		fp_.FDIV(regs_.F(inst.dest), SCRATCHF1, regs_.F(inst.src1));
		break;

	case IROp::FAsin:
		callFuncF_F(&vfpu_asin);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_RoundingMode(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::RestoreRoundingMode:
		RestoreRoundingMode();
		break;

	case IROp::ApplyRoundingMode:
		ApplyRoundingMode();
		break;

	case IROp::UpdateRoundingMode:
		UpdateRoundingMode();
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
