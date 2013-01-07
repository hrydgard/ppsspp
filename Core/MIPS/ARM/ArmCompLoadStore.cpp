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

#define OLDD Comp_Generic(op); return;

namespace MIPSComp
{
	void Jit::Comp_ITypeMem(u32 op)
	{
		OLDD

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
		case 36: //R(rt) = ReadMem8 (addr); break; //lbu
			Comp_Generic(op);
			return;

		case 35: //R(rt) = ReadMem32(addr); break; //lw
			/*
			gpr.Lock(rt, rs);
			gpr.BindToRegister(rt, rt == rs, true);
			MOV(32, R(EAX), gpr.R(rs));
			AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
			MOV(32, gpr.R(rt), MDisp(EAX, (u32)Memory::base + offset));
			gpr.UnlockAll();*/
			break;

		case 132: //R(rt) = (u32)(s32)(s8) ReadMem8 (addr); break; //lb
		case 133: //R(rt) = (u32)(s32)(s16)ReadMem16(addr); break; //lh
		case 136: //R(rt) = ReadMem8 (addr); break; //lbu
		case 140: //WriteMem8 (addr, R(rt)); break; //sb
			
		case 40:
		case 41: //WriteMem16(addr, R(rt)); break; //sh
			Comp_Generic(op);
			return;

		case 43: //WriteMem32(addr, R(rt)); break; //sw
			/*
			{
				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, true, false);
				MOV(32, R(EAX), gpr.R(rs));
				AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
				MOV(32, MDisp(EAX, (u32)Memory::base + offset), gpr.R(rt));
				gpr.UnlockAll();
			}*/

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
