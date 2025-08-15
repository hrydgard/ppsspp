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

static bool Overlap(IRReg r1, int l1, IRReg r2, int l2) {
	return r1 < r2 + l2 && r1 + l1 > r2;
}

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
		if (Overlap(inst.dest, 4, inst.src2, 1) || Overlap(inst.src1, 4, inst.src2, 1)) {
			// ARM64 can handle this, but we have to map specially.
			regs_.SpillLockFPR(inst.dest, inst.src1);
			regs_.MapVec4(inst.src1);
			regs_.MapVec4(inst.src2 & ~3);
			regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
			fp_.FMUL(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), regs_.FQ(inst.src2 & ~3), inst.src2 & 3);
		} else {
			regs_.Map(inst);
			fp_.FMUL(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), regs_.FQ(inst.src2), 0);
		}
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
		regs_.Map(inst);
		switch (Vec4Init(inst.src1)) {
		case Vec4Init::AllZERO:
			fp_.MOVI(32, regs_.FQ(inst.dest), 0);
			break;

		case Vec4Init::AllONE:
		case Vec4Init::AllMinusONE:
			fp_.MOVI2FDUP(regs_.FQ(inst.dest), 1.0f, INVALID_REG, Vec4Init(inst.src1) == Vec4Init::AllMinusONE);
			break;

		case Vec4Init::Set_1000:
		case Vec4Init::Set_0100:
		case Vec4Init::Set_0010:
		case Vec4Init::Set_0001:
			fp_.MOVI(32, regs_.FQ(inst.dest), 0);
			fp_.MOVI2FDUP(EncodeRegToQuad(SCRATCHF1), 1.0f);
			fp_.INS(32, regs_.FQ(inst.dest), inst.src1 - (int)Vec4Init::Set_1000, EncodeRegToQuad(SCRATCHF1), inst.src1 - (int)Vec4Init::Set_1000);
			break;

		default:
			_assert_msg_(false, "Unexpected Vec4Init value %d", inst.src1);
			DISABLE;
		}
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
		regs_.Map(inst);
		if (inst.src1 == inst.src2) {
			// Shouldn't really happen, just making sure the below doesn't have to think about it.
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			break;
		}

		// To reduce overlap cases to consider, let's inverse src1/src2 if dest == src2.
		// Thus, dest could be src1, but no other overlap is possible.
		if (inst.dest == inst.src2) {
			std::swap(inst.src1, inst.src2);
			inst.constant ^= 0xF;
		}

		switch (inst.constant & 0xF) {
		case 0b0000:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			break;

		case 0b0001:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(32, regs_.FQ(inst.dest), 0, regs_.FQ(inst.src2), 0);
			break;

		case 0b0010:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(32, regs_.FQ(inst.dest), 1, regs_.FQ(inst.src2), 1);
			break;

		case 0b0011:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(64, regs_.FQ(inst.dest), 0, regs_.FQ(inst.src2), 0);
			break;

		case 0b0100:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(32, regs_.FQ(inst.dest), 2, regs_.FQ(inst.src2), 2);
			break;

		case 0b0101:
			// To get AbCd: REV64 to BADC, then TRN2 xAxC, xbxd.
			fp_.REV64(32, EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src2));
			fp_.TRN2(32, regs_.FQ(inst.dest), EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1));
			break;

		case 0b0110:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(32, regs_.FQ(inst.dest), 1, regs_.FQ(inst.src2), 1);
			fp_.INS(32, regs_.FQ(inst.dest), 2, regs_.FQ(inst.src2), 2);
			break;

		case 0b0111:
			if (inst.dest != inst.src1) {
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 3, regs_.FQ(inst.src1), 3);
			} else {
				fp_.MOV(EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1));
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 3, EncodeRegToQuad(SCRATCHF1), 3);
			}
			break;

		case 0b1000:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(32, regs_.FQ(inst.dest), 3, regs_.FQ(inst.src2), 3);
			break;

		case 0b1001:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(32, regs_.FQ(inst.dest), 0, regs_.FQ(inst.src2), 0);
			fp_.INS(32, regs_.FQ(inst.dest), 3, regs_.FQ(inst.src2), 3);
			break;

		case 0b1010:
			// To get aBcD: REV64 to badc, then TRN2 xaxc, xBxD.
			fp_.REV64(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.TRN2(32, regs_.FQ(inst.dest), regs_.FQ(inst.dest), regs_.FQ(inst.src2));
			break;

		case 0b1011:
			if (inst.dest != inst.src1) {
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 2, regs_.FQ(inst.src1), 2);
			} else {
				fp_.MOV(EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1));
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 2, EncodeRegToQuad(SCRATCHF1), 2);
			}
			break;

		case 0b1100:
			if (inst.dest != inst.src1)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src1));
			fp_.INS(64, regs_.FQ(inst.dest), 1, regs_.FQ(inst.src2), 1);
			break;

		case 0b1101:
			if (inst.dest != inst.src1) {
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 1, regs_.FQ(inst.src1), 1);
			} else {
				fp_.MOV(EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1));
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 1, EncodeRegToQuad(SCRATCHF1), 1);
			}
			break;

		case 0b1110:
			if (inst.dest != inst.src1) {
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 0, regs_.FQ(inst.src1), 0);
			} else {
				fp_.MOV(EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1));
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
				fp_.INS(32, regs_.FQ(inst.dest), 0, EncodeRegToQuad(SCRATCHF1), 0);
			}
			break;

		case 0b1111:
			if (inst.dest != inst.src2)
				fp_.MOV(regs_.FQ(inst.dest), regs_.FQ(inst.src2));
			break;
		}
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
		regs_.Map(inst);
		fp_.MOVI(32, EncodeRegToQuad(SCRATCHF1), 0);
		fp_.SMAX(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), EncodeRegToQuad(SCRATCHF1));
		break;

	case IROp::Vec2ClampToZero:
		regs_.Map(inst);
		fp_.MOVI(32, EncodeRegToDouble(SCRATCHF1), 0);
		fp_.SMAX(32, regs_.FD(inst.dest), regs_.FD(inst.src1), EncodeRegToDouble(SCRATCHF1));
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
		if (Overlap(inst.dest, 1, inst.src1, 4) || Overlap(inst.dest, 1, inst.src2, 4)) {
			// To avoid overlap problems, map a little carefully.
			regs_.SpillLockFPR(inst.src1, inst.src2);
			regs_.MapVec4(inst.src1);
			regs_.MapVec4(inst.src2);
			regs_.MapVec4(inst.dest & ~3, MIPSMap::DIRTY);
			fp_.FMUL(32, EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1), regs_.FQ(inst.src2));
			fp_.FADDP(32, EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
			fp_.FADDP(32, EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
			fp_.INS(32, regs_.FQ(inst.dest & ~3), inst.dest & 3, EncodeRegToQuad(SCRATCHF1), 0);
		} else {
			regs_.Map(inst);
			fp_.FMUL(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), regs_.FQ(inst.src2));
			fp_.FADDP(32, regs_.FQ(inst.dest), regs_.FQ(inst.dest), regs_.FQ(inst.dest));
			fp_.FADDP(32, regs_.FQ(inst.dest), regs_.FQ(inst.dest), regs_.FQ(inst.dest));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4DuplicateUpperBitsAndShift1:
		// This operation swizzles the high 8 bits and converts to a signed int.
		// It's always after Vec4Unpack8To32.
		// 000A000B000C000D -> AAAABBBBCCCCDDDD and then shift right one (to match INT_MAX.)
		regs_.Map(inst);
		// First, USHR+ORR to get 0A0A0B0B0C0C0D0D.
		fp_.USHR(32, EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1), 16);
		fp_.ORR(EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1));
		// Now again, but by 8.
		fp_.USHR(32, regs_.FQ(inst.dest), EncodeRegToQuad(SCRATCHF1), 8);
		fp_.ORR(regs_.FQ(inst.dest), regs_.FQ(inst.dest), EncodeRegToQuad(SCRATCHF1));
		// Finally, shift away the sign.  The goal is to saturate 0xFF -> 0x7FFFFFFF.
		fp_.USHR(32, regs_.FQ(inst.dest), regs_.FQ(inst.dest), 1);
		break;

	case IROp::Vec2Pack31To16:
		// Same as Vec2Pack32To16, but we shift left 1 first to nuke the sign bit.
		if (Overlap(inst.dest, 1, inst.src1, 2)) {
			regs_.MapVec2(inst.src1, MIPSMap::DIRTY);
			fp_.SHL(32, EncodeRegToDouble(SCRATCHF1), regs_.FD(inst.src1), 1);
			fp_.UZP2(16, EncodeRegToDouble(SCRATCHF1), EncodeRegToDouble(SCRATCHF1), EncodeRegToDouble(SCRATCHF1));
			fp_.INS(32, regs_.FD(inst.dest & ~1), inst.dest & 1, EncodeRegToDouble(SCRATCHF1), 0);
		} else {
			regs_.Map(inst);
			fp_.SHL(32, regs_.FD(inst.dest), regs_.FD(inst.src1), 1);
			fp_.UZP2(16, regs_.FD(inst.dest), regs_.FD(inst.dest), regs_.FD(inst.dest));
		}
		break;

	case IROp::Vec2Pack32To16:
		// Viewed as 16 bit lanes: xAxB -> AB00... that's UZP2.
		if (Overlap(inst.dest, 1, inst.src1, 2)) {
			regs_.MapVec2(inst.src1, MIPSMap::DIRTY);
			fp_.UZP2(16, EncodeRegToDouble(SCRATCHF1), regs_.FD(inst.src1), regs_.FD(inst.src1));
			fp_.INS(32, regs_.FD(inst.dest & ~1), inst.dest & 1, EncodeRegToDouble(SCRATCHF1), 0);
		} else {
			regs_.Map(inst);
			fp_.UZP2(16, regs_.FD(inst.dest), regs_.FD(inst.src1), regs_.FD(inst.src1));
		}
		break;

	case IROp::Vec4Pack31To8:
		if (Overlap(inst.dest, 1, inst.src1, 4)) {
			regs_.MapVec4(inst.src1, MIPSMap::DIRTY);
		} else {
			regs_.Map(inst);
		}

		// Viewed as 8-bit lanes, after a shift by 23: AxxxBxxxCxxxDxxx.
		// So: UZP1 -> AxBxCxDx -> UZP1 again -> ABCD
		fp_.USHR(32, EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1), 23);
		fp_.UZP1(8, EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
		// Second one directly to dest, if we can.
		if (Overlap(inst.dest, 1, inst.src1, 4)) {
			fp_.UZP1(8, EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
			fp_.INS(32, regs_.FQ(inst.dest & ~3), inst.dest & 3, EncodeRegToQuad(SCRATCHF1), 0);
		} else {
			fp_.UZP1(8, regs_.FQ(inst.dest), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
		}
		break;

	case IROp::Vec4Pack32To8:
		if (Overlap(inst.dest, 1, inst.src1, 4)) {
			regs_.MapVec4(inst.src1, MIPSMap::DIRTY);
		} else {
			regs_.Map(inst);
		}

		// Viewed as 8-bit lanes, after a shift by 24: AxxxBxxxCxxxDxxx.
		// Same as Vec4Pack31To8, just a different shift.
		fp_.USHR(32, EncodeRegToQuad(SCRATCHF1), regs_.FQ(inst.src1), 24);
		fp_.UZP1(8, EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
		// Second one directly to dest, if we can.
		if (Overlap(inst.dest, 1, inst.src1, 4)) {
			fp_.UZP1(8, EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
			fp_.INS(32, regs_.FQ(inst.dest & ~3), inst.dest & 3, EncodeRegToQuad(SCRATCHF1), 0);
		} else {
			fp_.UZP1(8, regs_.FQ(inst.dest), EncodeRegToQuad(SCRATCHF1), EncodeRegToQuad(SCRATCHF1));
		}
		break;

	case IROp::Vec2Unpack16To31:
		// Viewed as 16-bit: ABxx -> 0A0B, then shift a zero into the sign place.
		if (Overlap(inst.dest, 2, inst.src1, 1)) {
			regs_.MapVec2(inst.dest, MIPSMap::DIRTY);
		} else {
			regs_.Map(inst);
		}
		if (inst.src1 == inst.dest + 1) {
			fp_.USHLL2(16, regs_.FQ(inst.dest), regs_.FD(inst.src1), 15);
		} else {
			fp_.USHLL(16, regs_.FQ(inst.dest), regs_.FD(inst.src1), 15);
		}
		break;

	case IROp::Vec2Unpack16To32:
		// Just Vec2Unpack16To31, without the shift.
		if (Overlap(inst.dest, 2, inst.src1, 1)) {
			regs_.MapVec2(inst.dest, MIPSMap::DIRTY);
		} else {
			regs_.Map(inst);
		}
		if (inst.src1 == inst.dest + 1) {
			fp_.SHLL2(16, regs_.FQ(inst.dest), regs_.FD(inst.src1));
		} else {
			fp_.SHLL(16, regs_.FQ(inst.dest), regs_.FD(inst.src1));
		}
		break;

	case IROp::Vec4Unpack8To32:
		// Viewed as 8-bit: ABCD -> 000A000B000C000D.
		if (Overlap(inst.dest, 4, inst.src1, 1)) {
			regs_.MapVec4(inst.dest, MIPSMap::DIRTY);
			if (inst.dest == inst.src1 + 2) {
				fp_.SHLL2(8, regs_.FQ(inst.dest), regs_.FD(inst.src1 & ~3));
			} else if (inst.dest != inst.src1) {
				fp_.DUP(32, regs_.FQ(inst.dest), regs_.FQ(inst.src1), inst.src1 & 3);
				fp_.SHLL(8, regs_.FQ(inst.dest), regs_.FD(inst.dest));
			} else {
				fp_.SHLL(8, regs_.FQ(inst.dest), regs_.FD(inst.src1));
			}
			fp_.SHLL(16, regs_.FQ(inst.dest), regs_.FD(inst.dest));
		} else {
			regs_.Map(inst);
			// Two steps: ABCD -> 0A0B0C0D, then to 000A000B000C000D.
			fp_.SHLL(8, regs_.FQ(inst.dest), regs_.FD(inst.src1));
			fp_.SHLL(16, regs_.FQ(inst.dest), regs_.FD(inst.dest));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
