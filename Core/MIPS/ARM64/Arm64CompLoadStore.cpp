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


#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
	using namespace Arm64Gen;
	using namespace Arm64JitConstants;
	
	// Destroys SCRATCH2
	void Arm64Jit::SetScratch1ToEffectiveAddress(MIPSGPReg rs, s16 offset) {
		if (offset) {
			ADDI2R(SCRATCH1, gpr.R(rs), offset, SCRATCH2);
		} else {
			MOV(SCRATCH1, gpr.R(rs));
		}
	}

	void Arm64Jit::SetCCAndSCRATCH1ForSafeAddress(MIPSGPReg rs, s16 offset, ARM64Reg tempReg, bool reverse) {
		SetScratch1ToEffectiveAddress(rs, offset);
		// TODO
	}

	void Arm64Jit::Comp_ITypeMemLR(MIPSOpcode op, bool load) {
		CONDITIONAL_DISABLE;
		int offset = (signed short)(op & 0xFFFF);
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;

		if (!js.inDelaySlot) {
			// Optimisation: Combine to single unaligned load/store
			bool isLeft = (o == 34 || o == 42);
			MIPSOpcode nextOp = Memory::Read_Instruction(js.compilerPC + 4);
			// Find a matching shift in opposite direction with opposite offset.
			if (nextOp == (isLeft ? (op.encoding + (4 << 26) - 3)
				: (op.encoding - (4 << 26) + 3))) {
				EatInstruction(nextOp);
				nextOp = MIPSOpcode(((load ? 35 : 43) << 26) | ((isLeft ? nextOp : op) & 0x03FFFFFF)); //lw, sw
				Comp_ITypeMem(nextOp);
				return;
			}
		}

		u32 iaddr = gpr.IsImm(rs) ? offset + gpr.GetImm(rs) : 0xFFFFFFFF;
		bool doCheck = false;
		FixupBranch skip;

		if (gpr.IsImm(rs) && Memory::IsValidAddress(iaddr)) {
			u32 addr = iaddr & 0x3FFFFFFF;
			// Need to initialize since this only loads part of the register.
			// But rs no longer matters (even if rs == rt) since we have the address.
			gpr.MapReg(rt, load ? MAP_DIRTY : 0);
			gpr.SetRegImm(SCRATCH1, addr & ~3);

			u8 shift = (addr & 3) * 8;

			switch (o) {
			case 34: // lwl
				LDR(SCRATCH1, MEMBASEREG, SCRATCH1);
				ANDI2R(gpr.R(rt), gpr.R(rt), 0x00ffffff >> shift, SCRATCH2);
				ORR(gpr.R(rt), gpr.R(rt), SCRATCH1, ArithOption(gpr.R(rt), ST_LSL, 24 - shift));
				break;

			case 38: // lwr
				LDR(SCRATCH1, MEMBASEREG, SCRATCH1);
				ANDI2R(gpr.R(rt), gpr.R(rt), 0xffffff00 << (24 - shift), SCRATCH2);
				ORR(gpr.R(rt), gpr.R(rt), SCRATCH1, ArithOption(gpr.R(rt), ST_LSR, shift));
				break;

			case 42: // swl
				LDR(SCRATCH2, MEMBASEREG, SCRATCH1);
				// Don't worry, can't use temporary.
				ANDI2R(SCRATCH2, SCRATCH2, 0xffffff00 << shift, SCRATCH1);
				ORR(SCRATCH2, SCRATCH2, SCRATCH2, ArithOption(gpr.R(rt), ST_LSR, 24 - shift));
				STR(SCRATCH2, MEMBASEREG, SCRATCH1);
				break;

			case 46: // swr
				LDR(SCRATCH2, MEMBASEREG, SCRATCH1);
				// Don't worry, can't use temporary.
				ANDI2R(SCRATCH2, SCRATCH2, 0x00ffffff >> (24 - shift), SCRATCH1);
				ORR(SCRATCH2, SCRATCH2, SCRATCH2, ArithOption(gpr.R(rt), ST_LSL, shift));
				STR(SCRATCH2, MEMBASEREG, SCRATCH1);
				break;
			}
			return;
		}

		/*
		_dbg_assert_msg_(JIT, !gpr.IsImm(rs), "Invalid immediate address?  CPU bug?");
		load ? gpr.MapDirtyIn(rt, rs, false) : gpr.MapInIn(rt, rs);

		if (!g_Config.bFastMemory && rs != MIPS_REG_SP) {
			SetCCAndSCRATCH1ForSafeAddress(rs, offset, SCRATCHREG2, true);
			doCheck = true;
		} else {
			SetScratch1ToEffectiveAddress(rs, offset);
		}
		if (doCheck) {
			skip = B();
		}
		SetCC(CC_AL);

		// Need temp regs.  TODO: Get from the regcache?
		static const ARM64Reg LR_SCRATCHREG3 = R9;
		static const ARM64Reg LR_SCRATCHREG4 = R10;
		if (load) {
			PUSH(1, LR_SCRATCHREG3);
		} else {
			PUSH(2, LR_SCRATCHREG3, LR_SCRATCHREG4);
		}

		// Here's our shift amount.
		AND(SCRATCHREG2, R0, 3);
		LSL(SCRATCHREG2, SCRATCHREG2, 3);

		// Now align the address for the actual read.
		BIC(R0, R0, 3);

		switch (o) {
		case 34: // lwl
			MOVI2R(LR_SCRATCHREG3, 0x00ffffff);
			LDR(R0, MEMBASEREG, R0);
			AND(gpr.R(rt), gpr.R(rt), Operand2(LR_SCRATCHREG3, ST_LSR, SCRATCHREG2));
			RSB(SCRATCHREG2, SCRATCHREG2, 24);
			ORR(gpr.R(rt), gpr.R(rt), Operand2(R0, ST_LSL, SCRATCHREG2));
			break;

		case 38: // lwr
			MOVI2R(LR_SCRATCHREG3, 0xffffff00);
			LDR(R0, MEMBASEREG, R0);
			LSR(R0, R0, SCRATCHREG2);
			RSB(SCRATCHREG2, SCRATCHREG2, 24);
			AND(gpr.R(rt), gpr.R(rt), Operand2(LR_SCRATCHREG3, ST_LSL, SCRATCHREG2));
			ORR(gpr.R(rt), gpr.R(rt), R0);
			break;

		case 42: // swl
			MOVI2R(LR_SCRATCHREG3, 0xffffff00);
			LDR(LR_SCRATCHREG4, MEMBASEREG, R0);
			AND(LR_SCRATCHREG4, LR_SCRATCHREG4, Operand2(LR_SCRATCHREG3, ST_LSL, SCRATCHREG2));
			RSB(SCRATCHREG2, SCRATCHREG2, 24);
			ORR(LR_SCRATCHREG4, LR_SCRATCHREG4, Operand2(gpr.R(rt), ST_LSR, SCRATCHREG2));
			STR(LR_SCRATCHREG4, MEMBASEREG, R0);
			break;

		case 46: // swr
			MOVI2R(LR_SCRATCHREG3, 0x00ffffff);
			LDR(LR_SCRATCHREG4, MEMBASEREG, R0);
			RSB(SCRATCHREG2, SCRATCHREG2, 24);
			AND(LR_SCRATCHREG4, LR_SCRATCHREG4, Operand2(LR_SCRATCHREG3, ST_LSR, SCRATCHREG2));
			RSB(SCRATCHREG2, SCRATCHREG2, 24);
			ORR(LR_SCRATCHREG4, LR_SCRATCHREG4, Operand2(gpr.R(rt), ST_LSL, SCRATCHREG2));
			STR(LR_SCRATCHREG4, MEMBASEREG, R0);
			break;
		}

		if (load) {
			POP(1, LR_SCRATCHREG3);
		} else {
			POP(2, LR_SCRATCHREG3, LR_SCRATCHREG4);
		}

		if (doCheck) {
			SetJumpTarget(skip);
		}*/
	}

	void Arm64Jit::Comp_ITypeMem(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		int offset = (signed short)(op & 0xFFFF);
		bool load = false;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int o = op >> 26;
		if (((op >> 29) & 1) == 0 && rt == MIPS_REG_ZERO) {
			// Don't load anything into $zr
			return;
		}

		u32 iaddr = gpr.IsImm(rs) ? offset + gpr.GetImm(rs) : 0xFFFFFFFF;
		bool doCheck = false;
		ARM64Reg addrReg = SCRATCH1;

		switch (o) {
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
				// TODO: Avoid mapping a register for the "zero" register, use R0 instead.

				// We can compute the full address at compile time. Kickass.
				u32 addr = iaddr & 0x3FFFFFFF;

				if (addr == iaddr && offset == 0) {
					// It was already safe.  Let's shove it into a reg and use it directly.
					load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);
					addrReg = gpr.R(rs);
				} else {
					// In this case, only map rt. rs+offset will be in R0.
					gpr.MapReg(rt, load ? MAP_NOINIT : 0);
					gpr.SetRegImm(SCRATCH1, addr);
					addrReg = SCRATCH1;
				}
			} else {
				_dbg_assert_msg_(JIT, !gpr.IsImm(rs), "Invalid immediate address?  CPU bug?");
				_dbg_assert_msg_(JIT, g_Config.bFastMemory, "Slow mem doesn't work yet in ARM64! Turn on Fast Memory in system settings");
				load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);

				if (!g_Config.bFastMemory && rs != MIPS_REG_SP) {
					// TODO: This doesn't work!
					SetCCAndSCRATCH1ForSafeAddress(rs, offset, SCRATCH2);
					doCheck = true;
				} else {
					SetScratch1ToEffectiveAddress(rs, offset);
				}
				addrReg = SCRATCH1;
			}

			switch (o) {
				// Load
			case 35: LDR(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 37: LDRH(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 33: LDRSH(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 36: LDRB(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 32: LDRSB(gpr.R(rt), MEMBASEREG, addrReg); break;
				// Store
			case 43: STR(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 41: STRH(gpr.R(rt), MEMBASEREG, addrReg); break;
			case 40: STRB(gpr.R(rt), MEMBASEREG, addrReg); break;
			}
			/*
			if (doCheck) {
				if (load) {
					SetCC(CC_EQ);
					MOVI2R(gpr.R(rt), 0);
				}
				SetCC(CC_AL);
			}*/
			break;
		case 34: //lwl
		case 38: //lwr
			load = true;
		case 42: //swl
		case 46: //swr
			DISABLE;
			// Comp_ITypeMemLR(op, load);
			break;
		default:
			Comp_Generic(op);
			return;
		}
	}

	void Arm64Jit::Comp_Cache(MIPSOpcode op) {
		DISABLE;
	}
}
