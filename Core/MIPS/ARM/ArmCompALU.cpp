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
	static u32 EvalXor(u32 a, u32 b) { return a ^ b; }
	static u32 EvalAnd(u32 a, u32 b) { return a & b; }

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

		switch (op >> 26) 
		{
		case 8:	// same as addiu?
		case 9:	// R(rt) = R(rs) + simm; break;	//addiu
			{
				if (gpr.IsImm(rs)) {
					gpr.SetImm(rt, gpr.GetImm(rs) + simm);
				} else if (rs == 0) {  // add to zero register = immediate
					gpr.SetImm(rt, (u32)simm);
				} else {
					gpr.MapDirtyIn(rt, rs);
					Operand2 op2;
					bool negated;
					if (TryMakeOperand2_AllowNegation(simm, op2, &negated)) {
						if (!negated)
							ADD(gpr.R(rt), gpr.R(rs), op2);
						else
							SUB(gpr.R(rt), gpr.R(rs), op2);
					} else {
						MOVI2R(R0, (u32)simm);
						ADD(gpr.R(rt), gpr.R(rs), R0);
					}
				}
				break;
			}

		case 12: CompImmLogic(rs, rt, uimm, &ARMXEmitter::AND, &EvalAnd); break;
		case 13: CompImmLogic(rs, rt, uimm, &ARMXEmitter::ORR, &EvalOr); break;
		case 14: CompImmLogic(rs, rt, uimm, &ARMXEmitter::EOR, &EvalXor); break;

		case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
			{
				gpr.MapDirtyIn(rt, rs);
				Operand2 op2;
				bool negated;
				if (TryMakeOperand2_AllowNegation(simm, op2, &negated)) {
					if (!negated)
						CMP(gpr.R(rs), op2);
					else
						CMN(gpr.R(rs), op2);
				} else {
					MOVI2R(R0, simm);
					CMP(gpr.R(rs), R0);
				}
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
				Operand2 op2;
				bool negated;
				if (TryMakeOperand2_AllowNegation(suimm, op2, &negated)) {
					if (!negated)
						CMP(gpr.R(rs), op2);
					else
						CMN(gpr.R(rs), op2);
				} else {
					MOVI2R(R0, suimm);
					CMP(gpr.R(rs), R0);
				}
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
		DISABLE;
	}

	void Jit::Comp_RType3(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		switch (op & 63) 
		{
		//case 10: if (!R(rt)) R(rd) = R(rs);       break; //movz
		//case 11: if (R(rt)) R(rd) = R(rs);        break; //movn
			
		// case 32: //R(rd) = R(rs) + R(rt);        break; //add
		case 33: //R(rd) = R(rs) + R(rt);           break; //addu
			// Some optimized special cases
			if (rs == 0) {
				gpr.MapDirtyIn(rd, rt);
				MOV(gpr.R(rd), gpr.R(rt));
			} else if (rt == 0) {
				gpr.MapDirtyIn(rd, rs);
				MOV(gpr.R(rd), gpr.R(rs));
			} else {
				gpr.MapDirtyInIn(rd, rs, rt);
				ADD(gpr.R(rd), gpr.R(rs), gpr.R(rt));
			}
			break;
		case 34: //R(rd) = R(rs) - R(rt);           break; //sub
		case 35:
			gpr.MapDirtyInIn(rd, rs, rt);
			SUB(gpr.R(rd), gpr.R(rs), gpr.R(rt));
			break;
		case 36: //R(rd) = R(rs) & R(rt);           break; //and
			gpr.MapDirtyInIn(rd, rs, rt);
			AND(gpr.R(rd), gpr.R(rs), gpr.R(rt));
			break;
		case 37: //R(rd) = R(rs) | R(rt);           break; //or
			gpr.MapDirtyInIn(rd, rs, rt);
			ORR(gpr.R(rd), gpr.R(rs), gpr.R(rt));
			break;
		case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
			gpr.MapDirtyInIn(rd, rs, rt);
			EOR(gpr.R(rd), gpr.R(rs), gpr.R(rt));
			break;

		case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
			gpr.MapDirtyInIn(rd, rs, rt);
			if (rt == 0) {
				MVN(gpr.R(rd), gpr.R(rs));
			} else {
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
			// gpr.UnlockAll();
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
		MOV(gpr.R(rd), Operand2(sa, shiftType, gpr.R(rt)));
	}

	// "over-shifts" work the same as on x86 - only bottom 5 bits are used to get the shift value
	/*
	void Jit::CompShiftVar(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg))
	{
		int rd = _RD;
		int rt = _RT;
		int rs = _RS;
		gpr.FlushLockX(ECX);
		gpr.Lock(rd, rt, rs);
		gpr.BindToRegister(rd, true, true);
		if (rd != rt)
			MOV(32, gpr.R(rd), gpr.R(rt));
		MOV(32, R(ECX), gpr.R(rs));	// Only ECX can be used for variable shifts.
		AND(32, R(ECX), Imm32(0x1f));
		(this->*shift)(32, gpr.R(rd), R(ECX));
		gpr.UnlockAll();
		gpr.UnlockAllX();
	}
*/
	void Jit::Comp_ShiftType(u32 op)
	{
		CONDITIONAL_DISABLE;
		int rs = _RS;
		int fd = _FD;
		// WARNIGN : ROTR
		switch (op & 0x3f)
		{
		case 0: CompShiftImm(op, ST_LSL); break;
		case 2: CompShiftImm(op, rs == 1 ? ST_ROR : ST_LSR); break;	// srl
		case 3: CompShiftImm(op, ST_ASR); break;	// sra
		
	 // case 4: CompShiftVar(op, &XEmitter::SHL); break;	// R(rd) = R(rt) << R(rs);				break; //sllv
	//	case 6: CompShiftVar(op, fd == 1 ? &XEmitter::ROR : &XEmitter::SHR); break;	// R(rd) = R(rt) >> R(rs);				break; //srlv
	//	case 7: CompShiftVar(op, &XEmitter::SAR); break;	// R(rd) = ((s32)R(rt)) >> R(rs); break; //srav
		
		default:
			Comp_Generic(op);
			break;
		}
	}

	void Jit::Comp_Special3(u32 op)
	{
		CONDITIONAL_DISABLE;
		
		bool useUBFXandBFI = false;

		if (!cpu_info.bArmV7) {
			// useUBFXandBFI = true;
		}

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

			gpr.MapDirtyIn(rt, rs, false);
			if (useUBFXandBFI) {
				UBFX(gpr.R(rt), gpr.R(rs), pos, size);
			} else {
				MOV(gpr.R(rt), Operand2(pos, ST_LSR, gpr.R(rs)));
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
					if (useUBFXandBFI) {
						gpr.MapDirtyIn(rt, rs, false);
						BFI(gpr.R(rt), gpr.R(rs), pos, size);
					} else {
						gpr.MapDirtyIn(rt, rs, false);
						ANDI2R(R0, gpr.R(rs), sourcemask, R1);
						MOV(R0, Operand2(pos, ST_LSL, gpr.R(rs)));
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
				// swap odd and even bits
				v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
				// swap consecutive pairs
				v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
				// swap nibbles ...
				v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
				// swap bytes
				v = ((v >> 8) & 0x00FF00FF) | ((v & 0x00FF00FF) << 8);
				// swap 2-byte long pairs
				v = ( v >> 16             ) | ( v               << 16);
				gpr.SetImm(rd, v);
				break;
			}
			// Intentional fall-through.
		default:
			Comp_Generic(op);
			return;
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

		case 28: //madd
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
			SMLAL(gpr.R(MIPSREG_LO), gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 29: //maddu
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
			UMLAL(gpr.R(MIPSREG_LO), gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		default:
			DISABLE;
		}
	}

}
