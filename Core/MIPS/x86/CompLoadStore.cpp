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

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
	void Jit::CompITypeMemRead(u32 op, u32 bits, void (XEmitter::*mov)(int, int, X64Reg, OpArg), void *safeFunc)
	{
		CONDITIONAL_DISABLE;
		int offset = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;

		gpr.Lock(rt, rs);
		gpr.BindToRegister(rt, rt == rs, true);

		JitSafeMem safe(this, rs, offset);
		OpArg src;
		if (safe.PrepareRead(src, 4))
			(this->*mov)(32, bits, gpr.RX(rt), src);
		if (safe.PrepareSlowRead(safeFunc))
			(this->*mov)(32, bits, gpr.RX(rt), R(EAX));
		safe.Finish();

		gpr.UnlockAll();
	}

	void Jit::CompITypeMemWrite(u32 op, u32 bits, void *safeFunc)
	{
		CONDITIONAL_DISABLE;
		int offset = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;

		gpr.Lock(rt, rs);
		gpr.BindToRegister(rt, true, false);

#ifdef _M_IX86
		// We use EDX so we can have DL for 8-bit ops.
		const bool needSwap = bits == 8 && !gpr.R(rt).IsSimpleReg(EDX) && !gpr.R(rt).IsSimpleReg(ECX);
		if (needSwap)
			gpr.FlushLockX(EDX);
#else
		const bool needSwap = false;
#endif

		JitSafeMem safe(this, rs, offset);
		OpArg dest;
		if (safe.PrepareWrite(dest, 4))
		{
			if (needSwap)
			{
				MOV(32, R(EDX), gpr.R(rt));
				MOV(bits, dest, R(EDX));
			}
			else
				MOV(bits, dest, gpr.R(rt));
		}
		if (safe.PrepareSlowWrite())
			safe.DoSlowWrite(safeFunc, gpr.R(rt));
		safe.Finish();

		if (needSwap)
			gpr.UnlockAllX();
		gpr.UnlockAll();
	}

	void Jit::Comp_ITypeMem(u32 op)
	{
		CONDITIONAL_DISABLE;
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
			CompITypeMemRead(op, 16, &XEmitter::MOVZX, (void *) &Memory::Read_U16);
			break;

		case 36: //R(rt) = ReadMem8 (addr); break; //lbu
			CompITypeMemRead(op, 8, &XEmitter::MOVZX, (void *) &Memory::Read_U8);
			break;

		case 35: //R(rt) = ReadMem32(addr); break; //lw
			CompITypeMemRead(op, 32, &XEmitter::MOVZX, (void *) &Memory::Read_U32);
			break;

		case 32: //R(rt) = (u32)(s32)(s8) ReadMem8 (addr); break; //lb
			CompITypeMemRead(op, 8, &XEmitter::MOVSX, (void *) &Memory::Read_U8);
			break;

		case 33: //R(rt) = (u32)(s32)(s16)ReadMem16(addr); break; //lh
			CompITypeMemRead(op, 16, &XEmitter::MOVSX, (void *) &Memory::Read_U16);
			break;

		case 40: //WriteMem8 (addr, R(rt)); break; //sb
			CompITypeMemWrite(op, 8, (void *) &Memory::Write_U8);
			break;

		case 41: //WriteMem16(addr, R(rt)); break; //sh
			CompITypeMemWrite(op, 16, (void *) &Memory::Write_U16);
			break;

		case 43: //WriteMem32(addr, R(rt)); break; //sw
			CompITypeMemWrite(op, 32, (void *) &Memory::Write_U32);
			break;

		case 34: //lwl
			{
				u32 nextOp = Memory::Read_Instruction(js.compilerPC + 4);
				// Looking for lwr rd, offset-3(rs) which makes a pair.
				u32 desiredOp = ((op + (4 << 26)) & 0xFFFF0000) + (offset - 3);
				if (!js.inDelaySlot && nextOp == desiredOp)
				{
					EatInstruction(nextOp);
					// nextOp has the correct address.
					CompITypeMemRead(nextOp, 32, &XEmitter::MOVZX, (void *) &Memory::Read_U32);
				}
				else
					Comp_Generic(op);
			}
			break;

		case 38: //lwr
			{
				u32 nextOp = Memory::Read_Instruction(js.compilerPC + 4);
				// Looking for lwl rd, offset+3(rs) which makes a pair.
				u32 desiredOp = ((op - (4 << 26)) & 0xFFFF0000) + (offset + 3);
				if (!js.inDelaySlot && nextOp == desiredOp)
				{
					EatInstruction(nextOp);
					// op has the correct address.
					CompITypeMemRead(op, 32, &XEmitter::MOVZX, (void *) &Memory::Read_U32);
				}
				else
					Comp_Generic(op);
			}
			break;

		case 42: //swl
			{
				u32 nextOp = Memory::Read_Instruction(js.compilerPC + 4);
				// Looking for swr rd, offset-3(rs) which makes a pair.
				u32 desiredOp = ((op + (4 << 26)) & 0xFFFF0000) + (offset - 3);
				if (!js.inDelaySlot && nextOp == desiredOp)
				{
					EatInstruction(nextOp);
					// nextOp has the correct address.
					CompITypeMemWrite(nextOp, 32, (void *) &Memory::Write_U32);
				}
				else
					Comp_Generic(op);
			}
			break;

		case 46: //swr
			{
				u32 nextOp = Memory::Read_Instruction(js.compilerPC + 4);
				// Looking for swl rd, offset+3(rs) which makes a pair.
				u32 desiredOp = ((op - (4 << 26)) & 0xFFFF0000) + (offset + 3);
				if (!js.inDelaySlot && nextOp == desiredOp)
				{
					EatInstruction(nextOp);
					// op has the correct address.
					CompITypeMemWrite(op, 32, (void *) &Memory::Write_U32);
				}
				else
					Comp_Generic(op);
			}
			break;

		default:
			Comp_Generic(op);
			return ;
		}

	}
}
