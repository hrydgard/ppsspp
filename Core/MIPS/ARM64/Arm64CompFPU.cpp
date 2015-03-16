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

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Common/CPUDetect.h"

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

namespace MIPSComp {
	using namespace Arm64Gen;
	using namespace Arm64JitConstants;

void Arm64Jit::Comp_FPU3op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int ft = _FT;
	int fs = _FS;
	int fd = _FD;

	fpr.MapDirtyInIn(fd, fs, ft);
	switch (op & 0x3f) {
	case 0: fp.FADD(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) + F(ft); //add
	case 1: fp.FSUB(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) - F(ft); //sub
	case 2: fp.FMUL(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) * F(ft); //mul
	case 3: fp.FDIV(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) / F(ft); //div
	default:
		DISABLE;
		return;
	}
}

void Arm64Jit::Comp_FPULS(MIPSOpcode op)
{
	CONDITIONAL_DISABLE;

	// Surprisingly, these work fine alraedy.

	s32 offset = (s16)(op & 0xFFFF);
	int ft = _FT;
	MIPSGPReg rs = _RS;
	// u32 addr = R(rs) + offset;
	// logBlocks = 1;
	bool doCheck = false;
	switch (op >> 26) {
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		fpr.SpillLock(ft);
		fpr.MapReg(ft, MAP_NOINIT | MAP_DIRTY);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			gpr.SetRegImm(SCRATCH1_64, (uintptr_t)(Memory::base + addr));
		} else {
			gpr.MapReg(rs);
			if (g_Config.bFastMemory) {
				SetScratch1ToEffectiveAddress(rs, offset);
			} else {
				SetCCAndSCRATCH1ForSafeAddress(rs, offset, SCRATCH2);
				doCheck = true;
			}
			ADD(SCRATCH1_64, SCRATCH1_64, MEMBASEREG);
		}
		FixupBranch skip;
		if (doCheck) {
			skip = B(CC_EQ);
		}
		LDR(INDEX_UNSIGNED, fpr.R(ft), SCRATCH1_64, 0);
		if (doCheck) {
			SetJumpTarget(skip);
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();
		break;

	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		fpr.MapReg(ft);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			gpr.SetRegImm(SCRATCH1_64, addr + (uintptr_t)(Memory::base));
		} else {
			gpr.MapReg(rs);
			if (g_Config.bFastMemory) {
				SetScratch1ToEffectiveAddress(rs, offset);
			} else {
				SetCCAndSCRATCH1ForSafeAddress(rs, offset, SCRATCH2);
				doCheck = true;
			}
			ADD(SCRATCH1_64, SCRATCH1_64, MEMBASEREG);
		}
		FixupBranch skip2;
		if (doCheck) {
			skip2 = B(CC_EQ);
		}
		STR(INDEX_UNSIGNED, fpr.R(ft), SCRATCH1_64, 0);
		if (doCheck) {
			SetJumpTarget(skip2);
		}
		break;

	default:
		Comp_Generic(op);
		return;
	}
}

void Arm64Jit::Comp_FPUComp(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_FPU2op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	// These don't work for some reason.
	DISABLE;
	int fs = _FS;
	int fd = _FD;

	// TODO: Most of these mishandle infinity/NAN.
	// Maybe we can try to track per reg if they *could* be INF/NAN to optimize out?

	switch (op & 0x3f) {
	case 4:	//F(fd)	   = sqrtf(F(fs));            break; //sqrt
		fpr.MapDirtyIn(fd, fs);
		fp.FSQRT(fpr.R(fd), fpr.R(fs));
		break;
	case 5:	//F(fd)    = fabsf(F(fs));            break; //abs
		fpr.MapDirtyIn(fd, fs);
		fp.FABS(fpr.R(fd), fpr.R(fs));
		break;
	case 6:	//F(fd)	   = F(fs);                   break; //mov
		fpr.MapDirtyIn(fd, fs);
		fp.FMOV(fpr.R(fd), fpr.R(fs));
		break;
	case 7:	//F(fd)	   = -F(fs);                  break; //neg
		fpr.MapDirtyIn(fd, fs);
		fp.FNEG(fpr.R(fd), fpr.R(fs));
		break;
		/*
	case 12: //FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s
		RestoreRoundingMode();
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED);
		break;
	case 13: //FsI(fd) = Rto0(F(fs)));            break; //trunc.w.s
		fpr.MapDirtyIn(fd, fs);
		VCMP(fpr.R(fs), fpr.R(fs));
		VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED | ROUND_TO_ZERO);
		VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
		SetCC(CC_VS);
		MOVIU2F(fpr.R(fd), 0x7FFFFFFF, SCRATCHREG1);
		SetCC(CC_AL);
		break;
	case 14: //FsI(fd) = (int)ceilf (F(fs));      break; //ceil.w.s
	{
		RestoreRoundingMode();
		fpr.MapDirtyIn(fd, fs);
		VMRS(SCRATCHREG2);
		// Assume we're always in round-to-nearest mode.
		ORR(SCRATCHREG1, SCRATCHREG2, AssumeMakeOperand2(1 << 22));
		VMSR(SCRATCHREG1);
		VCMP(fpr.R(fs), fpr.R(fs));
		VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED);
		VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
		SetCC(CC_VS);
		MOVIU2F(fpr.R(fd), 0x7FFFFFFF, SCRATCHREG1);
		SetCC(CC_AL);
		// Set the rounding mode back.  TODO: Keep it?  Dirty?
		VMSR(SCRATCHREG2);
		break;
	}
	case 15: //FsI(fd) = (int)floorf(F(fs));      break; //floor.w.s
	{
		RestoreRoundingMode();
		fpr.MapDirtyIn(fd, fs);
		VMRS(SCRATCHREG2);
		// Assume we're always in round-to-nearest mode.
		ORR(SCRATCHREG1, SCRATCHREG2, AssumeMakeOperand2(2 << 22));
		VMSR(SCRATCHREG1);
		VCMP(fpr.R(fs), fpr.R(fs));
		VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED);
		VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
		SetCC(CC_VS);
		MOVIU2F(fpr.R(fd), 0x7FFFFFFF, SCRATCHREG1);
		SetCC(CC_AL);
		// Set the rounding mode back.  TODO: Keep it?  Dirty?
		VMSR(SCRATCHREG2);
		break;
	}
	case 32: //F(fd)   = (float)FsI(fs);          break; //cvt.s.w
		fpr.MapDirtyIn(fd, fs);
		VCVT(fpr.R(fd), fpr.R(fs), TO_FLOAT | IS_SIGNED);
		break;
	case 36: //FsI(fd) = (int)  F(fs);            break; //cvt.w.s
		fpr.MapDirtyIn(fd, fs);
		VCMP(fpr.R(fs), fpr.R(fs));
		VCVT(fpr.R(fd), fpr.R(fs), TO_INT | IS_SIGNED);
		VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
		SetCC(CC_VS);
		MOVIU2F(fpr.R(fd), 0x7FFFFFFF, SCRATCHREG1);
		SetCC(CC_AL);
		break;
		*/
	default:
		DISABLE;
	}
}

void Arm64Jit::Comp_mxc1(MIPSOpcode op)
{
	DISABLE;
}

}	// namespace MIPSComp
