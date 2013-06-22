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
#include "Core/Config.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"

#include "ArmJit.h"
#include "ArmRegCache.h"

#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define _FS   ((op>>11) & 0x1F)
#define _FT   ((op>>16) & 0x1F)
#define _FD   ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

//#define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{

void Jit::Comp_FPU3op(u32 op)
{ 
	CONDITIONAL_DISABLE;

	int ft = _FT;
	int fs = _FS;
	int fd = _FD;
	
	fpr.MapDirtyInIn(fd, fs, ft);
	switch (op & 0x3f) 
	{
	case 0: VADD(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) + F(ft); //add
	case 1: VSUB(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) - F(ft); //sub
	case 2: { //F(fd) = F(fs) * F(ft); //mul
		u32 nextOp = Memory::Read_Instruction(js.compilerPC + 4);
		// Optimise possible if destination is the same
		if (fd == ((nextOp>>6) & 0x1F)) {
			// VMUL + VNEG -> VNMUL
			if (!strcmp(MIPSGetName(nextOp), "neg.s")) {
				if (fd == ((nextOp>>11) & 0x1F)) {
					VNMUL(fpr.R(fd), fpr.R(fs), fpr.R(ft));
					EatInstruction(nextOp);
				}
				return;
			}
		}
		VMUL(fpr.R(fd), fpr.R(fs), fpr.R(ft));
		break;
	}
	case 3: VDIV(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) / F(ft); //div
	default:
		DISABLE;
		return;
	}
}

extern int logBlocks;

void Jit::Comp_FPULS(u32 op)
{
	CONDITIONAL_DISABLE;

	s32 offset = (s16)(op & 0xFFFF);
	int ft = _FT;
	int rs = _RS;
	// u32 addr = R(rs) + offset;
	// logBlocks = 1;
	bool doCheck = false;
	switch(op >> 26)
	{
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		fpr.MapReg(ft, MAP_NOINIT | MAP_DIRTY);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			MOVI2R(R0, addr + (u32)Memory::base);
		} else {
			gpr.MapReg(rs);
			if (g_Config.bFastMemory) {
				SetR0ToEffectiveAddress(rs, offset);
			} else {
				SetCCAndR0ForSafeAddress(rs, offset, R1);
				doCheck = true;
			}
			ADD(R0, R0, R11);
		}
		VLDR(fpr.R(ft), R0, 0);
		if (doCheck) {
			SetCC(CC_EQ);
			MOVI2R(R0, 0);
			VMOV(fpr.R(ft), R0);
			SetCC(CC_AL);
		}
		break;

	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		fpr.MapReg(ft);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			MOVI2R(R0, addr + (u32)Memory::base);
		} else {
			gpr.MapReg(rs);
			if (g_Config.bFastMemory) {
				SetR0ToEffectiveAddress(rs, offset);
			} else {
				SetCCAndR0ForSafeAddress(rs, offset, R1);
				doCheck = true;
			}
			ADD(R0, R0, R11);
		}
		VSTR(fpr.R(ft), R0, 0);
		if (doCheck) {
			SetCC(CC_AL);
		}
		break;

	default:
		Comp_Generic(op);
		return;
	}
}

void Jit::Comp_FPUComp(u32 op) {
	CONDITIONAL_DISABLE;
	int opc = op & 0xF;
	if (opc >= 8) opc -= 8; // alias
	if (opc == 0)//f, sf (signalling false)
	{
		MOVI2R(R0, 0);
		STR(R0, CTXREG, offsetof(MIPSState, fpcond));
		return;
	}

	int fs = _FS;
	int ft = _FT;
	fpr.MapInIn(fs, ft);
	VCMP(fpr.R(fs), fpr.R(ft));
	VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
	switch(opc)
	{
	case 1:      // un,  ngle (unordered)
		SetCC(CC_VS);
		MOVI2R(R0, 1);
		SetCC(CC_VC);
		break;
	case 2:      // eq,  seq (equal, ordered)
		SetCC(CC_EQ);
		MOVI2R(R0, 1);
		SetCC(CC_NEQ);
		break;
	case 3:      // ueq, ngl (equal, unordered)
		SetCC(CC_EQ);
		MOVI2R(R0, 1);
		SetCC(CC_NEQ);
		MOVI2R(R0, 0);
		SetCC(CC_VS);
		MOVI2R(R0, 1);
		SetCC(CC_AL);
		STR(R0, CTXREG, offsetof(MIPSState, fpcond));
		return;
	case 4:      // olt, lt (less than, ordered)
		SetCC(CC_LO);
		MOVI2R(R0, 1);
		SetCC(CC_HS);
		break;
	case 5:      // ult, nge (less than, unordered)
		SetCC(CC_LT);
		MOVI2R(R0, 1);
		SetCC(CC_GE);
		break;
	case 6:      // ole, le (less equal, ordered)
		SetCC(CC_LS);
		MOVI2R(R0, 1);
		SetCC(CC_HI);
		break;
	case 7:      // ule, ngt (less equal, unordered)
		SetCC(CC_LE);
		MOVI2R(R0, 1);
		SetCC(CC_GT);
		break;
	default:
		Comp_Generic(op);
		return;
	}
	MOVI2R(R0, 0);
	SetCC(CC_AL);
	STR(R0, CTXREG, offsetof(MIPSState, fpcond));
}

void Jit::Comp_FPU2op(u32 op)
{
	CONDITIONAL_DISABLE;

	int fs = _FS;
	int fd = _FD;
	// logBlocks = 1;

	switch (op & 0x3f) 
	{
	case 4:	//F(fd)	   = sqrtf(F(fs));            break; //sqrt
		fpr.MapDirtyIn(fd, fs);
		VSQRT(fpr.R(fd), fpr.R(fs));
		break;
	case 5:	//F(fd)    = fabsf(F(fs));            break; //abs
		fpr.MapDirtyIn(fd, fs);
		VABS(fpr.R(fd), fpr.R(fs));
		break;
	case 6:	//F(fd)	   = F(fs);                   break; //mov
		fpr.MapDirtyIn(fd, fs);
		VMOV(fpr.R(fd), fpr.R(fs));
		break;
	case 7:	//F(fd)	   = -F(fs);                  break; //neg
		fpr.MapDirtyIn(fd, fs);
		VNEG(fpr.R(fd), fpr.R(fs));
		break;
	case 12: //FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED);
		break;
	case 13: //FsI(fd) = Rto0(F(fs)));            break; //trunc.w.s
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED | ROUND_TO_ZERO);
		break;
	case 14: //FsI(fd) = (int)ceilf (F(fs));      break; //ceil.w.s
		fpr.MapDirtyIn(fd, fs);
		MOVI2F(S0, 0.5f, R0);
		VADD(S0,fpr.R(fs),S0);
		VCVT(fpr.R(fd), S0,        TO_INT | IS_SIGNED);
		break;
	case 15: //FsI(fd) = (int)floorf(F(fs));      break; //floor.w.s
		fpr.MapDirtyIn(fd, fs);
		MOVI2F(S0, 0.5f, R0);
		VSUB(S0,fpr.R(fs),S0);
		VCVT(fpr.R(fd), S0,        TO_INT | IS_SIGNED);
		break;
	case 32: //F(fd)   = (float)FsI(fs);          break; //cvt.s.w
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), TO_FLOAT | IS_SIGNED);
		break;
	case 36: //FsI(fd) = (int)  F(fs);            break; //cvt.w.s
		fpr.MapDirtyIn(fd, fs);
		LDR(R0, CTXREG, offsetof(MIPSState, fcr31));
		AND(R0, R0, Operand2(3));
		// MIPS Rounding Mode:
		//	 0: Round nearest
		//	 1: Round to zero
		//	 2: Round up (ceil)
		//	 3: Round down (floor)
		CMP(R0, Operand2(2));
		SetCC(CC_GE); MOVI2F(S0, 0.5f, R1);
		SetCC(CC_GT); VSUB(S0,fpr.R(fs),S0);
		SetCC(CC_EQ); VADD(S0,fpr.R(fs),S0);
		SetCC(CC_GE); VCVT(fpr.R(fd), S0, TO_INT | IS_SIGNED); /* 2,3 */
		SetCC(CC_AL);
		CMP(R0, Operand2(1));
		SetCC(CC_EQ); VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED | ROUND_TO_ZERO); /* 1 */
		SetCC(CC_LT); VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED); /* 0 */
		SetCC(CC_AL);
		break;
	default:
		DISABLE;
	}
}

void Jit::Comp_mxc1(u32 op)
{
	CONDITIONAL_DISABLE;

	int fs = _FS;
	int rt = _RT;

	switch((op >> 21) & 0x1f) 
	{
	case 0: // R(rt) = FI(fs); break; //mfc1
		// Let's just go through RAM for now.
		fpr.FlushR(fs);
		gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
		LDR(gpr.R(rt), CTXREG, fpr.GetMipsRegOffset(fs));
		return;

	case 2: //cfc1
		if (fs == 31)
		{
			gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
			LDR(R0, CTXREG, offsetof(MIPSState, fpcond));
			AND(R0, R0, Operand2(1)); // Just in case
			LDR(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
			BIC(gpr.R(rt), gpr.R(rt), Operand2(0x1 << 23));
			ORR(gpr.R(rt), gpr.R(rt), Operand2(R0, ST_LSL, 23));
		}
		else if (fs == 0)
		{
			gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
			LDR(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr0));
		}
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		// Let's just go through RAM for now.
		gpr.FlushR(rt);
		fpr.MapReg(fs, MAP_DIRTY | MAP_NOINIT);
		VLDR(fpr.R(fs), CTXREG, gpr.GetMipsRegOffset(rt));
		return;

	case 6: //ctc1
		if (fs == 31)
		{
			gpr.MapReg(rt, 0);
			// Hardware rounding method.
			// Left here in case it is faster than conditional method.
			/*
			AND(R0, gpr.R(rt), Operand2(3));
			// MIPS Rounding Mode <-> ARM Rounding Mode
			//         0, 1, 2, 3 <->  0, 3, 1, 2
			CMP(R0, Operand2(1));
			SetCC(CC_EQ); ADD(R0, R0, Operand2(2));
			SetCC(CC_GT); SUB(R0, R0, Operand2(1));
			SetCC(CC_AL);

			// Load and Store RM to FPSCR
			VMRS(R1);
			BIC(R1, R1, Operand2(0x3 << 22));
			ORR(R1, R1, Operand2(R0, ST_LSL, 22));
			VMSR(R1);
			*/
			// Update MIPS state
			STR(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
			MOV(R0, Operand2(gpr.R(rt), ST_LSR, 23));
			AND(R0, R0, Operand2(1));
			STR(R0, CTXREG, offsetof(MIPSState, fpcond));
		}
		return;
	}
}

}	// namespace MIPSComp
