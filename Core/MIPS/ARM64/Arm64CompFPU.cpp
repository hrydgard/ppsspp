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


// FPCR interesting bits:
// 24: FZ (flush-to-zero)
// 23:22: RMode (0 = nearest, 1 = +inf, 2 = -inf, 3 = zero)
// not much else is interesting for us, but should be preserved.
// To access: MRS Xt, FPCR ;  MSR FPCR, Xt


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
		if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset <= 16380 && offset >= 0) {
			gpr.MapRegAsPointer(rs);
			fpr.MapReg(ft, MAP_NOINIT | MAP_DIRTY);
			fp.LDR(32, INDEX_UNSIGNED, fpr.R(ft), gpr.RPtr(rs), offset);
			break;
		}

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
			MOVK(SCRATCH1_64, ((uint64_t)Memory::base) >> 32, SHIFT_32);
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
		if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset <= 16380 && offset >= 0) {
			gpr.MapRegAsPointer(rs);
			fpr.MapReg(ft, 0);
			fp.STR(32, INDEX_UNSIGNED, fpr.R(ft), gpr.RPtr(rs), offset);
			break;
		}

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
			MOVK(SCRATCH1_64, ((uint64_t)Memory::base) >> 32, SHIFT_32);
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
	CONDITIONAL_DISABLE;

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
	fp.FCMP(fpr.R(fs), fpr.R(ft));

	switch (opc) {
	case 1:      // un,  ngle (unordered)
		CSET(gpr.R(MIPS_REG_FPCOND), CC_VS);
		break;
	case 2:      // eq,  seq (equal, ordered)
		CSET(gpr.R(MIPS_REG_FPCOND), CC_EQ);
		break;
	case 3:      // ueq, ngl (equal, unordered)
		CSET(gpr.R(MIPS_REG_FPCOND), CC_EQ);
		CSET(SCRATCH1, CC_VS);
		ORR(gpr.R(MIPS_REG_FPCOND), gpr.R(MIPS_REG_FPCOND), SCRATCH1);
		return;
	case 4:      // olt, lt (less than, ordered)
		CSET(gpr.R(MIPS_REG_FPCOND), CC_LO);
		break;
	case 5:      // ult, nge (less than, unordered)
		CSET(gpr.R(MIPS_REG_FPCOND), CC_LT);
		break;
	case 6:      // ole, le (less equal, ordered)
		CSET(gpr.R(MIPS_REG_FPCOND), CC_LS);
		break;
	case 7:      // ule, ngt (less equal, unordered)
		CSET(gpr.R(MIPS_REG_FPCOND), CC_LE);
		break;
	default:
		Comp_Generic(op);
		return;
	}
}

void Arm64Jit::Comp_FPU2op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	int fs = _FS;
	int fd = _FD;

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

	case 12: //FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s
	{
		fpr.MapDirtyIn(fd, fs);
		fp.FCMP(fpr.R(fs), fpr.R(fs));  // Detect NaN
		fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_N);  // to nearest, ties to even
		FixupBranch skip = B(CC_VC);
		MOVI2R(SCRATCH1, 0x7FFFFFFF);
		fp.FMOV(fpr.R(fd), SCRATCH1);
		SetJumpTarget(skip);
		break;
	}

	case 13: //FsI(fd) = Rto0(F(fs)));            break; //trunc.w.s
	{
		fpr.MapDirtyIn(fd, fs);
		fp.FCMP(fpr.R(fs), fpr.R(fs));
		fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_Z);
		FixupBranch skip = B(CC_VC);
		MOVI2R(SCRATCH1, 0x7FFFFFFF);
		fp.FMOV(fpr.R(fd), SCRATCH1);
		SetJumpTarget(skip);
		break;
	}

	case 14://FsI(fd) = (int)ceilf (F(fs));      break; //ceil.w.s
	{
		fpr.MapDirtyIn(fd, fs);
		fp.FCMP(fpr.R(fs), fpr.R(fs));
		fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_P);  // towards +inf
		FixupBranch skip = B(CC_VC);
		MOVI2R(SCRATCH1, 0x7FFFFFFF);
		fp.FMOV(fpr.R(fd), SCRATCH1);
		SetJumpTarget(skip);
		break;
	}
	case 15: //FsI(fd) = (int)floorf(F(fs));      break; //floor.w.s
	{
		fpr.MapDirtyIn(fd, fs);
		fp.FCMP(fpr.R(fs), fpr.R(fs));
		fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_M);  // towards -inf
		FixupBranch skip = B(CC_VC);
		MOVI2R(SCRATCH1, 0x7FFFFFFF);
		fp.FMOV(fpr.R(fd), SCRATCH1);
		SetJumpTarget(skip);
		break;
	}

	case 32: //F(fd)   = (float)FsI(fs);          break; //cvt.s.w
		fpr.MapDirtyIn(fd, fs);
		fp.SCVTF(fpr.R(fd), fpr.R(fs));
		break;

	case 36: //FsI(fd) = (int)  F(fs);            break; //cvt.w.s
		fpr.MapDirtyIn(fd, fs);
		if (js.hasSetRounding) {
			// Urgh, this looks awfully expensive and bloated.. Perhaps we should have a global function pointer to the right FCVTS to use,
			// and update it when the rounding mode is switched.
			fp.FCMP(fpr.R(fs), fpr.R(fs));
			FixupBranch skip_nan = B(CC_VC);
			MOVI2R(SCRATCH1, 0x7FFFFFFF);
			fp.FMOV(fpr.R(fd), SCRATCH1);
			FixupBranch skip_rest = B();
			// MIPS Rounding Mode:
			//   0: Round nearest
			//   1: Round to zero
			//   2: Round up (ceil)
			//   3: Round down (floor)
			SetJumpTarget(skip_nan);
			LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, fcr31));
			ANDI2R(SCRATCH1, SCRATCH1, 3);
			ADR(SCRATCH2_64, 12);  // PC + 12 = address of first FCVTS below
			ADD(SCRATCH2_64, SCRATCH2_64, EncodeRegTo64(SCRATCH1), ArithOption(SCRATCH2_64, ST_LSL, 3));
			BR(SCRATCH2_64);  // choose from the four variants below!
			fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_N);
			FixupBranch skip1 = B();
			fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_Z);
			FixupBranch skip2 = B();
			fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_P);
			FixupBranch skip3 = B();
			fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_M);
			SetJumpTarget(skip1);
			SetJumpTarget(skip2);
			SetJumpTarget(skip3);
			SetJumpTarget(skip_rest);
		} else {
			fp.FCMP(fpr.R(fs), fpr.R(fs));
			fp.FCVTS(fpr.R(fd), fpr.R(fs), ROUND_Z);
			FixupBranch skip_nan = B(CC_VC);
			MOVI2R(SCRATCH1, 0x7FFFFFFF);
			fp.FMOV(fpr.R(fd), SCRATCH1);
			SetJumpTarget(skip_nan);
		}
		break;

	default:
		DISABLE;
	}
}

void Arm64Jit::Comp_mxc1(MIPSOpcode op)
{
	CONDITIONAL_DISABLE;

	int fs = _FS;
	MIPSGPReg rt = _RT;

	switch ((op >> 21) & 0x1f) {
	case 0: // R(rt) = FI(fs); break; //mfc1
		if (rt == MIPS_REG_ZERO) {
			return;
		}
		gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
		if (fpr.IsMapped(fs)) {
			fp.FMOV(gpr.R(rt), fpr.R(fs));
		} else {
			LDR(INDEX_UNSIGNED, gpr.R(rt), CTXREG, fpr.GetMipsRegOffset(fs));
		}
		return;

	case 2: //cfc1
		if (rt == MIPS_REG_ZERO) {
			return;
		}
		if (fs == 31) {
			if (gpr.IsImm(MIPS_REG_FPCOND)) {
				gpr.MapReg(rt, MAP_DIRTY | MAP_NOINIT);
				LDR(INDEX_UNSIGNED, gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
				if (gpr.GetImm(MIPS_REG_FPCOND) & 1) {
					ORRI2R(gpr.R(rt), gpr.R(rt), 0x1 << 23, SCRATCH2);
				} else {
					ANDI2R(gpr.R(rt), gpr.R(rt), ~(0x1 << 23), SCRATCH2);
				}
			} else {
				gpr.MapDirtyIn(rt, MIPS_REG_FPCOND);
				LDR(INDEX_UNSIGNED, gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
				// BFI(gpr.R(rt), gpr.R(MIPS_REG_FPCOND), 23, 1);
				ANDI2R(SCRATCH1, gpr.R(MIPS_REG_FPCOND), 1); // Just in case
				ANDI2R(gpr.R(rt), gpr.R(rt), ~(0x1 << 23), SCRATCH2);  // SCRATCHREG2 won't be used, this turns into a simple BIC.
				ORR(gpr.R(rt), gpr.R(rt), SCRATCH1, ArithOption(gpr.R(rt), ST_LSL, 23));
			}
		} else if (fs == 0) {
			gpr.SetImm(rt, MIPSState::FCR0_VALUE);
		} else {
			// Unsupported regs are always 0.
			gpr.SetImm(rt, 0);
		}
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		if (gpr.IsImm(rt)) {
			uint32_t ival = gpr.GetImm(rt);
			float floatval;
			memcpy(&floatval, &ival, sizeof(floatval));
			uint8_t imm8;
			// If zero, just zero it.
			fpr.MapReg(fs, MAP_NOINIT | MAP_DIRTY);
			if (ival == 0) {
				fp.FMOV(fpr.R(fs), WZR);  // This is supposedly special cased in hardware to be fast.
			} else if (FPImm8FromFloat(floatval, &imm8)) {
				fp.FMOV(fpr.R(fs), imm8);
			} else {
				// Materialize the register and do a cross move.
				gpr.MapReg(rt);
				fp.FMOV(fpr.R(fs), gpr.R(rt));
			}
		} else {
			gpr.MapReg(rt);
			fpr.MapReg(fs, MAP_NOINIT | MAP_DIRTY);
			fp.FMOV(fpr.R(fs), gpr.R(rt));
		}
		return;

	case 6: //ctc1
		if (fs == 31) {
			// Must clear before setting, since ApplyRoundingMode() assumes it was cleared.
			RestoreRoundingMode();
			bool wasImm = gpr.IsImm(rt);
			if (wasImm) {
				gpr.SetImm(MIPS_REG_FPCOND, (gpr.GetImm(rt) >> 23) & 1);
				gpr.MapReg(rt);
			} else {
				gpr.MapDirtyIn(MIPS_REG_FPCOND, rt);
			}

			// Update MIPS state
			// TODO: Technically, should mask by 0x0181FFFF.  Maybe just put all of FCR31 in the reg?
			STR(INDEX_UNSIGNED, gpr.R(rt), CTXREG, offsetof(MIPSState, fcr31));
			if (!wasImm) {
				// UBFX(gpr.R(MIPS_REG_FPCOND), gpr.R(rt), 23, 1);
				LSR(SCRATCH1, gpr.R(rt), 23);
				ANDI2R(gpr.R(MIPS_REG_FPCOND), SCRATCH1, 1);
			}
			UpdateRoundingMode();
			ApplyRoundingMode();
		} else {
			Comp_Generic(op);
		}
		return;
	default:
		DISABLE;
		break;
	}
}

}	// namespace MIPSComp
