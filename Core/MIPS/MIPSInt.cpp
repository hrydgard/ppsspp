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

// MIPS is really trivial :)

#include <cmath>

#include "math/math_util.h"

#include "Common/Common.h"
#include "../Core.h"
#include "MIPS.h"
#include "MIPSInt.h"
#include "MIPSTables.h"
#include "Core/Reporting.h"
#include "Core/Config.h"

#include "../HLE/HLE.h"
#include "../System.h"

#define R(i) (currentMIPS->r[i])
#define F(i) (currentMIPS->f[i])
#define FI(i) (currentMIPS->fi[i])
#define FsI(i) (currentMIPS->fs[i])
#define PC (currentMIPS->pc)

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define HI currentMIPS->hi
#define LO currentMIPS->lo


inline int is_even(float d) {
	float int_part;
	modff(d / 2.0f, &int_part);
	return 2.0f * int_part == d;
}

// Rounds *.5 to closest even number
float round_ieee_754(float d) {
	float i = floorf(d);
	d -= i;
	if(d < 0.5f)
		return i;
	if(d > 0.5f)
		return i + 1.0f;
	if(is_even(i))
		return i;
	return i + 1.0f;
}

static inline void DelayBranchTo(u32 where)
{
	PC += 4;
	mipsr4k.nextPC = where;
	mipsr4k.inDelaySlot = true;
}

static inline void SkipLikely()
{
	PC += 8;
	--mipsr4k.downcount;
}

int MIPS_SingleStep()
{
#if defined(ARM)
	u32 op = Memory::ReadUnchecked_U32(mipsr4k.pc);
#else
	u32 op = Memory::Read_Opcode_JIT(mipsr4k.pc);
#endif
	/*
	// Choke on VFPU
	u32 info = MIPSGetInfo(op);
	if (info & IS_VFPU)
	{
		if (!Core_IsStepping() && !GetAsyncKeyState(VK_LSHIFT))
		{
			Core_EnableStepping(true);
			return;
		}
	}*/

	if (mipsr4k.inDelaySlot)
	{
		MIPSInterpret(op);
		if (mipsr4k.inDelaySlot)
		{
			mipsr4k.pc = mipsr4k.nextPC;
			mipsr4k.inDelaySlot = false;
		}
	}
	else
	{
		MIPSInterpret(op);
	}
	return 1;
}



u32 MIPS_GetNextPC()
{
	if (mipsr4k.inDelaySlot)
		return mipsr4k.nextPC;
	else
		return mipsr4k.pc + 4;
}


void MIPS_ClearDelaySlot()
{
	mipsr4k.inDelaySlot = false;
}


namespace MIPSInt
{
	void Int_Cache(u32 op)
	{
		int imm = (s16)(op & 0xFFFF);
		int rs = _RS;
		int addr = R(rs) + imm;
		int func = (op >> 16) & 0x1F;

		// It appears that a cache line is 0x40 (64) bytes.
		switch (func) {
		case 24:
			// "Create Dirty Exclusive" - for avoiding a cacheline fill before writing to it.
			// Will cause garbage on the real machine so we just ignore it, the app will overwrite the cacheline.
			break;
		case 25:  // Hit Invalidate - zaps the line if present in cache. Should not writeback???? scary.
			// No need to do anything.
			break;
		case 27:  // D-cube. Hit Writeback Invalidate.
			break;
		case 30:  // GTA LCS, a lot. Fill (prefetch).
			break;

		default:
			DEBUG_LOG(CPU,"cache instruction affecting %08x : function %i", addr, func);
		}

		PC += 4;
	}

	void Int_Syscall(u32 op)
	{
		// Need to pre-move PC, as CallSyscall may result in a rescheduling!
		// To do this neater, we'll need a little generated kernel loop that syscall can jump to and then RFI from 
		// but I don't see a need to bother.
		if (mipsr4k.inDelaySlot)
		{
			mipsr4k.pc = mipsr4k.nextPC;
		}
		else
		{
			mipsr4k.pc += 4;
		}
		mipsr4k.inDelaySlot = false;
		CallSyscall(op);
	}

	void Int_Sync(u32 op)
	{
		//DEBUG_LOG(CPU, "sync");
		PC += 4;
	}

	void Int_Break(u32 op)
	{
		Reporting::ReportMessage("BREAK instruction hit");
		ERROR_LOG(CPU, "BREAK!");
		if (!g_Config.bIgnoreBadMemAccess) 
			Core_UpdateState(CORE_STEPPING);
		PC += 4;
	}

	void Int_RelBranch(u32 op)
	{
		int imm = (signed short)(op&0xFFFF)<<2;
		int rs = _RS;
		int rt = _RT;
		u32 addr = PC + imm + 4;

		switch (op >> 26) 
		{
		case 4:  if (R(rt) == R(rs))  DelayBranchTo(addr); else PC += 4; break; //beq
		case 5:  if (R(rt) != R(rs))  DelayBranchTo(addr); else PC += 4; break; //bne
		case 6:  if ((s32)R(rs) <= 0) DelayBranchTo(addr); else PC += 4; break; //blez
		case 7:  if ((s32)R(rs) > 0) DelayBranchTo(addr); else PC += 4; break; //bgtz

		case 20: if (R(rt) == R(rs))  DelayBranchTo(addr); else SkipLikely(); break; //beql
		case 21: if (R(rt) != R(rs))  DelayBranchTo(addr); else SkipLikely(); break; //bnel
		case 22: if ((s32)R(rs) <= 0) DelayBranchTo(addr); else SkipLikely(); break; //blezl
		case 23: if ((s32)R(rs) >  0) DelayBranchTo(addr); else SkipLikely(); break; //bgtzl

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_RelBranchRI(u32 op)
	{
		int imm = (signed short)(op&0xFFFF)<<2;
		int rs = _RS;
		u32 addr = PC + imm + 4;

		switch ((op>>16) & 0x1F)
		{
		case 0: if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltz
		case 1: if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgez
		case 2: if ((s32)R(rs) <  0) DelayBranchTo(addr); else SkipLikely(); break;//bltzl
		case 3: if ((s32)R(rs) >= 0) DelayBranchTo(addr); else SkipLikely(); break;//bgezl
		case 16: R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <  0) DelayBranchTo(addr); else PC += 4; break;//bltzal
		case 17: R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else PC += 4; break;//bgezal
		case 18: R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) <	0) DelayBranchTo(addr); else SkipLikely(); break;//bltzall
		case 19: R(MIPS_REG_RA) = PC + 8; if ((s32)R(rs) >= 0) DelayBranchTo(addr); else SkipLikely(); break;//bgezall
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}


	void Int_VBranch(u32 op)
	{
		int imm = (signed short)(op&0xFFFF)<<2;
		u32 addr = PC + imm + 4;

		int imm3 = (op>>18)&7;
		int val = (currentMIPS->vfpuCtrl[VFPU_CTRL_CC] >> imm3) & 1;

		switch ((op >> 16) & 3)
		{
		case 0: if (!val) DelayBranchTo(addr); else PC += 4; break; //bvf
		case 1: if ( val) DelayBranchTo(addr); else PC += 4; break; //bvt
		case 2: if (!val) DelayBranchTo(addr); else SkipLikely(); break; //bvfl
		case 3: if ( val) DelayBranchTo(addr); else SkipLikely(); break; //bvtl
		}
	}

	void Int_FPUBranch(u32 op)
	{
		int imm = (signed short)(op&0xFFFF)<<2;
		u32 addr = PC + imm + 4;
		switch((op>>16)&0x1f)
		{
		case 0: if (!currentMIPS->fpcond) DelayBranchTo(addr); else PC += 4; break;//bc1f
		case 1: if ( currentMIPS->fpcond) DelayBranchTo(addr); else PC += 4; break;//bc1t
		case 2: if (!currentMIPS->fpcond) DelayBranchTo(addr); else SkipLikely(); break;//bc1fl
		case 3: if ( currentMIPS->fpcond) DelayBranchTo(addr); else SkipLikely(); break;//bc1tl
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}
	
	void Int_JumpType(u32 op)
	{
		if (mipsr4k.inDelaySlot)
			_dbg_assert_msg_(CPU,0,"Jump in delay slot :(");

		u32 off = ((op & 0x3FFFFFF) << 2);
		u32 addr = (currentMIPS->pc & 0xF0000000) | off;

		switch (op>>26) 
		{
		case 2: DelayBranchTo(addr); break; //j
		case 3: //jal
			R(31) = PC + 8;
			DelayBranchTo(addr);
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_JumpRegType(u32 op)
	{
		if (mipsr4k.inDelaySlot)
		{
			// There's one of these in Star Soldier at 0881808c, which seems benign - it should probably be ignored.
			if (op == 0x03e00008)
				return;
			ERROR_LOG(HLE, "Jump in delay slot :(");
			_dbg_assert_msg_(CPU,0,"Jump in delay slot :(");
		}

		int rs = (op>>21)&0x1f;
		u32 addr = R(rs);
		switch (op & 0x3f) 
		{
		case 8: //jr
			//			LOG(CPU,"returning from: %08x",PC);
			DelayBranchTo(addr);
			break;
		case 9: //jalr
			R(31) = PC + 8;
			DelayBranchTo(addr);
			break;
		}
	}

	void Int_IType(u32 op)
	{
		s32 simm = (s32)(s16)(op & 0xFFFF);
		u32 uimm = (u32)(u16)(op & 0xFFFF);

		u32 suimm = (u32)simm;

		int rt = _RT;
		int rs = _RS;

		if (rt == 0) { //destination register is zero register
			PC += 4;
			return; //nop
		}

		switch (op>>26) 
		{
		case 8:	R(rt) = R(rs) + simm; break; //addi
		case 9:	R(rt) = R(rs) + simm; break;	//addiu
		case 10: R(rt) = (s32)R(rs) < simm; break; //slti
		case 11: R(rt) = R(rs) < suimm; break; //sltiu
		case 12: R(rt) = R(rs) & uimm; break; //andi
		case 13: R(rt) = R(rs) | uimm; break; //ori
		case 14: R(rt) = R(rs) ^ uimm; break; //xori
		case 15: R(rt) = uimm << 16;	 break; //lui
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_StoreSync(u32 op)
	{
		s32 imm = (signed short)(op&0xFFFF);
		int base = ((op >> 21) & 0x1f);
		int rt = (op >> 16) & 0x1f;
		u32 addr = R(base) + imm;

		switch (op >> 26)
		{
		case 48: // ll
			if (rt != 0) {
				R(rt) = Memory::Read_U32(addr);
			}
			currentMIPS->llBit = 1;
			break;
		case 56: // sc
			if (currentMIPS->llBit) {
				Memory::Write_U32(R(rt), addr);
				if (rt != 0) {
					R(rt) = 1;
				}
			} else if (rt != 0) {
				R(rt) = 0;
			}
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}


	void Int_RType3(u32 op)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		static bool has_warned = false;

		// Don't change $zr.
		if (rd == 0)
		{
			PC += 4;
			return;
		}

		switch (op & 63) 
		{
		case 10: if (R(rt) == 0) R(rd) = R(rs); break; //movz
		case 11: if (R(rt) != 0) R(rd) = R(rs); break; //movn
		case 32: 
			if (!has_warned) {
				ERROR_LOG(HLE,"WARNING : exception-causing add at %08x", PC);
				has_warned = true;
			}
			R(rd) = R(rs) + R(rt);		break; //add
		case 33: R(rd) = R(rs) + R(rt);		break; //addu
		case 34: 
			if (!has_warned) {
				ERROR_LOG(HLE,"WARNING : exception-causing sub at %08x", PC);
				has_warned = true;
			}
			R(rd) = R(rs) - R(rt);		break; //sub
		case 35: R(rd) = R(rs) - R(rt);		break; //subu
		case 36: R(rd) = R(rs) & R(rt);		break; //and
		case 37: R(rd) = R(rs) | R(rt);		break; //or
		case 38: R(rd) = R(rs) ^ R(rt);		break; //xor
		case 39: R(rd) = ~(R(rs) | R(rt)); break; //nor
		case 42: R(rd) = (s32)R(rs) < (s32)R(rt); break; //slt
		case 43: R(rd) = R(rs) < R(rt);		break; //sltu
		case 44: R(rd) = ((s32)R(rs) > (s32)R(rt)) ? R(rs) : R(rt); break; //max
		case 45: R(rd) = ((s32)R(rs) < (s32)R(rt)) ? R(rs) : R(rt); break;//min
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}


	void Int_ITypeMem(u32 op)
	{
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		u32 addr = R(rs) + imm;

		if (((op >> 29) & 1) == 0 && rt == 0) {
			// Don't load anything into $zr
			PC += 4;
			return;
		}

		switch (op >> 26) 
		{
		case 32: R(rt) = (u32)(s32)(s8) Memory::Read_U8(addr); break; //lb
		case 33: R(rt) = (u32)(s32)(s16)Memory::Read_U16(addr); break; //lh
		case 35: R(rt) = Memory::Read_U32(addr); break; //lw
		case 36: R(rt) = Memory::Read_U8 (addr); break; //lbu
		case 37: R(rt) = Memory::Read_U16(addr); break; //lhu
		case 40: Memory::Write_U8(R(rt), addr); break; //sb
		case 41: Memory::Write_U16(R(rt), addr); break; //sh
		case 43: Memory::Write_U32(R(rt), addr); break; //sw

		// When there's an LWL and an LWR together, we should be able to peephole optimize that
		// into a single non-alignment-checking LW.
		case 34: //lwl
			{
				u32 shift = (addr & 3) * 8;
				u32 mem = Memory::Read_U32(addr & 0xfffffffc);
				u32 result = ( u32(R(rt)) & (0x00ffffff >> shift) ) | ( mem << (24 - shift) );
				R(rt) = result;
			}
			break;

		case 38: //lwr
			{
				u32 shift = (addr & 3) * 8;
				u32 mem = Memory::Read_U32(addr & 0xfffffffc);
				u32 regval = R(rt);
				u32 result = ( regval & (0xffffff00 << (24 - shift)) ) | ( mem	>> shift );
				R(rt) = result;
			}
			break;

		case 42: //swl
			{
				u32 shift = (addr & 3) * 8;
				u32 mem = Memory::Read_U32(addr & 0xfffffffc);
				u32 result = ( ( u32(R(rt)) >>	(24 - shift) ) ) | (	mem & (0xffffff00 << shift) );
				Memory::Write_U32(result, (addr & 0xfffffffc));
			}
			break;

		case 46: //swr
			{
				u32 shift = (addr & 3) << 3;
				u32 mem = Memory::Read_U32(addr & 0xfffffffc);
				u32 result = ( ( u32(R(rt)) << shift ) | (mem	& (0x00ffffff >> (24 - shift)) ) );
				Memory::Write_U32(result, (addr & 0xfffffffc));
			}
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret Mem instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_FPULS(u32 op)
	{
		s32 offset = (s16)(op&0xFFFF);
		int ft = _FT;
		int rs = _RS;
		u32 addr = R(rs) + offset;

		switch(op >> 26)
		{
		case 49: FI(ft) = Memory::Read_U32(addr); break; //lwc1
		case 57: Memory::Write_U32(FI(ft), addr); break; //swc1
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret FPULS instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_mxc1(u32 op)
	{
		int fs = _FS;
		int rt = _RT;

		switch((op>>21)&0x1f) 
		{
		case 0: if (rt != 0) R(rt) = FI(fs); break; //mfc1
		case 2: if (rt != 0) R(rt) = currentMIPS->ReadFCR(fs); break; //cfc1
		case 4: FI(fs) = R(rt);	break; //mtc1
		case 6: currentMIPS->WriteFCR(fs, R(rt)); break; //ctc1
		
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_RType2(u32 op)
	{
		int rs = _RS;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0)
		{
			PC += 4;
			return;
		}

		switch (op & 63)
		{
		case 22:	//clz
			{ //TODO: verify
				int x = 31;
				int count=0;
				while (!(R(rs) & (1<<x)) && x >= 0)
				{
					count++;
					x--;
				}
				R(rd) = count;
			}
			break;
		case 23: //clo
			{ //TODO: verify
				int x = 31;
				int count=0;
				while ((R(rs) & (1<<x)) && x >= 0)
				{
					count++;
					x--;
				}
				R(rd) = count;
			}
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_MulDivType(u32 op)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		switch (op & 63) 
		{
		case 24: //mult
			{
				s64 result = (s64)(s32)R(rs) * (s64)(s32)R(rt);
				u64 resultBits = (u64)(result);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 25: //multu
			{
				u64 resultBits = (u64)R(rs) * (u64)R(rt);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 28: //madd
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origValBits = (u64)lo | ((u64)(hi)<<32);
				s64 origVal = (s64)origValBits;
				s64 result = origVal + (s64)(s32)a * (s64)(s32)b;
				u64 resultBits = (u64)(result);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 29: //maddu
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origVal = (u64)lo | ((u64)(hi)<<32);
				u64 result = origVal + (u64)a * (u64)b;
				LO = (u32)(result);
				HI = (u32)(result>>32);
			}
			break;
		case 46: //msub
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origValBits = (u64)lo | ((u64)(hi)<<32);
				s64 origVal = (s64)origValBits;
				s64 result = origVal - (s64)(s32)a * (s64)(s32)b;
				u64 resultBits = (u64)(result);
				LO = (u32)(resultBits);
				HI = (u32)(resultBits>>32);
			}
			break;
		case 47: //msubu
			{
				u32 a=R(rs),b=R(rt),hi=HI,lo=LO;
				u64 origVal = (u64)lo | ((u64)(hi)<<32);
				u64 result = origVal - (u64)a * (u64)b;
				LO = (u32)(result);
				HI = (u32)(result>>32);
			}
			break;
		case 16: R(rd) = HI; break; //mfhi
		case 17: HI = R(rs); break; //mthi
		case 18: R(rd) = LO; break; //mflo
		case 19: LO = R(rs); break; //mtlo
		case 26: //div
			{
				s32 a = (s32)R(rs);
				s32 b = (s32)R(rt);
				if (a == (s32)0x80000000 && b == -1) {
					LO = 0x80000000;
				} else if (b != 0) {
					LO = (u32)(a / b);
					HI = (u32)(a % b);
				} else {
					LO = HI = 0;	// Not sure what the right thing to do is?
				}
			}
			break;
		case 27: //divu
			{
				u32 a = R(rs);
				u32 b = R(rt);
				if (b != 0) 
				{
					LO = (a/b);
					HI = (a%b);
				} else {
					LO = HI = 0;
				}
			}
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}


	void Int_ShiftType(u32 op)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		int sa = _FD;

		// Don't change $zr.
		if (rd == 0)
		{
			PC += 4;
			return;
		}
		
		switch (op & 0x3f)
		{
		case 0: R(rd) = R(rt) << sa;					 break; //sll
		case 2: 
			if (_RS == 0) //srl
			{
				R(rd) = R(rt) >> sa;
				break; 
			} 
			else if (_RS == 1) //rotr
			{
				R(rd) = __rotr(R(rt), sa);
				break;
			}
			else
				goto wrong;

		case 3: R(rd) = (u32)(((s32)R(rt)) >> sa);		break; //sra
		case 4: R(rd) = R(rt) << (R(rs)&0x1F);				break; //sllv
		case 6:
			if (_FD == 0) //srlv
			{
				R(rd) = R(rt) >> (R(rs)&0x1F);
				break; 
			}
			else if (_FD == 1) // rotrv
			{
				R(rd) = __rotr(R(rt), R(rs));
				break;
			}
			else goto wrong;
		case 7: R(rd) = (u32)(((s32)R(rt)) >> (R(rs)&0x1F)); break; //srav
		default:
			wrong:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Allegrex(u32 op)
	{
		int rt = _RT;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0)
		{
			PC += 4;
			return;
		}

		switch((op>>6)&31)
		{
		case 16: // seb
			R(rd) = (u32)(s32)(s8)(u8)R(rt);
			break;

		case 20: // bitrev
			{
				u32 tmp = 0;
				for (int i = 0; i < 32; i++)
				{
					if (R(rt) & (1 << i))
					{
						tmp |= (0x80000000 >> i);
					}
				}
				R(rd) = tmp;
			}
			break;

		case 24: // seh
			R(rd) = (u32)(s32)(s16)(u16)R(rt);
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret ALLEGREX instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Allegrex2(u32 op)
	{
		int rt = _RT;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0)
		{
			PC += 4;
			return;
		}

		switch (op & 0x3ff)
		{
		case 0xA0: //wsbh
			R(rd) = ((R(rt) & 0xFF00FF00) >> 8) | ((R(rt) & 0x00FF00FF) << 8);
			break;
		case 0xE0: //wsbw
			R(rd) = _byteswap_ulong(R(rt));
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret ALLEGREX instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Special3(u32 op)
	{
		int rs = _RS;
		int rt = _RT;
		int pos = _POS;

		// Don't change $zr.
		if (rt == 0)
		{
			PC += 4;
			return;
		}

		switch (op & 0x3f)
		{
		case 0x0: //ext
			{
				int size = _SIZE + 1;
				R(rt) = (R(rs) >> pos) & ((1<<size) - 1);
			}
			break;
		case 0x4: //ins
			{
				int size = (_SIZE + 1) - pos;
				int sourcemask = (1 << size) - 1;
				int destmask = sourcemask << pos;
				R(rt) = (R(rt) & ~destmask) | ((R(rs)&sourcemask) << pos);
			}
			break;
		}

		PC += 4;
	}

	void Int_FPU2op(u32 op)
	{
		int fs = _FS;
		int fd = _FD;

		switch (op & 0x3f)
		{
		case 4:	F(fd)	= sqrtf(F(fs)); break; //sqrt
		case 5:	F(fd)	= fabsf(F(fs)); break; //abs
		case 6:	F(fd)	= F(fs); break; //mov
		case 7:	F(fd)	= -F(fs); break; //neg
		case 12: FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s
		case 13: FsI(fd) = F(fs)>=0 ? (int)floorf(F(fs)) : (int)ceilf(F(fs)); break;//trunc.w.s
		case 14: FsI(fd) = (int)ceilf (F(fs)); break; //ceil.w.s
		case 15: FsI(fd) = (int)floorf(F(fs)); break; //floor.w.s
		case 32: F(fd) = (float)FsI(fs); break; //cvt.s.w

		case 36:
			switch (currentMIPS->fcr31 & 3)
			{
			case 0: FsI(fd) = (int)round_ieee_754(F(fs)); break;  // RINT_0
			case 1: FsI(fd) = (int)F(fs); break;  // CAST_1
			case 2: FsI(fd) = (int)ceilf(F(fs)); break;  // CEIL_2
			case 3: FsI(fd) = (int)floorf(F(fs)); break;  // FLOOR_3
			}
			break; //cvt.w.s
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret FPU2Op instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_FPUComp(u32 op)
	{
		int fs = _FS;
		int ft = _FT;
		bool cond;
		switch (op & 0xf)
		{
		case 0: //f
		case 8: //sf
			cond = false;
			break;

		case 1: //un
		case 9: //ngle
			cond = my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		case 2: //eq
		case 10: //seq
			cond = (F(fs) == F(ft));
			break;

		case 3: //ueq
		case 11: //ngl
			cond = (F(fs) == F(ft)) || my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		case 4: //olt
		case 12: //lt
			cond = (F(fs) < F(ft));
			break;

		case 5: //ult
		case 13: //nge
			cond = (F(fs) < F(ft)) || my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		case 6: //ole
		case 14: //le
			cond = (F(fs) <= F(ft));
			break;

		case 7: //ule
		case 15: //ngt
			cond = (F(fs) <= F(ft)) || my_isnan(F(fs)) || my_isnan(F(ft));
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret FPUComp instruction that can't be interpreted");
			cond = false;
			break;
		}
		currentMIPS->fpcond = cond;
		PC += 4;
	}

	void Int_FPU3op(u32 op)
	{
		int ft = _FT;
		int fs = _FS;
		int fd = _FD;

		switch (op & 0x3f)
		{
		case 0: F(fd) = F(fs) + F(ft); break; //add
		case 1: F(fd) = F(fs) - F(ft); break; //sub
		case 2: F(fd) = F(fs) * F(ft); break; //mul
		case 3: F(fd) = F(fs) / F(ft); break; //div
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret FPU3Op instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Interrupt(u32 op)
	{
		static int reported = 0;
		switch (op & 1)
		{
		case 0:
			if (!reported) {
				Reporting::ReportMessage("INTERRUPT instruction hit");
				WARN_LOG(CPU,"Disable/Enable Interrupt CPU instruction");
				reported = 1;
			}
			break;
		}
		PC += 4;
	}


	void Int_Emuhack(u32 op)
	{
		_dbg_assert_msg_(CPU,0,"Trying to interpret emuhack instruction that can't be interpreted");
	}


}
