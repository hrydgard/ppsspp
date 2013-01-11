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
			Comp_Generic(op);
			return;

		case 35:   //R(rt) = ReadMem32(addr); //lw
		case 36: //R(rt) = ReadMem8 (addr); break; //lbu
			if (true || g_Config.bFastMemory) {
				gpr.SpillLock(rt, rs);
				gpr.MapReg(rt, MAP_DIRTY);
				gpr.MapReg(rs);
				gpr.ReleaseSpillLocks();
			
				Operand2 op2;
				if (TryMakeOperand2(offset, op2)) {
					ADD(R0, gpr.R(rs), op2);
				} else {
					ARMABI_MOVI2R(R0, (u32)offset);
					ADD(R0, gpr.R(rs), R0);
				}
				BIC(R0, R0, Operand2(0xC0, 4));   // &= 0x3FFFFFFF
				ADD(R0, R0, R11);   // TODO: Merge with next instruction
				if (o == 35) {
					LDR(gpr.R(rt), R0);
				} else if (o == 36) {
					LDRB(gpr.R(rt), R0);
				}
			} else {
				Comp_Generic(op);
				return;
			}
			break;

		case 41: //WriteMem16(addr, R(rt)); break; //sh
			Comp_Generic(op);
			return;

		case 40: //sb
		case 43: //WriteMem32(addr, R(rt)); break; //sw
			if (true || g_Config.bFastMemory) {
				gpr.SpillLock(rt, rs);
				gpr.MapReg(rt);
				gpr.MapReg(rs);
				gpr.ReleaseSpillLocks();

				Operand2 op2;
				if (TryMakeOperand2(offset, op2)) {
					ADD(R0, gpr.R(rs), op2);
				} else {
					ARMABI_MOVI2R(R0, (u32)offset);
					ADD(R0, gpr.R(rs), R0);
				}
				BIC(R0, R0, Operand2(0xC0, 4));   // &= 0x3FFFFFFF
				ADD(R0, R0, R11);
				if (o == 43) {
					STR(R0, gpr.R(rt));
				} else if (o == 40) {
					STRB(R0, gpr.R(rt));
				}
			} else {
				Comp_Generic(op);
				return;
			}
			break;
			// break;
			/*
		case 34: //lwl
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);
				//R(rt) = ( u32(R(rt)) & (0x00ffffff >> shift) ) | ( mem << (24 - shift) );
			}
			break;

		case 38: //lwr
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);

				//R(rt) = ( u32(rt) & (0xffffff00 << (24 - shift)) ) | ( mem	>> shift );
			}
			break;
 
		case 42: //swl
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);
				//WriteMem32((addr & 0xfffffffc),	( ( u32(R(rt)) >>	(24 - shift) ) ) |
				//	(	mem & (0xffffff00 << shift) ));
			}
			break;
		case 46: //swr
			{
				Crash();
				//	u32 shift = (addr & 3) << 3;
			//	u32 mem = ReadMem32(addr & 0xfffffffc);
//
//				WriteMem32((addr & 0xfffffffc), ( ( u32(R(rt)) << shift ) |
//					(mem	& (0x00ffffff >> (24 - shift)) ) ) );
			}
			break;*/
		default:
			Comp_Generic(op);
			return ;
		}

	}
}
