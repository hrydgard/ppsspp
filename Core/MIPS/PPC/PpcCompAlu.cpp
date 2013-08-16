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
	static u32 EvalEor(u32 a, u32 b) { return a ^ b; }
	static u32 EvalAnd(u32 a, u32 b) { return a & b; }
	static u32 EvalAdd(u32 a, u32 b) { return a + b; }
	static u32 EvalSub(u32 a, u32 b) { return a - b; }

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
					ADDI(gpr.R(rt), gpr.R(rs), simm);
				}
				break;
			}
		case 15: // R(rt) = uimm << 16;	 //lui
			gpr.SetImm(rt, uimm << 16);
			break;
		default:
			Comp_Generic(op);
			break;
		}
	}

void Jit::Comp_RType2(u32 op) {
	Comp_Generic(op);
}

// Utilities to reduce duplicated code
void Jit::CompImmLogic(int rs, int rt, u32 uimm, void (PPCXEmitter::*arith)(PPCReg Rd, PPCReg Ra, PPCReg Rb), u32 (*eval)(u32 a, u32 b)) {
	DebugBreak();
}
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
		CompType3(rd, rs, rt, &PPCXEmitter::XOR, &EvalEor);
		break;
	default:
		Comp_Generic(op);
		break;
	}
}

void Jit::Comp_ShiftType(u32 op) {
	Comp_Generic(op);
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