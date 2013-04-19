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


// Optimization ideas:
//
// It's common to see sequences of stores writing or reading to a contiguous set of
// addresses in function prologues/epilogues:
//  sw s5, 104(sp)
//  sw s4, 100(sp)
//  sw s3, 96(sp)
//  sw s2, 92(sp)
//  sw s1, 88(sp)
//  sw s0, 84(sp)
//  sw ra, 108(sp)
//  mov s4, a0
//  mov s3, a1
//  ...
// Such sequences could easily be detected and turned into nice contiguous
// sequences of ARM stores instead of the current 3 instructions per sw/lw.
//
// Also, if we kept track of the likely register content of a cached register,
// (pointer or data), we could avoid many BIC instructions.


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

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
	void Jit::SetR0ToEffectiveAddress(int rs, s16 offset) {
		Operand2 op2;
		if (offset) {
			bool negated;
			if (TryMakeOperand2_AllowNegation(offset, op2, &negated)) {
				if (!negated)
					ADD(R0, gpr.R(rs), op2);
				else
					SUB(R0, gpr.R(rs), op2);
			} else {
				// Try to avoid using MOVT
				if (offset < 0) {
					MOVI2R(R0, (u32)(-offset));
					SUB(R0, gpr.R(rs), R0);
				} else {
					MOVI2R(R0, (u32)offset);
					ADD(R0, gpr.R(rs), R0);
				}
			}
			BIC(R0, R0, Operand2(0xC0, 4));   // &= 0x3FFFFFFF
		} else {
			BIC(R0, gpr.R(rs), Operand2(0xC0, 4));   // &= 0x3FFFFFFF
		}
	}

	void Jit::SetCCAndR0ForSafeAddress(int rs, s16 offset, ARMReg tempReg) {
		SetR0ToEffectiveAddress(rs, offset);

		// There are three valid ranges.  Each one gets a bit.
		const u32 BIT_SCRATCH = 1, BIT_RAM = 2, BIT_VRAM = 4;
		MOVI2R(tempReg, BIT_SCRATCH | BIT_RAM | BIT_VRAM);

		CMP(R0, AssumeMakeOperand2(PSP_GetScratchpadMemoryBase()));
		SetCC(CC_LO);
		BIC(tempReg, tempReg, BIT_SCRATCH);
		SetCC(CC_HS);
		CMP(R0, AssumeMakeOperand2(PSP_GetScratchpadMemoryEnd()));
		BIC(tempReg, tempReg, BIT_SCRATCH);

		// If it was in that range, later compares don't matter.
		CMP(R0, AssumeMakeOperand2(PSP_GetVidMemBase()));
		SetCC(CC_LO);
		BIC(tempReg, tempReg, BIT_VRAM);
		SetCC(CC_HS);
		CMP(R0, AssumeMakeOperand2(PSP_GetVidMemEnd()));
		BIC(tempReg, tempReg, BIT_VRAM);

		CMP(R0, AssumeMakeOperand2(PSP_GetKernelMemoryBase()));
		SetCC(CC_LO);
		BIC(tempReg, tempReg, BIT_RAM);
		SetCC(CC_HS);
		CMP(R0, AssumeMakeOperand2(PSP_GetUserMemoryEnd()));
		BIC(tempReg, tempReg, BIT_RAM);

		// If we left any bit set, the address is OK.
		SetCC(CC_AL);
		CMP(tempReg, 0);
		SetCC(CC_GT);
	}

	void Jit::Comp_ITypeMem(u32 op)
	{
		CONDITIONAL_DISABLE;
		int offset = (signed short)(op&0xFFFF);
		bool load = false;
		int rt = _RT;
		int rs = _RS;
		int o = op>>26;
		if (((op >> 29) & 1) == 0 && rt == 0) {
			// Don't load anything into $zr
			return;
		}

		u32 iaddr = gpr.IsImm(rs) ? offset + gpr.GetImm(rs) : 0xFFFFFFFF;
		bool doCheck = false;

		switch (o)
		{
		case 32: //lb
		case 33: //lh
		case 35: //lw
		case 36: //lbu
		case 37: //lhu
			load = true;
		case 40: //sb
		case 41: //sh
		case 43: //sw
			if (gpr.IsImm(rs) && Memory::IsValidAddress(iaddr)) {
				// We can compute the full address at compile time. Kickass.
				u32 addr = iaddr & 0x3FFFFFFF;
				// Must be OK even if rs == rt since we have the value from imm already.
				gpr.MapReg(rt, load ? MAP_NOINIT | MAP_DIRTY : 0);
				MOVI2R(R0, addr);
			} else {
				_dbg_assert_msg_(JIT, !gpr.IsImm(rs), "Invalid immediate address?  CPU bug?");
				load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);

				if (!g_Config.bFastMemory) {
					SetCCAndR0ForSafeAddress(rs, offset, R1);
					doCheck = true;
				} else {
					SetR0ToEffectiveAddress(rs, offset);
				}
			}
			switch (o)
			{
			// Load
			case 35: LDR  (gpr.R(rt), R11, R0); break;
			case 37: LDRH (gpr.R(rt), R11, R0); break;
			case 33: LDRSH(gpr.R(rt), R11, R0); break;
			case 36: LDRB (gpr.R(rt), R11, R0); break;
			case 32: LDRSB(gpr.R(rt), R11, R0); break;
			// Store
			case 43: STR  (gpr.R(rt), R11, R0); break;
			case 41: STRH (gpr.R(rt), R11, R0); break;
			case 40: STRB (gpr.R(rt), R11, R0); break;
			}
			if (doCheck) {
				if (load) {
					SetCC(CC_EQ);
					MOVI2R(gpr.R(rt), 0);
				}
				SetCC(CC_AL);
			}
			break;
		case 34: //lwl
		case 38: //lwr
			load = true;
		case 42: //swl
		case 46: //swr
			if (!js.inDelaySlot) {
				// Optimisation: Combine to single unaligned load/store
				bool isLeft = (o == 34 || o == 42);
				u32 nextOp = Memory::Read_Instruction(js.compilerPC + 4);
				// Find a matching shift in opposite direction with opposite offset.
				if (nextOp == (isLeft ? (op + (4<<26) - 3)
				                      : (op - (4<<26) + 3)))
				{
					EatInstruction(nextOp);
					nextOp = ((load ? 35 : 43) << 26) | ((isLeft ? nextOp : op) & 0x3FFFFFF); //lw, sw
					Comp_ITypeMem(nextOp);
					return;
				}
			}

			DISABLE; // Disabled until crashes are resolved.
			if (g_Config.bFastMemory) {
				int shift;
				if (gpr.IsImm(rs)) {
					u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
					shift = (addr & 3) << 3;
					addr &= 0xfffffffc;
					load ? gpr.MapReg(rt, MAP_DIRTY) : gpr.MapReg(rt, 0);
					MOVI2R(R0, addr);
				} else {
					load ? gpr.MapDirtyIn(rt, rs, false) : gpr.MapInIn(rt, rs);
					shift = (offset & 3) << 3; // Should be addr. Difficult as we don't know it yet.
					offset &= 0xfffffffc;
					SetR0ToEffectiveAddress(rs, offset);
				}
				switch (o)
				{
				// Load
				case 34:
					AND(gpr.R(rt), gpr.R(rt), 0x00ffffff >> shift);
					LDR(R0, R11, R0);
					ORR(gpr.R(rt), gpr.R(rt), Operand2(R0, ST_LSL, 24 - shift));
					break;
				case 38:
					AND(gpr.R(rt), gpr.R(rt), 0xffffff00 << (24 - shift));
					LDR(R0, R11, R0);
					ORR(gpr.R(rt), gpr.R(rt), Operand2(R0, ST_LSR, shift));
					break;
				// Store
				case 42:
					LDR(R1, R11, R0);
					AND(R1, R1, 0xffffff00 << shift);
					ORR(R1, R1, Operand2(gpr.R(rt), ST_LSR, 24 - shift));
					STR(R1, R11, R0);
					break;
				case 46:
					LDR(R1, R11, R0);
					AND(R1, R1, 0x00ffffff >> (24 - shift));
					ORR(R1, R1, Operand2(gpr.R(rt), ST_LSL, shift));
					STR(R1, R11, R0);
					break;
				}
			} else {
				Comp_Generic(op);
				return;
			}
			break;
		default:
			Comp_Generic(op);
			return ;
		}

	}
}
