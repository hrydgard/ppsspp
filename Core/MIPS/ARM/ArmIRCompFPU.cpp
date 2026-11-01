// Copyright (c) 2012- PPSSPP Project.

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
#if PPSSPP_ARCH(ARM)

#include <cstddef>
#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

// This file contains compilation for floating point arithmetic, compares, and conversions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace ArmGen;
using namespace ArmIRJitConstants;

void ArmJitBackend::CompIR_FArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FAdd:
		regs_.Map(inst);
		VADD(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FSub:
		regs_.Map(inst);
		VSUB(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FMul:
		// inf * 0 gives the default NaN, 0x7FC00000, as the IR expects.
		regs_.Map(inst);
		VMUL(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FDiv:
		regs_.Map(inst);
		VDIV(regs_.F(inst.dest), regs_.F(inst.src1), regs_.F(inst.src2));
		break;

	case IROp::FSqrt:
		// A negative input gives the positive default NaN here too.
		regs_.Map(inst);
		VSQRT(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FNeg:
		regs_.Map(inst);
		VNEG(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_FAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FMov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			VMOV(regs_.F(inst.dest), regs_.F(inst.src1));
		}
		break;

	case IROp::FAbs:
		regs_.Map(inst);
		VABS(regs_.F(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FSign:
		// Zero and denormals both sign as zero on the hardware. Anything else, NaN included, is
		// 1.0 with the input's sign.
		regs_.Map(inst);
		VMOV(SCRATCH1, regs_.F(inst.src1));
		BIC(SCRATCH2, SCRATCH1, AssumeMakeOperand2(0x80000000));
		CMP(SCRATCH2, AssumeMakeOperand2(0x00800000));
		AND(SCRATCH1, SCRATCH1, AssumeMakeOperand2(0x80000000));
		ORR(SCRATCH1, SCRATCH1, AssumeMakeOperand2(0x3F800000));
		SetCC(CC_LO);
		MOV(SCRATCH1, Operand2(0));
		SetCC(CC_AL);
		VMOV(regs_.F(inst.dest), SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_FCompare(IRInst inst) {
	CONDITIONAL_DISABLE;

	constexpr IRReg IRREG_VFPU_CC = IRREG_VFPU_CTRL_BASE + VFPU_CTRL_CC;

	// Sets dest to 1 if any of the conditions holds after a VCMP, else 0.
	auto setFromFlags = [&](ARMReg dest, CCFlags cc, CCFlags cc2 = CC_AL) {
		VMRS_APSR();
		MOV(dest, Operand2(0));
		SetCC(cc);
		MOV(dest, Operand2(1));
		if (cc2 != CC_AL) {
			SetCC(cc2);
			MOV(dest, Operand2(1));
		}
		SetCC(CC_AL);
	};

	switch (inst.op) {
	case IROp::FCmp:
		if (inst.dest == IRFpCompareMode::False) {
			regs_.SetGPRImm(IRREG_FPCOND, 0);
			break;
		}

		regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
		VCMP(regs_.F(inst.src1), regs_.F(inst.src2));
		switch (inst.dest) {
		case IRFpCompareMode::EitherUnordered:
			setFromFlags(regs_.R(IRREG_FPCOND), CC_VS);
			break;
		case IRFpCompareMode::EqualOrdered:
			setFromFlags(regs_.R(IRREG_FPCOND), CC_EQ);
			break;
		case IRFpCompareMode::EqualUnordered:
			setFromFlags(regs_.R(IRREG_FPCOND), CC_EQ, CC_VS);
			break;
		case IRFpCompareMode::LessEqualOrdered:
			setFromFlags(regs_.R(IRREG_FPCOND), CC_LS);
			break;
		case IRFpCompareMode::LessEqualUnordered:
			setFromFlags(regs_.R(IRREG_FPCOND), CC_LE);
			break;
		case IRFpCompareMode::LessOrdered:
			setFromFlags(regs_.R(IRREG_FPCOND), CC_LO);
			break;
		case IRFpCompareMode::LessUnordered:
			setFromFlags(regs_.R(IRREG_FPCOND), CC_LT);
			break;
		default:
			_assert_msg_(false, "Unexpected IRFpCompareMode %d", inst.dest);
		}
		break;

	case IROp::FCmovVfpuCC:
		regs_.MapWithExtra(inst, { { 'G', IRREG_VFPU_CC, 1, MIPSMap::INIT } });
		TST(regs_.R(IRREG_VFPU_CC), AssumeMakeOperand2(1 << (inst.src2 & 0xF)));
		if (inst.dest != inst.src1) {
			// With bit 7 of src2, it moves when the CC bit is set, otherwise when it's clear.
			SetCC(((inst.src2 >> 7) & 1) ? CC_NEQ : CC_EQ);
			VMOV(regs_.F(inst.dest), regs_.F(inst.src1));
			SetCC(CC_AL);
		}
		break;

	case IROp::FCmpVfpuBit:
	{
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);

		// Compares the magnitude against infinity, for the EI/NI/ES/NS conditions.
		auto cmpAbsToInf = [&]() {
			MOVI2R(SCRATCH1, 0x7F800000);
			VMOV(SCRATCHF2, SCRATCH1);
			VABS(SCRATCHF1, regs_.F(inst.src1));
			VCMP(SCRATCHF1, SCRATCHF2);
		};

		switch (VCondition(inst.dest & 0xF)) {
		case VC_EQ:
			regs_.Map(inst);
			VCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			setFromFlags(SCRATCH1, CC_EQ);
			break;
		case VC_NE:
			regs_.Map(inst);
			VCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			setFromFlags(SCRATCH1, CC_NEQ);
			break;
		case VC_LT:
			regs_.Map(inst);
			VCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			setFromFlags(SCRATCH1, CC_LO);
			break;
		case VC_LE:
			regs_.Map(inst);
			VCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			setFromFlags(SCRATCH1, CC_LS);
			break;
		case VC_GT:
			regs_.Map(inst);
			VCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			setFromFlags(SCRATCH1, CC_GT);
			break;
		case VC_GE:
			regs_.Map(inst);
			VCMP(regs_.F(inst.src1), regs_.F(inst.src2));
			setFromFlags(SCRATCH1, CC_GE);
			break;
		case VC_EZ:
			regs_.MapFPR(inst.src1);
			VCMP(regs_.F(inst.src1));
			setFromFlags(SCRATCH1, CC_EQ);
			break;
		case VC_NZ:
			regs_.MapFPR(inst.src1);
			VCMP(regs_.F(inst.src1));
			setFromFlags(SCRATCH1, CC_NEQ);
			break;
		case VC_EN:
			regs_.MapFPR(inst.src1);
			VCMP(regs_.F(inst.src1));
			setFromFlags(SCRATCH1, CC_VS);
			break;
		case VC_NN:
			regs_.MapFPR(inst.src1);
			VCMP(regs_.F(inst.src1));
			setFromFlags(SCRATCH1, CC_VC);
			break;
		case VC_EI:
			regs_.MapFPR(inst.src1);
			cmpAbsToInf();
			setFromFlags(SCRATCH1, CC_GE);
			break;
		case VC_NI:
			regs_.MapFPR(inst.src1);
			cmpAbsToInf();
			// Less than or NAN.
			setFromFlags(SCRATCH1, CC_LT);
			break;
		case VC_ES:
			regs_.MapFPR(inst.src1);
			cmpAbsToInf();
			// Greater than or equal to Infinity, or NAN.
			setFromFlags(SCRATCH1, CC_HS);
			break;
		case VC_NS:
			regs_.MapFPR(inst.src1);
			cmpAbsToInf();
			// Less than Infinity, but not NAN.
			setFromFlags(SCRATCH1, CC_LO);
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
	}

	case IROp::FCmpVfpuAggregate:
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);
		if (inst.dest == 1) {
			// Just replicate the lowest bit to the others.
			BFI(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), 4, 1);
			BFI(regs_.R(IRREG_VFPU_CC), regs_.R(IRREG_VFPU_CC), 5, 1);
		} else {
			ANDI2R(SCRATCH2, regs_.R(IRREG_VFPU_CC), inst.dest, SCRATCH1);
			// The any bit.
			CMP(SCRATCH2, Operand2(0));
			MOV(SCRATCH1, Operand2(0));
			SetCC(CC_NEQ);
			MOV(SCRATCH1, Operand2(1));
			SetCC(CC_AL);
			BFI(regs_.R(IRREG_VFPU_CC), SCRATCH1, 4, 1);
			// The all bit.
			CMPI2R(SCRATCH2, inst.dest, SCRATCH1);
			MOV(SCRATCH1, Operand2(0));
			SetCC(CC_EQ);
			MOV(SCRATCH1, Operand2(1));
			SetCC(CC_AL);
			BFI(regs_.R(IRREG_VFPU_CC), SCRATCH1, 5, 1);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_FCondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	if (inst.op != IROp::FMin && inst.op != IROp::FMax)
		INVALIDOP;

	// vmin/vmax: compare by vfpu_minmax_key, and select the operand with its bits.
	ARMReg tempReg = regs_.GetAndLockTempGPR();
	regs_.Map(inst);
	auto key = [&](ARMReg r, IRReg src) {
		VMOV(r, regs_.F(src));
		// 0x7F800000 isn't a valid immediate, so this goes through tempReg.
		TSTI2R(r, 0x7F800000, tempReg);
		// These don't touch the flags.
		ASR(tempReg, r, 31);
		EOR(r, r, Operand2(tempReg, ST_LSR, 1));
		SetCC(CC_EQ);
		MOV(r, Operand2(0));
		SetCC(CC_AL);
	};
	key(SCRATCH1, inst.src1);
	key(SCRATCH2, inst.src2);
	CMP(SCRATCH1, SCRATCH2);
	// A tie takes src2. Exactly one of the moves happens, so dest may be either source.
	CCFlags pickSrc1 = inst.op == IROp::FMin ? CC_LT : CC_GT;
	if (inst.dest != inst.src1) {
		SetCC(pickSrc1);
		VMOV(regs_.F(inst.dest), regs_.F(inst.src1));
	}
	if (inst.dest != inst.src2) {
		SetCC(InvertCond(pickSrc1));
		VMOV(regs_.F(inst.dest), regs_.F(inst.src2));
	}
	SetCC(CC_AL);
}

// Converts to an integer with the given ARM rounding mode (0 nearest, 1 up, 2 down, 3 zero), or
// with FPSCR's own when rmode is -1. Only RMode is swapped, so the guest's flush-to-zero still
// applies. A NaN gives 0x7FFFFFFF, and the conversion saturates the rest.
void ArmJitBackend::CompFloatToIntWithRounding(ARMReg dest, ARMReg src, int rmode) {
	if (rmode == 3) {
		VCMP(src, src);
		VCVT(dest, src, TO_INT | IS_SIGNED | ROUND_TO_ZERO);
	} else {
		if (rmode >= 0) {
			VMRS(SCRATCH2);
			BIC(SCRATCH1, SCRATCH2, AssumeMakeOperand2(3 << 22));
			if (rmode != 0)
				ORR(SCRATCH1, SCRATCH1, AssumeMakeOperand2(rmode << 22));
			VMSR(SCRATCH1);
		}
		VCMP(src, src);
		VCVT(dest, src, TO_INT | IS_SIGNED);
		// Restoring FPSCR restores its flags too, so take the compare's first.
		VMRS_APSR();
		if (rmode >= 0)
			VMSR(SCRATCH2);
	}
	if (rmode == 3)
		VMRS_APSR();
	SetCC(CC_VS);
	MVN(SCRATCH1, AssumeMakeOperand2(0x80000000));
	VMOV(dest, SCRATCH1);
	SetCC(CC_AL);
}

void ArmJitBackend::CompIR_FCvt(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FCvtWS:
		// The rounding mode in FPSCR is the guest's, see applyRoundingMode.
		regs_.Map(inst);
		CompFloatToIntWithRounding(regs_.F(inst.dest), regs_.F(inst.src1), -1);
		break;

	case IROp::FCvtSW:
		regs_.Map(inst);
		VCVT(regs_.F(inst.dest), regs_.F(inst.src1), TO_FLOAT | IS_SIGNED);
		break;

	case IROp::FCvtScaledWS:
	{
		int rmode = 0;
		switch (IRRoundMode(inst.src2 >> 6)) {
		case IRRoundMode::RINT_0: rmode = 0; break;
		case IRRoundMode::CAST_1: rmode = 3; break;
		case IRRoundMode::CEIL_2: rmode = 1; break;
		case IRRoundMode::FLOOR_3: rmode = 2; break;
		}
		regs_.Map(inst);
		// Scaling by a power of two is exact (or overflows, which saturates the same way.)
		MOVI2F(SCRATCHF1, (float)(1UL << (inst.src2 & 0x1F)), SCRATCH1);
		VMUL(SCRATCHF1, regs_.F(inst.src1), SCRATCHF1);
		CompFloatToIntWithRounding(regs_.F(inst.dest), SCRATCHF1, rmode);
		break;
	}

	case IROp::FCvtScaledSW:
		regs_.Map(inst);
		VCVT(regs_.F(inst.dest), regs_.F(inst.src1), TO_FLOAT | IS_SIGNED);
		MOVI2F(SCRATCHF1, 1.0f / (1UL << (inst.src2 & 0x1F)), SCRATCH1);
		VMUL(regs_.F(inst.dest), regs_.F(inst.dest), SCRATCHF1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_FRound(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.Map(inst);
	switch (inst.op) {
	case IROp::FRound:
		CompFloatToIntWithRounding(regs_.F(inst.dest), regs_.F(inst.src1), 0);
		break;

	case IROp::FTrunc:
		CompFloatToIntWithRounding(regs_.F(inst.dest), regs_.F(inst.src1), 3);
		break;

	case IROp::FCeil:
		CompFloatToIntWithRounding(regs_.F(inst.dest), regs_.F(inst.src1), 1);
		break;

	case IROp::FFloor:
		CompFloatToIntWithRounding(regs_.F(inst.dest), regs_.F(inst.src1), 2);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_FSat(IRInst inst) {
	CONDITIONAL_DISABLE;

	// vfpu_clamp: v >= max ? max : (v <= min ? min : v). A NaN stays, and -0.0 becomes the min
	// when that's +0.0. There's no scalar min/max on VFP, so it's compares and moves.
	switch (inst.op) {
	case IROp::FSat0_1:
	case IROp::FSatMinus1_1:
	{
		regs_.Map(inst);
		ARMReg dest = regs_.F(inst.dest);
		ARMReg src = regs_.F(inst.src1);
		MOVI2F(SCRATCHF1, 1.0f, SCRATCH1);
		if (inst.op == IROp::FSat0_1) {
			MOVI2F(SCRATCHF2, 0.0f, SCRATCH1);
		} else {
			VNEG(SCRATCHF2, SCRATCHF1);
		}
		VCMP(src, SCRATCHF1);
		VMRS_APSR();
		if (dest != src)
			VMOV(dest, src);
		SetCC(CC_GE);
		VMOV(dest, SCRATCHF1);
		SetCC(CC_AL);
		// If it was replaced, it's 1.0, which isn't <= either min.
		VCMP(dest, SCRATCHF2);
		VMRS_APSR();
		SetCC(CC_LS);
		VMOV(dest, SCRATCHF2);
		SetCC(CC_AL);
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_FSpecial(IRInst inst) {
	CONDITIONAL_DISABLE;

	// The argument goes in S0 and the result comes back there with the hard float ABI. With
	// softfp (Android's armeabi-v7a), floats are passed and returned in R0 (and R1.)
	auto loadArg = [&]() {
		regs_.FlushBeforeCall();
		WriteDebugProfilerStatus(IRProfilerStatus::MATH_HELPER);
		// It might be in a callee-saved register.
		if (regs_.IsFPRMapped(inst.src1)) {
			VMOV(S0, regs_.F(inst.src1));
		} else {
			VLDR(S0, CTXREG, offsetof(MIPSState, f) + inst.src1 * 4);
		}
#if !defined(__ARM_PCS_VFP)
		VMOV(R0, S0);
#endif
	};
	// Before anything else can use R0 and R1.
	auto resultToS0 = [&](bool twoFloats) {
#if !defined(__ARM_PCS_VFP)
		VMOV(S0, R0);
		if (twoFloats)
			VMOV(S1, R1);
#endif
	};
	auto callFuncF_F = [&](float (*func)(float)) {
		loadArg();
		QuickCallFunction(R12, func);
		resultToS0(false);
		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		VMOV(regs_.F(inst.dest), S0);
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
		callFuncF_F(&vfpu_rsqrt);
		break;

	case IROp::FRecip:
		callFuncF_F(&vfpu_rcp);
		break;

	case IROp::FAsin:
		callFuncF_F(&vfpu_asin);
		break;

	case IROp::FVSqrt:
		callFuncF_F(&vfpu_sqrt);
		break;

	case IROp::FExp2:
		callFuncF_F(&vfpu_exp2);
		break;

	case IROp::FLog2:
		callFuncF_F(&vfpu_log2);
		break;

	case IROp::FHalfToFloat:
		callFuncF_F(inst.src2 ? &vfpu_h2f_upper : &vfpu_h2f_lower);
		break;

	case IROp::FSinCos:
		// The helper returns the sine and cosine packed into a double, which is S0 and S1.
		loadArg();
		QuickCallFunction(R12, &vfpu_sincos_packed);
		resultToS0(true);
		regs_.Map(inst);
		VMOV(regs_.F(inst.dest), S0);
		VMOV(regs_.F(inst.dest + 1), S1);
		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_RoundingMode(IRInst inst) {
	CONDITIONAL_DISABLE;

	// The helpers are called with BL and use R12, so those mustn't hold anything.
	switch (inst.op) {
	case IROp::RestoreRoundingMode:
		regs_.FlushBeforeCall();
		RestoreRoundingMode();
		break;

	case IROp::ApplyRoundingMode:
		regs_.FlushBeforeCall();
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
