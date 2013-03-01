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
#include "../MIPS.h"

#include "ArmJit.h"
#include "ArmRegCache.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define DISABLE Comp_Generic(op); return;
#define CONDITIONAL_DISABLE ; 

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
	case 2: VMUL(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) * F(ft); //mul
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
	switch(op >> 26)
	{
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		fpr.MapReg(ft, MAP_NOINIT | MAP_DIRTY);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			MOVI2R(R0, addr + (u32)Memory::base);
		} else {
			gpr.MapReg(rs);
			SetR0ToEffectiveAddress(rs, offset);
			ADD(R0, R0, R11);
		}
		VLDR(fpr.R(ft), R0, 0);
		break;

	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		fpr.MapReg(ft);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			MOVI2R(R0, addr + (u32)Memory::base);
		} else {
			gpr.MapReg(rs);
			SetR0ToEffectiveAddress(rs, offset);
			ADD(R0, R0, R11);
		}
		VSTR(fpr.R(ft), R0, 0);
		break;

	default:
		Comp_Generic(op);
		return;
	}
}

void Jit::Comp_FPUComp(u32 op) {
	DISABLE;
	int fs = _FS;
	int ft = _FT;

	switch (op & 0xf) 	{
	case 0: //f
	case 8: //sf
		/*MOVI2R(R0, (u32)&currentMIPS->fpcond);
		MOV(R0, Operand2(0));*/
		break;

	case 1: //un
	case 9: //ngle
		// CompFPComp(fs, ft, CMPUNORDSS);
		break;

	case 2: //eq
	case 10: //seq
		// CompFPComp(fs, ft, CMPEQSS);
		break;

	case 3: //ueq
	case 11: //ngl
		// CompFPComp(fs, ft, CMPEQSS, true);
		break;

	case 4: //olt
	case 12: //lt
		// CompFPComp(fs, ft, CMPLTSS);
		break;

	case 5: //ult
	case 13: //nge
		// CompFPComp(ft, fs, CMPNLESS);
		break;

	case 6: //ole
	case 14: //le
		// This VCMP crashes on ARM11 with an exception.
		/*
		fpr.MapInIn(fpr.R(fs), fpr.R(ft));
		VCMP(fpr.R(fs), fpr.R(ft));
		MOVI2R(R0, (u32)&currentMIPS->fpcond);
		SetCC(CC_LT);
		// TODO: Should set R0 to 0 or 1
		VSTR(fpr.R(fs), R0, 0);
		SetCC(CC_AL);
		*/
		break;

	case 7: //ule
	case 15: //ngt
		// CompFPComp(ft, fs, CMPNLTSS);
		break;

	default:
		DISABLE;
	}
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
		VCVT(fpr.R(fd), fpr.R(fs), true, true, false);
		break;
	case 13: //FsI(fd) = Rto0(F(fs)));            break; //trunc.w.s
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), true, true, true);
		break;
	case 14: //FsI(fd) = (int)ceilf (F(fs));      break; //ceil.w.s
		fpr.MapDirtyIn(fd, fs);
		MOVI2R(R0, 0x3F000000); // 0.5f
		VMOV(S0, R0);
		VADD(S0,fpr.R(fs),S0);
		VCVT(fpr.R(fd), S0, true, true, false);
		break;
	case 15: //FsI(fd) = (int)floorf(F(fs));      break; //floor.w.s
		fpr.MapDirtyIn(fd, fs);
		MOVI2R(R0, 0x3F000000); // 0.5f
		VMOV(S0, R0);
		VSUB(S0,fpr.R(fs),S0);
		VCVT(fpr.R(fd), S0, true, true, false);
		break;
	case 32: //F(fd)   = (float)FsI(fs);          break; //cvt.s.w
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), false, true);
		break;
	case 36: //FsI(fd) = (int)  F(fs);            break; //cvt.w.s
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), true, false, true);
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

	case 2: // R(rt) = currentMIPS->ReadFCR(fs); break; //cfc1
		Comp_Generic(op);
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		// Let's just go through RAM for now.
		gpr.FlushR(rt);
		fpr.MapReg(fs, MAP_DIRTY | MAP_NOINIT);
		VLDR(fpr.R(fs), CTXREG, gpr.GetMipsRegOffset(rt));
		return;

	case 6: //currentMIPS->WriteFCR(fs, R(rt)); break; //ctc1
		Comp_Generic(op);
		return;
	}
}

}	// namespace MIPSComp
