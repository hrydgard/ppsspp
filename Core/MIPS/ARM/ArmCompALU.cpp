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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM)

#include <algorithm>

#include "Common/BitSet.h"
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/ARM/ArmRegCache.h"

using namespace MIPSAnalyst;

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

// #define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
	using namespace ArmGen;
	using namespace ArmJitConstants;

	static u32 EvalOr(u32 a, u32 b) { return a | b; }
	static u32 EvalEor(u32 a, u32 b) { return a ^ b; }
	static u32 EvalAnd(u32 a, u32 b) { return a & b; }
	static u32 EvalAdd(u32 a, u32 b) { return a + b; }
	static u32 EvalSub(u32 a, u32 b) { return a - b; }

	void ArmJit::CompImmLogic(MIPSGPReg rs, MIPSGPReg rt, u32 uimm, void (ARMXEmitter::*arith)(ARMReg dst, ARMReg src, Operand2 op2), bool (ARMXEmitter::*tryArithI2R)(ARMReg dst, ARMReg src, u32 val), u32 (*eval)(u32 a, u32 b))
	{
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, (*eval)(gpr.GetImm(rs), uimm));
		} else {
			gpr.MapDirtyIn(rt, rs);
			if (!(this->*tryArithI2R)(gpr.R(rt), gpr.R(rs), uimm)) {
				gpr.SetRegImm(SCRATCHREG1, uimm);
				(this->*arith)(gpr.R(rt), gpr.R(rs), SCRATCHREG1);
			}
		}
	}

	void ArmJit::Comp_IType(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(ALU_IMM);
		u32 uimm = op & 0xFFFF;
		s32 simm = SignExtend16ToS32(op);
		u32 suimm = SignExtend16ToU32(op);

		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;

		// noop, won't write to ZERO.
		if (rt == 0)
			return;

		switch (op >> 26) 
		{
		case 8:	// same as addiu?
		case 9:	// R(rt) = R(rs) + simm; break;	//addiu
			CompImmLogic(rs, rt, simm, &ARMXEmitter::ADD, &ARMXEmitter::TryADDI2R, &EvalAdd);
			break;

		case 12: CompImmLogic(rs, rt, uimm, &ARMXEmitter::AND, &ARMXEmitter::TryANDI2R, &EvalAnd); break;
		case 13: CompImmLogic(rs, rt, uimm, &ARMXEmitter::ORR, &ARMXEmitter::TryORI2R, &EvalOr); break;
		case 14: CompImmLogic(rs, rt, uimm, &ARMXEmitter::EOR, &ARMXEmitter::TryEORI2R, &EvalEor); break;

		case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
			{
				if (gpr.IsImm(rs)) {
					gpr.SetImm(rt, (s32)gpr.GetImm(rs) < simm ? 1 : 0);
					break;
				} else if (simm == 0) {
					gpr.MapDirtyIn(rt, rs);
					// Shift to get the sign bit only (for < 0.)
					LSR(gpr.R(rt), gpr.R(rs), 31);
					break;
				}
				gpr.MapDirtyIn(rt, rs);
				if (!TryCMPI2R(gpr.R(rs), simm)) {
					gpr.SetRegImm(SCRATCHREG1, simm);
					CMP(gpr.R(rs), SCRATCHREG1);
				}
				SetCC(CC_LT);
				MOVI2R(gpr.R(rt), 1);
				SetCC(CC_GE);
				MOVI2R(gpr.R(rt), 0);
				SetCC(CC_AL);
			}
			break;

		case 11: // R(rt) = R(rs) < suimm; break; //sltiu
			{
				if (gpr.IsImm(rs)) {
					gpr.SetImm(rt, gpr.GetImm(rs) < suimm ? 1 : 0);
					break;
				}
				gpr.MapDirtyIn(rt, rs);
				if (!TryCMPI2R(gpr.R(rs), suimm)) {
					gpr.SetRegImm(SCRATCHREG1, suimm);
					CMP(gpr.R(rs), SCRATCHREG1);
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

	void ArmJit::Comp_RType2(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(ALU_BIT);
		MIPSGPReg rs = _RS;
		MIPSGPReg rd = _RD;

		// Don't change $zr.
		if (rd == 0)
			return;

		switch (op & 63)
		{
		case 22: //clz
			if (gpr.IsImm(rs)) {
				u32 value = gpr.GetImm(rs);
				int x = 31;
				int count = 0;
				while (x >= 0 && !(value & (1 << x))) {
					count++;
					x--;
				}
				gpr.SetImm(rd, count);
				break;
			}
			gpr.MapDirtyIn(rd, rs);
			CLZ(gpr.R(rd), gpr.R(rs));
			break;
		case 23: //clo
			if (gpr.IsImm(rs)) {
				u32 value = gpr.GetImm(rs);
				int x = 31;
				int count = 0;
				while (x >= 0 && (value & (1 << x))) {
					count++;
					x--;
				}
				gpr.SetImm(rd, count);
				break;
			}
			gpr.MapDirtyIn(rd, rs);
			MVN(SCRATCHREG1, gpr.R(rs));
			CLZ(gpr.R(rd), SCRATCHREG1);
			break;
		default:
			DISABLE;
		}
	}

	void ArmJit::CompType3(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, void (ARMXEmitter::*arith)(ARMReg dst, ARMReg rm, Operand2 rn), bool (ARMXEmitter::*tryArithI2R)(ARMReg dst, ARMReg rm, u32 val), u32 (*eval)(u32 a, u32 b), bool symmetric)
	{
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, (*eval)(gpr.GetImm(rs), gpr.GetImm(rt)));
			return;
		}

		if (gpr.IsImm(rt) || (gpr.IsImm(rs) && symmetric)) {
			MIPSGPReg lhs = gpr.IsImm(rs) ? rt : rs;
			MIPSGPReg rhs = gpr.IsImm(rs) ? rs : rt;
			u32 rhsImm = gpr.GetImm(rhs);
			gpr.MapDirtyIn(rd, lhs);
			if ((this->*tryArithI2R)(gpr.R(rd), gpr.R(lhs), rhsImm)) {
				return;
			}
			// If rd is rhs, we may have lost it in the MapDirtyIn().  lhs was kept.
			// This means the rhsImm value was never flushed to rhs, and would be garbage.
			if (rd == rhs) {
				// Luckily, it was just an imm.
				gpr.SetImm(rhs, rhsImm);
			}
		} else if (gpr.IsImm(rs) && !symmetric) {
			Operand2 op2;
			// For SUB, we can use RSB as a reverse operation.
			if (eval == &EvalSub && TryMakeOperand2(gpr.GetImm(rs), op2)) {
				gpr.MapDirtyIn(rd, rt);
				RSB(gpr.R(rd), gpr.R(rt), op2);
				return;
			}
		}

		// Generic solution.  If it's an imm, better to flush at this point.
		gpr.MapDirtyInIn(rd, rs, rt);
		(this->*arith)(gpr.R(rd), gpr.R(rs), gpr.R(rt));
	}

	void ArmJit::Comp_RType3(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(ALU);
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		MIPSGPReg rd = _RD;

		// noop, won't write to ZERO.
		if (rd == 0)
			return;

		switch (op & 63) 
		{
		case 10: //if (!R(rt)) R(rd) = R(rs);       break; //movz
			if (rd == rs || (gpr.IsImm(rd) && gpr.IsImm(rs) && gpr.GetImm(rd) == gpr.GetImm(rs)))
				break;
			if (!gpr.IsImm(rt)) {
				Operand2 op2;
				// Avoid flushing the imm if possible.
				if (gpr.IsImm(rs) && TryMakeOperand2(gpr.GetImm(rs), op2)) {
					gpr.MapDirtyIn(rd, rt, false);
				} else {
					gpr.MapDirtyInIn(rd, rt, rs, false);
					op2 = gpr.R(rs);
				}
				CMP(gpr.R(rt), Operand2(0));
				SetCC(CC_EQ);
				MOV(gpr.R(rd), op2);
				SetCC(CC_AL);
			} else if (gpr.GetImm(rt) == 0) {
				// Yes, this actually happens.
				if (gpr.IsImm(rs)) {
					gpr.SetImm(rd, gpr.GetImm(rs));
				} else {
					gpr.MapDirtyIn(rd, rs);
					MOV(gpr.R(rd), gpr.R(rs));
				}
			}
			break;
		case 11:// if (R(rt)) R(rd) = R(rs);		break; //movn
			if (rd == rs || (gpr.IsImm(rd) && gpr.IsImm(rs) && gpr.GetImm(rd) == gpr.GetImm(rs)))
				break;
			if (!gpr.IsImm(rt)) {
				Operand2 op2;
				// Avoid flushing the imm if possible.
				if (gpr.IsImm(rs) && TryMakeOperand2(gpr.GetImm(rs), op2)) {
					gpr.MapDirtyIn(rd, rt, false);
				} else {
					gpr.MapDirtyInIn(rd, rt, rs, false);
					op2 = gpr.R(rs);
				}
				CMP(gpr.R(rt), Operand2(0));
				SetCC(CC_NEQ);
				MOV(gpr.R(rd), op2);
				SetCC(CC_AL);
			} else if (gpr.GetImm(rt) != 0) {
				// Yes, this actually happens.
				if (gpr.IsImm(rs)) {
					gpr.SetImm(rd, gpr.GetImm(rs));
				} else {
					gpr.MapDirtyIn(rd, rs);
					MOV(gpr.R(rd), gpr.R(rs));
				}
			}
			break;
			
		case 32: //R(rd) = R(rs) + R(rt);           break; //add
		case 33: //R(rd) = R(rs) + R(rt);           break; //addu
			// We optimize out 0 as an operand2 ADD.
			CompType3(rd, rs, rt, &ARMXEmitter::ADD, &ARMXEmitter::TryADDI2R, &EvalAdd, true);
			break;

		case 34: //R(rd) = R(rs) - R(rt);           break; //sub
		case 35: //R(rd) = R(rs) - R(rt);           break; //subu
			CompType3(rd, rs, rt, &ARMXEmitter::SUB, &ARMXEmitter::TrySUBI2R, &EvalSub, false);
			break;
		case 36: //R(rd) = R(rs) & R(rt);           break; //and
			CompType3(rd, rs, rt, &ARMXEmitter::AND, &ARMXEmitter::TryANDI2R, &EvalAnd, true);
			break;
		case 37: //R(rd) = R(rs) | R(rt);           break; //or
			CompType3(rd, rs, rt, &ARMXEmitter::ORR, &ARMXEmitter::TryORI2R, &EvalOr, true);
			break;
		case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
			CompType3(rd, rs, rt, &ARMXEmitter::EOR, &ARMXEmitter::TryEORI2R, &EvalEor, true);
			break;

		case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				gpr.SetImm(rd, ~(gpr.GetImm(rs) | gpr.GetImm(rt)));
			} else if (gpr.IsImm(rs) || gpr.IsImm(rt)) {
				MIPSGPReg lhs = gpr.IsImm(rs) ? rt : rs;
				MIPSGPReg rhs = gpr.IsImm(rs) ? rs : rt;
				u32 rhsImm = gpr.GetImm(rhs);
				Operand2 op2;
				if (TryMakeOperand2(rhsImm, op2)) {
					gpr.MapDirtyIn(rd, lhs);
				} else {
					gpr.MapDirtyInIn(rd, rs, rt);
					op2 = gpr.R(rhs);
				}
				if (rhsImm == 0) {
					MVN(gpr.R(rd), gpr.R(lhs));
				} else {
					ORR(gpr.R(rd), gpr.R(lhs), op2);
					MVN(gpr.R(rd), gpr.R(rd));
				}
			} else {
				gpr.MapDirtyInIn(rd, rs, rt);
				ORR(gpr.R(rd), gpr.R(rs), gpr.R(rt));
				MVN(gpr.R(rd), gpr.R(rd));
			}
			break;

		case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				gpr.SetImm(rd, (s32)gpr.GetImm(rs) < (s32)gpr.GetImm(rt));
			} else {
				CCFlags caseOne = CC_LT;
				CCFlags caseZero = CC_GE;
				Operand2 op2;
				bool negated;
				if (gpr.IsImm(rs) && TryMakeOperand2_AllowNegation(gpr.GetImm(rs), op2, &negated)) {
					gpr.MapDirtyIn(rd, rt);
					if (!negated)
						CMP(gpr.R(rt), op2);
					else
						CMN(gpr.R(rt), op2);

					// Swap the condition since we swapped the arguments.
					caseOne = CC_GT;
					caseZero = CC_LE;
				} else if (gpr.IsImm(rt) && TryMakeOperand2_AllowNegation(gpr.GetImm(rt), op2, &negated)) {
					gpr.MapDirtyIn(rd, rs);
					if (!negated)
						CMP(gpr.R(rs), op2);
					else
						CMN(gpr.R(rs), op2);
				} else {
					gpr.MapDirtyInIn(rd, rs, rt);
					CMP(gpr.R(rs), gpr.R(rt));
				}

				SetCC(caseOne);
				MOVI2R(gpr.R(rd), 1);
				SetCC(caseZero);
				MOVI2R(gpr.R(rd), 0);
				SetCC(CC_AL);
			}
			break; 

		case 43: //R(rd) = R(rs) < R(rt);           break; //sltu
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				gpr.SetImm(rd, gpr.GetImm(rs) < gpr.GetImm(rt));
			} else {
				CCFlags caseOne = CC_LO;
				CCFlags caseZero = CC_HS;
				Operand2 op2;
				bool negated;
				if (gpr.IsImm(rs) && TryMakeOperand2_AllowNegation(gpr.GetImm(rs), op2, &negated)) {
					gpr.MapDirtyIn(rd, rt);
					if (!negated)
						CMP(gpr.R(rt), op2);
					else
						CMN(gpr.R(rt), op2);

					// Swap the condition since we swapped the arguments.
					caseOne = CC_HI;
					caseZero = CC_LS;
				} else if (gpr.IsImm(rt) && TryMakeOperand2_AllowNegation(gpr.GetImm(rt), op2, &negated)) {
					gpr.MapDirtyIn(rd, rs);
					if (!negated)
						CMP(gpr.R(rs), op2);
					else
						CMN(gpr.R(rs), op2);
				} else {
					gpr.MapDirtyInIn(rd, rs, rt);
					CMP(gpr.R(rs), gpr.R(rt));
				}

				SetCC(caseOne);
				MOVI2R(gpr.R(rd), 1);
				SetCC(caseZero);
				MOVI2R(gpr.R(rd), 0);
				SetCC(CC_AL);
			}
			break;

		case 44: //R(rd) = max(R(rs), R(rt);        break; //max
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				gpr.SetImm(rd, std::max(gpr.GetImm(rs), gpr.GetImm(rt)));
				break;
			}
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			SetCC(CC_GT);
			if (rd != rs)
				MOV(gpr.R(rd), gpr.R(rs));
			SetCC(CC_LE);
			if (rd != rt)
				MOV(gpr.R(rd), gpr.R(rt));
			SetCC(CC_AL);
			break;

		case 45: //R(rd) = min(R(rs), R(rt));       break; //min
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				gpr.SetImm(rd, std::min(gpr.GetImm(rs), gpr.GetImm(rt)));
				break;
			}
			gpr.MapDirtyInIn(rd, rs, rt);
			CMP(gpr.R(rs), gpr.R(rt));
			SetCC(CC_LT);
			if (rd != rs)
				MOV(gpr.R(rd), gpr.R(rs));
			SetCC(CC_GE);
			if (rd != rt)
				MOV(gpr.R(rd), gpr.R(rt));
			SetCC(CC_AL);
			break;

		default:
			Comp_Generic(op);
			break;
		}
	}

	void ArmJit::CompShiftImm(MIPSOpcode op, ArmGen::ShiftType shiftType, int sa)
	{
		MIPSGPReg rd = _RD;
		MIPSGPReg rt = _RT;

		if (gpr.IsImm(rt)) {
			switch (shiftType) {
			case ST_LSL:
				gpr.SetImm(rd, gpr.GetImm(rt) << sa);
				break;
			case ST_LSR:
				gpr.SetImm(rd, gpr.GetImm(rt) >> sa);
				break;
			case ST_ASR:
				gpr.SetImm(rd, (int)gpr.GetImm(rt) >> sa);
				break;
			case ST_ROR:
				gpr.SetImm(rd, (gpr.GetImm(rt) >> sa) | (gpr.GetImm(rt) << (32 - sa)));
				break;
			default:
				DISABLE;
			}
		} else {
			gpr.MapDirtyIn(rd, rt);
			MOV(gpr.R(rd), Operand2(gpr.R(rt), shiftType, sa));
		}
	}

	void ArmJit::CompShiftVar(MIPSOpcode op, ArmGen::ShiftType shiftType)
	{
		MIPSGPReg rd = _RD;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		if (gpr.IsImm(rs)) {
			int sa = gpr.GetImm(rs) & 0x1F;
			CompShiftImm(op, shiftType, sa);
			return;
		}
		gpr.MapDirtyInIn(rd, rs, rt);
		AND(SCRATCHREG1, gpr.R(rs), Operand2(0x1F));
		MOV(gpr.R(rd), Operand2(gpr.R(rt), shiftType, SCRATCHREG1));
	}

	void ArmJit::Comp_ShiftType(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(ALU);
		MIPSGPReg rs = _RS;
		MIPSGPReg rd = _RD;
		int fd = _FD;
		int sa = _SA;

		// noop, won't write to ZERO.
		if (rd == 0)
			return;

		// WARNING : ROTR
		switch (op & 0x3f) {
		case 0: CompShiftImm(op, ST_LSL, sa); break; //sll
		case 2: CompShiftImm(op, rs == 1 ? ST_ROR : ST_LSR, sa); break;	//srl
		case 3: CompShiftImm(op, ST_ASR, sa); break; //sra
		case 4: CompShiftVar(op, ST_LSL); break; //sllv
		case 6: CompShiftVar(op, fd == 1 ? ST_ROR : ST_LSR); break; //srlv
		case 7: CompShiftVar(op, ST_ASR); break; //srav
		
		default:
			Comp_Generic(op);
			break;
		}
	}

	void ArmJit::Comp_Special3(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(ALU_BIT);

		MIPSGPReg rs = _RS;
		MIPSGPReg rt = _RT;

		int pos = _POS;
		int size = _SIZE + 1;
		u32 mask = 0xFFFFFFFFUL >> (32 - size);

		// Don't change $zr.
		if (rt == 0)
			return;

		switch (op & 0x3f) {
		case 0x0: //ext
			if (gpr.IsImm(rs)) {
				gpr.SetImm(rt, (gpr.GetImm(rs) >> pos) & mask);
				return;
			}

			gpr.MapDirtyIn(rt, rs);
#if PPSSPP_ARCH(ARMV7)
			UBFX(gpr.R(rt), gpr.R(rs), pos, size);
#else
			MOV(gpr.R(rt), Operand2(gpr.R(rs), ST_LSR, pos));
			ANDI2R(gpr.R(rt), gpr.R(rt), mask, SCRATCHREG1);
#endif
			break;

		case 0x4: //ins
			{
				u32 sourcemask = mask >> pos;
				u32 destmask = ~(sourcemask << pos);
				if (gpr.IsImm(rs)) {
					u32 inserted = (gpr.GetImm(rs) & sourcemask) << pos;
					if (gpr.IsImm(rt)) {
						gpr.SetImm(rt, (gpr.GetImm(rt) & destmask) | inserted);
						return;
					}

					gpr.MapReg(rt, MAP_DIRTY);
					ANDI2R(gpr.R(rt), gpr.R(rt), destmask, SCRATCHREG1);
					if (inserted != 0) {
						ORI2R(gpr.R(rt), gpr.R(rt), inserted, SCRATCHREG1);
					}
				} else {
					gpr.MapDirtyIn(rt, rs, false);
#if PPSSPP_ARCH(ARMV7)
					BFI(gpr.R(rt), gpr.R(rs), pos, size - pos);
#else
					ANDI2R(SCRATCHREG1, gpr.R(rs), sourcemask, SCRATCHREG2);
					ANDI2R(gpr.R(rt), gpr.R(rt), destmask, SCRATCHREG2);
					ORR(gpr.R(rt), gpr.R(rt), Operand2(SCRATCHREG1, ST_LSL, pos));
#endif
				}
			}
			break;
		}
	}

	void ArmJit::Comp_Allegrex(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(ALU_BIT);
		MIPSGPReg rt = _RT;
		MIPSGPReg rd = _RD;
		// Don't change $zr.
		if (rd == 0)
			return;

		switch ((op >> 6) & 31) {
		case 16: // seb	// R(rd) = SignExtend8ToU32(R(rt));
			if (gpr.IsImm(rt)) {
				gpr.SetImm(rd, SignExtend8ToU32(gpr.GetImm(rt)));
				return;
			}
			gpr.MapDirtyIn(rd, rt);
			SXTB(gpr.R(rd), gpr.R(rt));
			break;

		case 24: // seh
			if (gpr.IsImm(rt)) {
				gpr.SetImm(rd, SignExtend16ToU32(gpr.GetImm(rt)));
				return;
			}
			gpr.MapDirtyIn(rd, rt);
			SXTH(gpr.R(rd), gpr.R(rt));
			break;
		
		case 20: //bitrev
			if (gpr.IsImm(rt)) {
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

#if PPSSPP_ARCH(ARMV7)
			gpr.MapDirtyIn(rd, rt);
			RBIT(gpr.R(rd), gpr.R(rt));
#else
			Comp_Generic(op);
#endif
			break;
		default:
			Comp_Generic(op);
			return;
		}
	}

	void ArmJit::Comp_Allegrex2(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(ALU_BIT);
		MIPSGPReg rt = _RT;
		MIPSGPReg rd = _RD;
		// Don't change $zr.
		if (rd == 0)
			return;

		switch (op & 0x3ff) {
		case 0xA0: //wsbh
			if (gpr.IsImm(rt)) {
				gpr.SetImm(rd, ((gpr.GetImm(rt) & 0xFF00FF00) >> 8) | ((gpr.GetImm(rt) & 0x00FF00FF) << 8));
			} else {
				gpr.MapDirtyIn(rd, rt);
				REV16(gpr.R(rd), gpr.R(rt));
			}
			break;
		case 0xE0: //wsbw
			if (gpr.IsImm(rt)) {
				gpr.SetImm(rd, swap32(gpr.GetImm(rt)));
			} else {
				gpr.MapDirtyIn(rd, rt);
				REV(gpr.R(rd), gpr.R(rt));
			}
			break;
		default:
			Comp_Generic(op);
			break;
		}
	}

	void ArmJit::Comp_MulDivType(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(MULDIV);
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		MIPSGPReg rd = _RD;

		switch (op & 63) {
		case 16: // R(rd) = HI; //mfhi
			if (rd != MIPS_REG_ZERO) {
				if (gpr.IsImm(MIPS_REG_HI)) {
					gpr.SetImm(rd, gpr.GetImm(MIPS_REG_HI));
					break;
				}
				gpr.MapDirtyIn(rd, MIPS_REG_HI);
				MOV(gpr.R(rd), gpr.R(MIPS_REG_HI));
			}
			break; 

		case 17: // HI = R(rs); //mthi
			if (gpr.IsImm(rs)) {
				gpr.SetImm(MIPS_REG_HI, gpr.GetImm(rs));
				break;
			}
			gpr.MapDirtyIn(MIPS_REG_HI, rs);
			MOV(gpr.R(MIPS_REG_HI), gpr.R(rs));
			break; 

		case 18: // R(rd) = LO; break; //mflo
			if (rd != MIPS_REG_ZERO) {
				if (gpr.IsImm(MIPS_REG_LO)) {
					gpr.SetImm(rd, gpr.GetImm(MIPS_REG_LO));
					break;
				}
				gpr.MapDirtyIn(rd, MIPS_REG_LO);
				MOV(gpr.R(rd), gpr.R(MIPS_REG_LO));
			}
			break;

		case 19: // LO = R(rs); break; //mtlo
			if (gpr.IsImm(rs)) {
				gpr.SetImm(MIPS_REG_LO, gpr.GetImm(rs));
				break;
			}
			gpr.MapDirtyIn(MIPS_REG_LO, rs);
			MOV(gpr.R(MIPS_REG_LO), gpr.R(rs));
			break; 

		case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				s64 result = (s64)(s32)gpr.GetImm(rs) * (s64)(s32)gpr.GetImm(rt);
				u64 resultBits = (u64)result;
				gpr.SetImm(MIPS_REG_LO, (u32)(resultBits >> 0));
				gpr.SetImm(MIPS_REG_HI, (u32)(resultBits >> 32));
				break;
			}
			gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);
			SMULL(gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				u64 resultBits = (u64)gpr.GetImm(rs) * (u64)gpr.GetImm(rt);
				gpr.SetImm(MIPS_REG_LO, (u32)(resultBits >> 0));
				gpr.SetImm(MIPS_REG_HI, (u32)(resultBits >> 32));
				break;
			}
			gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);
			UMULL(gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 26: //div
			if (cpu_info.bIDIVa) {
				// TODO: Does this handle INT_MAX, 0, etc. correctly?
				gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);

				CMPI2R(gpr.R(rt), 0, SCRATCHREG1);
				FixupBranch skipZero = B_CC(CC_NEQ);
				MOV(gpr.R(MIPS_REG_HI), gpr.R(rs));
				MOVI2R(gpr.R(MIPS_REG_LO), -1);
				CMPI2R(gpr.R(rs), 0, SCRATCHREG1);
				SetCC(CC_LT);
				MOVI2R(gpr.R(MIPS_REG_LO), 1);
				SetCC(CC_AL);
				FixupBranch skipDiv = B();

				SetJumpTarget(skipZero);
				SDIV(gpr.R(MIPS_REG_LO), gpr.R(rs), gpr.R(rt));
				SetJumpTarget(skipDiv);
				MUL(SCRATCHREG1, gpr.R(rt), gpr.R(MIPS_REG_LO));
				SUB(gpr.R(MIPS_REG_HI), gpr.R(rs), Operand2(SCRATCHREG1));
			} else {
				DISABLE;
			}
			break;

		case 27: //divu
			// Do we have a known power-of-two denominator?  Yes, this happens.
			if (gpr.IsImm(rt) && (gpr.GetImm(rt) & (gpr.GetImm(rt) - 1)) == 0 && gpr.GetImm(rt) != 0) {
				u32 denominator = gpr.GetImm(rt);
				gpr.MapDirtyDirtyIn(MIPS_REG_LO, MIPS_REG_HI, rs);
				// Remainder is just an AND, neat.
				ANDI2R(gpr.R(MIPS_REG_HI), gpr.R(rs), denominator - 1, SCRATCHREG1);
				int shift = 0;
				while (denominator != 0) {
					++shift;
					denominator >>= 1;
				}
				// The shift value is one too much for the divide by the same value.
				if (shift > 1) {
					LSR(gpr.R(MIPS_REG_LO), gpr.R(rs), shift - 1);
				} else {
					MOV(gpr.R(MIPS_REG_LO), gpr.R(rs));
				}
			} else if (cpu_info.bIDIVa) {
				// TODO: Does this handle INT_MAX, 0, etc. correctly?
				gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);

				CMPI2R(gpr.R(rt), 0, SCRATCHREG1);
				FixupBranch skipZero = B_CC(CC_NEQ);
				MOV(gpr.R(MIPS_REG_HI), gpr.R(rs));
				MOVI2R(SCRATCHREG1, 0xFFFF);
				CMP(gpr.R(rs), SCRATCHREG1);
				MOVI2R(gpr.R(MIPS_REG_LO), -1);
				SetCC(CC_LS);
				MOV(gpr.R(MIPS_REG_LO), SCRATCHREG1);
				SetCC(CC_AL);
				FixupBranch skipDiv = B();

				SetJumpTarget(skipZero);
				UDIV(gpr.R(MIPS_REG_LO), gpr.R(rs), gpr.R(rt));
				SetJumpTarget(skipDiv);
				MUL(SCRATCHREG1, gpr.R(rt), gpr.R(MIPS_REG_LO));
				SUB(gpr.R(MIPS_REG_HI), gpr.R(rs), Operand2(SCRATCHREG1));
			} else {
				// If rt is 0, we either caught it above, or it's not an imm.
				gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt);
				MOV(SCRATCHREG1, gpr.R(rt));
				// We start at rs for the remainder and subtract out.
				MOV(gpr.R(MIPS_REG_HI), gpr.R(rs));

				CMP(gpr.R(rt), 0);
				FixupBranch skipper = B_CC(CC_EQ);

				// Double SCRATCHREG1 until it would be (but isn't) bigger than the numerator.
				CMP(SCRATCHREG1, Operand2(gpr.R(rs), ST_LSR, 1));
				const u8 *doubleLoop = GetCodePtr();
					SetCC(CC_LS);
					MOV(SCRATCHREG1, Operand2(SCRATCHREG1, ST_LSL, 1));
					SetCC(CC_AL);
					CMP(SCRATCHREG1, Operand2(gpr.R(rs), ST_LSR, 1));
				B_CC(CC_LS, doubleLoop);

				MOV(gpr.R(MIPS_REG_LO), 0);

				// Subtract and halve SCRATCHREG1 (doubling and adding the result) until it's below the denominator.
				const u8 *subLoop = GetCodePtr();
					CMP(gpr.R(MIPS_REG_HI), SCRATCHREG1);
					SetCC(CC_HS);
					SUB(gpr.R(MIPS_REG_HI), gpr.R(MIPS_REG_HI), SCRATCHREG1);
					SetCC(CC_AL);
					// Carry will be set if we subtracted.
					ADC(gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_LO));
					MOV(SCRATCHREG1, Operand2(SCRATCHREG1, ST_LSR, 1));
					CMP(SCRATCHREG1, gpr.R(rt));
				B_CC(CC_HS, subLoop);

				// We didn't change rt.  If it was 0, then clear HI and LO.
				FixupBranch zeroSkip = B();
				SetJumpTarget(skipper);
				MOVI2R(SCRATCHREG1, 0xFFFF);
				CMP(gpr.R(rs), SCRATCHREG1);
				MOVI2R(gpr.R(MIPS_REG_LO), -1);
				SetCC(CC_LS);
				MOV(gpr.R(MIPS_REG_LO), SCRATCHREG1);
				SetCC(CC_AL);
				SetJumpTarget(zeroSkip);
			}
			break;

		case 28: //madd
			gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
			SMLAL(gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 29: //maddu
			gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
			UMLAL(gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 46: // msub
			gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
			SMULL(SCRATCHREG1, SCRATCHREG2, gpr.R(rs), gpr.R(rt));
			SUBS(gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_LO), SCRATCHREG1);
			SBC(gpr.R(MIPS_REG_HI), gpr.R(MIPS_REG_HI), SCRATCHREG2);
			break;

		case 47: // msubu
			gpr.MapDirtyDirtyInIn(MIPS_REG_LO, MIPS_REG_HI, rs, rt, false);
			UMULL(SCRATCHREG1, SCRATCHREG2, gpr.R(rs), gpr.R(rt));
			SUBS(gpr.R(MIPS_REG_LO), gpr.R(MIPS_REG_LO), SCRATCHREG1);
			SBC(gpr.R(MIPS_REG_HI), gpr.R(MIPS_REG_HI), SCRATCHREG2);
			break;

		default:
			DISABLE;
		}
	}

}

#endif // PPSSPP_ARCH(ARM)
