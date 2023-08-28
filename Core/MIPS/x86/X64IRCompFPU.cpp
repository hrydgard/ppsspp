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

#ifndef offsetof
#include <cstddef>
#endif

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

static struct SimdConstants {
alignas(16) const u32 reverseQNAN[4] = { 0x803FFFFF, 0x803FFFFF, 0x803FFFFF, 0x803FFFFF };
alignas(16) const u32 noSignMask[4] = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };
alignas(16) const u32 positiveInfinity[4] = { 0x7F800000, 0x7F800000, 0x7F800000, 0x7F800000 };
alignas(16) const u32 signBitAll[4] = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
alignas(16) const u32 ones[4] = { 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000 };
} simdConstants;

void X64JitBackend::CompIR_FArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FAdd:
		regs_.Map(inst);
		if (inst.dest == inst.src1) {
			ADDSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			ADDSS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else if (cpu_info.bAVX) {
			VADDSS(regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else {
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			ADDSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::FSub:
		if (inst.dest == inst.src1) {
			regs_.Map(inst);
			SUBSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (cpu_info.bAVX) {
			regs_.Map(inst);
			VSUBSS(regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			X64Reg tempReg = regs_.MapWithFPRTemp(inst);
			MOVAPS(tempReg, regs_.F(inst.src2));
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			SUBSS(regs_.FX(inst.dest), R(tempReg));
		} else {
			regs_.Map(inst);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			SUBSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::FMul:
	{
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);

		// tempReg = !my_isnan(src1) && !my_isnan(src2)
		MOVSS(tempReg, regs_.F(inst.src1));
		CMPORDSS(tempReg, regs_.F(inst.src2));
		if (inst.dest == inst.src1) {
			MULSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			MULSS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else if (cpu_info.bAVX) {
			VMULSS(regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else {
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			MULSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}

		// Abuse a lane of tempReg to remember dest: NAN, NAN, res, res.
		SHUFPS(tempReg, regs_.F(inst.dest), 0);
		// dest = my_isnan(dest) && !my_isnan(src1) && !my_isnan(src2)
		CMPUNORDSS(regs_.FX(inst.dest), regs_.F(inst.dest));
		ANDPS(regs_.FX(inst.dest), R(tempReg));
		// At this point fd = FFFFFFFF if non-NAN inputs produced a NAN output.
		// We'll AND it with the inverse QNAN bits to clear (00000000 means no change.)
		if (RipAccessible(&simdConstants.reverseQNAN)) {
			ANDPS(regs_.FX(inst.dest), M(&simdConstants.reverseQNAN));  // rip accessible
		} else {
			MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.reverseQNAN));
			ANDPS(regs_.FX(inst.dest), MatR(SCRATCH1));
		}
		// ANDN is backwards, which is why we saved XMM0 to start.  Now put it back.
		SHUFPS(tempReg, R(tempReg), 0xFF);
		ANDNPS(regs_.FX(inst.dest), R(tempReg));
		break;
	}

	case IROp::FDiv:
		if (inst.dest == inst.src1) {
			regs_.Map(inst);
			DIVSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (cpu_info.bAVX) {
			regs_.Map(inst);
			VDIVSS(regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			X64Reg tempReg = regs_.MapWithFPRTemp(inst);
			MOVAPS(tempReg, regs_.F(inst.src2));
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			DIVSS(regs_.FX(inst.dest), R(tempReg));
		} else {
			regs_.Map(inst);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			DIVSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::FSqrt:
		regs_.Map(inst);
		SQRTSS(regs_.FX(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FNeg:
		regs_.Map(inst);
		if (cpu_info.bAVX) {
			if (RipAccessible(&simdConstants.signBitAll)) {
				VXORPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(&simdConstants.signBitAll));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.signBitAll));
				VXORPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), MatR(SCRATCH1));
			}
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			if (RipAccessible(&simdConstants.signBitAll)) {
				XORPS(regs_.FX(inst.dest), M(&simdConstants.signBitAll));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.signBitAll));
				XORPS(regs_.FX(inst.dest), MatR(SCRATCH1));
			}
		}
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
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
		}
		break;

	case IROp::FAbs:
		regs_.Map(inst);
		if (cpu_info.bAVX) {
			if (RipAccessible(&simdConstants.noSignMask)) {
				VANDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(&simdConstants.noSignMask));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.noSignMask));
				VANDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), MatR(SCRATCH1));
			}
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			if (RipAccessible(&simdConstants.noSignMask)) {
				ANDPS(regs_.FX(inst.dest), M(&simdConstants.noSignMask));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.noSignMask));
				ANDPS(regs_.FX(inst.dest), MatR(SCRATCH1));
			}
		}
		break;

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

	auto ccToFpcond = [&](IRReg lhs, IRReg rhs, CCFlags cc) {
		if (regs_.HasLowSubregister(regs_.RX(IRREG_FPCOND))) {
			XOR(32, regs_.R(IRREG_FPCOND), regs_.R(IRREG_FPCOND));
			UCOMISS(regs_.FX(lhs), regs_.F(rhs));
			SETcc(cc, regs_.R(IRREG_FPCOND));
		} else {
			UCOMISS(regs_.FX(lhs), regs_.F(rhs));
			SETcc(cc, R(SCRATCH1));
			MOVZX(32, 8, regs_.RX(IRREG_FPCOND), R(SCRATCH1));
		}
	};

	switch (inst.op) {
	case IROp::FCmp:
		switch (inst.dest) {
		case IRFpCompareMode::False:
			regs_.SetGPRImm(IRREG_FPCOND, 0);
			break;

		case IRFpCompareMode::EitherUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			// PF = UNORDERED.
			ccToFpcond(inst.src1, inst.src2, CC_P);
			break;

		case IRFpCompareMode::EqualOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			// Clear the upper bits of SCRATCH1 so we can AND later.
			// We don't have a single flag we can check, unfortunately.
			XOR(32, R(SCRATCH1), R(SCRATCH1));
			UCOMISS(regs_.FX(inst.src1), regs_.F(inst.src2));
			// E/ZF = EQUAL or UNORDERED (not exactly what we want.)
			SETcc(CC_E, R(SCRATCH1));
			if (regs_.HasLowSubregister(regs_.RX(IRREG_FPCOND))) {
				// NP/!PF = ORDERED.
				SETcc(CC_NP, regs_.R(IRREG_FPCOND));
				AND(32, regs_.R(IRREG_FPCOND), R(SCRATCH1));
			} else {
				MOVZX(32, 8, regs_.RX(IRREG_FPCOND), R(SCRATCH1));
				// Neither of those affected flags, luckily.
				// NP/!PF = ORDERED.
				SETcc(CC_NP, R(SCRATCH1));
				AND(32, regs_.R(IRREG_FPCOND), R(SCRATCH1));
			}
			break;

		case IRFpCompareMode::EqualUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			// E/ZF = EQUAL or UNORDERED.
			ccToFpcond(inst.src1, inst.src2, CC_E);
			break;

		case IRFpCompareMode::LessEqualOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			// AE/!CF = GREATER or EQUAL (src2/src1 reversed.)
			ccToFpcond(inst.src2, inst.src1, CC_AE);
			break;

		case IRFpCompareMode::LessEqualUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			// BE/CF||ZF = LESS THAN or EQUAL or UNORDERED.
			ccToFpcond(inst.src1, inst.src2, CC_BE);
			break;

		case IRFpCompareMode::LessOrdered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			// A/!CF&&!ZF = GREATER (src2/src1 reversed.)
			ccToFpcond(inst.src2, inst.src1, CC_A);
			break;

		case IRFpCompareMode::LessUnordered:
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });
			// B/CF = LESS THAN or UNORDERED.
			ccToFpcond(inst.src1, inst.src2, CC_B);
			break;
		}
		break;

	case IROp::FCmovVfpuCC:
		regs_.MapWithExtra(inst, { { 'G', IRREG_VFPU_CC, 1, MIPSMap::INIT } });
		if (regs_.HasLowSubregister(regs_.RX(IRREG_VFPU_CC))) {
			TEST(8, regs_.R(IRREG_VFPU_CC), Imm8(1 << (inst.src2 & 7)));
		} else {
			TEST(32, regs_.R(IRREG_VFPU_CC), Imm32(1 << (inst.src2 & 7)));
		}

		if ((inst.src2 >> 7) & 1) {
			FixupBranch skip = J_CC(CC_Z);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			SetJumpTarget(skip);
		} else {
			FixupBranch skip = J_CC(CC_NZ);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			SetJumpTarget(skip);
		}
		break;

	case IROp::FCmpVfpuBit:
	{
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);
		uint8_t affectedBit = 1 << (inst.dest >> 4);
		bool condNegated = (inst.dest & 4) != 0;

		bool takeBitFromTempReg = true;
		switch (VCondition(inst.dest & 0xF)) {
		case VC_EQ:
			if (cpu_info.bAVX) {
				VCMPSS(tempReg, regs_.FX(inst.src1), regs_.F(inst.src2), CMP_EQ);
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				CMPSS(tempReg, regs_.F(inst.src2), CMP_EQ);
			}
			break;
		case VC_NE:
			if (cpu_info.bAVX) {
				VCMPSS(tempReg, regs_.FX(inst.src1), regs_.F(inst.src2), CMP_NEQ);
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				CMPSS(tempReg, regs_.F(inst.src2), CMP_NEQ);
			}
			break;
		case VC_LT:
			if (cpu_info.bAVX) {
				VCMPSS(tempReg, regs_.FX(inst.src1), regs_.F(inst.src2), CMP_LT);
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				CMPSS(tempReg, regs_.F(inst.src2), CMP_LT);
			}
			break;
		case VC_LE:
			if (cpu_info.bAVX) {
				VCMPSS(tempReg, regs_.FX(inst.src1), regs_.F(inst.src2), CMP_LE);
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				CMPSS(tempReg, regs_.F(inst.src2), CMP_LE);
			}
			break;
		case VC_GT:
			// This is just LT with src1/src2 swapped.
			if (cpu_info.bAVX) {
				VCMPSS(tempReg, regs_.FX(inst.src2), regs_.F(inst.src1), CMP_LT);
			} else {
				MOVAPS(tempReg, regs_.F(inst.src2));
				CMPSS(tempReg, regs_.F(inst.src1), CMP_LT);
			}
			break;
		case VC_GE:
			// This is just LE with src1/src2 swapped.
			if (cpu_info.bAVX) {
				VCMPSS(tempReg, regs_.FX(inst.src2), regs_.F(inst.src1), CMP_LE);
			} else {
				MOVAPS(tempReg, regs_.F(inst.src2));
				CMPSS(tempReg, regs_.F(inst.src1), CMP_LE);
			}
			break;
		case VC_EZ:
		case VC_NZ:
			XORPS(tempReg, R(tempReg));
			CMPSS(tempReg, regs_.F(inst.src1), !condNegated ? CMP_EQ : CMP_NEQ);
			break;
		case VC_EN:
		case VC_NN:
			CMPSS(tempReg, regs_.F(inst.src1), !condNegated ? CMP_UNORD : CMP_ORD);
			break;
		case VC_EI:
		case VC_NI:
			regs_.MapFPR(inst.src1);
			if (!RipAccessible(&simdConstants.noSignMask) || !RipAccessible(&simdConstants.positiveInfinity)) {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants));
			}
			if (cpu_info.bAVX) {
				if (RipAccessible(&simdConstants.noSignMask)) {
					VANDPS(128, tempReg, regs_.FX(inst.src1), M(&simdConstants.noSignMask));  // rip accessible
				} else {
					VANDPS(128, tempReg, regs_.FX(inst.src1), MDisp(SCRATCH1, offsetof(SimdConstants, noSignMask)));
				}
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				if (RipAccessible(&simdConstants.noSignMask)) {
					ANDPS(tempReg, M(&simdConstants.noSignMask));  // rip accessible
				} else {
					ANDPS(tempReg, MDisp(SCRATCH1, offsetof(SimdConstants, noSignMask)));
				}
			}
			if (RipAccessible(&simdConstants.positiveInfinity)) {
				CMPSS(tempReg, M(&simdConstants.positiveInfinity), !condNegated ? CMP_EQ : CMP_LT);  // rip accessible
			} else {
				CMPSS(tempReg, MDisp(SCRATCH1, offsetof(SimdConstants, positiveInfinity)), !condNegated ? CMP_EQ : CMP_LT);
			}
			break;
		case VC_ES:
		case VC_NS:
			// NAN - NAN is NAN, and Infinity - Infinity is also NAN.
			if (cpu_info.bAVX) {
				VSUBSS(tempReg, regs_.FX(inst.src1), regs_.F(inst.src1));
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				SUBSS(tempReg, regs_.F(inst.src1));
			}
			CMPSS(tempReg, regs_.F(inst.src1), !condNegated ? CMP_UNORD : CMP_ORD);
			break;
		case VC_TR:
			OR(32, regs_.R(IRREG_VFPU_CC), Imm8(affectedBit));
			takeBitFromTempReg = true;
			break;
		case VC_FL:
			AND(32, regs_.R(IRREG_VFPU_CC), Imm8(~affectedBit));
			takeBitFromTempReg = false;
			break;
		}

		if (takeBitFromTempReg) {
			MOVD_xmm(R(SCRATCH1), tempReg);
			AND(32, R(SCRATCH1), Imm8(affectedBit));
			AND(32, regs_.R(IRREG_VFPU_CC), Imm8(~affectedBit));
			OR(32, regs_.R(IRREG_VFPU_CC), R(SCRATCH1));
		}
		break;
	}

	case IROp::FCmpVfpuAggregate:
		regs_.MapGPR(IRREG_VFPU_CC, MIPSMap::DIRTY);
		// First, clear out the bits we're aggregating.
		// The register refuses writes to bits outside 0x3F, and we're setting 0x30.
		AND(32, regs_.R(IRREG_VFPU_CC), Imm8(0xF));

		// Set the any bit.
		TEST(32, regs_.R(IRREG_VFPU_CC), Imm32(inst.dest));
		SETcc(CC_NZ, R(SCRATCH1));
		SHL(32, R(SCRATCH1), Imm8(4));
		OR(32, regs_.R(IRREG_VFPU_CC), R(SCRATCH1));

		// Next up, the "all" bit.  A bit annoying...
		MOV(32, R(SCRATCH1), regs_.R(IRREG_VFPU_CC));
		AND(32, R(SCRATCH1), Imm8(inst.dest));
		CMP(32, R(SCRATCH1), Imm8(inst.dest));
		SETcc(CC_E, R(SCRATCH1));
		SHL(32, R(SCRATCH1), Imm8(5));
		OR(32, regs_.R(IRREG_VFPU_CC), R(SCRATCH1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FCondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	FixupBranch skipNAN;
	FixupBranch finishNAN;
	FixupBranch negativeSigns;
	FixupBranch finishNANSigns;
	X64Reg tempReg = INVALID_REG;
	switch (inst.op) {
	case IROp::FMin:
		tempReg = regs_.GetAndLockTempGPR();
		regs_.Map(inst);
		UCOMISS(regs_.FX(inst.src1), regs_.F(inst.src1));
		skipNAN = J_CC(CC_NP, true);

		// Slow path: NAN case.  Check if both are negative.
		MOVD_xmm(R(tempReg), regs_.FX(inst.src1));
		MOVD_xmm(R(SCRATCH1), regs_.FX(inst.src2));
		TEST(32, R(SCRATCH1), R(tempReg));
		negativeSigns = J_CC(CC_S);

		// Okay, one or the other positive.
		CMP(32, R(tempReg), R(SCRATCH1));
		CMOVcc(32, tempReg, R(SCRATCH1), CC_G);
		MOVD_xmm(regs_.FX(inst.dest), R(tempReg));
		finishNAN = J();

		// Okay, both negative.
		SetJumpTarget(negativeSigns);
		CMP(32, R(tempReg), R(SCRATCH1));
		CMOVcc(32, tempReg, R(SCRATCH1), CC_L);
		MOVD_xmm(regs_.FX(inst.dest), R(tempReg));
		finishNANSigns = J();

		SetJumpTarget(skipNAN);
		if (cpu_info.bAVX) {
			VMINSS(regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			MINSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		SetJumpTarget(finishNAN);
		SetJumpTarget(finishNANSigns);
		break;

	case IROp::FMax:
		tempReg = regs_.GetAndLockTempGPR();
		regs_.Map(inst);
		UCOMISS(regs_.FX(inst.src1), regs_.F(inst.src1));
		skipNAN = J_CC(CC_NP, true);

		// Slow path: NAN case.  Check if both are negative.
		MOVD_xmm(R(tempReg), regs_.FX(inst.src1));
		MOVD_xmm(R(SCRATCH1), regs_.FX(inst.src2));
		TEST(32, R(SCRATCH1), R(tempReg));
		negativeSigns = J_CC(CC_S);

		// Okay, one or the other positive.
		CMP(32, R(tempReg), R(SCRATCH1));
		CMOVcc(32, tempReg, R(SCRATCH1), CC_L);
		MOVD_xmm(regs_.FX(inst.dest), R(tempReg));
		finishNAN = J();

		// Okay, both negative.
		SetJumpTarget(negativeSigns);
		CMP(32, R(tempReg), R(SCRATCH1));
		CMOVcc(32, tempReg, R(SCRATCH1), CC_G);
		MOVD_xmm(regs_.FX(inst.dest), R(tempReg));
		finishNANSigns = J();

		SetJumpTarget(skipNAN);
		if (cpu_info.bAVX) {
			VMAXSS(regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			MAXSS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		SetJumpTarget(finishNAN);
		SetJumpTarget(finishNANSigns);
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
		CompIR_Generic(inst);
		break;

	case IROp::FCvtSW:
		regs_.Map(inst);
		CVTDQ2PS(regs_.FX(inst.dest), regs_.F(inst.src1));
		break;

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
		CompIR_Generic(inst);
		break;

	case IROp::FTrunc:
	{
		regs_.SpillLockFPR(inst.dest, inst.src1);
		X64Reg tempZero = regs_.GetAndLockTempFPR();
		regs_.Map(inst);

		CVTTSS2SI(SCRATCH1, regs_.F(inst.src1));

		// Did we get an indefinite integer value?
		CMP(32, R(SCRATCH1), Imm32(0x80000000));
		FixupBranch wasExact = J_CC(CC_NE);

		XORPS(tempZero, R(tempZero));
		if (inst.dest == inst.src1) {
			CMPSS(regs_.FX(inst.dest), R(tempZero), CMP_LT);
		} else if (cpu_info.bAVX) {
			VCMPSS(regs_.FX(inst.dest), regs_.FX(inst.src1), R(tempZero), CMP_LT);
		} else {
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			CMPSS(regs_.FX(inst.dest), R(tempZero), CMP_LT);
		}

		// At this point, -inf = 0xffffffff, inf/nan = 0x00000000.
		// We want -inf to be 0x80000000 inf/nan to be 0x7fffffff, so we flip those bits.
		MOVD_xmm(R(SCRATCH1), regs_.FX(inst.dest));
		XOR(32, R(SCRATCH1), Imm32(0x7fffffff));

		SetJumpTarget(wasExact);
		MOVD_xmm(regs_.FX(inst.dest), R(SCRATCH1));
		break;
	}

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

#if X64JIT_USE_XMM_CALL
static float X64JIT_XMM_CALL x64_sin(float f) {
	return vfpu_sin(f);
}

static float X64JIT_XMM_CALL x64_cos(float f) {
	return vfpu_cos(f);
}

static float X64JIT_XMM_CALL x64_asin(float f) {
	return vfpu_asin(f);
}
#else
static uint32_t x64_sin(uint32_t v) {
	float f;
	memcpy(&f, &v, sizeof(v));
	f = vfpu_sin(f);
	memcpy(&v, &f, sizeof(v));
	return v;
}

static uint32_t x64_cos(uint32_t v) {
	float f;
	memcpy(&f, &v, sizeof(v));
	f = vfpu_cos(f);
	memcpy(&v, &f, sizeof(v));
	return v;
}

static uint32_t x64_asin(uint32_t v) {
	float f;
	memcpy(&f, &v, sizeof(v));
	f = vfpu_asin(f);
	memcpy(&v, &f, sizeof(v));
	return v;
}
#endif

void X64JitBackend::CompIR_FSpecial(IRInst inst) {
	CONDITIONAL_DISABLE;

	// TODO: Regcache... maybe emitter helper too?
	auto laneToReg0 = [&](X64Reg dest, X64Reg src, int lane) {
		if (lane == 0) {
			if (dest != src)
				MOVAPS(dest, R(src));
		} else if (lane == 1 && cpu_info.bSSE3) {
			MOVSHDUP(dest, R(src));
		} else if (lane == 2) {
			MOVHLPS(dest, src);
		} else if (cpu_info.bAVX) {
			VPERMILPS(128, dest, R(src), VFPU_SWIZZLE(lane, lane, lane, lane));
		} else {
			if (dest != src)
				MOVAPS(dest, R(src));
			SHUFPS(dest, R(dest), VFPU_SWIZZLE(lane, lane, lane, lane));
		}
	};

	auto callFuncF_F = [&](const void *func) {
		regs_.FlushBeforeCall();

#if X64JIT_USE_XMM_CALL
		if (regs_.IsFPRMapped(inst.src1)) {
			int lane = regs_.GetFPRLane(inst.src1);
			laneToReg0(XMM0, regs_.FX(inst.src1), lane);
		} else {
			// Account for CTXREG being increased by 128 to reduce imm sizes.
			int offset = offsetof(MIPSState, f) + inst.src1 * 4 - 128;
			MOVSS(XMM0, MDisp(CTXREG, offset));
		}
		ABI_CallFunction((const void *)func);

		// It's already in place, NOINIT won't modify.
		regs_.MapFPR(inst.dest, MIPSMap::NOINIT | X64Map::XMM0);
#else
		if (regs_.IsFPRMapped(inst.src1)) {
			int lane = regs_.GetFPRLane(inst.src1);
			if (lane == 0) {
				MOVD_xmm(R(SCRATCH1), regs_.FX(inst.src1));
			} else {
				laneToReg0(XMM0, regs_.FX(inst.src1), lane);
				MOVD_xmm(R(SCRATCH1), XMM0);
			}
		} else {
			int offset = offsetof(MIPSState, f) + inst.src1 * 4;
			MOV(32, R(SCRATCH1), MDisp(CTXREG, offset));
		}
		ABI_CallFunctionR((const void *)func, SCRATCH1);

		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		MOVD_xmm(regs_.FX(inst.dest), R(SCRATCH1));
#endif
	};

	switch (inst.op) {
	case IROp::FSin:
		callFuncF_F((const void *)&x64_sin);
		break;

	case IROp::FCos:
		callFuncF_F((const void *)&x64_cos);
		break;

	case IROp::FRSqrt:
		{
			X64Reg tempReg = regs_.MapWithFPRTemp(inst);
			SQRTSS(tempReg, regs_.F(inst.src1));

			if (RipAccessible(&simdConstants.ones)) {
				MOVSS(regs_.FX(inst.dest), M(&simdConstants.ones));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.ones));
				MOVSS(regs_.FX(inst.dest), MatR(SCRATCH1));
			}
			DIVSS(regs_.FX(inst.dest), R(tempReg));
			break;
		}

	case IROp::FRecip:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			if (RipAccessible(&simdConstants.ones)) {
				MOVSS(regs_.FX(inst.dest), M(&simdConstants.ones));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.ones));
				MOVSS(regs_.FX(inst.dest), MatR(SCRATCH1));
			}
			DIVSS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else {
			X64Reg tempReg = regs_.MapWithFPRTemp(inst);
			if (RipAccessible(&simdConstants.ones)) {
				MOVSS(tempReg, M(&simdConstants.ones));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.ones));
				MOVSS(tempReg, MatR(SCRATCH1));
			}
			if (cpu_info.bAVX) {
				VDIVSS(regs_.FX(inst.dest), tempReg, regs_.F(inst.src1));
			} else {
				DIVSS(tempReg, regs_.F(inst.src1));
				MOVSS(regs_.FX(inst.dest), R(tempReg));
			}
		}
		break;

	case IROp::FAsin:
		callFuncF_F((const void *)&x64_asin);
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
