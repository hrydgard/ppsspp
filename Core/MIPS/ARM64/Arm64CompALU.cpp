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

#include <algorithm>

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Common/CPUDetect.h"

using namespace MIPSAnalyst;

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

//#define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
using namespace Arm64Gen;
using namespace Arm64JitConstants;

static u32 EvalOr(u32 a, u32 b) { return a | b; }
static u32 EvalEor(u32 a, u32 b) { return a ^ b; }
static u32 EvalAnd(u32 a, u32 b) { return a & b; }
static u32 EvalAdd(u32 a, u32 b) { return a + b; }
static u32 EvalSub(u32 a, u32 b) { return a - b; }

void Arm64Jit::CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, void (ARM64XEmitter::*arith)(ARM64Reg dst, ARM64Reg src, ARM64Reg src2), bool (ARM64XEmitter::*tryArithI2R)(ARM64Reg dst, ARM64Reg src, u32 val), u32 (*eval)(u32 a, u32 b)) {
	if (gpr.IsImm(rs)) {
		gpr.SetImm(rt, (*eval)(gpr.GetImm(rs), uimm));
	} else {
		gpr.MapDirtyIn(rt, rs);
		if (!(this->*tryArithI2R)(gpr.R(rt), gpr.R(rs), uimm)) {
			gpr.SetRegImm(SCRATCH1, uimm);
			(this->*arith)(gpr.R(rt), gpr.R(rs), SCRATCH1);
		}
	}
}

void Arm64Jit::Comp_IType(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	s32 simm = (s32)(s16)(op & 0xFFFF);  // sign extension
	u32 uimm = op & 0xFFFF;
	u32 suimm = (u32)(s32)simm;

	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;

	// noop, won't write to ZERO.
	if (rt == 0)
		return;

	switch (op >> 26) {
	case 8:	// same as addiu?
	case 9:	// R(rt) = R(rs) + simm; break;	//addiu
		if (simm >= 0) {
			CompImmLogic(rs, rt, simm, &ARM64XEmitter::ADD, &ARM64XEmitter::TryADDI2R, &EvalAdd);
		} else if (simm < 0) {
			CompImmLogic(rs, rt, -simm, &ARM64XEmitter::SUB, &ARM64XEmitter::TrySUBI2R, &EvalSub);
		}
		break;

	case 12: CompImmLogic(rs, rt, uimm, &ARM64XEmitter::AND, &ARM64XEmitter::TryANDI2R, &EvalAnd); break;
	case 13: CompImmLogic(rs, rt, uimm, &ARM64XEmitter::ORR, &ARM64XEmitter::TryORRI2R, &EvalOr); break;
	case 14: CompImmLogic(rs, rt, uimm, &ARM64XEmitter::EOR, &ARM64XEmitter::TryEORI2R, &EvalEor); break;

	case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, (s32)gpr.GetImm(rs) < simm ? 1 : 0);
			break;
		} else if (simm == 0) {
			gpr.MapDirtyIn(rt, rs);
			// Shift to get the sign bit only (for < 0.)
			LSR(gpr.R(rt), gpr.R(rs), 31);
			break;
		}
		gpr.MapDirtyIn(rt, rs);
		if (!TryCMPI2R(gpr.R(rs), simm)) {
			gpr.SetRegImm(SCRATCH1, simm);
			CMP(gpr.R(rs), SCRATCH1);
		}
		CSET(gpr.R(rt), CC_LT);
		break;

	case 11: // R(rt) = R(rs) < suimm; break; //sltiu
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, gpr.GetImm(rs) < suimm ? 1 : 0);
			break;
		}
		gpr.MapDirtyIn(rt, rs);
		if (!TryCMPI2R(gpr.R(rs), suimm)) {
			gpr.SetRegImm(SCRATCH1, suimm);
			CMP(gpr.R(rs), SCRATCH1);
		}
		CSET(gpr.R(rt), CC_LO);
		break;

	case 15: // R(rt) = uimm << 16;	 //lui
		gpr.SetImm(rt, uimm << 16);
		break;

	default:
		Comp_Generic(op);
		break;
	}
}

void Arm64Jit::Comp_RType2(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	DISABLE;

	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// Don't change $zr.
	if (rd == 0)
		return;

	switch (op & 63) {
	case 22: //clz
		if (gpr.IsImm(rs)) {
			u32 value = gpr.GetImm(rs);
			int x = 31;
			int count = 0;
			while (x >= 0 && !(value & (1 << x))) {
				count++;
				x--;
			}
			gpr.SetImm(rd, count);
			break;
		}
		gpr.MapDirtyIn(rd, rs);
		CLZ(gpr.R(rd), gpr.R(rs));
		break;
	case 23: //clo
		if (gpr.IsImm(rs)) {
			u32 value = gpr.GetImm(rs);
			int x = 31;
			int count = 0;
			while (x >= 0 && (value & (1 << x))) {
				count++;
				x--;
			}
			gpr.SetImm(rd, count);
			break;
		}
		gpr.MapDirtyIn(rd, rs);
		MVN(SCRATCH1, gpr.R(rs));
		CLZ(gpr.R(rd), SCRATCH1);
		break;
	default:
		DISABLE;
	}
}

void Arm64Jit::CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, void (ARM64XEmitter::*arith)(ARM64Reg dst, ARM64Reg rm, ARM64Reg rn), bool (ARM64XEmitter::*tryArithI2R)(ARM64Reg dst, ARM64Reg rm, u32 val), u32(*eval)(u32 a, u32 b), bool symmetric) {
	if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
		gpr.SetImm(rd, (*eval)(gpr.GetImm(rs), gpr.GetImm(rt)));
		return;
	}

	if (gpr.IsImm(rt) || (gpr.IsImm(rs) && symmetric)) {
		MIPSGPReg lhs = gpr.IsImm(rs) ? rt : rs;
		MIPSGPReg rhs = gpr.IsImm(rs) ? rs : rt;
		u32 rhsImm = gpr.GetImm(rhs);
		gpr.MapDirtyIn(rd, lhs);
		if ((this->*tryArithI2R)(gpr.R(rd), gpr.R(lhs), rhsImm)) {
			return;
		}
		// If rd is rhs, we may have lost it in the MapDirtyIn().  lhs was kept.
		if (rd == rhs) {
			// Luckily, it was just an imm.
			gpr.SetImm(rhs, rhsImm);
		}
	}

	// Can't do the RSB optimization on ARM64 - no RSB!

	// Generic solution.  If it's an imm, better to flush at this point.
	gpr.MapDirtyInIn(rd, rs, rt);
	(this->*arith)(gpr.R(rd), gpr.R(rs), gpr.R(rt));
}

void Arm64Jit::Comp_RType3(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// noop, won't write to ZERO.
	if (rd == 0)
		return;

	switch (op & 63) {
	case 10: //if (!R(rt)) R(rd) = R(rs);       break; //movz
		DISABLE;
		gpr.MapDirtyInIn(rd, rt, rs);
		CMP(gpr.R(rt), 0);
		CSEL(gpr.R(rd), gpr.R(rs), gpr.R(rd), CC_EQ);
		break;
	case 11:// if (R(rt)) R(rd) = R(rs);		break; //movn
		DISABLE;
		gpr.MapDirtyInIn(rd, rt, rs);
		CMP(gpr.R(rt), 0);
		CSEL(gpr.R(rd), gpr.R(rs), gpr.R(rd), CC_NEQ);
		break;

	case 32: //R(rd) = R(rs) + R(rt);           break; //add
	case 33: //R(rd) = R(rs) + R(rt);           break; //addu
		CompType3(rd, rs, rt, &ARM64XEmitter::ADD, &ARM64XEmitter::TryADDI2R, &EvalAdd, true);
		break;

	case 34: //R(rd) = R(rs) - R(rt);           break; //sub
	case 35: //R(rd) = R(rs) - R(rt);           break; //subu
		CompType3(rd, rs, rt, &ARM64XEmitter::SUB, &ARM64XEmitter::TrySUBI2R, &EvalSub, false);
		break;

	case 36: //R(rd) = R(rs) & R(rt);           break; //and
		CompType3(rd, rs, rt, &ARM64XEmitter::AND, &ARM64XEmitter::TryANDI2R, &EvalAnd, true);
		break;
	case 37: //R(rd) = R(rs) | R(rt);           break; //or
		if (rs == 0 && rd != rt) {
			gpr.MapDirtyIn(rd, rt);
			MOV(gpr.R(rd), gpr.R(rt));
		} else if (rt == 0 && rd != rs) {
			gpr.MapDirtyIn(rd, rs);
			MOV(gpr.R(rd), gpr.R(rs));
		} else if (rt == 0 && rs == 0) {
			gpr.SetImm(rd, 0);
		} else {
			CompType3(rd, rs, rt, &ARM64XEmitter::ORR, &ARM64XEmitter::TryORRI2R, &EvalOr, true);
		}
		break;
	case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
		CompType3(rd, rs, rt, &ARM64XEmitter::EOR, &ARM64XEmitter::TryEORI2R, &EvalEor, true);
		break;

	case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
		DISABLE;
		break;

	case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, (s32)gpr.GetImm(rs) < (s32)gpr.GetImm(rt));
		} else {
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			CSET(gpr.R(rd), CC_LT);
		}
		break;

	case 43: //R(rd) = R(rs) < R(rt);           break; //sltu
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, gpr.GetImm(rs) < gpr.GetImm(rt));
		} else {
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			CSET(gpr.R(rd), CC_LO);
		}
		break;

	case 44: //R(rd) = max(R(rs), R(rt);        break; //max
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, std::max(gpr.GetImm(rs), gpr.GetImm(rt)));
			break;
		}
		gpr.MapDirtyInIn(rd, rs, rt);
		CMP(gpr.R(rs), gpr.R(rt));
		CSEL(gpr.R(rd), gpr.R(rs), gpr.R(rt), CC_GE);
		break;

	case 45: //R(rd) = min(R(rs), R(rt));       break; //min
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, std::min(gpr.GetImm(rs), gpr.GetImm(rt)));
			break;
		}
		gpr.MapDirtyInIn(rd, rs, rt);
		CMP(gpr.R(rs), gpr.R(rt));
		CSEL(gpr.R(rd), gpr.R(rs), gpr.R(rt), CC_LE);
		break;

	default:
		Comp_Generic(op);
		break;
	}
}

void Arm64Jit::CompShiftImm(MIPSOpcode op, Arm64Gen::ShiftType shiftType, int sa) {
	MIPSGPReg rd = _RD;
	MIPSGPReg rt = _RT;
	if (gpr.IsImm(rt)) {
		switch (shiftType) {
		case ST_LSL:
			gpr.SetImm(rd, gpr.GetImm(rt) << sa);
			break;
		case ST_LSR:
			gpr.SetImm(rd, gpr.GetImm(rt) >> sa);
			break;
		case ST_ASR:
			gpr.SetImm(rd, (int)gpr.GetImm(rt) >> sa);
			break;
		case ST_ROR:
			gpr.SetImm(rd, (gpr.GetImm(rt) >> sa) | (gpr.GetImm(rt) << (32 - sa)));
			break;
		default:
			DISABLE;
		}
	} else {
		gpr.MapDirtyIn(rd, rt);
		MOV(gpr.R(rd), gpr.R(rt), ArithOption(gpr.R(rd), shiftType, sa));
	}
}

void Arm64Jit::CompShiftVar(MIPSOpcode op, Arm64Gen::ShiftType shiftType) {
	MIPSGPReg rd = _RD;
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	if (gpr.IsImm(rs)) {
		int sa = gpr.GetImm(rs) & 0x1F;
		CompShiftImm(op, shiftType, sa);
		return;
	}
	gpr.MapDirtyInIn(rd, rs, rt);
	ANDI2R(SCRATCH1, gpr.R(rs), 0x1F, INVALID_REG);  // Not sure if ARM64 wraps like this so let's do it for it.
	switch (shiftType) {
	case ST_LSL: LSLV(gpr.R(rd), gpr.R(rt), SCRATCH1); break;
	case ST_LSR: LSRV(gpr.R(rd), gpr.R(rt), SCRATCH1); break;
	case ST_ASR: ASRV(gpr.R(rd), gpr.R(rt), SCRATCH1); break;
	case ST_ROR: RORV(gpr.R(rd), gpr.R(rt), SCRATCH1); break;
	}
}

void Arm64Jit::Comp_ShiftType(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;
	int fd = _FD;
	int sa = _SA;

	// noop, won't write to ZERO.
	if (rd == 0)
		return;

	// WARNING : ROTR
	switch (op & 0x3f) {
	case 0: CompShiftImm(op, ST_LSL, sa); break; //sll
	case 2: CompShiftImm(op, rs == 1 ? ST_ROR : ST_LSR, sa); break;	//srl
	case 3: CompShiftImm(op, ST_ASR, sa); break; //sra
	case 4: CompShiftVar(op, ST_LSL); break; //sllv
	case 6: CompShiftVar(op, fd == 1 ? ST_ROR : ST_LSR); break; //srlv
	case 7: CompShiftVar(op, ST_ASR); break; //srav
	default:
		DISABLE;
		break;
	}
}

void Arm64Jit::Comp_Special3(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_Allegrex(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	MIPSGPReg rt = _RT;
	MIPSGPReg rd = _RD;
	// Don't change $zr.
	if (rd == 0)
		return;

	switch ((op >> 6) & 31) {
	case 16: // seb	// R(rd) = (u32)(s32)(s8)(u8)R(rt);
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, (s32)(s8)(u8)gpr.GetImm(rt));
			return;
		}
		gpr.MapDirtyIn(rd, rt);
		SXTB(gpr.R(rd), gpr.R(rt));
		break;

	case 24: // seh
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, (s32)(s16)(u16)gpr.GetImm(rt));
			return;
		}
		gpr.MapDirtyIn(rd, rt);
		SXTH(gpr.R(rd), gpr.R(rt));
		break;

	case 20: //bitrev
		if (gpr.IsImm(rt)) {
			// http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel
			u32 v = gpr.GetImm(rt);
			v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1); //   odd<->even
			v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2); //  pair<->pair
			v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4); //  nibb<->nibb
			v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8); //  byte<->byte
			v = (v >> 16) | (v << 16); // hword<->hword
			gpr.SetImm(rd, v);
			return;
		}

		gpr.MapDirtyIn(rd, rt);
		RBIT(gpr.R(rd), gpr.R(rt));
		break;

	default:
		Comp_Generic(op);
		return;
	}
}

void Arm64Jit::Comp_Allegrex2(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	MIPSGPReg rt = _RT;
	MIPSGPReg rd = _RD;
	// Don't change $zr.
	if (rd == 0)
		return;

	switch (op & 0x3ff) {
	case 0xA0: //wsbh
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, ((gpr.GetImm(rt) & 0xFF00FF00) >> 8) | ((gpr.GetImm(rt) & 0x00FF00FF) << 8));
		} else {
			gpr.MapDirtyIn(rd, rt);
			REV16(gpr.R(rd), gpr.R(rt));
		}
		break;
	case 0xE0: //wsbw
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, swap32(gpr.GetImm(rt)));
		} else {
			gpr.MapDirtyIn(rd, rt);
			REV32(gpr.R(rd), gpr.R(rt));
		}
		break;
	default:
		Comp_Generic(op);
		break;
	}
}

void Arm64Jit::Comp_MulDivType(MIPSOpcode op) {
	CONDITIONAL_DISABLE;


	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	switch (op & 63) {
	case 16: // R(rd) = HI; //mfhi
		if (gpr.IsImm(MIPS_REG_HI)) {
			gpr.SetImm(rd, gpr.GetImm(MIPS_REG_HI));
			break;
		}
		gpr.MapDirtyIn(rd, MIPS_REG_HI);
		MOV(gpr.R(rd), gpr.R(MIPS_REG_HI));
		break;

	case 17: // HI = R(rs); //mthi
		if (gpr.IsImm(rs)) {
			gpr.SetImm(MIPS_REG_HI, gpr.GetImm(rs));
			break;
		}
		gpr.MapDirtyIn(MIPS_REG_HI, rs);
		MOV(gpr.R(MIPS_REG_HI), gpr.R(rs));
		break;

	case 18: // R(rd) = LO; break; //mflo
		if (gpr.IsImm(MIPS_REG_LO)) {
			gpr.SetImm(rd, gpr.GetImm(MIPS_REG_LO));
			break;
		}
		gpr.MapDirtyIn(rd, MIPS_REG_LO);
		MOV(gpr.R(rd), gpr.R(MIPS_REG_LO));
		break;

	case 19: // LO = R(rs); break; //mtlo
		if (gpr.IsImm(rs)) {
			gpr.SetImm(MIPS_REG_LO, gpr.GetImm(rs));
			break;
		}
		gpr.MapDirtyIn(MIPS_REG_LO, rs);
		MOV(gpr.R(MIPS_REG_LO), gpr.R(rs));
		break;

	// TODO: All of these could be more elegant if we cached HI and LO together in one 64-bit register!
	case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			s64 result = (s64)(s32)gpr.GetImm(rs) * (s64)(s32)gpr.GetImm(rt);
			u64 resultBits = (u64)result;
			gpr.SetImm(MIPS_REG_LO, (u32)(resultBits >> 0));
			gpr.SetImm(MIPS_REG_HI, (u32)(resultBits >> 32));
			break;
		}
		gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);
		SMULL(EncodeRegTo64(gpr.R(MIPS_REG_LO)), gpr.R(rs), gpr.R(rt));
		LSR(EncodeRegTo64(gpr.R(MIPS_REG_HI)), EncodeRegTo64(gpr.R(MIPS_REG_LO)), 32);
		break;

	case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			u64 resultBits = (u64)gpr.GetImm(rs) * (u64)gpr.GetImm(rt);
			gpr.SetImm(MIPS_REG_LO, (u32)(resultBits >> 0));
			gpr.SetImm(MIPS_REG_HI, (u32)(resultBits >> 32));
			break;
		}
		gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);
		MUL(EncodeRegTo64(gpr.R(MIPS_REG_LO)), EncodeRegTo64(gpr.R(rs)), EncodeRegTo64(gpr.R(rt)));
		LSR(EncodeRegTo64(gpr.R(MIPS_REG_HI)), EncodeRegTo64(gpr.R(MIPS_REG_LO)), 32);
		break;

	case 26: //div
		// TODO: Does this handle INT_MAX, 0, etc. correctly?
		gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);
		SDIV(gpr.R(MIPS_REG_LO), gpr.R(rs), gpr.R(rt));
		MUL(SCRATCH1, gpr.R(rt), gpr.R(MIPS_REG_LO));
		SUB(gpr.R(MIPS_REG_HI), gpr.R(rs), SCRATCH1);
		break;

	case 27: //divu
		// Do we have a known power-of-two denominator?  Yes, this happens.
		if (gpr.IsImm(rt) && (gpr.GetImm(rt) & (gpr.GetImm(rt) - 1)) == 0) {
			u32 denominator = gpr.GetImm(rt);
			if (denominator == 0) {
				// TODO: Is this correct?
				gpr.SetImm(MIPS_REG_LO, 0);
				gpr.SetImm(MIPS_REG_HI, 0);
			} else {
				gpr.MapDirtyDirtyIn(MIPS_REG_LO, MIPS_REG_HI, rs);
				// Remainder is just an AND, neat.
				ANDI2R(gpr.R(MIPS_REG_HI), gpr.R(rs), denominator - 1, SCRATCH1);
				int shift = 0;
				while (denominator != 0) {
					++shift;
					denominator >>= 1;
				}
				// The shift value is one too much for the divide by the same value.
				if (shift > 1) {
					LSR(gpr.R(MIPS_REG_LO), gpr.R(rs), shift - 1);
				} else {
					MOV(gpr.R(MIPS_REG_LO), gpr.R(rs));
				}
			}
		} else {
			// TODO: Does this handle INT_MAX, 0, etc. correctly?
			gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);
			UDIV(gpr.R(MIPS_REG_LO), gpr.R(rs), gpr.R(rt));
			MUL(SCRATCH1, gpr.R(rt), gpr.R(MIPS_REG_LO));
			SUB(gpr.R(MIPS_REG_HI), gpr.R(rs), SCRATCH1);
		}
		break;

	case 28: //madd
	{
		gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		ARM64Reg hi64 = EncodeRegTo64(gpr.R(MIPS_REG_HI));
		ORR(lo64, lo64, hi64, ArithOption(lo64, ST_LSL, 32));
		SMADDL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
		LSR(hi64, lo64, 32);
	}
		break;

	case 29: //maddu
	{
		gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		ARM64Reg hi64 = EncodeRegTo64(gpr.R(MIPS_REG_HI));
		ORR(lo64, lo64, hi64, ArithOption(lo64, ST_LSL, 32));
		UMADDL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
		LSR(hi64, lo64, 32);
	}
		break;

	case 46: // msub
	{
		gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		ARM64Reg hi64 = EncodeRegTo64(gpr.R(MIPS_REG_HI));
		ORR(lo64, lo64, hi64, ArithOption(lo64, ST_LSL, 32));
		SMSUBL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
		LSR(hi64, lo64, 32);
	}
		break;

	case 47: // msubu
	{
		gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		ARM64Reg hi64 = EncodeRegTo64(gpr.R(MIPS_REG_HI));
		ORR(lo64, lo64, hi64, ArithOption(lo64, ST_LSL, 32));
		UMSUBL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
		LSR(hi64, lo64, 32);
		break;
	}

	default:
		DISABLE;
	}
}

}
