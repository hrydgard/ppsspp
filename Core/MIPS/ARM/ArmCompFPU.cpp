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

#define CONDITIONAL_DISABLE ;
#define DISABLE Comp_Generic(op); return;

namespace MIPSComp
{
	/*
void Jit::CompFPTriArith(u32 op, void (XEmitter::*arith)(X64Reg reg, OpArg), bool orderMatters)
{
	int ft = _FT;
	int fs = _FS;
	int fd = _FD;
	fpr.Lock(ft, fs, fd);

	if (false && fs == fd) 
	{
		fpr.BindToRegister(fd, true, true);
		(this->*arith)(fpr.RX(fd), fpr.R(ft));
	}
	else 
	{
		MOVSS(XMM0, fpr.R(fs));
		MOVSS(XMM1, fpr.R(ft));
		fpr.BindToRegister(fd, true, true);
		(this->*arith)(XMM0, R(XMM1));
		MOVSS(fpr.RX(fd), R(XMM0));
	}
	fpr.UnlockAll();
}
*/



void Jit::Comp_FPU3op(u32 op)
{ 
	DISABLE
	switch (op & 0x3f) 
	{
	//case 0: CompFPTriArith(op, &XEmitter::ADDSS, false); break; //F(fd) = F(fs) + F(ft); //add
	//case 1: CompFPTriArith(op, &XEmitter::SUBSS, true); break; //F(fd) = F(fs) - F(ft); //sub
	//case 2: CompFPTriArith(op, &XEmitter::MULSS, false); break; //F(fd) = F(fs) * F(ft); //mul
	//case 3: CompFPTriArith(op, &XEmitter::DIVSS, true); break; //F(fd) = F(fs) / F(ft); //div
	default:
		Comp_Generic(op);
		return;
	}
}

void Jit::Comp_FPULS(u32 op)
{
	DISABLE

	s32 offset = (s16)(op&0xFFFF);
	int ft = ((op>>16)&0x1f);
	int rs = _RS;
	// u32 addr = R(rs) + offset;

	switch(op >> 26)
	{
		/*
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		gpr.Lock(rs);
		fpr.Lock(ft);
		fpr.BindToRegister(ft, false, true);
		MOV(32, R(EAX), gpr.R(rs));
		AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
		MOVSS(fpr.RX(ft), MDisp(EAX, (u32)Memory::base + offset));
		gpr.UnlockAll();
		fpr.UnlockAll();
		break;
	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		gpr.Lock(rs);
		fpr.Lock(ft);
		fpr.BindToRegister(ft, true, false);
		MOV(32, R(EAX), gpr.R(rs));
		AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
		MOVSS(MDisp(EAX, (u32)Memory::base + offset), fpr.RX(ft));
		gpr.UnlockAll();
		fpr.UnlockAll();
		break;
		*/
	default:
		Comp_Generic(op);
		return;
	}
}

void Jit::Comp_FPUComp(u32 op) {
	DISABLE;
}

void Jit::Comp_FPU2op(u32 op)
{
	DISABLE
	int fs = _FS;
	int fd = _FD;

	switch (op & 0x3f) 
	{
		/*
	case 5:	//F(fd)	= fabsf(F(fs)); break; //abs
		fpr.Lock(fd, fs);
		fpr.BindToRegister(fd, fd == fs, true);
		MOVSS(fpr.RX(fd), fpr.R(fs));
		PAND(fpr.RX(fd), M((void *)ssNoSignMask));
		fpr.UnlockAll();
		break;

	case 6:	//F(fd)	= F(fs);				break; //mov
		if (fd != fs) {
			fpr.Lock(fd, fs);
			fpr.BindToRegister(fd, fd == fs, true);
			MOVSS(fpr.RX(fd), fpr.R(fs));
			fpr.UnlockAll();
		}
		break;

	case 7:	//F(fd)	= -F(fs);			 break; //neg
		fpr.Lock(fd, fs);
		fpr.BindToRegister(fd, fd == fs, true);
		MOVSS(fpr.RX(fd), fpr.R(fs));
		PXOR(fpr.RX(fd), M((void *)ssSignBits2));
		fpr.UnlockAll();
		break;

	case 12: //FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s

	case 4:	//F(fd)	= sqrtf(F(fs)); break; //sqrt
		Comp_Generic(op);
		return;

	case 13: //FsI(fd) = F(fs)>=0 ? (int)floorf(F(fs)) : (int)ceilf(F(fs)); break;//trunc.w.s
		fpr.Lock(fs, fd);
		fpr.StoreFromRegister(fd);
		CVTTSS2SI(EAX, fpr.R(fs));
		MOV(32, fpr.R(fd), R(EAX));
		fpr.UnlockAll();
		break;

	case 14: //FsI(fd) = (int)ceilf (F(fs)); break; //ceil.w.s
	case 15: //FsI(fd) = (int)floorf(F(fs)); break; //floor.w.s
	case 32: //F(fd)	= (float)FsI(fs);			break; //cvt.s.w
	case 36: //FsI(fd) = (int)	F(fs);			 break; //cvt.w.s
	*/
	default:
		Comp_Generic(op);
		return;
	}
}

void Jit::Comp_mxc1(u32 op)
{
	DISABLE
	int fs = _FS;
	int rt = _RT;

	switch((op >> 21) & 0x1f) 
	{
		/*
	case 0: // R(rt) = FI(fs); break; //mfc1
		// Cross move! slightly tricky
		fpr.StoreFromRegister(fs);
		gpr.Lock(rt);
		gpr.BindToRegister(rt, false, true);
		MOV(32, gpr.R(rt), fpr.R(fs));
		gpr.UnlockAll();
		return;

	case 2: // R(rt) = currentMIPS->ReadFCR(fs); break; //cfc1
		Comp_Generic(op);
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		// Cross move! slightly tricky
		gpr.StoreFromRegister(rt);
		fpr.Lock(fs);
		fpr.BindToRegister(fs, false, true);
		MOVSS(fpr.RX(fs), gpr.R(rt));
		fpr.UnlockAll();
		return;
		*/
	case 6: //currentMIPS->WriteFCR(fs, R(rt)); break; //ctc1
		Comp_Generic(op);
		return;
	}
}

}	// namespace MIPSComp
