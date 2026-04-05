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

#include <cmath>

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/math_util.h"

#include "Common/BitSet.h"
#include "Common/BitScan.h"
#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLETables.h"
#include "Core/HLE/ReplaceTables.h"

#define R(i) (currentMIPS->r[i])
#define F(i) (currentMIPS->f[i])
#define FI(i) (currentMIPS->fi[i])
#define FsI(i) (currentMIPS->fs[i])
#define PC (currentMIPS->pc)

#define _SIMM16_SHL2 ((u32)(s32)(s16)(op & 0xFFFF) << 2)
#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define _FS   ((op>>11) & 0x1F)
#define _FT   ((op>>16) & 0x1F)
#define _FD   ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

#define HI currentMIPS->hi
#define LO currentMIPS->lo

static inline void DelayBranchTo(u32 where)
{
	if (!Memory::IsValidAddress(where) || (where & 3) != 0) {
		Core_ExecException(where, PC, ExecExceptionType::JUMP);
	}
	PC += 4;
	mipsr4k.nextPC = where;
	mipsr4k.inDelaySlot = true;
}

static inline void SkipLikely() {
	MIPSInfo delaySlot = MIPSGetInfo(Memory::Read_Instruction(PC + 4, true));
	// Don't actually skip if it is a jump (seen in Brooktown High.)
	if (delaySlot & IS_JUMP) {
		PC += 4;
	} else {
		PC += 8;
		--mipsr4k.downcount;
	}
}

int MIPS_SingleStep()
{
	MIPSOpcode op = Memory::Read_Opcode_JIT(mipsr4k.pc);
	if (mipsr4k.inDelaySlot) {
		MIPSInterpret(op);
		if (mipsr4k.inDelaySlot) {
			mipsr4k.pc = mipsr4k.nextPC;
			mipsr4k.inDelaySlot = false;
		}
	} else {
		MIPSInterpret(op);
	}
	return 1;
}

namespace MIPSInt
{
	void Int_Cache(MIPSOpcode op)
	{
		int imm = SignExtend16ToS32(op);
		int rs = _RS;
		uint32_t addr = R(rs) + imm;
		int func = (op >> 16) & 0x1F;

		// Let's only report this once per run to be safe from impacting perf.
		static bool loggedAlignment = false;

		// It appears that a cache line is 0x40 (64) bytes, loops in games
		// issue the cache instruction at that interval.

		// These codes might be PSP-specific, they don't match regular MIPS cache codes very well

		// NOTE: If you add support for more, make sure they are handled in the various Jit::Comp_Cache.
		switch (func) {
		// Icache
		case 8:
			// Invalidate the instruction cache at this address.
			// We assume the CPU won't be reset during this, so no locking.
			if (MIPSComp::jit) {
				// Let's over invalidate to be super safe.
				uint32_t alignedAddr = addr & ~0x3F;
				int size = 0x40 + (addr & 0x3F);
				MIPSComp::jit->InvalidateCacheAt(alignedAddr, size);
				// Using a bool to avoid locking/etc. in case it's slow.
				if (!loggedAlignment && (addr & 0x3F) != 0) {
					// These are seen exclusively in Lego games, and are really no big deal. Reporting removed.
					WARN_LOG(Log::JIT, "Unaligned icache invalidation of %08x (%08x + %d) at PC=%08x", addr, R(rs), imm, PC);
					loggedAlignment = true;
				}
				if (alignedAddr <= PC + 4 && alignedAddr + size >= PC - 4) {
					// This is probably rare so we don't use a static bool.
					WARN_LOG_REPORT_ONCE(icacheInvalidatePC, Log::JIT, "Invalidating address near PC: %08x (%08x + %d) at PC=%08x", addr, R(rs), imm, PC);
				}
			}
			break;

		// Dcache
		case 24:
			// "Create Dirty Exclusive" - for avoiding a cacheline fill before writing to it.
			// Will cause garbage on the real machine so we just ignore it, the app will overwrite the cacheline.
			break;
		case 25:  // Hit Invalidate - zaps the line if present in cache. Should not writeback???? scary.
			// No need to do anything.
			break;
		case 27:  // D-cube. Hit Writeback Invalidate.  Tony Hawk Underground 2
			break;
		case 30:  // GTA LCS, a lot. Fill (prefetch).   Tony Hawk Underground 2
			break;

		default:
			DEBUG_LOG(Log::CPU, "cache instruction affecting %08x : function %i", addr, func);
		}

		PC += 4;
	}

	void Int_Syscall(MIPSOpcode op)
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

	void Int_Sync(MIPSOpcode op)
	{
		//DEBUG_LOG(Log::CPU, "sync");
		PC += 4;
	}

	void Int_Break(MIPSOpcode op)
	{
		Reporting::ReportMessage("BREAK instruction hit");
		Core_BreakException(PC);
		PC += 4;
	}

	void Int_RelBranch(MIPSOpcode op)
	{
		int imm = _SIMM16_SHL2;
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
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_RelBranchRI(MIPSOpcode op)
	{
		int imm = _SIMM16_SHL2;
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
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}


	void Int_VBranch(MIPSOpcode op)
	{
		int imm = _SIMM16_SHL2;
		u32 addr = PC + imm + 4;

		// x, y, z, w, any, all, (invalid), (invalid)
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

	void Int_FPUBranch(MIPSOpcode op)
	{
		int imm = _SIMM16_SHL2;
		u32 addr = PC + imm + 4;
		switch((op>>16)&0x1f)
		{
		case 0: if (!currentMIPS->fpcond) DelayBranchTo(addr); else PC += 4; break;//bc1f
		case 1: if ( currentMIPS->fpcond) DelayBranchTo(addr); else PC += 4; break;//bc1t
		case 2: if (!currentMIPS->fpcond) DelayBranchTo(addr); else SkipLikely(); break;//bc1fl
		case 3: if ( currentMIPS->fpcond) DelayBranchTo(addr); else SkipLikely(); break;//bc1tl
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_JumpType(MIPSOpcode op)
	{
		if (mipsr4k.inDelaySlot)
			ERROR_LOG(Log::CPU, "Jump in delay slot :(");

		u32 off = ((op & 0x03FFFFFF) << 2);
		u32 addr = (currentMIPS->pc & 0xF0000000) | off;

		switch (op>>26)
		{
		case 2: //j
			if (!mipsr4k.inDelaySlot)
				DelayBranchTo(addr);
			break;
		case 3: //jal
			R(MIPS_REG_RA) = PC + 8;
			if (!mipsr4k.inDelaySlot)
				DelayBranchTo(addr);
			break;
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Int_JumpRegType(MIPSOpcode op)
	{
		if (mipsr4k.inDelaySlot)
		{
			// There's one of these in Star Soldier at 0881808c, which seems benign.
			ERROR_LOG(Log::CPU, "Jump in delay slot :(");
		}

		int rs = _RS;
		int rd = _RD;
		u32 addr = R(rs);
		switch (op & 0x3f)
		{
		case 8: //jr
			if (!mipsr4k.inDelaySlot)
				DelayBranchTo(addr);
			break;
		case 9: //jalr
			if (rd != 0)
				R(rd) = PC + 8;
			// Update rd, but otherwise do not take the branch if we're branching.
			if (!mipsr4k.inDelaySlot)
				DelayBranchTo(addr);
			break;
		}
	}

	void Int_IType(MIPSOpcode op)
	{
		u32 uimm = op & 0xFFFF;
		u32 suimm = SignExtend16ToU32(op);
		s32 simm = SignExtend16ToS32(op);

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
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_StoreSync(MIPSOpcode op)
	{
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		u32 addr = R(rs) + imm;

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
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}


	void Int_RType3(MIPSOpcode op)
	{
		int rt = _RT;
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
		case 10: if (R(rt) == 0) R(rd) = R(rs); break; //movz
		case 11: if (R(rt) != 0) R(rd) = R(rs); break; //movn
		case 32: R(rd) = R(rs) + R(rt);		break; //add (exception on overflow)
		case 33: R(rd) = R(rs) + R(rt);		break; //addu
		case 34: R(rd) = R(rs) - R(rt);		break; //sub (exception on overflow)
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
			_dbg_assert_msg_( 0, "Unknown MIPS instruction %08x", op.encoding);
			break;
		}
		PC += 4;
	}


	void Int_ITypeMem(MIPSOpcode op)
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
		case 32: R(rt) = SignExtend8ToU32(Memory::Read_U8(addr)); break; //lb
		case 33: R(rt) = SignExtend16ToU32(Memory::Read_U16(addr)); break; //lh
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
			_dbg_assert_msg_(false,"Trying to interpret Mem instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_FPULS(MIPSOpcode op)
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
			_dbg_assert_msg_(false,"Trying to interpret FPULS instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_mxc1(MIPSOpcode op)
	{
		int fs = _FS;
		int rt = _RT;

		switch ((op>>21)&0x1f) {
		case 0: //mfc1
			if (rt != 0)
				R(rt) = FI(fs);
			break;

		case 2: //cfc1
			if (rt != 0) {
				if (fs == 31) {
					currentMIPS->fcr31 = (currentMIPS->fcr31 & ~(1<<23)) | ((currentMIPS->fpcond & 1)<<23);
					R(rt) = currentMIPS->fcr31;
				} else if (fs == 0) {
					R(rt) = MIPSState::FCR0_VALUE;
				} else {
					WARN_LOG_REPORT(Log::CPU, "ReadFCR: Unexpected reg %d", fs);
					R(rt) = 0;
				}
				break;
			}
			break;

		case 4: //mtc1
			FI(fs) = R(rt);
			break;

		case 6: //ctc1
			{
				u32 value = R(rt);
				if (fs == 31) {
					currentMIPS->fcr31 = value & 0x0181FFFF;
					currentMIPS->fpcond = (value >> 23) & 1;
					// Don't bother locking, assuming the CPU can't be reset now anyway.
					if (MIPSComp::jit) {
						// In case of DISABLE, we need to tell jit we updated FCR31.
						MIPSComp::jit->UpdateFCR31();
					}
				} else {
					WARN_LOG_REPORT(Log::CPU, "WriteFCR: Unexpected reg %d (value %08x)", fs, value);
				}
				DEBUG_LOG(Log::CPU, "FCR%i written to, value %08x", fs, value);
				break;
			}

		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_RType2(MIPSOpcode op)
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
			R(rd) = clz32(R(rs));
			break;
		case 23: //clo
			R(rd) = clz32(~R(rs));
			break;
		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_MulDivType(MIPSOpcode op)
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
		case 16: if (rd != 0) R(rd) = HI; break; //mfhi
		case 17: HI = R(rs); break; //mthi
		case 18: if (rd != 0) R(rd) = LO; break; //mflo
		case 19: LO = R(rs); break; //mtlo
		case 26: //div
			{
				s32 a = (s32)R(rs);
				s32 b = (s32)R(rt);
				if (a == (s32)0x80000000 && b == -1) {
					LO = 0x80000000;
					HI = -1;
				} else if (b != 0) {
					LO = (u32)(a / b);
					HI = (u32)(a % b);
				} else {
					LO = a < 0 ? 1 : -1;
					HI = a;
				}
			}
			break;
		case 27: //divu
			{
				u32 a = R(rs);
				u32 b = R(rt);
				if (b != 0) {
					LO = (a/b);
					HI = (a%b);
				} else {
					LO = a <= 0xFFFF ? 0xFFFF : -1;
					HI = a;
				}
			}
			break;

		default:
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}


	void Int_ShiftType(MIPSOpcode op)
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
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Allegrex(MIPSOpcode op)
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
			R(rd) = SignExtend8ToU32(R(rt));
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
			R(rd) = SignExtend16ToU32(R(rt));
			break;

		default:
			_dbg_assert_msg_(false,"Trying to interpret ALLEGREX instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Allegrex2(MIPSOpcode op)
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
			R(rd) = swap32(R(rt));
			break;
		default:
			_dbg_assert_msg_(false,"Trying to interpret ALLEGREX instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Special2(MIPSOpcode op)
	{
		static int reported = 0;
		switch (op & 0x3F)
		{
		case 36:  // mfic
			// move from interrupt controller, not implemented
			// See related report https://report.ppsspp.org/logs/kind/316 for possible locations.
			// Also see https://forums.ps2dev.org/viewtopic.php?p=32700#p32700 .
			// TODO: Should we actually implement this?
			if (!reported) {
				WARN_LOG(Log::CPU, "MFIC Disable/Enable Interrupt CPU instruction");
				reported = 1;
			}
			break;
		case 38:  // mtic
			// move to interrupt controller, not implemented
			if (!reported) {
				WARN_LOG(Log::CPU, "MTIC Disable/Enable Interrupt CPU instruction");
				reported = 1;
			}
			break;
		}
		PC += 4;
	}

	void Int_Special3(MIPSOpcode op)
	{
		int rs = _RS;
		int rt = _RT;
		int pos = _POS;

		// Don't change $zr.
		if (rt == 0) {
			PC += 4;
			return;
		}

		switch (op & 0x3f) {
		case 0x0: //ext
			{
				int size = _SIZE + 1;
				u32 sourcemask = 0xFFFFFFFFUL >> (32 - size);
				R(rt) = (R(rs) >> pos) & sourcemask;
			}
			break;
		case 0x4: //ins
			{
				int size = (_SIZE + 1) - pos;
				u32 sourcemask = 0xFFFFFFFFUL >> (32 - size);
				u32 destmask = sourcemask << pos;
				R(rt) = (R(rt) & ~destmask) | ((R(rs)&sourcemask) << pos);
			}
			break;
		}

		PC += 4;
	}

	void Int_FPU2op(MIPSOpcode op)
	{
		int fs = _FS;
		int fd = _FD;

		switch (op & 0x3f)
		{
		case 4:	F(fd)	= sqrtf(F(fs)); break; //sqrt
		case 5:	F(fd)	= fabsf(F(fs)); break; //abs
		case 6:	F(fd)	= F(fs); break; //mov
		case 7:	F(fd)	= -F(fs); break; //neg
		case 12:
		case 13:
		case 14:
		case 15:
			if (my_isnanorinf(F(fs)))
			{
				FsI(fd) = my_isinf(F(fs)) && F(fs) < 0.0f ? -2147483648LL : 2147483647LL;
				break;
			}
			switch (op & 0x3f)
			{
			case 12: FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s
			case 13: //trunc.w.s
				if (F(fs) >= 0.0f) {
					FsI(fd) = (int)floorf(F(fs));
					// Overflow, but it was positive.
					if (FsI(fd) == -2147483648LL) {
						FsI(fd) = 2147483647LL;
					}
				} else {
					// Overflow happens to be the right value anyway.
					FsI(fd) = (int)ceilf(F(fs));
				}
				break;
			case 14: FsI(fd) = (int)ceilf (F(fs)); break; //ceil.w.s
			case 15: FsI(fd) = (int)floorf(F(fs)); break; //floor.w.s
			}
			break;
		case 32: F(fd) = (float)FsI(fs); break; //cvt.s.w

		case 36:
			if (my_isnanorinf(F(fs)))
			{
				FsI(fd) = my_isinf(F(fs)) && F(fs) < 0.0f ? -2147483648LL : 2147483647LL;
				break;
			}
			switch (currentMIPS->fcr31 & 3)
			{
			case 0: FsI(fd) = (int)round_ieee_754(F(fs)); break;  // RINT_0
			case 1: FsI(fd) = (int)F(fs); break;  // CAST_1
			case 2: FsI(fd) = (int)ceilf(F(fs)); break;  // CEIL_2
			case 3: FsI(fd) = (int)floorf(F(fs)); break;  // FLOOR_3
			}
			break; //cvt.w.s
		default:
			_dbg_assert_msg_(false,"Trying to interpret FPU2Op instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_FPUComp(MIPSOpcode op)
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
			cond = !my_isnan(F(fs)) && !my_isnan(F(ft)) && (F(fs) == F(ft));
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
			_dbg_assert_msg_(false,"Trying to interpret FPUComp instruction that can't be interpreted");
			cond = false;
			break;
		}
		currentMIPS->fpcond = cond;
		PC += 4;
	}

	void Int_FPU3op(MIPSOpcode op)
	{
		int ft = _FT;
		int fs = _FS;
		int fd = _FD;

		switch (op & 0x3f)
		{
		case 0: F(fd) = F(fs) + F(ft); break; // add.s
		case 1: F(fd) = F(fs) - F(ft); break; // sub.s
		case 2: // mul.s
			if ((my_isinf(F(fs)) && F(ft) == 0.0f) || (my_isinf(F(ft)) && F(fs) == 0.0f)) {
				// Must be positive NAN, see #12519.
				FI(fd) = 0x7fc00000;
			} else {
				F(fd) = F(fs) * F(ft);
			}
			break;
		case 3: F(fd) = F(fs) / F(ft); break; // div.s
		default:
			_dbg_assert_msg_(false,"Trying to interpret FPU3Op instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Interrupt(MIPSOpcode op)
	{
		static int reported = 0;
		switch (op & 1)
		{
		case 0:
			// unlikely to be legitimately used
			if (!reported) {
				Reporting::ReportMessage("INTERRUPT instruction hit (%08x) at %08x", op.encoding, currentMIPS->pc);
				WARN_LOG(Log::CPU, "Disable/Enable Interrupt CPU instruction");
				reported = 1;
			}
			break;
		}
		PC += 4;
	}

	void Int_Emuhack(MIPSOpcode op)
	{
		if (((op >> 24) & 3) != EMUOP_CALL_REPLACEMENT) {
			_dbg_assert_msg_(false, "Trying to interpret emuhack instruction that can't be interpreted");
		}

		_assert_((PC & 3) == 0);

		// It's a replacement func!
		int index = op.encoding & 0xFFFFFF;
		const ReplacementTableEntry *entry = GetReplacementFunc(index);
		if (entry && entry->replaceFunc && (entry->flags & REPFLAG_DISABLED) == 0) {
			int cycles = entry->replaceFunc();

			if (entry->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT)) {
				// Interpret the original instruction under the hook.
				MIPSInterpret(Memory::Read_Instruction(PC, true));
			} else if (cycles < 0) {
				// Leave PC unchanged, call the replacement again (assumes args are modified.)
				currentMIPS->downcount += cycles;
			} else {
				PC = currentMIPS->r[MIPS_REG_RA];
				currentMIPS->downcount -= cycles;
			}
		} else {
			if (!entry || !entry->replaceFunc) {
				ERROR_LOG(Log::CPU, "Bad replacement function index %i", index);
			}
			// Interpret the original instruction under it.
			MIPSInterpret(Memory::Read_Instruction(PC, true));
		}
	}
}
