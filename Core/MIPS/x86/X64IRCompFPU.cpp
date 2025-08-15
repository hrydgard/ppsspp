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

void X64JitBackend::EmitFPUConstants() {
	EmitConst4x32(&constants.noSignMask, 0x7FFFFFFF);
	EmitConst4x32(&constants.signBitAll, 0x80000000);
	EmitConst4x32(&constants.positiveZeroes, 0x00000000);
	EmitConst4x32(&constants.positiveInfinity, 0x7F800000);
	EmitConst4x32(&constants.qNAN, 0x7FC00000);
	EmitConst4x32(&constants.positiveOnes, 0x3F800000);
	EmitConst4x32(&constants.negativeOnes, 0xBF800000);
	EmitConst4x32(&constants.maxIntBelowAsFloat, 0x4EFFFFFF);

	constants.mulTableVi2f = (const float *)GetCodePointer();
	for (uint8_t i = 0; i < 32; ++i) {
		float fval = 1.0f / (1UL << i);
		uint32_t val;
		memcpy(&val, &fval, sizeof(val));

		Write32(val);
	}

	constants.mulTableVf2i = (const float *)GetCodePointer();
	for (uint8_t i = 0; i < 32; ++i) {
		float fval = (float)(1ULL << i);
		uint32_t val;
		memcpy(&val, &fval, sizeof(val));

		Write32(val);
	}
}

void X64JitBackend::CopyVec4ToFPRLane0(Gen::X64Reg dest, Gen::X64Reg src, int lane) {
	// TODO: Move to regcache or emitter maybe?
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
}

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
		regs_.Map(inst);

		UCOMISS(regs_.FX(inst.src1), regs_.F(inst.src2));
		SETcc(CC_P, R(SCRATCH1));

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

		UCOMISS(regs_.FX(inst.dest), regs_.F(inst.dest));
		FixupBranch handleNAN = J_CC(CC_P);
		FixupBranch finish = J();

		SetJumpTarget(handleNAN);
		TEST(8, R(SCRATCH1), R(SCRATCH1));
		FixupBranch keepNAN = J_CC(CC_NZ);

		MOVSS(regs_.FX(inst.dest), M(constants.qNAN));  // rip accessible

		SetJumpTarget(keepNAN);
		SetJumpTarget(finish);
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
			VXORPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(constants.signBitAll));  // rip accessible
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			XORPS(regs_.FX(inst.dest), M(constants.signBitAll));  // rip accessible
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
		// Just to make sure we don't generate bad code.
		if (inst.dest == inst.src1)
			break;
		if (regs_.IsFPRMapped(inst.src1 & 3) && regs_.GetFPRLaneCount(inst.src1) == 4 && (inst.dest & ~3) != (inst.src1 & ~3)) {
			// Okay, this is an extract.  Avoid unvec4ing src1.
			regs_.SpillLockFPR(inst.src1 & ~3);
			regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
			CopyVec4ToFPRLane0(regs_.FX(inst.dest), regs_.FX(inst.src1 & ~3), inst.src1 & 3);
		} else {
			regs_.Map(inst);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
		}
		break;

	case IROp::FAbs:
		regs_.Map(inst);
		if (cpu_info.bAVX) {
			VANDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(constants.noSignMask));  // rip accessible
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			ANDPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible
		}
		break;

	case IROp::FSign:
	{
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);

		// Set tempReg to +1.0 or -1.0 per sign bit.
		if (cpu_info.bAVX) {
			VANDPS(128, tempReg, regs_.FX(inst.src1), M(constants.signBitAll));  // rip accessible
		} else {
			MOVAPS(tempReg, regs_.F(inst.src1));
			ANDPS(tempReg, M(constants.signBitAll));  // rip accessible
		}
		ORPS(tempReg, M(constants.positiveOnes));  // rip accessible

		// Set dest = 0xFFFFFFFF if +0.0 or -0.0.
		if (inst.dest != inst.src1) {
			XORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
			CMPPS(regs_.FX(inst.dest), regs_.F(inst.src1), CMP_EQ);
		} else {
			CMPPS(regs_.FX(inst.dest), M(constants.positiveZeroes), CMP_EQ);  // rip accessible
		}

		// Now not the mask to keep zero if it was zero.
		ANDNPS(regs_.FX(inst.dest), R(tempReg));
		break;
	}

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
		{
			// Since UCOMISS doesn't give us ordered == directly, CMPSS is better.
			regs_.SpillLockFPR(inst.src1, inst.src2);
			X64Reg tempReg = regs_.GetAndLockTempFPR();
			regs_.MapWithExtra(inst, { { 'G', IRREG_FPCOND, 1, MIPSMap::NOINIT } });

			if (cpu_info.bAVX) {
				VCMPSS(tempReg, regs_.FX(inst.src1), regs_.F(inst.src2), CMP_EQ);
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				CMPSS(tempReg, regs_.F(inst.src2), CMP_EQ);
			}
			MOVD_xmm(regs_.R(IRREG_FPCOND), tempReg);
			AND(32, regs_.R(IRREG_FPCOND), Imm32(1));
			break;
		}

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

		default:
			_assert_msg_(false, "Unexpected IRFpCompareMode %d", inst.dest);
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
			if (cpu_info.bAVX) {
				VANDPS(128, tempReg, regs_.FX(inst.src1), M(constants.noSignMask));  // rip accessible
			} else {
				MOVAPS(tempReg, regs_.F(inst.src1));
				ANDPS(tempReg, M(constants.noSignMask));  // rip accessible
			}
			CMPSS(tempReg, M(constants.positiveInfinity), !condNegated ? CMP_EQ : CMP_LT);  // rip accessible
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
		if (inst.dest == 1) {
			// Special case 1, which is not uncommon.
			AND(32, regs_.R(IRREG_VFPU_CC), Imm8(0xF));
			BT(32, regs_.R(IRREG_VFPU_CC), Imm8(0));
			FixupBranch skip = J_CC(CC_NC);
			OR(32, regs_.R(IRREG_VFPU_CC), Imm8(0x30));
			SetJumpTarget(skip);
		} else if (inst.dest == 3) {
			AND(32, regs_.R(IRREG_VFPU_CC), Imm8(0xF));
			MOV(32, R(SCRATCH1), regs_.R(IRREG_VFPU_CC));
			AND(32, R(SCRATCH1), Imm8(3));
			// 0, 1, and 3 are already correct for the any and all bits.
			CMP(32, R(SCRATCH1), Imm8(2));

			FixupBranch skip = J_CC(CC_NE);
			SUB(32, R(SCRATCH1), Imm8(1));
			SetJumpTarget(skip);

			SHL(32, R(SCRATCH1), Imm8(4));
			OR(32, regs_.R(IRREG_VFPU_CC), R(SCRATCH1));
		} else if (inst.dest == 0xF) {
			XOR(32, R(SCRATCH1), R(SCRATCH1));

			// Clear out the bits we're aggregating.
			// The register refuses writes to bits outside 0x3F, and we're setting 0x30.
			AND(32, regs_.R(IRREG_VFPU_CC), Imm8(0xF));

			// Set the any bit, just using the AND above.
			FixupBranch noneSet = J_CC(CC_Z);
			OR(32, regs_.R(IRREG_VFPU_CC), Imm8(0x10));

			// Next up, the "all" bit.
			CMP(32, regs_.R(IRREG_VFPU_CC), Imm8(0x1F));
			SETcc(CC_E, R(SCRATCH1));
			SHL(32, R(SCRATCH1), Imm8(5));
			OR(32, regs_.R(IRREG_VFPU_CC), R(SCRATCH1));

			SetJumpTarget(noneSet);
		} else {
			XOR(32, R(SCRATCH1), R(SCRATCH1));

			// Clear out the bits we're aggregating.
			// The register refuses writes to bits outside 0x3F, and we're setting 0x30.
			AND(32, regs_.R(IRREG_VFPU_CC), Imm8(0xF));

			// Set the any bit.
			if (regs_.HasLowSubregister(regs_.RX(IRREG_VFPU_CC)))
				TEST(8, regs_.R(IRREG_VFPU_CC), Imm8(inst.dest));
			else
				TEST(32, regs_.R(IRREG_VFPU_CC), Imm32(inst.dest));
			FixupBranch noneSet = J_CC(CC_Z);
			OR(32, regs_.R(IRREG_VFPU_CC), Imm8(0x10));

			// Next up, the "all" bit.  A bit annoying...
			MOV(32, R(SCRATCH1), regs_.R(IRREG_VFPU_CC));
			AND(32, R(SCRATCH1), Imm8(inst.dest));
			CMP(32, R(SCRATCH1), Imm8(inst.dest));
			SETcc(CC_E, R(SCRATCH1));
			SHL(32, R(SCRATCH1), Imm8(5));
			OR(32, regs_.R(IRREG_VFPU_CC), R(SCRATCH1));

			SetJumpTarget(noneSet);
		}
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
	{
		regs_.Map(inst);
		UCOMISS(regs_.FX(inst.src1), M(constants.maxIntBelowAsFloat));  // rip accessible

		CVTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.src1));
		// UCOMISS set CF if LESS and ZF if EQUAL to maxIntBelowAsFloat.
		// We want noSignMask otherwise, GREATER or UNORDERED.
		FixupBranch isNAN = J_CC(CC_P);
		FixupBranch skip = J_CC(CC_BE);
		SetJumpTarget(isNAN);
		MOVAPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible

		SetJumpTarget(skip);
		break;
	}

	case IROp::FCvtSW:
		regs_.Map(inst);
		CVTDQ2PS(regs_.FX(inst.dest), regs_.F(inst.src1));
		break;

	case IROp::FCvtScaledWS:
		regs_.Map(inst);
		if (cpu_info.bSSE4_1) {
			int scale = inst.src2 & 0x1F;
			IRRoundMode rmode = (IRRoundMode)(inst.src2 >> 6);

			if (scale != 0 && cpu_info.bAVX) {
				VMULSS(regs_.FX(inst.dest), regs_.FX(inst.src1), M(&constants.mulTableVf2i[scale]));  // rip accessible
			} else {
				if (inst.dest != inst.src1)
					MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
				if (scale != 0)
					MULSS(regs_.FX(inst.dest), M(&constants.mulTableVf2i[scale]));  // rip accessible
			}

			UCOMISS(regs_.FX(inst.dest), M(constants.maxIntBelowAsFloat));  // rip accessible

			switch (rmode) {
			case IRRoundMode::RINT_0:
				ROUNDNEARPS(regs_.FX(inst.dest), regs_.F(inst.dest));
				CVTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.dest));
				break;

			case IRRoundMode::CAST_1:
				CVTTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.dest));
				break;

			case IRRoundMode::CEIL_2:
				ROUNDCEILPS(regs_.FX(inst.dest), regs_.F(inst.dest));
				CVTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.dest));
				break;

			case IRRoundMode::FLOOR_3:
				ROUNDFLOORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
				CVTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.dest));
				break;
			}

			// UCOMISS set CF if LESS and ZF if EQUAL to maxIntBelowAsFloat.
			// We want noSignMask otherwise, GREATER or UNORDERED.
			FixupBranch isNAN = J_CC(CC_P);
			FixupBranch skip = J_CC(CC_BE);
			SetJumpTarget(isNAN);
			MOVAPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible
			SetJumpTarget(skip);
		} else {
			int scale = inst.src2 & 0x1F;
			IRRoundMode rmode = (IRRoundMode)(inst.src2 >> 6);

			int setMXCSR = -1;
			bool useTrunc = false;
			switch (rmode) {
			case IRRoundMode::RINT_0:
				// TODO: Could skip if hasSetRounding, but we don't have the flag.
				setMXCSR = 0;
				break;
			case IRRoundMode::CAST_1:
				useTrunc = true;
				break;
			case IRRoundMode::CEIL_2:
				setMXCSR = 2;
				break;
			case IRRoundMode::FLOOR_3:
				setMXCSR = 1;
				break;
			}

			// Except for truncate, we need to update MXCSR to our preferred rounding mode.
			// TODO: Might be possible to cache this and update between instructions?
			// Probably kinda expensive to switch each time...
			if (setMXCSR != -1) {
				STMXCSR(MDisp(CTXREG, mxcsrTempOffset));
				MOV(32, R(SCRATCH1), MDisp(CTXREG, mxcsrTempOffset));
				AND(32, R(SCRATCH1), Imm32(~(3 << 13)));
				if (setMXCSR != 0) {
					OR(32, R(SCRATCH1), Imm32(setMXCSR << 13));
				}
				MOV(32, MDisp(CTXREG, tempOffset), R(SCRATCH1));
				LDMXCSR(MDisp(CTXREG, tempOffset));
			}

			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			if (scale != 0)
				MULSS(regs_.FX(inst.dest), M(&constants.mulTableVf2i[scale]));  // rip accessible

			UCOMISS(regs_.FX(inst.dest), M(constants.maxIntBelowAsFloat));  // rip accessible

			if (useTrunc) {
				CVTTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.dest));
			} else {
				CVTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.dest));
			}

			// UCOMISS set CF if LESS and ZF if EQUAL to maxIntBelowAsFloat.
			// We want noSignMask otherwise, GREATER or UNORDERED.
			FixupBranch isNAN = J_CC(CC_P);
			FixupBranch skip = J_CC(CC_BE);
			SetJumpTarget(isNAN);
			MOVAPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible
			SetJumpTarget(skip);

			// Return MXCSR to its previous value.
			if (setMXCSR != -1) {
				LDMXCSR(MDisp(CTXREG, mxcsrTempOffset));
			}
		}
		break;

	case IROp::FCvtScaledSW:
		regs_.Map(inst);
		CVTDQ2PS(regs_.FX(inst.dest), regs_.F(inst.src1));
		MULSS(regs_.FX(inst.dest), M(&constants.mulTableVi2f[inst.src2 & 0x1F]));  // rip accessible
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FRound(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::FCeil:
	case IROp::FFloor:
	case IROp::FRound:
		if (cpu_info.bSSE4_1) {
			regs_.Map(inst);
			UCOMISS(regs_.FX(inst.src1), M(constants.maxIntBelowAsFloat));  // rip accessible

			switch (inst.op) {
			case IROp::FCeil:
				ROUNDCEILPS(regs_.FX(inst.dest), regs_.F(inst.src1));
				break;

			case IROp::FFloor:
				ROUNDFLOORPS(regs_.FX(inst.dest), regs_.F(inst.src1));
				break;

			case IROp::FRound:
				ROUNDNEARPS(regs_.FX(inst.dest), regs_.F(inst.src1));
				break;

			default:
				INVALIDOP;
			}
			CVTTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.dest));
			// UCOMISS set CF if LESS and ZF if EQUAL to maxIntBelowAsFloat.
			// We want noSignMask otherwise, GREATER or UNORDERED.
			FixupBranch isNAN = J_CC(CC_P);
			FixupBranch skip = J_CC(CC_BE);
			SetJumpTarget(isNAN);
			MOVAPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible

			SetJumpTarget(skip);
		} else {
			regs_.Map(inst);

			int setMXCSR = -1;
			switch (inst.op) {
			case IROp::FRound:
				// TODO: Could skip if hasSetRounding, but we don't have the flag.
				setMXCSR = 0;
				break;
			case IROp::FCeil:
				setMXCSR = 2;
				break;
			case IROp::FFloor:
				setMXCSR = 1;
				break;
			default:
				INVALIDOP;
			}

			// TODO: Might be possible to cache this and update between instructions?
			// Probably kinda expensive to switch each time...
			if (setMXCSR != -1) {
				STMXCSR(MDisp(CTXREG, mxcsrTempOffset));
				MOV(32, R(SCRATCH1), MDisp(CTXREG, mxcsrTempOffset));
				AND(32, R(SCRATCH1), Imm32(~(3 << 13)));
				if (setMXCSR != 0) {
					OR(32, R(SCRATCH1), Imm32(setMXCSR << 13));
				}
				MOV(32, MDisp(CTXREG, tempOffset), R(SCRATCH1));
				LDMXCSR(MDisp(CTXREG, tempOffset));
			}

			UCOMISS(regs_.FX(inst.src1), M(constants.maxIntBelowAsFloat));  // rip accessible

			CVTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.src1));
			// UCOMISS set CF if LESS and ZF if EQUAL to maxIntBelowAsFloat.
			// We want noSignMask otherwise, GREATER or UNORDERED.
			FixupBranch isNAN = J_CC(CC_P);
			FixupBranch skip = J_CC(CC_BE);
			SetJumpTarget(isNAN);
			MOVAPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible

			SetJumpTarget(skip);

			// Return MXCSR to its previous value.
			if (setMXCSR != -1) {
				LDMXCSR(MDisp(CTXREG, mxcsrTempOffset));
			}
		}
		break;

	case IROp::FTrunc:
	{
		regs_.Map(inst);
		UCOMISS(regs_.FX(inst.src1), M(constants.maxIntBelowAsFloat));  // rip accessible

		CVTTPS2DQ(regs_.FX(inst.dest), regs_.F(inst.src1));
		// UCOMISS set CF if LESS and ZF if EQUAL to maxIntBelowAsFloat.
		// We want noSignMask otherwise, GREATER or UNORDERED.
		FixupBranch isNAN = J_CC(CC_P);
		FixupBranch skip = J_CC(CC_BE);
		SetJumpTarget(isNAN);
		MOVAPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible

		SetJumpTarget(skip);
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FSat(IRInst inst) {
	CONDITIONAL_DISABLE;

	X64Reg tempReg = INVALID_REG;
	switch (inst.op) {
	case IROp::FSat0_1:
		tempReg = regs_.MapWithFPRTemp(inst);

		// The second argument's NAN is taken if either is NAN, so put known first.
		MOVSS(tempReg, M(constants.positiveOnes));
		MINSS(tempReg, regs_.F(inst.src1));

		// Now for NAN, we want known first again.
		// Unfortunately, this will retain -0.0, which we'll fix next.
		XORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
		MAXSS(tempReg, regs_.F(inst.dest));

		// Important: this should clamp -0.0 to +0.0.
		ADDSS(regs_.FX(inst.dest), R(tempReg));
		break;

	case IROp::FSatMinus1_1:
		tempReg = regs_.MapWithFPRTemp(inst);

		// The second argument's NAN is taken if either is NAN, so put known first.
		MOVSS(tempReg, M(constants.negativeOnes));
		MAXSS(tempReg, regs_.F(inst.src1));

		// Again, stick with the first argument being known.
		MOVSS(regs_.FX(inst.dest), M(constants.positiveOnes));
		MINSS(regs_.FX(inst.dest), R(tempReg));
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

	auto callFuncF_F = [&](const void *func) {
		regs_.FlushBeforeCall();
		WriteDebugProfilerStatus(IRProfilerStatus::MATH_HELPER);

#if X64JIT_USE_XMM_CALL
		if (regs_.IsFPRMapped(inst.src1)) {
			int lane = regs_.GetFPRLane(inst.src1);
			CopyVec4ToFPRLane0(XMM0, regs_.FX(inst.src1), lane);
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
				CopyVec4ToFPRLane0(XMM0, regs_.FX(inst.src1), lane);
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

		WriteDebugProfilerStatus(IRProfilerStatus::IN_JIT);
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

			MOVSS(regs_.FX(inst.dest), M(constants.positiveOnes));  // rip accessible
			DIVSS(regs_.FX(inst.dest), R(tempReg));
			break;
		}

	case IROp::FRecip:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOVSS(regs_.FX(inst.dest), M(constants.positiveOnes));  // rip accessible
			DIVSS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else {
			X64Reg tempReg = regs_.MapWithFPRTemp(inst);
			MOVSS(tempReg, M(constants.positiveOnes));  // rip accessible
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
