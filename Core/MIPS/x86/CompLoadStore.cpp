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

#include "../../MemMap.h"
#include "../MIPSAnalyst.h"
#include "../../Config.h"

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
	static void ReadMemSafe32(u32 addr, int preg, u32 offset)
	{
		currentMIPS->r[preg] = Memory::Read_U32(addr + offset);
	}

	static void ReadMemSafe16(u32 addr, int preg, u32 offset)
	{
		currentMIPS->r[preg] = Memory::Read_U16(addr + offset);
	}

	static void WriteMemSafe32(u32 addr, int preg, u32 offset)
	{
		Memory::Write_U32(currentMIPS->r[preg], addr + offset);
	}

	static void WriteMemSafe16(u32 addr, int preg, u32 offset)
	{
		Memory::Write_U16(currentMIPS->r[preg], addr + offset);
	}

	void Jit::Comp_ITypeMem(u32 op)
	{
		int offset = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		int o = op>>26;
		if (((op >> 29) & 1) == 0 && rt == 0) {
			// Don't load anything into $zr
			return;
		}

		switch (o)
		{
		case 37: //R(rt) = ReadMem16(addr); break; //lhu
			if (!g_Config.bFastMemory)
			{
				FlushAll();

				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, rt == rs, true);

				MOV(32, R(EAX), gpr.R(rs));
				CMP(32, R(EAX), Imm32(0x08000000));
				FixupBranch tooLow = J_CC(CC_L);
				CMP(32, R(EAX), Imm32(0x0A000000));
				FixupBranch tooHigh = J_CC(CC_GE);
#ifdef _M_IX86
				MOVZX(32, 16, gpr.RX(rt), MDisp(EAX, (u32)Memory::base + offset));
#else
				MOVZX(32, 16, gpr.RX(rt), MComplex(RBX, EAX, SCALE_1, offset));
#endif
				gpr.UnlockAll();
				FlushAll();

				FixupBranch skip = J();
				SetJumpTarget(tooLow);
				SetJumpTarget(tooHigh);
				ABI_CallFunctionACC((void *) &ReadMemSafe16, gpr.R(rs), rt, offset);
				SetJumpTarget(skip);
			}
			else
			{
				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, rt == rs, true);
#ifdef _M_IX86
				MOV(32, R(EAX), gpr.R(rs));
				AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
				MOVZX(32, 16, gpr.RX(rt), MDisp(EAX, (u32)Memory::base + offset));
#else
				MOV(32, R(EAX), gpr.R(rs));
				MOVZX(32, 16, gpr.RX(rt), MComplex(RBX, EAX, SCALE_1, offset));
#endif
				gpr.UnlockAll();
			}
			break;

		case 36: //R(rt) = ReadMem8 (addr); break; //lbu
			Comp_Generic(op);
			return;

		case 35: //R(rt) = ReadMem32(addr); break; //lw
			if (!g_Config.bFastMemory)
			{
				FlushAll();

				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, rt == rs, true);

				MOV(32, R(EAX), gpr.R(rs));
				CMP(32, R(EAX), Imm32(0x08000000));
				FixupBranch tooLow = J_CC(CC_L);
				CMP(32, R(EAX), Imm32(0x0A000000));
				FixupBranch tooHigh = J_CC(CC_GE);
#ifdef _M_IX86
				MOV(32, gpr.R(rt), MDisp(EAX, (u32)Memory::base + offset));
#else
				MOV(32, gpr.R(rt), MComplex(RBX, EAX, SCALE_1, offset));
#endif
				gpr.UnlockAll();
				FlushAll();

				FixupBranch skip = J();
				SetJumpTarget(tooLow);
				SetJumpTarget(tooHigh);
				ABI_CallFunctionACC((void *) &ReadMemSafe32, gpr.R(rs), rt, offset);
				SetJumpTarget(skip);
			}
			else
			{
				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, rt == rs, true);
#ifdef _M_IX86
				MOV(32, R(EAX), gpr.R(rs));
				AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
				MOV(32, gpr.R(rt), MDisp(EAX, (u32)Memory::base + offset));
#else
				MOV(32, R(EAX), gpr.R(rs));
				MOV(32, gpr.R(rt), MComplex(RBX, EAX, SCALE_1, offset));
#endif
				gpr.UnlockAll();
			}
			break;

		case 132: //R(rt) = (u32)(s32)(s8) ReadMem8 (addr); break; //lb
		case 133: //R(rt) = (u32)(s32)(s16)ReadMem16(addr); break; //lh
		case 136: //R(rt) = ReadMem8 (addr); break; //lbu
		case 140: //WriteMem8 (addr, R(rt)); break; //sb
			Comp_Generic(op);
			return;

		case 41: //WriteMem16(addr, R(rt)); break; //sh
			if (!g_Config.bFastMemory)
			{
				FlushAll();

				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, true, true);

				MOV(32, R(EAX), gpr.R(rs));
				CMP(32, R(EAX), Imm32(0x08000000));
				FixupBranch tooLow = J_CC(CC_L);
				CMP(32, R(EAX), Imm32(0x0A000000));
				FixupBranch tooHigh = J_CC(CC_GE);
#ifdef _M_IX86
				MOV(16, MDisp(EAX, (u32)Memory::base + offset), gpr.R(rt));
#else
				MOV(16, MComplex(RBX, EAX, SCALE_1, offset), gpr.R(rt));
#endif
				gpr.UnlockAll();
				FlushAll();

				FixupBranch skip = J();
				SetJumpTarget(tooLow);
				SetJumpTarget(tooHigh);
				ABI_CallFunctionACC((void *) &WriteMemSafe16, gpr.R(rs), rt, offset);
				SetJumpTarget(skip);
			}
			else
			{
				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, true, false);
#ifdef _M_IX86
				MOV(32, R(EAX), gpr.R(rs));
				AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
				MOV(16, MDisp(EAX, (u32)Memory::base + offset), gpr.R(rt));
#else
				MOV(32, R(EAX), gpr.R(rs));
				MOV(16, MComplex(RBX, EAX, SCALE_1, offset), gpr.R(rt));
#endif
				gpr.UnlockAll();
			}
			break;

		case 43: //WriteMem32(addr, R(rt)); break; //sw
			if (!g_Config.bFastMemory)
			{
				FlushAll();

				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, true, true);

				MOV(32, R(EAX), gpr.R(rs));
				CMP(32, R(EAX), Imm32(0x08000000));
				FixupBranch tooLow = J_CC(CC_L);
				CMP(32, R(EAX), Imm32(0x0A000000));
				FixupBranch tooHigh = J_CC(CC_GE);
#ifdef _M_IX86
				MOV(32, MDisp(EAX, (u32)Memory::base + offset), gpr.R(rt));
#else
				MOV(32, MComplex(RBX, EAX, SCALE_1, offset), gpr.R(rt));
#endif
				gpr.UnlockAll();
				FlushAll();

				FixupBranch skip = J();
				SetJumpTarget(tooLow);
				SetJumpTarget(tooHigh);
				ABI_CallFunctionACC((void *) &WriteMemSafe32, gpr.R(rs), rt, offset);
				SetJumpTarget(skip);
			}
			else
			{
				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, true, false);
#ifdef _M_IX86
				MOV(32, R(EAX), gpr.R(rs));
				AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
				MOV(32, MDisp(EAX, (u32)Memory::base + offset), gpr.R(rt));
#else
				MOV(32, R(EAX), gpr.R(rs));
				MOV(32, MComplex(RBX, EAX, SCALE_1, offset), gpr.R(rt));
#endif
				gpr.UnlockAll();
			}
			break;

		case 134: //lwl
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);
				//R(rt) = ( u32(R(rt)) & (0x00ffffff >> shift) ) | ( mem << (24 - shift) );
			}
			break;

		case 138: //lwr
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);

				//R(rt) = ( u32(rt) & (0xffffff00 << (24 - shift)) ) | ( mem	>> shift );
			}
			break;

		case 142: //swl
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);
				//WriteMem32((addr & 0xfffffffc),	( ( u32(R(rt)) >>	(24 - shift) ) ) |
				//	(	mem & (0xffffff00 << shift) ));
			}
			break;
		case 146: //swr
			{
				Crash();
				//	u32 shift = (addr & 3) << 3;
			//	u32 mem = ReadMem32(addr & 0xfffffffc);
//
//				WriteMem32((addr & 0xfffffffc), ( ( u32(R(rt)) << shift ) |
//					(mem	& (0x00ffffff >> (24 - shift)) ) ) );
			}
			break;
		default:
			Comp_Generic(op);
			return ;
		}

	}
}
