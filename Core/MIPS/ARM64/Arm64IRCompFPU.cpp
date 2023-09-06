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
		CompIR_Generic(inst);
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
	case IROp::FCmpVfpuBit:
	case IROp::FCmpVfpuAggregate:
		CompIR_Generic(inst);
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

void Arm64JitBackend::CompIR_FRound(IRInst inst) {
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

void Arm64JitBackend::CompIR_FSat(IRInst inst) {
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

void Arm64JitBackend::CompIR_FSpecial(IRInst inst) {
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
