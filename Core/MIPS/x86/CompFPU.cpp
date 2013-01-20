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

#include "../../Config.h"
#include "Common/Common.h"
#include "Jit.h"
#include "RegCache.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE Comp_Generic(op); return;
#define CONDITIONAL_DISABLE ;
#define DISABLE Comp_Generic(op); return;

namespace MIPSComp
{

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
		/*
		fpr.BindToRegister(fd, true, true);
		if (fd != fs)
			MOVSS(fpr.RX(fd), fpr.R(fs));
		(this->*arith)(fpr.RX(fd), fpr.R(ft));*/
		MOVSS(XMM0, fpr.R(fs));
		MOVSS(XMM1, fpr.R(ft));
		fpr.BindToRegister(fd, true, true);
		(this->*arith)(XMM0, R(XMM1));
		MOVSS(fpr.RX(fd), R(XMM0));
	}
	fpr.UnlockAll();
}

void Jit::Comp_FPU3op(u32 op)
{ 
	CONDITIONAL_DISABLE;
	switch (op & 0x3f) 
	{
	case 0: CompFPTriArith(op, &XEmitter::ADDSS, false); break; //F(fd) = F(fs) + F(ft); //add
	case 1: CompFPTriArith(op, &XEmitter::SUBSS, true); break;  //F(fd) = F(fs) - F(ft); //sub
	case 2: CompFPTriArith(op, &XEmitter::MULSS, false); break; //F(fd) = F(fs) * F(ft); //mul
	case 3: CompFPTriArith(op, &XEmitter::DIVSS, true); break;  //F(fd) = F(fs) / F(ft); //div
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile FPU3Op instruction that can't be interpreted");
		break;
	}
}

static u32 GC_ALIGNED16(ssLoadStoreTemp[1]);

void Jit::Comp_FPULS(u32 op)
{
	CONDITIONAL_DISABLE;
	s32 offset = (s16)(op&0xFFFF);
	int ft = _FT;
	int rs = _RS;

	switch(op >> 26)
	{
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		gpr.Lock(rs);
		fpr.Lock(ft);
		fpr.BindToRegister(ft, false, true);

		if (gpr.R(rs).IsImm())
		{
			void *data = Memory::GetPointer(gpr.R(rs).GetImmValue() + offset);
			if (data)
			{
#ifdef _M_IX86
				MOVSS(fpr.RX(ft), M(data));
#else
				MOVSS(fpr.RX(ft), MDisp(RBX, gpr.R(rs).GetImmValue() + offset));
#endif
			}
			else
			{
				MOV(32, M((void *)ssLoadStoreTemp), Imm32(0));
				MOVSS(fpr.RX(ft), M((void *)ssLoadStoreTemp));
			}
		}
		else
		{
			// We may not even need to move into EAX as a temporary.
			X64Reg addr;
			if (gpr.R(rs).IsSimpleReg())
			{
				// TODO: Maybe just add a check if it's away, don't mind copying to EAX instead...
				gpr.BindToRegister(rs, true, false);
				addr = gpr.RX(rs);
			}
			else
			{
				MOV(32, R(EAX), gpr.R(rs));
				addr = EAX;
			}

			if (!g_Config.bFastMemory)
			{
				// Is it in physical ram?
				CMP(32, R(addr), Imm32(0x08000000));
				FixupBranch tooLow = J_CC(CC_L);
				CMP(32, R(addr), Imm32(0x0A000000));
				FixupBranch tooHigh = J_CC(CC_GE);

				const u8* safe = GetCodePtr();
#ifdef _M_IX86
				MOVSS(fpr.RX(ft), MDisp(addr, (u32)Memory::base + offset));
#else
				MOVSS(fpr.RX(ft), MComplex(RBX, addr, SCALE_1, offset));
#endif

				FixupBranch skip = J();
				SetJumpTarget(tooLow);
				SetJumpTarget(tooHigh);

				// Might also be the scratchpad.
				CMP(32, R(addr), Imm32(0x00010000));
				FixupBranch tooLow2 = J_CC(CC_L);
				CMP(32, R(addr), Imm32(0x00014000));
				J_CC(CC_L, safe);
				SetJumpTarget(tooLow2);

				LEA(32, EAX, MDisp(addr, offset));
				ABI_CallFunctionA(thunks.ProtectFunction((void *) &Memory::Read_U32, 1), R(EAX));
				MOV(32, M((void *)&ssLoadStoreTemp), R(EAX));
				MOVSS(fpr.RX(ft), M((void *)&ssLoadStoreTemp));

				SetJumpTarget(skip);
			}
			else
			{
#ifdef _M_IX86
				// Need to modify it, too bad.
				if (addr != EAX)
					MOV(32, R(EAX), gpr.R(rs));
				AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
				MOVSS(fpr.RX(ft), MDisp(EAX, (u32)Memory::base + offset));
#else
				MOVSS(fpr.RX(ft), MComplex(RBX, addr, SCALE_1, offset));
#endif
			}
		}

		gpr.UnlockAll();
		fpr.UnlockAll();
		break;
	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		gpr.Lock(rs);
		fpr.Lock(ft);
		fpr.BindToRegister(ft, true, false);

		if (gpr.R(rs).IsImm())
		{
			void *data = Memory::GetPointer(gpr.R(rs).GetImmValue() + offset);
			if (data)
			{
#ifdef _M_IX86
				MOVSS(M(data), fpr.RX(ft));
#else
				MOVSS(MDisp(RBX, gpr.R(rs).GetImmValue() + offset), fpr.RX(ft));
#endif
			}
		}
		else
		{
			// We may not even need to move into EAX as a temporary.
			X64Reg addr;
			if (gpr.R(rs).IsSimpleReg())
			{
				// TODO: Maybe just add a check if it's away, don't mind copying to EAX instead...
				gpr.BindToRegister(rs, true, false);
				addr = gpr.RX(rs);
			}
			else
			{
				MOV(32, R(EAX), gpr.R(rs));
				addr = EAX;
			}

			if (!g_Config.bFastMemory)
			{
				// Is it in physical ram?
				CMP(32, R(addr), Imm32(0x08000000));
				FixupBranch tooLow = J_CC(CC_L);
				CMP(32, R(addr), Imm32(0x0A000000));
				FixupBranch tooHigh = J_CC(CC_GE);

				const u8* safe = GetCodePtr();
#ifdef _M_IX86
				MOVSS(MDisp(addr, (u32)Memory::base + offset), fpr.RX(ft));
#else
				MOVSS(MComplex(RBX, addr, SCALE_1, offset), fpr.RX(ft));
#endif

				FixupBranch skip = J();
				SetJumpTarget(tooLow);
				SetJumpTarget(tooHigh);

				// Might also be the scratchpad.
				CMP(32, R(addr), Imm32(0x00010000));
				FixupBranch tooLow2 = J_CC(CC_L);
				CMP(32, R(addr), Imm32(0x00014000));
				J_CC(CC_L, safe);
				SetJumpTarget(tooLow2);

				LEA(32, EAX, MDisp(addr, offset));
				MOVSS(M((void *)&ssLoadStoreTemp), fpr.RX(ft));
				ABI_CallFunctionAA(thunks.ProtectFunction((void *) &Memory::Write_U32, 2), M((void *)&ssLoadStoreTemp), R(EAX));

				SetJumpTarget(skip);
			}
			else
			{
#ifdef _M_IX86
				// Need to modify it, too bad.
				if (addr != EAX)
					MOV(32, R(EAX), gpr.R(rs));
				AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
				MOVSS(MDisp(EAX, (u32)Memory::base + offset), fpr.RX(ft));
#else
				MOVSS(MComplex(RBX, addr, SCALE_1, offset), fpr.RX(ft));
#endif
			}
		}

		gpr.UnlockAll();
		fpr.UnlockAll();
		break;

	default:
		_dbg_assert_msg_(CPU,0,"Trying to interpret FPULS instruction that can't be interpreted");
		break;
	}
}

static const u64 GC_ALIGNED16(ssSignBits2[2])	= {0x8000000080000000ULL, 0x8000000080000000ULL};
static const u64 GC_ALIGNED16(ssNoSignMask[2]) = {0x7FFFFFFF7FFFFFFFULL, 0x7FFFFFFF7FFFFFFFULL};

void Jit::Comp_FPU2op(u32 op)
{
	CONDITIONAL_DISABLE;
	
	int fs = _FS;
	int fd = _FD;

	switch (op & 0x3f) 
	{
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
/*		fpr.Lock(fd, fs); // this probably works, just badly tested
		fpr.BindToRegister(fd, fd == fs, true);
		SQRTSS(fpr.RX(fd), fpr.R(fs));
		fpr.UnlockAll();
		break;*/ 
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
	default:
		Comp_Generic(op);
		return;
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

	case 6: //currentMIPS->WriteFCR(fs, R(rt)); break; //ctc1
		Comp_Generic(op);
		return;
	}
}

}	// namespace MIPSComp
