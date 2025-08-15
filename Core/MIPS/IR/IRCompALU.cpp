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
#include "Core/MIPS/IR/IRFrontend.h"
#include "Common/CPUDetect.h"

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

// #define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (opts.disableFlags & (uint32_t)JitDisable::flag) { Comp_Generic(op); return; }
#define DISABLE { Comp_Generic(op); return; }
#define INVALIDOP { Comp_Generic(op); return; }

namespace MIPSComp {

void IRFrontend::Comp_IType(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU_IMM);

	u32 uimm = (u16)_IMM16;
	s32 simm = SignExtend16ToS32(op);
	u32 suimm = SignExtend16ToU32(op);

	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;

	// noop, won't write to ZERO.
	if (rt == MIPS_REG_ZERO)
		return;

	switch (op >> 26) {
	case 8:	// same as addiu?
	case 9:	// R(rt) = R(rs) + simm; break;	//addiu
		ir.Write(IROp::AddConst, rt, rs, ir.AddConstant(simm));
		break;

	case 12: ir.Write(IROp::AndConst, rt, rs, ir.AddConstant(uimm)); break;
	case 13: ir.Write(IROp::OrConst, rt, rs, ir.AddConstant(uimm)); break;
	case 14: ir.Write(IROp::XorConst, rt, rs, ir.AddConstant(uimm)); break;

	case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
		ir.Write(IROp::SltConst, rt, rs, ir.AddConstant(simm));
		break;

	case 11: // R(rt) = R(rs) < suimm; break; //sltiu
		ir.Write(IROp::SltUConst, rt, rs, ir.AddConstant(suimm));
		break;

	case 15: // R(rt) = uimm << 16;	 //lui
		ir.WriteSetConstant(rt, uimm << 16);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void IRFrontend::Comp_RType2(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU_BIT);

	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// Don't change $zr.
	if (rd == MIPS_REG_ZERO)
		return;

	switch (op & 63) {
	case 22: //clz
		ir.Write(IROp::Clz, rd, rs);
		break;
	case 23: //clo
		ir.Write(IROp::Not, IRTEMP_0, rs);
		ir.Write(IROp::Clz, rd, IRTEMP_0);
		break;
	default:
		INVALIDOP;
		break;
	}
}

void IRFrontend::Comp_RType3(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU);

	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	// noop, won't write to ZERO.
	if (rd == MIPS_REG_ZERO)
		return;

	switch (op & 63) {
	case 10: //if (!R(rt)) R(rd) = R(rs);       break; //movz
		ir.Write(IROp::MovZ, rd, rt, rs);
		break;
	case 11:// if (R(rt)) R(rd) = R(rs);		break; //movn
		ir.Write(IROp::MovNZ, rd, rt, rs);
		break;

	case 32: //R(rd) = R(rs) + R(rt);           break; //add
	case 33: //R(rd) = R(rs) + R(rt);           break; //addu
		ir.Write(IROp::Add, rd, rs, rt);
		break;

	case 34: //R(rd) = R(rs) - R(rt);           break; //sub
	case 35: //R(rd) = R(rs) - R(rt);           break; //subu
		ir.Write(IROp::Sub, rd, rs, rt);
		break;

	case 36: //R(rd) = R(rs) & R(rt);           break; //and
		ir.Write(IROp::And, rd, rs, rt);
		break;
	case 37: //R(rd) = R(rs) | R(rt);           break; //or
		ir.Write(IROp::Or, rd, rs, rt);
		break;
	case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
		ir.Write(IROp::Xor, rd, rs, rt);
		break;

	case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
		if (rs == 0) {
			ir.Write(IROp::Not, rd, rt);
		} else if (rt == 0) {
			ir.Write(IROp::Not, rd, rs);
		} else {
			ir.Write(IROp::Or, IRTEMP_0, rs, rt);
			ir.Write(IROp::Not, rd, IRTEMP_0);
		}
		break;

	case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
		ir.Write(IROp::Slt, rd, rs, rt);
		break;

	case 43: //R(rd) = R(rs) < R(rt);           break; //sltu
		ir.Write(IROp::SltU, rd, rs, rt);
		break;

	case 44: //R(rd) = max(R(rs), R(rt);        break; //max
		ir.Write(IROp::Max, rd, rs, rt);
		break;

	case 45: //R(rd) = min(R(rs), R(rt));       break; //min
		ir.Write(IROp::Min, rd, rs, rt);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void IRFrontend::CompShiftImm(MIPSOpcode op, IROp shiftOpImm, int sa) {
	MIPSGPReg rd = _RD;
	MIPSGPReg rt = _RT;
	ir.Write(shiftOpImm, rd, rt, sa);
}

void IRFrontend::CompShiftVar(MIPSOpcode op, IROp shiftOp) {
	MIPSGPReg rd = _RD;
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;

	if (opts.optimizeForInterpreter) {
		// The interpreter already masks where needed, don't need to generate extra ops.
		ir.Write(shiftOp, rd, rt, rs);
	} else {
		ir.Write(IROp::AndConst, IRTEMP_0, rs, ir.AddConstant(31));
		ir.Write(shiftOp, rd, rt, IRTEMP_0);
	}
}

void IRFrontend::Comp_ShiftType(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU);
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;
	int sa = _SA;

	// noop, won't write to ZERO.
	if (rd == MIPS_REG_ZERO)
		return;

	// WARNING: srl/rotr and srlv/rotrv share encodings (differentiated using unused bits.)
	switch (op & 0x3f) {
	case 0: CompShiftImm(op, IROp::ShlImm, sa); break; //sll
	case 2: CompShiftImm(op, (rs == 1 ? IROp::RorImm : IROp::ShrImm), sa); break;	//srl
	case 3: CompShiftImm(op, IROp::SarImm, sa); break; //sra
	case 4: CompShiftVar(op, IROp::Shl); break; //sllv
	case 6: CompShiftVar(op, (sa == 1 ? IROp::Ror : IROp::Shr)); break; //srlv
	case 7: CompShiftVar(op, IROp::Sar); break; //srav

	default:
		INVALIDOP;
		break;
	}
}

void IRFrontend::Comp_Special3(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU_BIT);
	MIPSGPReg rs = _RS;
	MIPSGPReg rt = _RT;

	int pos = _POS;
	int size = _SIZE + 1;
	u32 mask = 0xFFFFFFFFUL >> (32 - size);

	// Don't change $zr.
	if (rt == MIPS_REG_ZERO)
		return;

	switch (op & 0x3f) {
	case 0x0: // ext
		if (pos != 0) {
			ir.Write(IROp::ShrImm, rt, rs, pos);
			ir.Write(IROp::AndConst, rt, rt, ir.AddConstant(mask));
		} else {
			ir.Write(IROp::AndConst, rt, rs, ir.AddConstant(mask));
		}
		break;

	case 0x4: //ins
	{
		// TODO: Might be good to support natively in the interpreter. Though, would have to
		// abuse a register as a constant
		u32 sourcemask = mask >> pos;
		u32 destmask = ~(sourcemask << pos);

		if (size != 32) {
			// Need to use the sourcemask.
			ir.Write(IROp::AndConst, IRTEMP_0, rs, ir.AddConstant(sourcemask));
			if (pos != 0) {
				ir.Write(IROp::ShlImm, IRTEMP_0, IRTEMP_0, pos);
			}
		} else {
			// If the shl takes care of the sourcemask, don't need to and.
			if (pos != 0) {
				ir.Write(IROp::ShlImm, IRTEMP_0, rs, pos);
			} else {
				ir.Write(IROp::Mov, IRTEMP_0, rs);
			}
		}
		ir.Write(IROp::AndConst, rt, rt, ir.AddConstant(destmask));
		ir.Write(IROp::Or, rt, rt, IRTEMP_0);
	}
	break;

	default:
		INVALIDOP;
		break;
	}
}


void IRFrontend::Comp_Allegrex(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU_BIT);
	MIPSGPReg rt = _RT;
	MIPSGPReg rd = _RD;

	// Don't change $zr.
	if (rd == MIPS_REG_ZERO)
		return;

	switch ((op >> 6) & 31) {
	case 16: // seb	// R(rd) = SignExtend8ToU32(R(rt));
		ir.Write(IROp::Ext8to32, rd, rt);
		break;

	case 24: // seh
		ir.Write(IROp::Ext16to32, rd, rt);
		break;

	case 20: // bitrev
		ir.Write(IROp::ReverseBits, rd, rt);
		break;

	default:
		INVALIDOP;
		return;
	}
}

void IRFrontend::Comp_Allegrex2(MIPSOpcode op) {
	CONDITIONAL_DISABLE(ALU_BIT);
	MIPSGPReg rt = _RT;
	MIPSGPReg rd = _RD;

	// Don't change $zr.
	if (rd == MIPS_REG_ZERO)
		return;

	switch (op & 0x3ff) {
	case 0xA0: //wsbh
		ir.Write(IROp::BSwap16, rd, rt);
		break;
	case 0xE0: //wsbw
		ir.Write(IROp::BSwap32, rd, rt);
		break;
	default:
		INVALIDOP;
		break;
	}
}

void IRFrontend::Comp_MulDivType(MIPSOpcode op) {
	CONDITIONAL_DISABLE(MULDIV);
	MIPSGPReg rt = _RT;
	MIPSGPReg rs = _RS;
	MIPSGPReg rd = _RD;

	switch (op & 63) {
	case 16: // R(rd) = HI; //mfhi
		if (rd != MIPS_REG_ZERO) {
			ir.Write(IROp::MfHi, rd);
		}
		break;

	case 17: // HI = R(rs); //mthi
		ir.Write(IROp::MtHi, 0, rs);
		break;

	case 18: // R(rd) = LO; break; //mflo
		if (rd != MIPS_REG_ZERO) {
			ir.Write(IROp::MfLo, rd);
		}
		break;

	case 19: // LO = R(rs); break; //mtlo
		ir.Write(IROp::MtLo, 0, rs);
		break;

	case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
		ir.Write(IROp::Mult, 0, rs, rt);
		break;

	case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
		ir.Write(IROp::MultU, 0, rs, rt);
		break;

	case 26: //div
		ir.Write(IROp::Div, 0, rs, rt);
		break;

	case 27: //divu
		ir.Write(IROp::DivU, 0, rs, rt);
		break;

	case 28: //madd
		ir.Write(IROp::Madd, 0, rs, rt);
		break;

	case 29: //maddu
		ir.Write(IROp::MaddU, 0, rs, rt);
		break;

	case 46: // msub
		ir.Write(IROp::Msub, 0, rs, rt);
		break;

	case 47: // msubu
		ir.Write(IROp::MsubU, 0, rs, rt);
		break;

	default:
		INVALIDOP;
	}
}

}
