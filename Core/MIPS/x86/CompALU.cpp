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

#include "Jit.h"
#include "RegCache.h"

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

//#define CONDITIONAL_DISABLE Comp_Generic(op); return;
#define CONDITIONAL_DISABLE ;
#define DISABLE Comp_Generic(op); return;

namespace MIPSComp
{
	void Jit::CompImmLogic(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &))
	{
		u32 uimm = (u16)(op & 0xFFFF);
		int rt = _RT;
		int rs = _RS;
		gpr.Lock(rt, rs);
		gpr.BindToRegister(rt, rt == rs, true);
		if (rt != rs)
			MOV(32, gpr.R(rt), gpr.R(rs));
		(this->*arith)(32, gpr.R(rt), Imm32(uimm));
		gpr.UnlockAll();
	}

	void Jit::Comp_IType(u32 op)
	{
		CONDITIONAL_DISABLE;
		s32 simm = (s16)(op & 0xFFFF);
		u32 uimm = (u16)(op & 0xFFFF);

		int rt = _RT;
		int rs = _RS;

		// noop, won't write to ZERO.
		if (rt == 0)
			return;

		switch (op >> 26) 
		{
		case 8:	// same as addiu?
		case 9:	//R(rt) = R(rs) + simm; break;	//addiu
			{
				if (gpr.IsImmediate(rs))
				{
					gpr.SetImmediate32(rt, gpr.GetImmediate32(rs) + simm);
					break;
				}

				gpr.Lock(rt, rs);
				gpr.BindToRegister(rt, rt == rs, true);
				if (rt != rs)
					MOV(32, gpr.R(rt), gpr.R(rs));
				if (simm != 0)
					ADD(32, gpr.R(rt), Imm32((u32)(s32)simm));
				// TODO: Can also do LEA if both operands happen to be in registers.
				gpr.UnlockAll();
			}
			break;

		case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
			gpr.Lock(rt, rs);
			gpr.BindToRegister(rs, true, false);
			gpr.BindToRegister(rt, rt == rs, true);
			XOR(32, R(EAX), R(EAX));
			CMP(32, gpr.R(rs), Imm32(simm));
			SETcc(CC_L, R(EAX));
			MOV(32, gpr.R(rt), R(EAX));
			gpr.UnlockAll();
			break;

		case 11: // R(rt) = R(rs) < uimm; break; //sltiu
			gpr.Lock(rt, rs);
			gpr.BindToRegister(rs, true, false);
			gpr.BindToRegister(rt, rt == rs, true);
			XOR(32, R(EAX), R(EAX));
			CMP(32, gpr.R(rs), Imm32((u32)simm));
			SETcc(CC_B, R(EAX));
			MOV(32, gpr.R(rt), R(EAX));
			gpr.UnlockAll();
			break;

		case 12: // R(rt) = R(rs) & uimm; break; //andi
			if (uimm == 0)
				gpr.SetImmediate32(rt, 0);
			else if (gpr.IsImmediate(rs))
				gpr.SetImmediate32(rt, gpr.GetImmediate32(rs) & uimm);
			else
				CompImmLogic(op, &XEmitter::AND);
			break;

		case 13: // R(rt) = R(rs) | uimm; break; //ori
			if (gpr.IsImmediate(rs))
				gpr.SetImmediate32(rt, gpr.GetImmediate32(rs) | uimm);
			else
				CompImmLogic(op, &XEmitter::OR);
			break;

		case 14: // R(rt) = R(rs) ^ uimm; break; //xori
			if (gpr.IsImmediate(rs))
				gpr.SetImmediate32(rt, gpr.GetImmediate32(rs) ^ uimm);
			else
				CompImmLogic(op, &XEmitter::XOR);
			break;

		case 15: //R(rt) = uimm << 16;	 break; //lui
			gpr.SetImmediate32(rt, uimm << 16);
			break;

		default:
			Comp_Generic(op);
			break;
		}
	}

	static u32 RType3_ImmAdd(const u32 a, const u32 b)
	{
		return a + b;
	}

	static u32 RType3_ImmSub(const u32 a, const u32 b)
	{
		return a - b;
	}

	static u32 RType3_ImmAnd(const u32 a, const u32 b)
	{
		return a & b;
	}

	static u32 RType3_ImmOr(const u32 a, const u32 b)
	{
		return a | b;
	}

	static u32 RType3_ImmXor(const u32 a, const u32 b)
	{
		return a ^ b;
	}

	//rd = rs X rt
	void Jit::CompTriArith(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &), u32 (*doImm)(const u32, const u32))
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		// Yes, this happens.  Let's make it fast.
		if (doImm && gpr.IsImmediate(rs) && gpr.IsImmediate(rt))
		{
			gpr.SetImmediate32(rd, doImm(gpr.GetImmediate32(rs), gpr.GetImmediate32(rt)));
			return;
		}

		gpr.Lock(rt, rs, rd);
		// Optimize out + 0 and | 0.
		if ((doImm == &RType3_ImmAdd || doImm == &RType3_ImmOr) && (rs == 0 || rt == 0))
		{
			int rsource = rt == 0 ? rs : rt;
			if (rsource != rd)
			{
				gpr.BindToRegister(rd, false, true);
				MOV(32, gpr.R(rd), gpr.R(rsource));
			}
		}
		else
		{
			// Use EAX as a temporary if we'd overwrite it.
			if (rd == rt)
				MOV(32, R(EAX), gpr.R(rt));
			gpr.BindToRegister(rd, rs == rd, true);
			if (rs != rd)
				MOV(32, gpr.R(rd), gpr.R(rs));
			(this->*arith)(32, gpr.R(rd), rd == rt ? R(EAX) : gpr.R(rt));
		}
		gpr.UnlockAll();
	}

	void Jit::Comp_RType3(u32 op)
	{
		CONDITIONAL_DISABLE
		
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		// noop, won't write to ZERO.
		if (rd == 0)
			return;

		switch (op & 63)
		{
		//case 10: if (!R(rt)) R(rd) = R(rs); break; //movz
		//case 11: if (R(rt)) R(rd) = R(rs); break; //movn

		// case 32: //R(rd) = R(rs) + R(rt);		break; //add
		case 33: //R(rd) = R(rs) + R(rt);		break; //addu
			CompTriArith(op, &XEmitter::ADD, &RType3_ImmAdd);
			break;
		case 34: //R(rd) = R(rs) - R(rt);		break; //sub
		case 35:
			CompTriArith(op, &XEmitter::SUB, &RType3_ImmSub);
			break;
		case 36: //R(rd) = R(rs) & R(rt);		break; //and
			CompTriArith(op, &XEmitter::AND, &RType3_ImmAnd);
			break;
		case 37: //R(rd) = R(rs) | R(rt);		break; //or
			CompTriArith(op, &XEmitter::OR, &RType3_ImmOr);
			break;
		case 38: //R(rd) = R(rs) ^ R(rt);		break; //xor
			CompTriArith(op, &XEmitter::XOR, &RType3_ImmXor);
			break;

		case 39: // R(rd) = ~(R(rs) | R(rt)); //nor
			CompTriArith(op, &XEmitter::OR, &RType3_ImmOr);
			if (gpr.IsImmediate(rd))
				gpr.SetImmediate32(rd, ~gpr.GetImmediate32(rd));
			else
				NOT(32, gpr.R(rd));
			break;

		case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
			gpr.Lock(rt, rs, rd);
			gpr.BindToRegister(rs, true, true);
			gpr.BindToRegister(rd, true, true);
			XOR(32, R(EAX), R(EAX));
			CMP(32, gpr.R(rs), gpr.R(rt));
			SETcc(CC_L, R(EAX));
			MOV(32, gpr.R(rd), R(EAX));
			gpr.UnlockAll();
			break;

		case 43: //R(rd) = R(rs) < R(rt);		break; //sltu
			gpr.Lock(rd, rs, rt);
			gpr.BindToRegister(rs, true, true);
			gpr.BindToRegister(rd, true, true);
			XOR(32, R(EAX), R(EAX));
			CMP(32, gpr.R(rs), gpr.R(rt));
			SETcc(CC_B, R(EAX));
			MOV(32, gpr.R(rd), R(EAX));
			gpr.UnlockAll();
			break;

		// case 44: R(rd) = (R(rs) > R(rt)) ? R(rs) : R(rt); break; //max
		// CMP(a,b); CMOVLT(a,b)

		// case 45: R(rd) = (R(rs) < R(rt)) ? R(rs) : R(rt); break; //min
		// CMP(a,b); CMOVGT(a,b)
		default:
			Comp_Generic(op);
			break;
		}
	}


	void Jit::CompShiftImm(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg))
	{
		int rd = _RD;
		int rt = _RT;
		gpr.Lock(rd, rt);
		int sa = _SA;
		gpr.BindToRegister(rd, rd == rt, true);
		if (rd != rt)
			MOV(32, gpr.R(rd), gpr.R(rt));
		(this->*shift)(32, gpr.R(rd), Imm8(sa));
		gpr.UnlockAll();
	}

	// "over-shifts" work the same as on x86 - only bottom 5 bits are used to get the shift value
	void Jit::CompShiftVar(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg))
	{
		DISABLE;
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

	void Jit::Comp_ShiftType(u32 op)
	{
		CONDITIONAL_DISABLE
		int rs = _RS;
		int fd = _FD;
		// WARNIGN : ROTR
		switch (op & 0x3f)
		{
		case 0: CompShiftImm(op, &XEmitter::SHL); break;
		case 2: CompShiftImm(op, rs == 1 ? &XEmitter::ROR : &XEmitter::SHR); break;	// srl, rotr
		case 3: CompShiftImm(op, &XEmitter::SAR); break;	// sra

		case 4: CompShiftVar(op, &XEmitter::SHL); break; //sllv
		case 6: CompShiftVar(op, fd == 1 ? &XEmitter::ROR : &XEmitter::SHR); break;	//srlv
		case 7: CompShiftVar(op, &XEmitter::SAR); break; //srav

		default:
			Comp_Generic(op);
			//_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Jit::Comp_Allegrex(u32 op)
	{
		CONDITIONAL_DISABLE
		int rt = _RT;
		int rd = _RD;
		switch ((op >> 6) & 31)
		{
		case 16: // seb  // R(rd) = (u32)(s32)(s8)(u8)R(rt);
			gpr.Lock(rd, rt);
			gpr.BindToRegister(rd, true, true);
			MOV(32, R(EAX), gpr.R(rt));  // work around the byte-register addressing problem
			MOVSX(32, 8, gpr.RX(rd), R(EAX));
			gpr.UnlockAll();
			break;

		case 24: // seh
			gpr.Lock(rd, rt);
			// MOVSX doesn't like immediate arguments, for example, so let's force it to a register.
			gpr.BindToRegister(rt, true, false);
			gpr.BindToRegister(rd, true, true);
			MOVSX(32, 16, gpr.RX(rd), gpr.R(rt));
			gpr.UnlockAll();
			break;

		case 20: //bitrev
		default:
			Comp_Generic(op);
			return;
		}
	}


	void Jit::Comp_MulDivType(u32 op)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		switch (op & 63) 
		{
		case 16: // R(rd) = HI; //mfhi
			gpr.BindToRegister(rd, false, true);
			MOV(32, gpr.R(rd), M((void *)&mips_->hi));
			break; 

		case 17: // HI = R(rs); //mthi
			gpr.BindToRegister(rs, true, false);
			MOV(32, M((void *)&mips_->hi), gpr.R(rs));
			break; 

		case 18: // R(rd) = LO; break; //mflo
			gpr.BindToRegister(rd, false, true);
			MOV(32, gpr.R(rd), M((void *)&mips_->lo));
			break;

		case 19: // LO = R(rs); break; //mtlo
			gpr.BindToRegister(rs, true, false);
			MOV(32, M((void *)&mips_->lo), gpr.R(rs));
			break; 

		case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
			gpr.FlushLockX(EDX);
			gpr.KillImmediate(rt, true, false);
			MOV(32, R(EAX), gpr.R(rs));
			IMUL(32, gpr.R(rt));
			MOV(32, M((void *)&mips_->hi), R(EDX));
			MOV(32, M((void *)&mips_->lo), R(EAX));
			gpr.UnlockAllX();
			break;


		case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
			gpr.FlushLockX(EDX);
			gpr.KillImmediate(rt, true, false);
			MOV(32, R(EAX), gpr.R(rs));
			MUL(32, gpr.R(rt));
			MOV(32, M((void *)&mips_->hi), R(EDX));
			MOV(32, M((void *)&mips_->lo), R(EAX));
			gpr.UnlockAllX();
			break;


		default:
			DISABLE;	
			/*
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
			break;*/
		}
	}
}
