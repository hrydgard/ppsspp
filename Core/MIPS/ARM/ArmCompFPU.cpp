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

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
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

// #define CONDITIONAL_DISABLE(flag) { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
	using namespace ArmGen;
	using namespace ArmJitConstants;

void ArmJit::Comp_FPU3op(MIPSOpcode op)
{ 
	CONDITIONAL_DISABLE(FPU);

	int ft = _FT;
	int fs = _FS;
	int fd = _FD;
	
	fpr.MapDirtyInIn(fd, fs, ft);
	switch (op & 0x3f) 
	{
	case 0: VADD(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) + F(ft); //add
	case 1: VSUB(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) - F(ft); //sub
	case 2: { //F(fd) = F(fs) * F(ft); //mul
		MIPSOpcode nextOp = GetOffsetInstruction(1);
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
		VMUL(fpr.R(fd), fpr.R(fs), fpr.R(ft));
		break;
	}
	case 3: VDIV(fpr.R(fd), fpr.R(fs), fpr.R(ft)); break; //F(fd) = F(fs) / F(ft); //div
	default:
		DISABLE;
		return;
	}
}

extern int logBlocks;

void ArmJit::Comp_FPULS(MIPSOpcode op)
{
	CONDITIONAL_DISABLE(LSU_FPU);
	CheckMemoryBreakpoint();

	s32 offset = SignExtend16ToS32(op & 0xFFFF);
	int ft = _FT;
	MIPSGPReg rs = _RS;
	// u32 addr = R(rs) + offset;
	// logBlocks = 1;
	bool doCheck = false;
	switch(op >> 26)
	{
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset < 0x400 && offset > -0x400) {
			gpr.MapRegAsPointer(rs);
			fpr.MapReg(ft, MAP_NOINIT | MAP_DIRTY);
			VLDR(fpr.R(ft), gpr.RPtr(rs), offset);
			break;
		}

		fpr.SpillLock(ft);
		fpr.MapReg(ft, MAP_NOINIT | MAP_DIRTY);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			gpr.SetRegImm(R0, addr + (u32)Memory::base);
		} else {
			gpr.MapReg(rs);
			if (g_Config.bFastMemory) {
				SetR0ToEffectiveAddress(rs, offset);
			} else {
				SetCCAndR0ForSafeAddress(rs, offset, SCRATCHREG2);
				doCheck = true;
			}
			ADD(R0, R0, MEMBASEREG);
		}
#ifdef __ARM_ARCH_7S__
		FixupBranch skip;
		if (doCheck) {
			skip = B_CC(CC_EQ);
		}
		VLDR(fpr.R(ft), R0, 0);
		if (doCheck) {
			SetJumpTarget(skip);
		}
#else
		VLDR(fpr.R(ft), R0, 0);
		if (doCheck) {
			SetCC(CC_EQ);
			MOVI2R(R0, 0);
			VMOV(fpr.R(ft), R0);
			SetCC(CC_AL);
		}
#endif
		fpr.ReleaseSpillLocksAndDiscardTemps();
		break;

	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset < 0x400 && offset > -0x400) {
			gpr.MapRegAsPointer(rs);
			fpr.MapReg(ft, 0);
			VSTR(fpr.R(ft), gpr.RPtr(rs), offset);
			break;
		}

		fpr.MapReg(ft);
		if (gpr.IsImm(rs)) {
			u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
			gpr.SetRegImm(R0, addr + (u32)Memory::base);
		} else {
			gpr.MapReg(rs);
			if (g_Config.bFastMemory) {
				SetR0ToEffectiveAddress(rs, offset);
			} else {
				SetCCAndR0ForSafeAddress(rs, offset, SCRATCHREG2);
				doCheck = true;
			}
			ADD(R0, R0, MEMBASEREG);
		}
#ifdef __ARM_ARCH_7S__
		FixupBranch skip2;
		if (doCheck) {
			skip2 = B_CC(CC_EQ);
		}
		VSTR(fpr.R(ft), R0, 0);
		if (doCheck) {
			SetJumpTarget(skip2);
		}
#else
		VSTR(fpr.R(ft), R0, 0);
		if (doCheck) {
			SetCC(CC_AL);
		}
#endif
		break;

	default:
		Comp_Generic(op);
		return;
	}
}

void ArmJit::Comp_FPUComp(MIPSOpcode op) {
	CONDITIONAL_DISABLE(FPU_COMP);

	int opc = op & 0xF;
	if (opc >= 8) opc -= 8; // alias
	if (opc == 0) {  // f, sf (signalling false)
		gpr.SetImm(MIPS_REG_FPCOND, 0);
		return;
	}

	int fs = _FS;
	int ft = _FT;
	gpr.MapReg(MIPS_REG_FPCOND, MAP_DIRTY | MAP_NOINIT);
	fpr.MapInIn(fs, ft);
	VCMP(fpr.R(fs), fpr.R(ft));
	VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
	switch(opc)
	{
	case 1:      // un,  ngle (unordered)
		SetCC(CC_VS);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_VC);
		break;
	case 2:      // eq,  seq (equal, ordered)
		SetCC(CC_EQ);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_NEQ);
		break;
	case 3:      // ueq, ngl (equal, unordered)
		SetCC(CC_EQ);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_NEQ);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 0);
		SetCC(CC_VS);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_AL);
		return;
	case 4:      // olt, lt (less than, ordered)
		SetCC(CC_LO);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_HS);
		break;
	case 5:      // ult, nge (less than, unordered)
		SetCC(CC_LT);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_GE);
		break;
	case 6:      // ole, le (less equal, ordered)
		SetCC(CC_LS);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_HI);
		break;
	case 7:      // ule, ngt (less equal, unordered)
		SetCC(CC_LE);
		MOVI2R(gpr.R(MIPS_REG_FPCOND), 1);
		SetCC(CC_GT);
		break;
	default:
		Comp_Generic(op);
		return;
	}
	MOVI2R(gpr.R(MIPS_REG_FPCOND), 0);
	SetCC(CC_AL);
}

void ArmJit::Comp_FPU2op(MIPSOpcode op) {
	CONDITIONAL_DISABLE(FPU);

	int fs = _FS;
	int fd = _FD;

	switch (op & 0x3f) {
	case 4:	//F(fd)	   = sqrtf(F(fs));            break; //sqrt
		fpr.MapDirtyIn(fd, fs);
		VSQRT(fpr.R(fd), fpr.R(fs));
		break;
	case 5:	//F(fd)    = fabsf(F(fs));            break; //abs
		fpr.MapDirtyIn(fd, fs);
		VABS(fpr.R(fd), fpr.R(fs));
		break;
	case 6:	//F(fd)	   = F(fs);                   break; //mov
		fpr.MapDirtyIn(fd, fs);
		VMOV(fpr.R(fd), fpr.R(fs));
		break;
	case 7:	//F(fd)	   = -F(fs);                  break; //neg
		fpr.MapDirtyIn(fd, fs);
		VNEG(fpr.R(fd), fpr.R(fs));
		break;
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
	default:
		DISABLE;
	}
}

void ArmJit::Comp_mxc1(MIPSOpcode op)
{
	CONDITIONAL_DISABLE(FPU_XFER);

	int fs = _FS;
	MIPSGPReg rt = _RT;

	switch ((op >> 21) & 0x1f)
	{
	case 0: // R(rt) = FI(fs); break; //mfc1
		if (rt == MIPS_REG_ZERO) {
			return;
		}
		gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
		if (fpr.IsMapped(fs)) {
			VMOV(gpr.R(rt), fpr.R(fs));
		} else {
			LDR(gpr.R(rt), CTXREG, fpr.GetMipsRegOffset(fs));
		}
		return;

	case 2: //cfc1
		if (rt == MIPS_REG_ZERO) {
			return;
		}
		if (fs == 31) {
			if (gpr.IsImm(MIPS_REG_FPCOND)) {
				gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
				LDR(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
				if (gpr.GetImm(MIPS_REG_FPCOND) & 1) {
					ORI2R(gpr.R(rt), gpr.R(rt), 0x1 << 23, SCRATCHREG2);
				} else {
					ANDI2R(gpr.R(rt), gpr.R(rt), ~(0x1 << 23), SCRATCHREG2);
				}
			} else {
				gpr.MapDirtyIn(rt, MIPS_REG_FPCOND);
				LDR(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
#if PPSSPP_ARCH(ARMV7)
				BFI(gpr.R(rt), gpr.R(MIPS_REG_FPCOND), 23, 1);
#else
				AND(SCRATCHREG1, gpr.R(MIPS_REG_FPCOND), Operand2(1)); // Just in case
				ANDI2R(gpr.R(rt), gpr.R(rt), ~(0x1 << 23), SCRATCHREG2);  // SCRATCHREG2 won't be used, this turns into a simple BIC.
				ORR(gpr.R(rt), gpr.R(rt), Operand2(SCRATCHREG1, ST_LSL, 23));
#endif
			}
		} else if (fs == 0) {
			gpr.SetImm(rt, MIPSState::FCR0_VALUE);
		} else {
			// Unsupported regs are always 0.
			gpr.SetImm(rt, 0);
		}
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0) {
			fpr.MapReg(fs, MAP_NOINIT);
			MOVI2F(fpr.R(fs), 0.0f, R0);
		} else {
			gpr.MapReg(rt);
			fpr.MapReg(fs, MAP_NOINIT);
			VMOV(fpr.R(fs), gpr.R(rt));
		}
		return;

	case 6: //ctc1
		if (fs == 31) {
			// Must clear before setting, since ApplyRoundingMode() assumes it was cleared.
			RestoreRoundingMode();
			bool wasImm = gpr.IsImm(rt);
			u32 immVal = -1;
			if (wasImm) {
				immVal = gpr.GetImm(rt);
				gpr.SetImm(MIPS_REG_FPCOND, (immVal >> 23) & 1);
				gpr.MapReg(rt);
			} else {
				gpr.MapDirtyIn(MIPS_REG_FPCOND, rt);
			}

			// Update MIPS state
			// TODO: Technically, should mask by 0x0181FFFF.  Maybe just put all of FCR31 in the reg?
			STR(gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
			if (!wasImm) {
#if PPSSPP_ARCH(ARMV7)
				UBFX(gpr.R(MIPS_REG_FPCOND), gpr.R(rt), 23, 1);
#else
				MOV(SCRATCHREG1, Operand2(gpr.R(rt), ST_LSR, 23));
				AND(gpr.R(MIPS_REG_FPCOND), SCRATCHREG1, Operand2(1));
#endif
				UpdateRoundingMode();
			} else {
				UpdateRoundingMode(immVal);
			}
			ApplyRoundingMode();
		} else {
			Comp_Generic(op);
		}
		return;
	}
}

}	// namespace MIPSComp

#endif // PPSSPP_ARCH(ARM)
