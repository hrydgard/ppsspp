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

	void Jit::Comp_IType(u32 op)
	{
		CONDITIONAL_DISABLE;
		s32 simm = (s32)(s16)(op & 0xFFFF);  // sign extension
		u32 uimm = op & 0xFFFF;
		u32 suimm = (u32)(s32)simm;

		int rt = _RT;
		int rs = _RS;

		u8 o = op>>26;

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

		case 10: // slti
			{
				if (gpr.IsImm(rs))
				{
					gpr.SetImm(rt, (s32)gpr.GetImm(rs) < simm);
					break;
				}
				/*
				//Break();
				gpr.MapDirtyIn(rt, rs);

				// Can't find better :s
				CMPI(gpr.R(rs), uimm);

				PpcGen::FixupBranch ptr = B_Cond(_BLT);

				ptr = B_Cond(_BLT);

				MOVI2R(gpr.R(rt), 1);
				SetJumpTarget(ptr);
				MOVI2R(gpr.R(rt), 0);
				break;
				*/
			}

		case 11: //sltiu
			{
				if (gpr.IsImm(rs))
				{
					gpr.SetImm(rt, gpr.GetImm(rs) < uimm);
					break;
				}
				/*
				//Break();
				gpr.MapDirtyIn(rt, rs);

				// Can't find better :s
				CMPLI(gpr.R(rs), uimm);

				PpcGen::FixupBranch ptr = B_Cond(_BLT);

				MOVI2R(gpr.R(rt), 1);
				SetJumpTarget(ptr);
				MOVI2R(gpr.R(rt), 0);
				break;
				*/
			}

		default:
			Comp_Generic(op);
			break;
		}
	}

void Jit::Comp_RType2(u32 op) {
	Comp_Generic(op);
}


void Jit::Comp_RType3(u32 op) {
	CONDITIONAL_DISABLE;
	int rt = _RT;
	int rs = _RS;
	int rd = _RD;

	// noop, won't write to ZERO.
	if (rd == 0)
		return;

	switch (op & 63) 
	{
	
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
	default:
		Comp_Generic(op);
		break;
	}
}

void Jit::Comp_ShiftType(u32 op) {
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
			RLWINM(gpr.R(rd), gpr.R(rt), sa, 0, (31-sa));
			break;
		case 2: //srl
			gpr.MapDirtyIn(rd, rt);	
			RLWINM(gpr.R(rd), gpr.R(rt), (32-sa), sa, 31);
			break;
		case 3: //sra
			gpr.MapDirtyIn(rd, rt);	
			SRAWI(gpr.R(rd), gpr.R(rt), sa);
			break;

		case 4: //sllv
			if (gpr.IsImm(rs))
			{
				int sa = gpr.GetImm(rs) & 0x1F;
				gpr.MapDirtyIn(rd, rt);
				RLWINM(gpr.R(rd), gpr.R(rt), sa, 0, (31-sa));
				break;
			}
			gpr.MapDirtyInIn(rd, rs, rt);
			ANDI(SREG, gpr.R(rs), 0x1F);
			SLW(gpr.R(rd), gpr.R(rt), SREG);
			break;
		case 6: //srlv
			if (gpr.IsImm(rs))
			{
				int sa = gpr.GetImm(rs) & 0x1F;
				gpr.MapDirtyIn(rd, rt);
				RLWINM(gpr.R(rd), gpr.R(rt), (32-sa), sa, 31);
				break;
			}
			gpr.MapDirtyInIn(rd, rs, rt);
			ANDI(SREG, gpr.R(rs), 0x1F);
			SRW(gpr.R(rd), gpr.R(rt), SREG);
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

void Jit::Comp_Allegrex(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_Allegrex2(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_MulDivType(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_Special3(u32 op) {
	Comp_Generic(op);
}

}