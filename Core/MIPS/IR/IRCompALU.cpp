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
#include "Core/MIPS/IR/IRJit.h"
#include "Core/MIPS/IR/IRRegCache.h"
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

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp {

void IRJit::CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, IROp OP) {
	if (gpr.IsImm(rs)) {
		switch (OP) {
		case IROp::AddConst: gpr.SetImm(rt, gpr.GetImm(rs) + uimm); break;
		case IROp::SubConst: gpr.SetImm(rt, gpr.GetImm(rs) - uimm); break;
		case IROp::AndConst: gpr.SetImm(rt, gpr.GetImm(rs) & uimm); break;
		case IROp::OrConst: gpr.SetImm(rt, gpr.GetImm(rs) | uimm); break;
		case IROp::XorConst: gpr.SetImm(rt, gpr.GetImm(rs) ^ uimm); break;
		}
	} else {
		gpr.MapDirtyIn(rt, rs);
		ir.Write(OP, rt, rs, ir.AddConstant(uimm));
	}
}

void IRJit::Comp_IType(MIPSOpcode op) {
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
		// Special-case for small adjustments of pointerified registers. Commonly for SP but happens for others.
		if (simm >= 0) {
			CompImmLogic(rs, rt, simm, IROp::AddConst);
		} else if (simm < 0) {
			CompImmLogic(rs, rt, -simm, IROp::SubConst);
		}
		break;

	case 12: CompImmLogic(rs, rt, uimm, IROp::AndConst); break;
	case 13: CompImmLogic(rs, rt, uimm, IROp::OrConst); break;
	case 14: CompImmLogic(rs, rt, uimm, IROp::XorConst); break;

	case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, (s32)gpr.GetImm(rs) < simm ? 1 : 0);
			break;
		}
		gpr.MapDirtyIn(rt, rs);
		ir.Write(IROp::SltConst, rt, rs, ir.AddConstant(simm));
		break;

	case 11: // R(rt) = R(rs) < suimm; break; //sltiu
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, gpr.GetImm(rs) < suimm ? 1 : 0);
			break;
		}
		gpr.MapDirtyIn(rt, rs);
		ir.Write(IROp::SltUConst, rt, rs, ir.AddConstant(suimm));
		break;

	case 15: // R(rt) = uimm << 16;	 //lui
		gpr.SetImm(rt, uimm << 16);
		break;

	default:
		Comp_Generic(op);
		break;
	}
}

void IRJit::Comp_RType2(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// Don't change $zr.
	if (rd == 0)
		return;

	switch (op & 63) {
	case 22: //clz
		gpr.MapDirtyIn(rd, rs);
		ir.Write(IROp::Clz, rd, rs);
		break;
	case 23: //clo
		gpr.MapDirtyIn(rd, rs);
		ir.Write(IROp::Not, IRTEMP_0, rs);
		ir.Write(IROp::Clz, rd, IRTEMP_0);
		break;
	default:
		DISABLE;
	}
}

void IRJit::CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, IROp op, IROp constOp, bool symmetric) {
	if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
		switch (op) {
		case IROp::Add: gpr.SetImm(rd, gpr.GetImm(rs) + gpr.GetImm(rt)); break;
		case IROp::Sub: gpr.SetImm(rd, gpr.GetImm(rs) - gpr.GetImm(rt)); break;
		case IROp::And: gpr.SetImm(rd, gpr.GetImm(rs) & gpr.GetImm(rt)); break;
		case IROp::Or: gpr.SetImm(rd, gpr.GetImm(rs) | gpr.GetImm(rt)); break;
		case IROp::Xor: gpr.SetImm(rd, gpr.GetImm(rs) ^ gpr.GetImm(rt)); break;
		}
		return;
	}

	// Can't do the RSB optimization on ARM64 - no RSB!

	// Generic solution.  If it's an imm, better to flush at this point.
	gpr.MapDirtyInIn(rd, rs, rt);
	ir.Write(op, rd, rs, rt);
}

void IRJit::Comp_RType3(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// noop, won't write to ZERO.
	if (rd == 0)
		return;

	switch (op & 63) {
	case 10: //if (!R(rt)) R(rd) = R(rs);       break; //movz
		gpr.MapDirtyInIn(rd, rt, rs);
		ir.Write(IROp::MovZ, rd, rt, rs);
		break;
	case 11:// if (R(rt)) R(rd) = R(rs);		break; //movn
		gpr.MapDirtyInIn(rd, rt, rs);
		ir.Write(IROp::MovNZ, rd, rt, rs);
		break;

	case 32: //R(rd) = R(rs) + R(rt);           break; //add
	case 33: //R(rd) = R(rs) + R(rt);           break; //addu
		CompType3(rd, rs, rt, IROp::Add, IROp::AddConst, true);
		break;

	case 34: //R(rd) = R(rs) - R(rt);           break; //sub
	case 35: //R(rd) = R(rs) - R(rt);           break; //subu
		CompType3(rd, rs, rt, IROp::Sub, IROp::SubConst, false);
		break;

	case 36: //R(rd) = R(rs) & R(rt);           break; //and
		CompType3(rd, rs, rt, IROp::And, IROp::AndConst, true);
		break;
	case 37: //R(rd) = R(rs) | R(rt);           break; //or
		CompType3(rd, rs, rt, IROp::Or, IROp::OrConst, true);
		break;
	case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
		CompType3(rd, rs, rt, IROp::Xor, IROp::XorConst, true);
		break;

	case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, ~(gpr.GetImm(rs) | gpr.GetImm(rt)));
		} else {
			gpr.MapDirtyInIn(rd, rs, rt);
			if (rs == 0) {
				ir.Write(IROp::Not, rd, rt);
			} else if (rt == 0) {
				ir.Write(IROp::Not, rd, rs);
			} else {
				ir.Write(IROp::Or, IRTEMP_0, rs, rt);
				ir.Write(IROp::Not, rd, IRTEMP_0);
			}
		}
		break;

	case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, (s32)gpr.GetImm(rs) < (s32)gpr.GetImm(rt));
		} else {
			gpr.MapDirtyInIn(rd, rt, rs);
			ir.Write(IROp::Slt, rd, rs, rt);
		}
		break;

	case 43: //R(rd) = R(rs) < R(rt);           break; //sltu
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, gpr.GetImm(rs) < gpr.GetImm(rt));
		} else {
			gpr.MapDirtyInIn(rd, rt, rs);
			ir.Write(IROp::SltU, rd, rs, rt);
		}
		break;

	case 44: //R(rd) = max(R(rs), R(rt);        break; //max
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, std::max(gpr.GetImm(rs), gpr.GetImm(rt)));
			break;
		}
		gpr.MapDirtyInIn(rd, rs, rt);
		ir.Write(IROp::Max, rd, rs, rt);
		break;

	case 45: //R(rd) = min(R(rs), R(rt));       break; //min
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, std::min(gpr.GetImm(rs), gpr.GetImm(rt)));
			break;
		}
		gpr.MapDirtyInIn(rd, rs, rt);
		ir.Write(IROp::Min, rd, rs, rt);
		break;

	default:
		Comp_Generic(op);
		break;
	}
}

void IRJit::CompShiftImm(MIPSOpcode op, IROp shiftOpConst, int sa) {
	MIPSGPReg rd = _RD;
	MIPSGPReg rt = _RT;
	if (gpr.IsImm(rt)) {
		switch (shiftOpConst) {
		case IROp::ShlImm:
			gpr.SetImm(rd, gpr.GetImm(rt) << sa);
			break;
		case IROp::ShrImm:
			gpr.SetImm(rd, gpr.GetImm(rt) >> sa);
			break;
		case IROp::SarImm:
			gpr.SetImm(rd, (int)gpr.GetImm(rt) >> sa);
			break;
		case IROp::RorImm:
			gpr.SetImm(rd, (gpr.GetImm(rt) >> sa) | (gpr.GetImm(rt) << (32 - sa)));
			break;
		default:
			DISABLE;
		}
	} else {
		gpr.MapDirtyIn(rd, rt);
		ir.Write(shiftOpConst, rd, rt, sa);
	}
}

void IRJit::CompShiftVar(MIPSOpcode op, IROp shiftOp, IROp shiftOpConst) {
	MIPSGPReg rd = _RD;
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	if (gpr.IsImm(rs)) {
		int sa = gpr.GetImm(rs) & 0x1F;
		CompShiftImm(op, shiftOpConst, sa);
		return;
	}
	gpr.MapDirtyInIn(rd, rs, rt);
	// Not sure if ARM64 wraps like this so let's do it for it.  (TODO: According to the ARM ARM, it will indeed mask for us so this is not necessary)
	// ANDI2R(SCRATCH1, gpr.R(rs), 0x1F, INVALID_REG);
	ir.Write(IROp::AndConst, IRTEMP_0, rs, ir.AddConstant(31));
	ir.Write(shiftOp, rd, rt, IRTEMP_0);
}

void IRJit::Comp_ShiftType(MIPSOpcode op) {
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
	case 0: CompShiftImm(op, IROp::ShlImm, sa); break; //sll
	case 2: CompShiftImm(op, (rs == 1 ? IROp::RorImm : IROp::ShrImm), sa); break;	//srl
	case 3: CompShiftImm(op, IROp::SarImm, sa); break; //sra
	case 4: CompShiftVar(op, IROp::Shl, IROp::ShlImm); break; //sllv
	case 6: CompShiftVar(op, (fd == 1 ? IROp::Ror : IROp::Shr), (fd == 1 ? IROp::RorImm : IROp::ShrImm)); break; //srlv
	case 7: CompShiftVar(op, IROp::Sar, IROp::SarImm); break; //srav
	default:
		DISABLE;
		break;
	}
}

void IRJit::Comp_Special3(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

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
		ir.Write(IROp::Shl, rt, rs);
		ir.Write(IROp::AndConst, rt, rt, ir.AddConstant(mask));
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

			gpr.MapDirty(rt);
			ir.Write(IROp::AndConst, rt, rt, ir.AddConstant(destmask));
			if (inserted != 0) {
				ir.Write(IROp::OrConst, rt, rt, inserted);
			}
		} else {
			gpr.MapDirtyIn(rt, rs);
			ir.Write(IROp::AndConst, IRTEMP_0, rs, ir.AddConstant(sourcemask));
			ir.Write(IROp::AndConst, rt, rt, ir.AddConstant(destmask));
			ir.Write(IROp::ShlImm, IRTEMP_0, IRTEMP_0, pos);
			ir.Write(IROp::Or, rt, rt, IRTEMP_0);
		}
	}
	break;
	}
}

void IRJit::Comp_Allegrex(MIPSOpcode op) {
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
		ir.Write(IROp::Ext8to32, rd, rt);
		break;

	case 24: // seh
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, (s32)(s16)(u16)gpr.GetImm(rt));
			return;
		}
		gpr.MapDirtyIn(rd, rt);
		ir.Write(IROp::Ext16to32, rd, rt);
		break;

	case 20: //bitrev
	default:
		Comp_Generic(op);
		return;
	}
}

void IRJit::Comp_Allegrex2(MIPSOpcode op) {
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
			ir.Write(IROp::BSwap16, rd, rt);
		}
		break;
	case 0xE0: //wsbw
		if (gpr.IsImm(rt)) {
			gpr.SetImm(rd, swap32(gpr.GetImm(rt)));
		} else {
			gpr.MapDirtyIn(rd, rt);
			ir.Write(IROp::BSwap16, rd, rt);
		}
		break;
	default:
		Comp_Generic(op);
		break;
	}
}

void IRJit::Comp_MulDivType(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	DISABLE;
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	switch (op & 63) {
	case 16: // R(rd) = HI; //mfhi
		if (rd != MIPS_REG_ZERO) {
			gpr.MapDirty(rd);
			ir.Write(IROp::MfHi, rd);
		}
		break;

	case 17: // HI = R(rs); //mthi
		gpr.MapIn(rs);
		ir.Write(IROp::MtHi, 0, rs);
		break;

	case 18: // R(rd) = LO; break; //mflo
		if (rd != MIPS_REG_ZERO) {
			gpr.MapDirty(rd);
			ir.Write(IROp::MfLo, rd);
		}
		break;

	case 19: // LO = R(rs); break; //mtlo
		gpr.MapIn(rs);
		ir.Write(IROp::MtLo, 0, rs);
		break;

	case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
		ir.Write(IROp::Mult, 0, rs, rt);
		break;

	case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
		ir.Write(IROp::MultU, 0, rs, rt);
		break;

	case 26: //div
		DISABLE;
		ir.Write(IROp::Div, 0, rs, rt);
		break;

	case 27: //divu
		DISABLE;
		ir.Write(IROp::DivU, 0, rs, rt);
		break;

	case 28: //madd
		DISABLE;
		ir.Write(IROp::Madd, 0, rs, rt);
		break;

	case 29: //maddu
		DISABLE;
		ir.Write(IROp::MaddU, 0, rs, rt);
		break;

	case 46: // msub
		DISABLE;
		ir.Write(IROp::Msub, 0, rs, rt);
		break;

	case 47: // msubu
		DISABLE;
		ir.Write(IROp::MsubU, 0, rs, rt);
		break;

	default:
		DISABLE;
	}
}

}
