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

#include <algorithm>
#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/ARM64/Arm64IRJit.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"

// This file contains compilation for vector instructions.
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

void Arm64JitBackend::CompIR_VecArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Add:
		regs_.Map(inst);
		fp_.FADD(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), regs_.FQ(inst.src2));
		break;

	case IROp::Vec4Sub:
		regs_.Map(inst);
		fp_.FSUB(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), regs_.FQ(inst.src2));
		break;

	case IROp::Vec4Mul:
		regs_.Map(inst);
		fp_.FMUL(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), regs_.FQ(inst.src2));
		break;

	case IROp::Vec4Div:
		regs_.Map(inst);
		fp_.FDIV(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), regs_.FQ(inst.src2));
		break;

	case IROp::Vec4Scale:
		CompIR_Generic(inst);
		break;

	case IROp::Vec4Neg:
		regs_.Map(inst);
		fp_.FNEG(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1));
		break;

	case IROp::Vec4Abs:
		regs_.Map(inst);
		fp_.FABS(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

enum class Arm64Shuffle {
	DUP0_AAAA,
	DUP1_BBBB,
	DUP2_CCCC,
	DUP3_DDDD,
	MOV_ABCD,
	TRN1_AACC,
	TRN2_BBDD,
	UZP1_ACAC,
	UZP2_BDBD,
	ZIP1_AABB,
	ZIP2_CCDD,
	REV64_BADC,
	EXT4_BCDA,
	EXT8_CDAB,
	EXT12_DABC,

	// These steps are more expensive and use a temp.
	REV64_EXT8_CDBA,
	REV64_EXT8_DCAB,
	EXT4_UZP1_BDAC,
	EXT4_UZP2_CABD,
	EXT8_ZIP1_ACBD,
	EXT8_ZIP2_CADB,

	// Any that don't fully replace dest must be after this point.
	INS0_TO_1,
	INS0_TO_2,
	INS0_TO_3,
	INS1_TO_0,
	INS1_TO_2,
	INS1_TO_3,
	INS2_TO_0,
	INS2_TO_1,
	INS2_TO_3,
	INS3_TO_0,
	INS3_TO_1,
	INS3_TO_2,
	XTN2,

	// These hacks to prevent 4 instructions, but scoring isn't smart enough to avoid.
	EXT12_ZIP1_ADBA,
	DUP3_UZP1_DDAC,

	COUNT_NORMAL = EXT12_ZIP1_ADBA,
	COUNT_SIMPLE = REV64_EXT8_CDBA,
	COUNT_NOPREV = INS0_TO_1,
};

uint8_t Arm64ShuffleMask(Arm64Shuffle method) {
	// Hopefully optimized into a lookup table, this is a bit less confusing to read...
	switch (method) {
	case Arm64Shuffle::DUP0_AAAA: return 0x00;
	case Arm64Shuffle::DUP1_BBBB: return 0x55;
	case Arm64Shuffle::DUP2_CCCC: return 0xAA;
	case Arm64Shuffle::DUP3_DDDD: return 0xFF;
	case Arm64Shuffle::MOV_ABCD: return 0xE4;
	case Arm64Shuffle::TRN1_AACC: return 0xA0;
	case Arm64Shuffle::TRN2_BBDD: return 0xF5;
	case Arm64Shuffle::UZP1_ACAC: return 0x88;
	case Arm64Shuffle::UZP2_BDBD: return 0xDD;
	case Arm64Shuffle::ZIP1_AABB: return 0x50;
	case Arm64Shuffle::ZIP2_CCDD: return 0xFA;
	case Arm64Shuffle::REV64_BADC: return 0xB1;
	case Arm64Shuffle::EXT4_BCDA: return 0x39;
	case Arm64Shuffle::EXT8_CDAB: return 0x4E;
	case Arm64Shuffle::EXT12_DABC: return 0x93;
	case Arm64Shuffle::REV64_EXT8_CDBA: return 0x1E;
	case Arm64Shuffle::REV64_EXT8_DCAB: return 0x4B;
	case Arm64Shuffle::EXT4_UZP1_BDAC: return 0x8D;
	case Arm64Shuffle::EXT4_UZP2_CABD: return 0xD2;
	case Arm64Shuffle::EXT8_ZIP1_ACBD: return 0xD8;
	case Arm64Shuffle::EXT8_ZIP2_CADB: return 0x72;
	case Arm64Shuffle::INS0_TO_1: return 0xE0;
	case Arm64Shuffle::INS0_TO_2: return 0xC4;
	case Arm64Shuffle::INS0_TO_3: return 0x24;
	case Arm64Shuffle::INS1_TO_0: return 0xE5;
	case Arm64Shuffle::INS1_TO_2: return 0xD4;
	case Arm64Shuffle::INS1_TO_3: return 0x64;
	case Arm64Shuffle::INS2_TO_0: return 0xE6;
	case Arm64Shuffle::INS2_TO_1: return 0xE8;
	case Arm64Shuffle::INS2_TO_3: return 0xA4;
	case Arm64Shuffle::INS3_TO_0: return 0xE7;
	case Arm64Shuffle::INS3_TO_1: return 0xEC;
	case Arm64Shuffle::INS3_TO_2: return 0xF4;
	case Arm64Shuffle::XTN2: return 0x84;
	case Arm64Shuffle::EXT12_ZIP1_ADBA: return 0x1C;
	case Arm64Shuffle::DUP3_UZP1_DDAC: return 0x8F;
	default:
		_assert_(false);
		return 0;
	}
}

void Arm64ShuffleApply(ARM64FloatEmitter &fp, Arm64Shuffle method, ARM64Reg vd, ARM64Reg vs) {
	switch (method) {
	case Arm64Shuffle::DUP0_AAAA: fp.DUP(32, vd, vs, 0); return;
	case Arm64Shuffle::DUP1_BBBB: fp.DUP(32, vd, vs, 1); return;
	case Arm64Shuffle::DUP2_CCCC: fp.DUP(32, vd, vs, 2); return;
	case Arm64Shuffle::DUP3_DDDD: fp.DUP(32, vd, vs, 3); return;
	case Arm64Shuffle::MOV_ABCD: _assert_(vd != vs); fp.MOV(vd, vs); return;
	case Arm64Shuffle::TRN1_AACC: fp.TRN1(32, vd, vs, vs); return;
	case Arm64Shuffle::TRN2_BBDD: fp.TRN2(32, vd, vs, vs); return;
	case Arm64Shuffle::UZP1_ACAC: fp.UZP1(32, vd, vs, vs); return;
	case Arm64Shuffle::UZP2_BDBD: fp.UZP2(32, vd, vs, vs); return;
	case Arm64Shuffle::ZIP1_AABB: fp.ZIP1(32, vd, vs, vs); return;
	case Arm64Shuffle::ZIP2_CCDD: fp.ZIP2(32, vd, vs, vs); return;
	case Arm64Shuffle::REV64_BADC: fp.REV64(32, vd, vs); return;
	case Arm64Shuffle::EXT4_BCDA: fp.EXT(vd, vs, vs, 4); return;
	case Arm64Shuffle::EXT8_CDAB: fp.EXT(vd, vs, vs, 8); return;
	case Arm64Shuffle::EXT12_DABC: fp.EXT(vd, vs, vs, 12); return;

	case Arm64Shuffle::REV64_EXT8_CDBA:
		fp.REV64(32, EncodeRegToQuad(SCRATCHF1), vs);
		fp.EXT(vd, vs, EncodeRegToQuad(SCRATCHF1), 8);
		return;

	case Arm64Shuffle::REV64_EXT8_DCAB:
		fp.REV64(32, EncodeRegToQuad(SCRATCHF1), vs);
		fp.EXT(vd, EncodeRegToQuad(SCRATCHF1), vs, 8);
		return;

	case Arm64Shuffle::EXT4_UZP1_BDAC:
		fp.EXT(EncodeRegToQuad(SCRATCHF1), vs, vs, 4);
		fp.UZP1(32, vd, EncodeRegToQuad(SCRATCHF1), vs);
		return;

	case Arm64Shuffle::EXT4_UZP2_CABD:
		fp.EXT(EncodeRegToQuad(SCRATCHF1), vs, vs, 4);
		fp.UZP2(32, vd, EncodeRegToQuad(SCRATCHF1), vs);
		return;

	case Arm64Shuffle::EXT8_ZIP1_ACBD:
		fp.EXT(EncodeRegToQuad(SCRATCHF1), vs, vs, 8);
		fp.ZIP1(32, vd, vs, EncodeRegToQuad(SCRATCHF1));
		return;

	case Arm64Shuffle::EXT8_ZIP2_CADB:
		fp.EXT(EncodeRegToQuad(SCRATCHF1), vs, vs, 8);
		fp.ZIP2(32, vd, vs, EncodeRegToQuad(SCRATCHF1));
		return;

	case Arm64Shuffle::INS0_TO_1: fp.INS(32, vd, 1, vs, 0); return;
	case Arm64Shuffle::INS0_TO_2: fp.INS(32, vd, 2, vs, 0); return;
	case Arm64Shuffle::INS0_TO_3: fp.INS(32, vd, 3, vs, 0); return;
	case Arm64Shuffle::INS1_TO_0: fp.INS(32, vd, 0, vs, 1); return;
	case Arm64Shuffle::INS1_TO_2: fp.INS(32, vd, 2, vs, 1); return;
	case Arm64Shuffle::INS1_TO_3: fp.INS(32, vd, 3, vs, 1); return;
	case Arm64Shuffle::INS2_TO_0: fp.INS(32, vd, 0, vs, 2); return;
	case Arm64Shuffle::INS2_TO_1: fp.INS(32, vd, 1, vs, 2); return;
	case Arm64Shuffle::INS2_TO_3: fp.INS(32, vd, 3, vs, 2); return;
	case Arm64Shuffle::INS3_TO_0: fp.INS(32, vd, 0, vs, 3); return;
	case Arm64Shuffle::INS3_TO_1: fp.INS(32, vd, 1, vs, 3); return;
	case Arm64Shuffle::INS3_TO_2: fp.INS(32, vd, 2, vs, 3); return;

	case Arm64Shuffle::XTN2: fp.XTN2(32, vd, vs); return;

	case Arm64Shuffle::EXT12_ZIP1_ADBA:
		fp.EXT(EncodeRegToQuad(SCRATCHF1), vs, vs, 12);
		fp.ZIP1(32, vd, vs, EncodeRegToQuad(SCRATCHF1));
		return;

	case Arm64Shuffle::DUP3_UZP1_DDAC:
		fp.DUP(32, EncodeRegToQuad(SCRATCHF1), vs, 3);
		fp.UZP1(32, vd, EncodeRegToQuad(SCRATCHF1), vs);
		return;

	default:
		_assert_(false);
		return;
	}
}

uint8_t Arm64ShuffleResult(uint8_t mask, uint8_t prev) {
	if (prev == 0xE4)
		return mask;

	uint8_t result = 0;
	for (int i = 0; i < 4; ++i) {
		int takeLane = (mask >> (i * 2)) & 3;
		int lane = (prev >> (takeLane * 2)) & 3;
		result |= lane << (i * 2);
	}
	return result;
}

int Arm64ShuffleScore(uint8_t shuf, uint8_t goal, int steps = 1) {
	if (shuf == goal)
		return 100;

	int score = 0;
	bool needs[4]{};
	bool gets[4]{};
	for (int i = 0; i < 4; ++i) {
		uint8_t mask = 3 << (i * 2);
		needs[(goal & mask) >> (i * 2)] = true;
		gets[(shuf & mask) >> (i * 2)] = true;
		if ((shuf & mask) == (goal & mask))
			score += 4;
	}

	for (int i = 0; i < 4; ++i) {
		if (needs[i] && !gets[i])
			return 0;
	}

	// We need to look one level deeper to solve some, such as 1B (common) well.
	if (steps > 0) {
		int bestNextScore = 0;
		for (int m = 0; m < (int)Arm64Shuffle::COUNT_NORMAL; ++m) {
			uint8_t next = Arm64ShuffleResult(Arm64ShuffleMask((Arm64Shuffle)m), shuf);
			int nextScore = Arm64ShuffleScore(next, goal, steps - 1);
			if (nextScore > score) {
				bestNextScore = nextScore;
				if (bestNextScore == 100) {
					// Take the earliest that gives us two steps, it's cheaper (not 2 instructions.)
					score = 0;
					break;
				}
			}
		}

		score += bestNextScore / 2;
	}

	return score;
}

Arm64Shuffle Arm64BestShuffle(uint8_t goal, uint8_t prev, bool needsCopy) {
	// A couple special cases for optimal shuffles.
	if (goal == 0x7C && prev == 0xE4)
		return Arm64Shuffle::REV64_BADC;
	if (goal == 0x2B && prev == 0xE4)
		return Arm64Shuffle::EXT8_CDAB;
	if ((goal == 0x07 || goal == 0x1C) && prev == 0xE4)
		return Arm64Shuffle::EXT12_ZIP1_ADBA;
	if ((goal == 0x8F || goal == 0x2F) && prev == 0xE4)
		return Arm64Shuffle::DUP3_UZP1_DDAC;

	// needsCopy true means insert isn't possible.
	int attempts = needsCopy ? (int)Arm64Shuffle::COUNT_NOPREV : (int)Arm64Shuffle::COUNT_NORMAL;

	Arm64Shuffle best = Arm64Shuffle::MOV_ABCD;
	int bestScore = 0;
	for (int m = 0; m < attempts; ++m) {
		uint8_t result = Arm64ShuffleResult(Arm64ShuffleMask((Arm64Shuffle)m), prev);
		int score = Arm64ShuffleScore(result, goal);
		// Slightly discount options that involve an extra instruction.
		if (m >= (int)Arm64Shuffle::COUNT_SIMPLE && m < (int)Arm64Shuffle::COUNT_NOPREV)
			score--;
		if (score > bestScore) {
			best = (Arm64Shuffle)m;
			bestScore = score;
		}
	}

	_assert_(bestScore > 0);
	return best;
}


static void Arm64ShufflePerform(ARM64FloatEmitter &fp, ARM64Reg vd, ARM64Reg vs, u8 shuf) {
	// This performs all shuffles within 3 "steps" (some are two instructions, though.)
	_assert_msg_(shuf != 0xE4, "Non-shuffles shouldn't get here");

	uint8_t state = 0xE4;
	// If they're not the same, the first step needs to be a copy.
	bool needsCopy = vd != vs;
	for (int i = 0; i < 4 && state != shuf; ++i) {
		// Figure out the next step and write it out.
		Arm64Shuffle method = Arm64BestShuffle(shuf, state, needsCopy);
		Arm64ShuffleApply(fp, method, vd, needsCopy ? vs : vd);

		// Update our state to where we've ended up, for next time.
		needsCopy = false;
		state = Arm64ShuffleResult(Arm64ShuffleMask(method), state);
	}

	_assert_msg_(state == shuf, "Arm64ShufflePerform failed to resolve shuffle");
}

void Arm64JitBackend::CompIR_VecAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Init:
		CompIR_Generic(inst);
		break;

	case IROp::Vec4Shuffle:
		// There's not really an easy shuffle op on ARM64...
		if (regs_.GetFPRLaneCount(inst.src1) == 1 && (inst.src1 & 3) == 0 && inst.src2 == 0x00) {
			// This is a broadcast.  If dest == src1, this won't clear it.
			regs_.SpillLockFPR(inst.src1);
			regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
			fp_.DUP(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), 0);
		} else if (inst.src2 == 0xE4) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			}
		} else {
			regs_.Map(inst);
			Arm64ShufflePerform(fp_, regs_.FQ(inst.dest), regs_.FQ(inst.src1), inst.src2);
		}
		break;

	case IROp::Vec4Blend:
		CompIR_Generic(inst);
		break;

	case IROp::Vec4Mov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_VecClamp(IRInst inst) {
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

void Arm64JitBackend::CompIR_VecHoriz(IRInst inst) {
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

void Arm64JitBackend::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec2Unpack16To31:
	case IROp::Vec4Pack32To8:
	case IROp::Vec2Pack31To16:
	case IROp::Vec4Unpack8To32:
	case IROp::Vec2Unpack16To32:
	case IROp::Vec4DuplicateUpperBitsAndShift1:
	case IROp::Vec4Pack31To8:
	case IROp::Vec2Pack32To16:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
