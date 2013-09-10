#include "Common/ChunkFile.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"

#include "PpcRegCache.h"
#include "ppcEmitter.h"
#include "PpcJit.h"

/***************************************************************************************************
*	Current issues:
*		Comp_RType3(min/max): Can't select start in disgaea
*		Comp_ShiftType(srl/srlv?): Crash ridge racer 2
***************************************************************************************************/


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

//#define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{

	static u32 EvalOr(u32 a, u32 b) { return a | b; }
	static u32 EvalXor(u32 a, u32 b) { return a ^ b; }
	static u32 EvalAnd(u32 a, u32 b) { return a & b; }
	static u32 EvalAdd(u32 a, u32 b) { return a + b; }
	static u32 EvalSub(u32 a, u32 b) { return a - b; }
	static u32 EvalNor(u32 a, u32 b) { return ~(a | b); }

	// Utilities to reduce duplicated code
	void Jit::CompType3(int rd, int rs, int rt, void (PPCXEmitter::*arith)(PPCReg Rd, PPCReg Ra, PPCReg Rb), u32 (*eval)(u32 a, u32 b), bool isSub) {
		if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
			gpr.SetImm(rd, (*eval)(gpr.GetImm(rs), gpr.GetImm(rt)));
		} else if (gpr.IsImm(rt)) {
			u32 rtImm = gpr.GetImm(rt);
			gpr.MapDirtyIn(rd, rs);
			MOVI2R(SREG, rtImm);
			(this->*arith)(gpr.R(rd), gpr.R(rs), SREG);
		} else if (gpr.IsImm(rs)) {
			u32 rsImm = gpr.GetImm(rs);
			gpr.MapDirtyIn(rd, rt);
			// TODO: Special case when rsImm can be represented as an Operand2
			MOVI2R(SREG, rsImm);
			(this->*arith)(gpr.R(rd), SREG, gpr.R(rt));
		} else {
			// Generic solution
			gpr.MapDirtyInIn(rd, rs, rt);
			(this->*arith)(gpr.R(rd), gpr.R(rs), gpr.R(rt));
		}
	}

	void Jit::CompImmLogic(int rs, int rt, u32 uimm, void (PPCXEmitter::*arith)(PPCReg Rd, PPCReg Ra, unsigned short imm), u32 (*eval)(u32 a, u32 b))
	{
		if (gpr.IsImm(rs)) {
			gpr.SetImm(rt, (*eval)(gpr.GetImm(rs), uimm));
		} else {
			gpr.MapDirtyIn(rt, rs);
			(this->*arith)(gpr.R(rt), gpr.R(rs), uimm);
		}
	}

	void Jit::Comp_IType(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE;
		s32 simm = (s32)(s16)(op & 0xFFFF);  // sign extension
		u32 uimm = op & 0xFFFF;
		u32 suimm = (u32)(s32)simm;

		int rt = _RT;
		int rs = _RS;

		int o = op>>26;

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
					ADDI(gpr.R(rt), gpr.R(rs), simm);
				}
				break;
			}

			// Use with caution can change CR0 !
		case 12: CompImmLogic(rs, rt, uimm, &PPCXEmitter::ANDI, &EvalAnd); break;
			// Safe
		case 13: CompImmLogic(rs, rt, uimm, &PPCXEmitter::ORI, &EvalOr); break;
		case 14: CompImmLogic(rs, rt, uimm, &PPCXEmitter::XORI, &EvalXor); break;
		case 15: // R(rt) = uimm << 16;	 //lui
			gpr.SetImm(rt, uimm << 16);
			break;

		case 10: // slti - R(rt) = (s32)R(rs) < simm
			if (gpr.IsImm(rs))
			{
				gpr.SetImm(rt, (s32)gpr.GetImm(rs) < simm);
				break;
			} else {
				//DISABLE;
				gpr.MapDirtyIn(rt, rs);

				PPCReg ppc_rt = gpr.R(rt);
				PPCReg ppc_rs = gpr.R(rs);

				MOVI2R(R0, 0);
				ADDI(SREG, R0, uimm);

				SUBFC(R0, SREG, ppc_rs);
				EQV(ppc_rt, SREG, ppc_rs);
				SRWI(ppc_rt, ppc_rt, 31);
				ADDZE(ppc_rt, ppc_rt);
				RLWINM(ppc_rt, ppc_rt, 0, 31, 31);
				//Break();
				break;
			}

		case 11: //sltiu
			if (gpr.IsImm(rs))
			{
				gpr.SetImm(rt, gpr.GetImm(rs) < uimm);
				break;
			} else {
				//DISABLE;
				gpr.MapDirtyIn(rt, rs);

				PPCReg ppc_rt = gpr.R(rt);

				ADDI(SREG, R0, uimm);
				SUBFC(ppc_rt, SREG, gpr.R(rs));
				SUBFE(ppc_rt, ppc_rt, ppc_rt);
				NEG(ppc_rt, ppc_rt);

				break;
			}

		default:
			Comp_Generic(op);
			break;
		}
	}

	void Jit::Comp_RType2(MIPSOpcode op) {
		Comp_Generic(op);
	}


	void Jit::Comp_RType3(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		// noop, won't write to ZERO.
		if (rd == 0)
			return;

		u8 o = op & 63;

		switch (op & 63) 
		{
		case 10: // if (R(rt) == 0) R(rd) = R(rs); break; //movz
			if (rd == rs)
				break;
			if (!gpr.IsImm(rt))
			{
				gpr.MapDirtyInIn(rd, rt, rs, false);
				CMPI(gpr.R(rt), 0);
				PpcGen::FixupBranch ptr;

				ptr = B_Cond(_BNE);

				MR(gpr.R(rd), gpr.R(rs));

				SetJumpTarget(ptr);

			}
			else if (gpr.GetImm(rt) == 0)
			{
				// Yes, this actually happens.
				if (gpr.IsImm(rs))
					gpr.SetImm(rd, gpr.GetImm(rs));
				else
				{
					gpr.MapDirtyIn(rd, rs);
					MR(gpr.R(rd), gpr.R(rs));
				}
			}
			break;

		case 11:// if (R(rt) != 0) R(rd) = R(rs); break; //movn
			if (rd == rs)
				break;
			if (!gpr.IsImm(rt))
			{
				gpr.MapDirtyInIn(rd, rt, rs, false);
				CMPI(gpr.R(rt), 0);

				PpcGen::FixupBranch ptr;

				ptr = B_Cond(_BEQ);

				MR(gpr.R(rd), gpr.R(rs));

				SetJumpTarget(ptr);
			}
			else if (gpr.GetImm(rt) != 0)
			{
				// Yes, this actually happens.
				if (gpr.IsImm(rs))
					gpr.SetImm(rd, gpr.GetImm(rs));
				else
				{
					gpr.MapDirtyIn(rd, rs);
					MR(gpr.R(rd), gpr.R(rs));
				}
			}
			break;

		case 32: //R(rd) = R(rs) + R(rt);           break; //add
		case 33: //R(rd) = R(rs) + R(rt);           break; //addu
			// Some optimized special cases
			if (gpr.IsImm(rs) && gpr.GetImm(rs) == 0) {
				gpr.MapDirtyIn(rd, rt);
				MR(gpr.R(rd), gpr.R(rt));
			} else if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0) {
				gpr.MapDirtyIn(rd, rs);
				MR(gpr.R(rd), gpr.R(rs));
			} else {
				CompType3(rd, rs, rt, &PPCXEmitter::ADD, &EvalAdd);
			}
			break;
		case 34: //R(rd) = R(rs) - R(rt);           break; //sub
		case 35: //R(rd) = R(rs) - R(rt);           break; //subu
			CompType3(rd, rs, rt, &PPCXEmitter::SUB, &EvalSub, true);
			break;
		case 36: //R(rd) = R(rs) & R(rt);           break; //and
			CompType3(rd, rs, rt, &PPCXEmitter::AND, &EvalAnd);
			break;
		case 37: //R(rd) = R(rs) | R(rt);           break; //or
			CompType3(rd, rs, rt, &PPCXEmitter::OR, &EvalOr);
			break;
		case 38: //R(rd) = R(rs) ^ R(rt);           break; //xor/eor	
			CompType3(rd, rs, rt, &PPCXEmitter::XOR, &EvalXor);
			break;
			// Not tested !
		case 39: // R(rd) = ~(R(rs) | R(rt));       break; //nor
			CompType3(rd, rs, rt, &PPCXEmitter::NOR, &EvalNor);
			break;

		case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				gpr.SetImm(rd, (s32)gpr.GetImm(rs) < (s32)gpr.GetImm(rt));
			} else {
				gpr.MapDirtyInIn(rd, rs, rt);

				PPCReg ppc_rd = gpr.R(rd);			
				PPCReg ppc_rs = gpr.R(rs);			
				PPCReg ppc_rt = gpr.R(rt);

				SUBFC(R0, ppc_rt, ppc_rs);
				EQV(ppc_rd, ppc_rt, ppc_rs);
				SRWI(ppc_rd, ppc_rd, 31);
				ADDZE(ppc_rd, ppc_rd);
				RLWINM(ppc_rd, ppc_rd, 0, 31, 31);
			}

			break; 

		case 43: //R(rd) = R(rs) < R(rt);           break; //sltu
			if (gpr.IsImm(rs) && gpr.IsImm(rt)) {
				gpr.SetImm(rd, gpr.GetImm(rs) < gpr.GetImm(rt));
			} else {
				gpr.MapDirtyInIn(rd, rs, rt);

				PPCReg ppc_rd = gpr.R(rd);

				SUBFC(ppc_rd, gpr.R(rt), gpr.R(rs));
				SUBFE(ppc_rd, ppc_rd, ppc_rd);
				NEG(ppc_rd, ppc_rd);
			}
			break;


		case 44:// R(rd) = ((s32)R(rs) > (s32)R(rt)) ? R(rs) : R(rt); break; //max
			DISABLE;
			if (gpr.IsImm(rs) && gpr.IsImm(rt))
				gpr.SetImm(rd, std::max((s32)gpr.GetImm(rs), (s32)gpr.GetImm(rt)));
			else
			{
				gpr.MapDirtyInIn(rd, rs, rt);
				PpcGen::FixupBranch end;

				// by default rd = rt
				MR(gpr.R(rd), gpr.R(rt));

				// if rs > rt => end
				CMP(gpr.R(rs), gpr.R(rt));
				end = B_Cond(_BLE);	

				// rd = rs
				MR(gpr.R(rd), gpr.R(rs));

				SetJumpTarget(end);
			}
			break;

		case 45: //min
			DISABLE;
			if (gpr.IsImm(rs) && gpr.IsImm(rt))
				gpr.SetImm(rd, std::min((s32)gpr.GetImm(rs), (s32)gpr.GetImm(rt)));
			else
			{
				gpr.MapDirtyInIn(rd, rs, rt);
				PpcGen::FixupBranch end;
				
				// by default rd = rt
				MR(gpr.R(rd), gpr.R(rt));

				// if rs < rt => end
				CMP(gpr.R(rs), gpr.R(rt));
				end = B_Cond(_BGE);	
				
				// rd = rs
				MR(gpr.R(rd), gpr.R(rs));

				SetJumpTarget(end);
			}
			break;


		default:
			Comp_Generic(op);
			break;
		}
	}

	/**
	*	srl/srlv are disabled because they crash rr2
	**/
	void Jit::Comp_ShiftType(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		int rs = _RS;
		int rd = _RD;
		int	fd = _FD;
		int rt = _RT;
		int sa = _SA;

		// noop, won't write to ZERO.
		if (rd == 0)
			return;

		// WARNING : ROTR
		switch (op & 0x3f)
		{
		case 0:  //sll
			gpr.MapDirtyIn(rd, rt);	
			SLWI(gpr.R(rd), gpr.R(rt), sa);
			break;

		case 2:
			DISABLE;
			if (rs == 0) // srl
			{ 
				gpr.MapDirtyIn(rd, rt);	
				SRWI(gpr.R(rd), gpr.R(rt), sa); 
				//Break();
				break;
			} 
			else // rotr
			{
				gpr.MapDirtyIn(rd, rt);	
				ROTRWI(gpr.R(rd), gpr.R(rt), sa); 
				Break();
				break;
			}

		case 3: //sra
			gpr.MapDirtyIn(rd, rt);	
			SRAWI(gpr.R(rd), gpr.R(rt), sa);
			break;

		case 4: //sllv
			if (gpr.IsImm(rs))
			{
				int sa = gpr.GetImm(rs) & 0x1F;
				gpr.MapDirtyIn(rd, rt);
				SLWI(gpr.R(rd), gpr.R(rt), sa);
				break;
			}
			gpr.MapDirtyInIn(rd, rs, rt);
			ANDI(SREG, gpr.R(rs), 0x1F);
			SLW(gpr.R(rd), gpr.R(rt), SREG);
			break;

		case 6: 			
			DISABLE;
			if ( fd == 0) { //srlv				
				if (gpr.IsImm(rs))
				{
					int sa = gpr.GetImm(rs) & 0x1F;
					gpr.MapDirtyIn(rd, rt);
					SRWI(gpr.R(rd), gpr.R(rt), sa);
					break;
				} else {
					gpr.MapDirtyInIn(rd, rs, rt);
					ANDI(SREG, gpr.R(rs), 0x1F);
					SRW(gpr.R(rd), gpr.R(rt), SREG);
					break;
				}
			} else { // rotrv
				if (gpr.IsImm(rs))
				{
					int sa = gpr.GetImm(rs) & 0x1F;
					gpr.MapDirtyIn(rd, rt);
					ROTRWI(gpr.R(rd), gpr.R(rt), sa);
					break;
				}
				// Not made
				DISABLE;
			}
			break;

		case 7: //srav
			if (gpr.IsImm(rs))
			{
				int sa = gpr.GetImm(rs) & 0x1F;
				gpr.MapDirtyIn(rd, rt);
				SRAWI(gpr.R(rd), gpr.R(rt), sa);
				break;
			}
			gpr.MapDirtyInIn(rd, rs, rt);
			ANDI(SREG, gpr.R(rs), 0x1F);
			SRAW(gpr.R(rd), gpr.R(rt), SREG);
			break;

		default:
			Comp_Generic(op);
			break;
		}
	}

	void Jit::Comp_Allegrex(MIPSOpcode op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Allegrex2(MIPSOpcode op) {
		Comp_Generic(op);
	}

	void Jit::Comp_MulDivType(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		MIPSGPReg rt = _RT;
		MIPSGPReg rs = _RS;
		int rd = _RD;

		switch (op & 63) 
		{
		case 16: // R(rd) = HI; //mfhi
			gpr.MapDirtyIn(rd, MIPSREG_HI);
			MR(gpr.R(rd), gpr.R(MIPSREG_HI));
			break; 

		case 17: // HI = R(rs); //mthi
			gpr.MapDirtyIn(MIPSREG_HI, rs);
			MR(gpr.R(MIPSREG_HI), gpr.R(rs));
			break; 

		case 18: // R(rd) = LO; break; //mflo
			gpr.MapDirtyIn(rd, MIPSREG_LO);
			MR(gpr.R(rd), gpr.R(MIPSREG_LO));
			break;

		case 19: // LO = R(rs); break; //mtlo
			gpr.MapDirtyIn(MIPSREG_LO, rs);
			MR(gpr.R(MIPSREG_LO), gpr.R(rs));
			break; 

		case 24: //mult (the most popular one). lo,hi  = signed mul (rs * rt)
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
			MULLW(gpr.R(MIPSREG_LO), gpr.R(rs), gpr.R(rt));
			MULHW(gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 25: //multu (2nd) lo,hi  = unsigned mul (rs * rt)
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
			MULLW(gpr.R(MIPSREG_LO), gpr.R(rs), gpr.R(rt));
			MULHWU(gpr.R(MIPSREG_HI), gpr.R(rs), gpr.R(rt));
			break;

		case 26: //div
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
			DIVW(gpr.R(MIPSREG_LO), gpr.R(rs), gpr.R(rt));
			MULLW(SREG, gpr.R(rt), gpr.R(MIPSREG_LO));
			SUB(gpr.R(MIPSREG_HI), gpr.R(rs), SREG);			
			break;

		case 27: //divu
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt);
			DIVWU(gpr.R(MIPSREG_LO), gpr.R(rs), gpr.R(rt));
			MULLW(SREG, gpr.R(rt), gpr.R(MIPSREG_LO));
			SUB(gpr.R(MIPSREG_HI), gpr.R(rs), SREG);
			break;

		case 28: //madd
			DISABLE;
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
			break;

		case 29: //maddu
			DISABLE;
			gpr.MapDirtyDirtyInIn(MIPSREG_LO, MIPSREG_HI, rs, rt, false);
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

	void Jit::Comp_Special3(MIPSOpcode op) {
		Comp_Generic(op);
	}

}