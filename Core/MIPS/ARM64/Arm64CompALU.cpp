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

		/*
	case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
	{
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
			gpr.SetRegImm(SCRATCHREG1, simm);
			CMP(gpr.R(rs), SCRATCHREG1);
		}
		SetCC(CC_LT);
		MOVI2R(gpr.R(rt), 1);
		SetCC(CC_GE);
		MOVI2R(gpr.R(rt), 0);
		SetCC(CC_AL);
	}
		break;

	case 11: // R(rt) = R(rs) < suimm; break; //sltiu
	{
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, gpr.GetImm(rs) < suimm ? 1 : 0);
			break;
		}
		gpr.MapDirtyIn(rt, rs);
		if (!TryCMPI2R(gpr.R(rs), suimm)) {
			gpr.SetRegImm(SCRATCHREG1, suimm);
			CMP(gpr.R(rs), SCRATCHREG1);
		}
		SetCC(CC_LO);
		MOVI2R(gpr.R(rt), 1);
		SetCC(CC_HS);
		MOVI2R(gpr.R(rt), 0);
		SetCC(CC_AL);
	}
		break;*/

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
		break;
	case 11:// if (R(rt)) R(rd) = R(rs);		break; //movn
		DISABLE;
		break;

	case 32: //R(rd) = R(rs) + R(rt);           break; //add
	case 33: //R(rd) = R(rs) + R(rt);           break; //addu
		// We optimize out 0 as an operand2 ADD.
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
		CompType3(rd, rs, rt, &ARM64XEmitter::ORR, &ARM64XEmitter::TryORRI2R, &EvalOr, true);
		break;
	case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
		CompType3(rd, rs, rt, &ARM64XEmitter::EOR, &ARM64XEmitter::TryEORI2R, &EvalEor, true);
		break;

	case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
		DISABLE;
		break;

	case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
		DISABLE;
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, (s32)gpr.GetImm(rs) < (s32)gpr.GetImm(rt));
		} else {
			// TODO: Optimize imm cases
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			CSET(gpr.R(rd), CC_LT);
		}
		break;

	case 43: //R(rd) = R(rs) < R(rt);           break; //sltu
		DISABLE;
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, gpr.GetImm(rs) < gpr.GetImm(rt));
		} else {
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			CSET(gpr.R(rd), CC_LO);
		}
		break;

	case 44: //R(rd) = max(R(rs), R(rt);        break; //max
		DISABLE;
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, std::max(gpr.GetImm(rs), gpr.GetImm(rt)));
			break;
		}
		gpr.MapDirtyInIn(rd, rs, rt);
		CMP(gpr.R(rs), gpr.R(rt));
		CSEL(gpr.R(rd), gpr.R(rs), gpr.R(rt), CC_GE);
		break;

	case 45: //R(rd) = min(R(rs), R(rt));       break; //min
		DISABLE;
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

void Arm64Jit::Comp_ShiftType(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_Special3(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_Allegrex(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_Allegrex2(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_MulDivType(MIPSOpcode op) {
	DISABLE;
}
}
