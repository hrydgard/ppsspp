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
#if PPSSPP_ARCH(ARM64)

#include <algorithm>

#include "Common/BitSet.h"
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"

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

//#define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp {
using namespace Arm64Gen;
using namespace Arm64JitConstants;

static u32 EvalOr(u32 a, u32 b) { return a | b; }
static u32 EvalEor(u32 a, u32 b) { return a ^ b; }
static u32 EvalAnd(u32 a, u32 b) { return a & b; }
static u32 EvalAdd(u32 a, u32 b) { return a + b; }
static u32 EvalSub(u32 a, u32 b) { return a - b; }

void Arm64Jit::CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, void (ARM64XEmitter::*arith)(ARM64Reg dst, ARM64Reg src, ARM64Reg src2), bool (ARM64XEmitter::*tryArithI2R)(ARM64Reg dst, ARM64Reg src, u64 val), u32 (*eval)(u32 a, u32 b)) {
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
	CONDITIONAL_DISABLE(ALU_IMM);
	u32 uimm = op & 0xFFFF;
	s32 simm = SignExtend16ToS32(op);
	u32 suimm = SignExtend16ToU32(op);

	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;

	// noop, won't write to ZERO.
	if (rt == 0)
		return;

	switch (op >> 26) {
	case 8:	// same as addiu?
	case 9:	// R(rt) = R(rs) + simm; break;	//addiu
		// Special-case for small adjustments of pointerified registers. Commonly for SP but happens for others.
		if (rs == rt && gpr.IsMappedAsPointer(rs) && IsImmArithmetic(simm < 0 ? -simm : simm, nullptr, nullptr)) {
			ARM64Reg r32 = gpr.RPtr(rs);
			gpr.MarkDirty(r32);
			ARM64Reg r = EncodeRegTo64(r32);
			ADDI2R(r, r, simm);
		} else {
			if (simm >= 0) {
				CompImmLogic(rs, rt, simm, &ARM64XEmitter::ADD, &ARM64XEmitter::TryADDI2R, &EvalAdd);
			} else if (simm < 0) {
				CompImmLogic(rs, rt, -simm, &ARM64XEmitter::SUB, &ARM64XEmitter::TrySUBI2R, &EvalSub);
			}
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
			// Grab the sign bit (< 0) as 1/0.  Slightly faster than a shift.
			UBFX(gpr.R(rt), gpr.R(rs), 31, 1);
			break;
		}
		gpr.MapDirtyIn(rt, rs);
		if (!TryCMPI2R(gpr.R(rs), (u32)simm)) {
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
	CONDITIONAL_DISABLE(ALU_BIT);

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
		MVN(gpr.R(rd), gpr.R(rs));
		CLZ(gpr.R(rd), gpr.R(rd));
		break;
	default:
		DISABLE;
	}
}

void Arm64Jit::CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, void (ARM64XEmitter::*arith)(ARM64Reg dst, ARM64Reg rm, ARM64Reg rn), bool (ARM64XEmitter::*tryArithI2R)(ARM64Reg dst, ARM64Reg rm, u64 val), u32(*eval)(u32 a, u32 b), bool symmetric) {
	if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
		gpr.SetImm(rd, (*eval)(gpr.GetImm(rs), gpr.GetImm(rt)));
		return;
	}

	// Optimize anything against zero.
	if (gpr.IsImm(rs) && gpr.GetImm(rs) == 0) {
		gpr.MapDirtyIn(rd, rt);
		(this->*arith)(gpr.R(rd), WZR, gpr.R(rt));
		return;
	}
	if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0) {
		gpr.MapDirtyIn(rd, rs);
		(this->*arith)(gpr.R(rd), gpr.R(rs), WZR);
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
		// This means the rhsImm value was never flushed to rhs, and would be garbage.
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
	CONDITIONAL_DISABLE(ALU);

	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// noop, won't write to ZERO.
	if (rd == 0)
		return;

	switch (op & 63) {
	case 10: //if (!R(rt)) R(rd) = R(rs);       break; //movz
		gpr.MapDirtyInIn(rd, rt, rs, false);
		CMP(gpr.R(rt), 0);
		CSEL(gpr.R(rd), gpr.R(rs), gpr.R(rd), CC_EQ);
		break;
	case 11:// if (R(rt)) R(rd) = R(rs);		break; //movn
		gpr.MapDirtyInIn(rd, rt, rs, false);
		CMP(gpr.R(rt), 0);
		CSEL(gpr.R(rd), gpr.R(rs), gpr.R(rd), CC_NEQ);
		break;

	case 32: //R(rd) = R(rs) + R(rt);           break; //add
	case 33: //R(rd) = R(rs) + R(rt);           break; //addu
		if (gpr.IsImm(rs) && gpr.GetImm(rs) == 0 && !gpr.IsImm(rt)) {
			// Special case: actually a mov, avoid arithmetic.
			gpr.MapDirtyIn(rd, rt);
			MOV(gpr.R(rd), gpr.R(rt));
		} else if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0 && !gpr.IsImm(rs)) {
			gpr.MapDirtyIn(rd, rs);
			MOV(gpr.R(rd), gpr.R(rs));
		} else {
			CompType3(rd, rs, rt, &ARM64XEmitter::ADD, &ARM64XEmitter::TryADDI2R, &EvalAdd, true);
		}
		break;

	case 34: //R(rd) = R(rs) - R(rt);           break; //sub
	case 35: //R(rd) = R(rs) - R(rt);           break; //subu
		CompType3(rd, rs, rt, &ARM64XEmitter::SUB, &ARM64XEmitter::TrySUBI2R, &EvalSub, false);
		break;

	case 36: //R(rd) = R(rs) & R(rt);           break; //and
		CompType3(rd, rs, rt, &ARM64XEmitter::AND, &ARM64XEmitter::TryANDI2R, &EvalAnd, true);
		break;
	case 37: //R(rd) = R(rs) | R(rt);           break; //or
		CompType3(rd, rs, rt, &ARM64XEmitter::ORR, &ARM64XEmitter::TryORRI2R, &EvalOr, true);
		break;
	case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
		CompType3(rd, rs, rt, &ARM64XEmitter::EOR, &ARM64XEmitter::TryEORI2R, &EvalEor, true);
		break;

	case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, ~(gpr.GetImm(rs) | gpr.GetImm(rt)));
		} else if (gpr.IsImm(rs) || gpr.IsImm(rt)) {
			MIPSGPReg lhs = gpr.IsImm(rs) ? rt : rs;
			MIPSGPReg rhs = gpr.IsImm(rs) ? rs : rt;
			u32 rhsImm = gpr.GetImm(rhs);
			if (rhsImm == 0) {
				gpr.MapDirtyIn(rd, lhs);
				MVN(gpr.R(rd), gpr.R(lhs));
			} else {
				// Ignored, just for IsImmLogical.
				unsigned int n, imm_s, imm_r;
				if (IsImmLogical(rhsImm, 32, &n, &imm_s, &imm_r)) {
					// Great, we can avoid flushing a reg.
					gpr.MapDirtyIn(rd, lhs);
					ORRI2R(gpr.R(rd), gpr.R(lhs), rhsImm);
				} else {
					gpr.MapDirtyInIn(rd, rs, rt);
					ORR(gpr.R(rd), gpr.R(rs), gpr.R(rt));
				}
				MVN(gpr.R(rd), gpr.R(rd));
			}
		} else {
			gpr.MapDirtyInIn(rd, rs, rt);
			ORR(gpr.R(rd), gpr.R(rs), gpr.R(rt));
			MVN(gpr.R(rd), gpr.R(rd));
		}
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
	switch (shiftType) {
	case ST_LSL: LSLV(gpr.R(rd), gpr.R(rt), gpr.R(rs)); break;
	case ST_LSR: LSRV(gpr.R(rd), gpr.R(rt), gpr.R(rs)); break;
	case ST_ASR: ASRV(gpr.R(rd), gpr.R(rt), gpr.R(rs)); break;
	case ST_ROR: RORV(gpr.R(rd), gpr.R(rt), gpr.R(rs)); break;
	}
}

void Arm64Jit::Comp_ShiftType(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU);
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
	CONDITIONAL_DISABLE(ALU_BIT);
	MIPSGPReg rs = _RS;
	MIPSGPReg rt = _RT;

	int pos = _POS;
	int size = _SIZE + 1;
	u32 mask = 0xFFFFFFFFUL >> (32 - size);

	// Don't change $zr.
	if (rt == 0)
		return;

	switch (op & 0x3f) {
	case 0x0: //ext
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, (gpr.GetImm(rs) >> pos) & mask);
			return;
		}

		gpr.MapDirtyIn(rt, rs);
		UBFX(gpr.R(rt), gpr.R(rs), pos, size);
		break;

	case 0x4: //ins
		{
			u32 sourcemask = mask >> pos;
			u32 destmask = ~(sourcemask << pos);
			if (gpr.IsImm(rs)) {
				u32 inserted = (gpr.GetImm(rs) & sourcemask) << pos;
				if (gpr.IsImm(rt)) {
					gpr.SetImm(rt, (gpr.GetImm(rt) & destmask) | inserted);
					return;
				}

				// It might be nice to avoid flushing rs, but it's a little slower and
				// usually more instructions.  Not worth it.
				gpr.MapDirtyIn(rt, rs, false);
				BFI(gpr.R(rt), gpr.R(rs), pos, size - pos);
			} else {
				gpr.MapDirtyIn(rt, rs, false);
				BFI(gpr.R(rt), gpr.R(rs), pos, size - pos);
			}
		}
		break;
	}
}

void Arm64Jit::Comp_Allegrex(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU_BIT);
	MIPSGPReg rt = _RT;
	MIPSGPReg rd = _RD;
	// Don't change $zr.
	if (rd == 0)
		return;

	switch ((op >> 6) & 31) {
	case 16: // seb	// R(rd) = SignExtend8ToU32(R(rt));
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, SignExtend8ToU32(gpr.GetImm(rt)));
			return;
		}
		gpr.MapDirtyIn(rd, rt);
		SXTB(gpr.R(rd), gpr.R(rt));
		break;

	case 24: // seh
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, SignExtend16ToU32(gpr.GetImm(rt)));
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
	CONDITIONAL_DISABLE(ALU_BIT);
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
	CONDITIONAL_DISABLE(MULDIV);
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// Note that in all cases below, LO is actually mapped to HI:LO.
	// That is, the host reg is 64 bits and has HI at the top.
	// HI is not mappable.

	switch (op & 63) {
	case 16: // R(rd) = HI; //mfhi
		// LO and HI are in the same reg.
		if (gpr.IsImm(MIPS_REG_LO)) {
			gpr.SetImm(rd, gpr.GetImm(MIPS_REG_LO) >> 32);
			break;
		}
		gpr.MapDirtyIn(rd, MIPS_REG_LO);
		UBFX(EncodeRegTo64(gpr.R(rd)), EncodeRegTo64(gpr.R(MIPS_REG_LO)), 32, 32);
		break;

	case 17: // HI = R(rs); //mthi
		if (gpr.IsImm(rs) && gpr.IsImm(MIPS_REG_LO)) {
			gpr.SetImm(MIPS_REG_LO, (gpr.GetImm(rs) << 32) | (gpr.GetImm(MIPS_REG_LO) & 0xFFFFFFFFULL));
			break;
		}
		gpr.MapDirtyIn(MIPS_REG_LO, rs, false);
		BFI(EncodeRegTo64(gpr.R(MIPS_REG_LO)), EncodeRegTo64(gpr.R(rs)), 32, 32);
		break;

	case 18: // R(rd) = LO; break; //mflo
		if (gpr.IsImm(MIPS_REG_LO)) {
			gpr.SetImm(rd, gpr.GetImm(MIPS_REG_LO) & 0xFFFFFFFFULL);
			break;
		}
		gpr.MapDirtyIn(rd, MIPS_REG_LO);
		MOV(gpr.R(rd), gpr.R(MIPS_REG_LO));
		break;

	case 19: // LO = R(rs); break; //mtlo
		if (gpr.IsImm(rs) && gpr.IsImm(MIPS_REG_LO)) {
			gpr.SetImm(MIPS_REG_LO, gpr.GetImm(rs) | (gpr.GetImm(MIPS_REG_LO) & ~0xFFFFFFFFULL));
			break;
		}
		gpr.MapDirtyIn(MIPS_REG_LO, rs, false);
		BFI(EncodeRegTo64(gpr.R(MIPS_REG_LO)), EncodeRegTo64(gpr.R(rs)), 0, 32);
		break;

	case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			s64 result = (s64)(s32)gpr.GetImm(rs) * (s64)(s32)gpr.GetImm(rt);
			gpr.SetImm(MIPS_REG_LO, (u64)result);
			break;
		}
		gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt);
		SMULL(EncodeRegTo64(gpr.R(MIPS_REG_LO)), gpr.R(rs), gpr.R(rt));
		break;

	case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			u64 resultBits = (u64)gpr.GetImm(rs) * (u64)gpr.GetImm(rt);
			gpr.SetImm(MIPS_REG_LO, resultBits);
			break;
		}
		gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt);
		// In case of pointerification, let's use UMULL.
		UMULL(EncodeRegTo64(gpr.R(MIPS_REG_LO)), gpr.R(rs), gpr.R(rt));
		break;

	case 26: //div
	{
		// TODO: Does this handle INT_MAX, 0, etc. correctly?
		gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt);
		SDIV(gpr.R(MIPS_REG_LO), gpr.R(rs), gpr.R(rt));
		MSUB(SCRATCH1, gpr.R(rt), gpr.R(MIPS_REG_LO), gpr.R(rs));

		CMPI2R(gpr.R(rt), 0);
		FixupBranch skipZero = B(CC_NEQ);
		// HI set properly already, we just need to set LO.
		MOVI2R(gpr.R(MIPS_REG_LO), -1);
		CMPI2R(gpr.R(rs), 0);
		FixupBranch moreThan16Bit = B(CC_GE);
		MOVI2R(gpr.R(MIPS_REG_LO), 1);
		SetJumpTarget(moreThan16Bit);
		SetJumpTarget(skipZero);

		BFI(EncodeRegTo64(gpr.R(MIPS_REG_LO)), SCRATCH1_64, 32, 32);
		break;
	}

	case 27: //divu
		// Do we have a known power-of-two denominator?  Yes, this happens.
		if (gpr.IsImm(rt) && (gpr.GetImm(rt) & (gpr.GetImm(rt) - 1)) == 0 && gpr.GetImm(rt) != 0) {
			u32 denominator = gpr.GetImm(rt);
			gpr.MapDirtyIn(MIPS_REG_LO, rs);
			// Remainder is just an AND, neat.
			ANDI2R(SCRATCH1, gpr.R(rs), denominator - 1, SCRATCH1);
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
			BFI(EncodeRegTo64(gpr.R(MIPS_REG_LO)), SCRATCH1_64, 32, 32);
		} else {
			// TODO: Does this handle INT_MAX, 0, etc. correctly?
			gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt);
			UDIV(gpr.R(MIPS_REG_LO), gpr.R(rs), gpr.R(rt));
			MSUB(SCRATCH1, gpr.R(rt), gpr.R(MIPS_REG_LO), gpr.R(rs));

			CMPI2R(gpr.R(rt), 0);
			FixupBranch skipZero = B(CC_NEQ);
			// HI set properly, we just need to set LO.
			MOVI2R(SCRATCH2, 0xFFFF);
			MOVI2R(gpr.R(MIPS_REG_LO), -1);
			CMP(gpr.R(rs), SCRATCH2);
			FixupBranch moreThan16Bit = B(CC_HI);
			MOV(gpr.R(MIPS_REG_LO), SCRATCH2);
			SetJumpTarget(moreThan16Bit);
			SetJumpTarget(skipZero);

			BFI(EncodeRegTo64(gpr.R(MIPS_REG_LO)), SCRATCH1_64, 32, 32);
		}
		break;

	case 28: //madd
	{
		gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		SMADDL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
	}
		break;

	case 29: //maddu
	{
		gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		UMADDL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
	}
		break;

	case 46: // msub
	{
		gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		SMSUBL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
	}
		break;

	case 47: // msubu
	{
		gpr.MapDirtyInIn(MIPS_REG_LO, rs, rt, false);
		ARM64Reg lo64 = EncodeRegTo64(gpr.R(MIPS_REG_LO));
		UMSUBL(lo64, gpr.R(rs), gpr.R(rt), lo64);  // Operands are backwards in the emitter!
		break;
	}

	default:
		DISABLE;
	}
}

}

#endif // PPSSPP_ARCH(ARM64)
