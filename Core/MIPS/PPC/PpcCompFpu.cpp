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

#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define _FS   ((op>>11) & 0x1F)
#define _FT   ((op>>16) & 0x1F)
#define _FD   ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

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
		/*
		u32 nextOp = Memory::Read_Instruction(js.compilerPC + 4);
		// Optimization possible if destination is the same
		if (fd == (int)((nextOp>>6) & 0x1F)) {
			// VMUL + VNEG -> VNMUL
			if (!strcmp(MIPSGetName(nextOp), "neg.s")) {
				if (fd == (int)((nextOp>>11) & 0x1F)) {
					VNMUL(fpr.R(fd), fpr.R(fs), fpr.R(ft));
					EatInstruction(nextOp);
				}
				return;
			}
		}
		*/
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
			MOVI2R(R0, addr);
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

void Jit::Comp_FPUComp(MIPSOpcode op) {
	Comp_Generic(op);
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

void Jit::Comp_mxc1(MIPSOpcode op) {
	Comp_Generic(op);
}

}