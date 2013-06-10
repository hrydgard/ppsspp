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

#include "ArmJit.h"
#include "Common/CPUDetect.h"

using namespace MIPSAnalyst;
#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _SA ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

//#define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
	static u32 EvalOr(u32 a, u32 b) { return a | b; }
	static u32 EvalEor(u32 a, u32 b) { return a ^ b; }
	static u32 EvalAnd(u32 a, u32 b) { return a & b; }
	static u32 EvalAdd(u32 a, u32 b) { return a + b; }
	static u32 EvalSub(u32 a, u32 b) { return a - b; }

	void Jit::CompImmLogic(int rs, int rt, u32 uimm, void (ARMXEmitter::*arith)(ARMReg dst, ARMReg src, Operand2 op2), u32 (*eval)(u32 a, u32 b))
	{
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, (*eval)(gpr.GetImm(rs), uimm));
		} else {
			gpr.MapDirtyIn(rt, rs);
			// TODO: Special case when uimm can be represented as an Operand2
			Operand2 op2;
			if (TryMakeOperand2(uimm, op2)) {
				(this->*arith)(gpr.R(rt), gpr.R(rs), op2);
			} else {
				MOVI2R(R0, (u32)uimm);
				(this->*arith)(gpr.R(rt), gpr.R(rs), R0);
			}
		}
	}

	void Jit::Comp_IType(u32 op)
	{
		CONDITIONAL_DISABLE;
		s32 simm = (s32)(s16)(op & 0xFFFF);  // sign extension
		u32 uimm = op & 0xFFFF;
		u32 suimm = (u32)(s32)simm;

		int rt = _RT;
		int rs = _RS;

		// noop, won't write to ZERO.
		if (rt == 0)
			return;

		switch (op >> 26) 
		{
		case 8:	// same as addiu?
		case 9:	// R(rt) = R(rs) + simm; break;	//addiu
			{
				if (gpr.IsImm(rs)) {
					gpr.SetImm(rt, gpr.GetImm(rs) + simm);
				} else {
					gpr.MapDirtyIn(rt, rs);
					ADDI2R(gpr.R(rt), gpr.R(rs), simm, R0);
				}
				break;
			}

		case 12: CompImmLogic(rs, rt, uimm, &ARMXEmitter::AND, &EvalAnd); break;
		case 13: CompImmLogic(rs, rt, uimm, &ARMXEmitter::ORR, &EvalOr); break;
		case 14: CompImmLogic(rs, rt, uimm, &ARMXEmitter::EOR, &EvalEor); break;

		case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
			{
				gpr.MapDirtyIn(rt, rs);
				CMPI2R(gpr.R(rs), simm, R0);
				SetCC(CC_LT);
				MOVI2R(gpr.R(rt), 1);
				SetCC(CC_GE);
				MOVI2R(gpr.R(rt), 0);
				SetCC(CC_AL);
			}
			break;

		case 11: // R(rt) = R(rs) < uimm; break; //sltiu
			{
				gpr.MapDirtyIn(rt, rs);
				CMPI2R(gpr.R(rs), suimm, R0);
				SetCC(CC_LO);
				MOVI2R(gpr.R(rt), 1);
				SetCC(CC_HS);
				MOVI2R(gpr.R(rt), 0);
				SetCC(CC_AL);
			}
			break;

		case 15: // R(rt) = uimm << 16;	 //lui
			gpr.SetImm(rt, uimm << 16);
			break;

		default:
			Comp_Generic(op);
			break;
		}
	}

	void Jit::Comp_RType2(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rs = _RS;
		int rd = _RD;

		// Don't change $zr.
		if (rd == 0)
			return;

		switch (op & 63)
		{
		case 22: //clz
			gpr.MapDirtyIn(rd, rs);
			CLZ(gpr.R(rd), gpr.R(rs));
			break;
		case 23: //clo
			gpr.MapDirtyIn(rd, rs);
			MVN(R0, gpr.R(rs));
			CLZ(gpr.R(rd), R0);
			break;
		default:
			DISABLE;
		}
	}

	void Jit::CompType3(int rd, int rs, int rt, void (ARMXEmitter::*arith)(ARMReg dst, ARMReg rm, Operand2 rn), u32 (*eval)(u32 a, u32 b), bool isSub)
	{
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, (*eval)(gpr.GetImm(rs), gpr.GetImm(rt)));
		} else if (gpr.IsImm(rt)) {
			u32 rtImm = gpr.GetImm(rt);
			gpr.MapDirtyIn(rd, rs);
			Operand2 op2;
			if (TryMakeOperand2(rtImm, op2)) {
				(this->*arith)(gpr.R(rd), gpr.R(rs), op2);
			} else {
				MOVI2R(R0, rtImm);
				(this->*arith)(gpr.R(rd), gpr.R(rs), R0);
			}
		} else if (gpr.IsImm(rs)) {
			u32 rsImm = gpr.GetImm(rs);
			gpr.MapDirtyIn(rd, rt);
			// TODO: Special case when rsImm can be represented as an Operand2
			MOVI2R(R0, rsImm);
			(this->*arith)(gpr.R(rd), R0, gpr.R(rt));
		} else {
			// Generic solution
			gpr.MapDirtyInIn(rd, rs, rt);
			(this->*arith)(gpr.R(rd), gpr.R(rs), gpr.R(rt));
		}
	}

	void Jit::Comp_RType3(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		// noop, won't write to ZERO.
		if (rd == 0)
			return;

		switch (op & 63) 
		{
		case 10: //if (!R(rt)) R(rd) = R(rs);       break; //movz
			if (rd == rs)
				break;
			if (!gpr.IsImm(rt))
			{
				gpr.MapDirtyInIn(rd, rt, rs, false);
				CMP(gpr.R(rt), Operand2(0));
				SetCC(CC_EQ);
				MOV(gpr.R(rd), Operand2(gpr.R(rs)));
				SetCC(CC_AL);
			}
			else if (gpr.GetImm(rt) == 0)
			{
				// Yes, this actually happens.
				if (gpr.IsImm(rs))
					gpr.SetImm(rd, gpr.GetImm(rs));
				else
				{
					gpr.MapDirtyIn(rd, rs);
					MOV(gpr.R(rd), Operand2(gpr.R(rs)));
				}
			}
			break;
		case 11:// if (R(rt)) R(rd) = R(rs);		break; //movn
			if (rd == rs)
				break;
			if (!gpr.IsImm(rt))
			{
				gpr.MapDirtyInIn(rd, rt, rs, false);
				CMP(gpr.R(rt), Operand2(0));
				SetCC(CC_NEQ);
				MOV(gpr.R(rd), Operand2(gpr.R(rs)));
				SetCC(CC_AL);
			}
			else if (gpr.GetImm(rt) != 0)
			{
				// Yes, this actually happens.
				if (gpr.IsImm(rs))
					gpr.SetImm(rd, gpr.GetImm(rs));
				else
				{
					gpr.MapDirtyIn(rd, rs);
					MOV(gpr.R(rd), Operand2(gpr.R(rs)));
				}
			}
			break;
			
		case 32: //R(rd) = R(rs) + R(rt);           break; //add
		case 33: //R(rd) = R(rs) + R(rt);           break; //addu
			// Some optimized special cases
			if (gpr.IsImm(rs) && gpr.GetImm(rs) == 0) {
				gpr.MapDirtyIn(rd, rt);
				MOV(gpr.R(rd), gpr.R(rt));
			} else if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0) {
				gpr.MapDirtyIn(rd, rs);
				MOV(gpr.R(rd), gpr.R(rs));
			} else {
				CompType3(rd, rs, rt, &ARMXEmitter::ADD, &EvalAdd);
			}
			break;

		case 34: //R(rd) = R(rs) - R(rt);           break; //sub
		case 35: //R(rd) = R(rs) - R(rt);           break; //subu
			CompType3(rd, rs, rt, &ARMXEmitter::SUB, &EvalSub, true);
			break;
		case 36: //R(rd) = R(rs) & R(rt);           break; //and
			CompType3(rd, rs, rt, &ARMXEmitter::AND, &EvalAnd);
			break;
		case 37: //R(rd) = R(rs) | R(rt);           break; //or
			CompType3(rd, rs, rt, &ARMXEmitter::ORR, &EvalOr);
			break;
		case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
			CompType3(rd, rs, rt, &ARMXEmitter::EOR, &EvalEor);
			break;

		case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
			if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0) {
				gpr.MapDirtyIn(rd, rs);
				MVN(gpr.R(rd), gpr.R(rs));
			} else {
				gpr.MapDirtyInIn(rd, rs, rt);
				ORR(gpr.R(rd), gpr.R(rs), gpr.R(rt));
				MVN(gpr.R(rd), gpr.R(rd));
			}
			break;

		case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			SetCC(CC_LT);
			MOVI2R(gpr.R(rd), 1);
			SetCC(CC_GE);
			MOVI2R(gpr.R(rd), 0);
			SetCC(CC_AL);
			break; 

		case 43: //R(rd) = R(rs) < R(rt);           break; //sltu
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			SetCC(CC_LO);
			MOVI2R(gpr.R(rd), 1);
			SetCC(CC_HS);
			MOVI2R(gpr.R(rd), 0);
			SetCC(CC_AL);
			break;

		case 44: //R(rd) = max(R(rs), R(rt);        break; //max
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			SetCC(CC_GT);
			MOV(gpr.R(rd), gpr.R(rs));
			SetCC(CC_LE);
			MOV(gpr.R(rd), gpr.R(rt));
			SetCC(CC_AL);
			break;

		case 45: //R(rd) = min(R(rs), R(rt));       break; //min
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			SetCC(CC_LT);
			MOV(gpr.R(rd), gpr.R(rs));
			SetCC(CC_GE);
			MOV(gpr.R(rd), gpr.R(rt));
			SetCC(CC_AL);
			break;

		default:
			Comp_Generic(op);
			break;
		}
	}

	void Jit::CompShiftImm(u32 op, ArmGen::ShiftType shiftType)
	{
		int rd = _RD;
		int rt = _RT;
		int sa = _SA;
		
		gpr.MapDirtyIn(rd, rt);
		MOV(gpr.R(rd), Operand2(gpr.R(rt), shiftType, sa));
	}

	void Jit::CompShiftVar(u32 op, ArmGen::ShiftType shiftType)
	{
		int rd = _RD;
		int rt = _RT;
		int rs = _RS;
		if (gpr.IsImm(rs))
		{
			int sa = gpr.GetImm(rs) & 0x1F;
			gpr.MapDirtyIn(rd, rt);
			MOV(gpr.R(rd), Operand2(gpr.R(rt), shiftType, sa));
			return;
		}
		gpr.MapDirtyInIn(rd, rs, rt);
		AND(R0, gpr.R(rs), Operand2(0x1F));
		MOV(gpr.R(rd), Operand2(gpr.R(rt), shiftType, R0));
	}

	void Jit::Comp_ShiftType(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rs = _RS;
		int rd = _RD;
		int fd = _FD;

		// noop, won't write to ZERO.
		if (rd == 0)
			return;

		// WARNING : ROTR
		switch (op & 0x3f)
		{
		case 0: CompShiftImm(op, ST_LSL); break; //sll
		case 2: CompShiftImm(op, rs == 1 ? ST_ROR : ST_LSR); break;	//srl
		case 3: CompShiftImm(op, ST_ASR); break; //sra
		case 4: CompShiftVar(op, ST_LSL); break; //sllv
		case 6: CompShiftVar(op, fd == 1 ? ST_ROR : ST_LSR); break; //srlv
		case 7: CompShiftVar(op, ST_ASR); break; //srav
		
		default:
			Comp_Generic(op);
			break;
		}
	}

	void Jit::Comp_Special3(u32 op)
	{
		CONDITIONAL_DISABLE;

		int rs = _RS;
		int rt = _RT;

		int pos = _POS;
		int size = _SIZE + 1;
		u32 mask = 0xFFFFFFFFUL >> (32 - size);

		// Don't change $zr.
		if (rt == 0)
			return;

		switch (op & 0x3f)
		{
		case 0x0: //ext
			if (gpr.IsImm(rs))
			{
				gpr.SetImm(rt, (gpr.GetImm(rs) >> pos) & mask);
				return;
			}

			gpr.MapDirtyIn(rt, rs);
			if (cpu_info.bArmV7) {
				UBFX(gpr.R(rt), gpr.R(rs), pos, size);
			} else {
				MOV(gpr.R(rt), Operand2(gpr.R(rs), ST_LSR, pos));
				ANDI2R(gpr.R(rt), gpr.R(rt), mask, R0);
			}
			break;

		case 0x4: //ins
			{
				u32 sourcemask = mask >> pos;
				u32 destmask = ~(sourcemask << pos);
				if (gpr.IsImm(rs))
				{
					u32 inserted = (gpr.GetImm(rs) & sourcemask) << pos;
					if (gpr.IsImm(rt))
					{
						gpr.SetImm(rt, (gpr.GetImm(rt) & destmask) | inserted);
						return;
					}

					gpr.MapReg(rt, MAP_DIRTY);
					ANDI2R(gpr.R(rt), gpr.R(rt), destmask, R0);
					ORI2R(gpr.R(rt), gpr.R(rt), inserted, R0);
				}
				else
				{
					if (cpu_info.bArmV7) {
						gpr.MapDirtyIn(rt, rs, false);
						BFI(gpr.R(rt), gpr.R(rs), pos, size-pos);
					} else {
						gpr.MapDirtyIn(rt, rs, false);
						ANDI2R(R0, gpr.R(rs), sourcemask, R1);
						MOV(R0, Operand2(R0, ST_LSL, pos));
						ANDI2R(gpr.R(rt), gpr.R(rt), destmask, R1);
						ORR(gpr.R(rt), gpr.R(rt), R0);
					}
				}
			}
			break;
		}
	}

	void Jit::Comp_Allegrex(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rt = _RT;
		int rd = _RD;
		// Don't change $zr.
		if (rd == 0)
			return;

		switch ((op >> 6) & 31)
		{
		case 16: // seb	// R(rd) = (u32)(s32)(s8)(u8)R(rt);
			if (gpr.IsImm(rt))
			{
				gpr.SetImm(rd, (s32)(s8)(u8)gpr.GetImm(rt));
				return;
			}
			gpr.MapDirtyIn(rd, rt);
			SXTB(gpr.R(rd), gpr.R(rt));
			break;

		case 24: // seh
			if (gpr.IsImm(rt))
			{
				gpr.SetImm(rd, (s32)(s16)(u16)gpr.GetImm(rt));
				return;
			}
			gpr.MapDirtyIn(rd, rt);
			SXTH(gpr.R(rd), gpr.R(rt));
			break;
		
		case 20: //bitrev
			if (gpr.IsImm(rt))
			{
				// http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel
				u32 v = gpr.GetImm(rt);
				v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) <<  1); //   odd<->even
				v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) <<  2); //  pair<->pair
				v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) <<  4); //  nibb<->nibb
				v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) <<  8); //  byte<->byte
				v = ( v >> 16             ) | ( v               << 16); // hword<->hword
				gpr.SetImm(rd, v);
				return;
			}

			if (cpu_info.bArmV7) {
				gpr.MapDirtyIn(rd, rt);
				RBIT(gpr.R(rd), gpr.R(rt));
			} else {
				Comp_Generic(op);
			}
			break;
		default:
			Comp_Generic(op);
			return;
		}
	}

	void Jit::Comp_Allegrex2(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rt = _RT;
		int rd = _RD;
		// Don't change $zr.
		if (rd == 0)
			return;

		switch (op & 0x3ff)
		{
		case 0xA0: //wsbh
			if (cpu_info.bArmV7) {
				gpr.MapDirtyIn(rd, rt);
				REV16(gpr.R(rd), gpr.R(rt));
			} else {
				Comp_Generic(op);
			}
			break;
		case 0xE0: //wsbw
			if (cpu_info.bArmV7) {
				gpr.MapDirtyIn(rd, rt);
				REV(gpr.R(rd), gpr.R(rt));
			} else {
				Comp_Generic(op);
			}
			break;
		default:
			Comp_Generic(op);
			break;
		}
	}

	void Jit::Comp_MulDivType(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		switch (op & 63) 
		{
		case 16: // R(rd) = HI; //mfhi
			gpr.MapDirtyIn(rd, MIPSREG_HI);
			MOV(gpr.R(rd), gpr.R(MIPSREG_HI));
			break; 

		case 17: // HI = R(rs); //mthi
			gpr.MapDirtyIn(MIPSREG_HI, rs);
			MOV(gpr.R(MIPSREG_HI), gpr.R(rs));
			break; 

		case 18: // R(rd) = LO; break; //mflo
			gpr.MapDirtyIn(rd, MIPSREG_LO);
			MOV(gpr.R(rd), gpr.R(MIPSREG_LO));
			break;

		case 19: // LO = R(rs); break; //mtlo
			gpr.MapDirtyIn(MIPSREG_LO, rs);
			MOV(gpr.R(MIPSREG_LO), gpr.R(rs));
			break; 

		case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
			SMULL(gpr.R(MIPSREG_LO), gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
			UMULL(gpr.R(MIPSREG_LO), gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 26: //div
			if (cpu_info.bIDIVa)
			{
				gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
				SDIV(gpr.R(MIPSREG_LO), gpr.R(rs), gpr.R(rt));
				MUL(R0, gpr.R(rt), gpr.R(MIPSREG_LO));
				SUB(gpr.R(MIPSREG_HI), gpr.R(rs), Operand2(R0));
			} else {
				DISABLE;
			}
			break;

		case 27: //divu
			if (cpu_info.bIDIVa)
			{
				gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
				UDIV(gpr.R(MIPSREG_LO), gpr.R(rs), gpr.R(rt));
				MUL(R0, gpr.R(rt), gpr.R(MIPSREG_LO));
				SUB(gpr.R(MIPSREG_HI), gpr.R(rs), Operand2(R0));
			} else {
				DISABLE;
			}
			break;

		case 28: //madd
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
			SMLAL(gpr.R(MIPSREG_LO), gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 29: //maddu
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
			UMLAL(gpr.R(MIPSREG_LO), gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 46: // msub
			DISABLE;
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
			break;

		case 47: // msubu
			DISABLE;
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
			break;

		default:
			DISABLE;
		}
	}

}
