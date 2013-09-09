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

using namespace PpcGen;

namespace MIPSComp
{
	
void Jit::Comp_FPU3op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int ft = _FT;
	int fs = _FS;
	int fd = _FD;
	
	fpr.MapDirtyInIn(fd, fs, ft);
	switch (op & 0x3f) 
	{
	case 0: FADDS(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) + F(ft); //add
	case 1: FSUBS(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) - F(ft); //sub
	case 2: { //F(fd) = F(fs) * F(ft); //mul
		FMULS(fpr.R(fd), fpr.R(fs), fpr.R(ft));
		break;
	}
	case 3: FDIVS(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) / F(ft); //div
	default:
		DISABLE;
		return;
	}
}

void Jit::Comp_FPULS(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	s32 offset = (s16)(op & 0xFFFF);
	int ft = _FT;
	int rs = _RS;
	// u32 addr = R(rs) + offset;
	// logBlocks = 1;
	bool doCheck = false;

	switch(op >> 26)
	{
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		fpr.SpillLock(ft);
		fpr.MapReg(ft, MAP_NOINIT | MAP_DIRTY);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			MOVI2R(SREG, addr);
		} else {
			gpr.MapReg(rs);			
			SetRegToEffectiveAddress(SREG, rs, offset);
		}

		LoadFloatSwap(fpr.R(ft), BASEREG, SREG);

		fpr.ReleaseSpillLocksAndDiscardTemps();
		break;

	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		fpr.MapReg(ft);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			MOVI2R(SREG, addr);
		} else {
			gpr.MapReg(rs);
			SetRegToEffectiveAddress(SREG, rs, offset);
		}

		SaveFloatSwap(fpr.R(ft), BASEREG, SREG);
		break;

	default:
		Comp_Generic(op);
		return;
	}
}

/**
This can be made with branch, but i'm trying to do it branch free, not tested yet ...
**/
void Jit::Comp_FPUComp(MIPSOpcode op) {
	DISABLE;	
	CONDITIONAL_DISABLE;


	int opc = op & 0xF;
	if (opc >= 8) opc -= 8; // alias
	if (opc == 0) {  // f, sf (signalling false)
		MOVI2R(SREG, 0);
		STW(SREG, CTXREG, offsetof(MIPSState, fpcond));
		return;
	}

	int fs = _FS;
	int ft = _FT;
	fpr.MapInIn(fs, ft);

	PPCReg _tmp = FPR8;
	PPCReg _zero = FPR6;
	PPCReg _one = FPR7;

	//VCMP(fpr.R(fs), fpr.R(ft));
	//VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).

	/**
	Condition-Register Field and Floating-Point Condition Code Interpretation
	Bit	Name	Description
	0	FL	(FRA) < (FRB)
	1	FG	(FRA) > (FRB)
	2	FE	(FRA) = (FRB)
	3	FU	(FRA) ? (FRB) (unordered)
	**/
	switch(opc)
	{
	case 1:		 // un,  ngle (unordered)
		Break();
		// CR0 = cmp fs, fs
		FCMPU(0, fpr.R(fs), fpr.R(ft));
		// SREG = CR
		MFCR(SREG);
		// SREG = (SREG >> 3) & 1
		SRAWI(SREG, SREG, 3);
		ANDI(SREG, SREG, 0x1);
		break;
	case 2:		 //eq,  seq (equal, ordered)
		Break();
		FCMPO(0, fpr.R(fs), fpr.R(ft));
		MFCR(SREG);
		// SREG = (SREG >> 2) & 1
		SRAWI(SREG, SREG, 2);
		ANDI(SREG, SREG, 0x1);
		break;
	case 3:      // ueq, ngl (equal, unordered)
		Break();
		FCMPU(0, fpr.R(fs), fpr.R(ft));
		MFCR(R7);
		// SREG = (R7 >> 2) & 1
		SRAWI(SREG, R7, 2);
		ANDI(SREG, SREG, 0x1);
		// check unordered
		// R8 = (R7 >> 3) & 1
		SRAWI(R8, R7, 3);
		ANDI(R8, R8, 0x1);
		// SREG = ((R7 >> 2) & 1) || ((R8 >> 3) & 1)
		OR(SREG, R7, R8);
		return;
	case 4:      // olt, lt (less than, ordered)
		Break();
		FCMPO(0, fpr.R(fs), fpr.R(ft));
		MFCR(SREG);
		// SREG = SREG & 1
		ANDI(SREG, SREG, 0x1);
		break;
	case 5:      // ult, nge (less than, unordered)
		Break();
		FCMPO(0, fpr.R(fs), fpr.R(ft));
		MFCR(R7);
		// SREG = SREG & 1
		ANDI(SREG, R7, 0x1);
		// check unordered
		// R8 = (R7 >> 3) & 1
		SRAWI(R8, R7, 3);
		ANDI(R8, R8, 0x1);
		// SREG = (R7 & 1) || ((R8 >> 3) & 1)
		OR(SREG, R7, R8);
		break;
	case 6:      // ole, le (less equal, ordered)
		Break();
		FCMPO(0, fpr.R(ft), fpr.R(fs));
		MFCR(SREG);
		// SREG = (SREG >> 1) & 1
		SRAWI(SREG, SREG, 1);
		ANDI(SREG, SREG, 0x1);
		break;
	case 7:      // ule, ngt (less equal, unordered)
		Break();
		FCMPO(0, fpr.R(ft), fpr.R(fs));
		MFCR(R7);
		// SREG = (SREG >> 1) & 1
		SRAWI(SREG, R7, 1);
		ANDI(SREG, SREG, 0x1);
		// check unordered
		// R8 = (R7 >> 3) & 1
		SRAWI(R8, R7, 3);
		ANDI(R8, R8, 0x1);
		// SREG = (R7 & 1) || ((R8 >> 3) & 1)
		OR(SREG, R7, R8);
		break;
	default:
		Comp_Generic(op);
		return;
	}
	STW(SREG, CTXREG, offsetof(MIPSState, fpcond));
}

void Jit::Comp_FPU2op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int fs = _FS;
	int fd = _FD;

	switch (op & 0x3f) 
	{
	case 4:	//F(fd)	   = sqrtf(F(fs));            break; //sqrt
		fpr.MapDirtyIn(fd, fs);
		FSQRTS(fpr.R(fd), fpr.R(fs));
		break;
	case 5:	//F(fd)    = fabsf(F(fs));            break; //abs
		fpr.MapDirtyIn(fd, fs);
		FABS(fpr.R(fd), fpr.R(fs));
		break;
	case 6:	//F(fd)	   = F(fs);                   break; //mov
		fpr.MapDirtyIn(fd, fs);
		FMR(fpr.R(fd), fpr.R(fs));
		break;
	case 7:	//F(fd)	   = -F(fs);                  break; //neg
		fpr.MapDirtyIn(fd, fs);
		FNEG(fpr.R(fd), fpr.R(fs));
		break;
	default:
	Comp_Generic(op);
		break;
	}
}

/**
Not tested yet
**/
void Jit::Comp_mxc1(MIPSOpcode op) {
	DISABLE;
	CONDITIONAL_DISABLE;

	int fs = _FS;
	MIPSGPReg rt = _RT;

	switch ((op >> 21) & 0x1f)
	{
	case 0: // R(rt) = FI(fs); break; //mfc1
		// Let's just go through RAM for now.
		fpr.FlushR(fs);
		gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
		LWZ(gpr.R(rt), CTXREG, fpr.GetMipsRegOffset(fs));
		return;

	case 2: //cfc1
		if (fs == 31)
		{	
			/* Todo Lazy code ! */
			gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
			PPCReg _rt = gpr.R(rt);

			// SREG = fpcond & 1;
			LWZ(SREG, CTXREG, offsetof(MIPSState, fpcond));
			ANDI(SREG, SREG, 1); // Just in case
			// SREG << 23
			SLWI(SREG, SREG, 23);
			
			// RT = fcr31 & ~(1<<23)
			LWZ(_rt, CTXREG, offsetof(MIPSState, fcr31));
			MOVI2R(R8,  ~(1<<23));
			AND(_rt, _rt, R8);

			// RT = RT | SREG
			OR(_rt, _rt, SREG);
		}
		else if (fs == 0)
		{
			gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
			LWZ(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr0));
		}
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		// Let's just go through RAM for now.
		gpr.FlushR(rt);
		fpr.MapReg(fs, MAP_DIRTY | MAP_NOINIT);
		LFS(fpr.R(fs), CTXREG, gpr.GetMipsRegOffset(rt));
		return;

	case 6: //ctc1
		if (fs == 31)
		{
			gpr.MapReg(rt, 0);

			// Update MIPS state
			// fcr31 = rt
			STW(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));

			// fpcond = (rt >> 23) & 1;
			SRWI(SREG, gpr.R(rt), 23);
			ANDI(SREG, SREG, 1);
			STW(SREG, CTXREG, offsetof(MIPSState, fpcond));
		}
		return;
	}
}

}