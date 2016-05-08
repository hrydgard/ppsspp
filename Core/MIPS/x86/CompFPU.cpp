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
#include "Common/Common.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/x86/RegCache.h"

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

// #define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp {

using namespace Gen;
using namespace X64JitConstants;

void Jit::CopyFPReg(X64Reg dst, OpArg src) {
	if (src.IsSimpleReg()) {
		MOVAPS(dst, src);
	} else {
		MOVSS(dst, src);
	}
}

void Jit::CompFPTriArith(MIPSOpcode op, void (XEmitter::*arith)(X64Reg reg, OpArg), bool orderMatters) {
	int ft = _FT;
	int fs = _FS;
	int fd = _FD;
	fpr.SpillLock(fd, fs, ft);

	if (fs == fd) {
		fpr.MapReg(fd, true, true);
		(this->*arith)(fpr.RX(fd), fpr.R(ft));
	} else if (ft == fd && !orderMatters) {
		fpr.MapReg(fd, true, true);
		(this->*arith)(fpr.RX(fd), fpr.R(fs));
	} else if (ft != fd) {
		// fs can't be fd (handled above.)
		fpr.MapReg(fd, false, true);
		CopyFPReg(fpr.RX(fd), fpr.R(fs));
		(this->*arith)(fpr.RX(fd), fpr.R(ft));
	} else {
		// fd must be ft, and order must matter.
		fpr.MapReg(fd, true, true);
		CopyFPReg(XMM0, fpr.R(fs));
		(this->*arith)(XMM0, fpr.R(ft));
		MOVAPS(fpr.RX(fd), R(XMM0));
	}
	fpr.ReleaseSpillLocks();
}

void Jit::Comp_FPU3op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	switch (op & 0x3f) {
	case 0: CompFPTriArith(op, &XEmitter::ADDSS, false); break; //F(fd) = F(fs) + F(ft); //add
	case 1: CompFPTriArith(op, &XEmitter::SUBSS, true); break;  //F(fd) = F(fs) - F(ft); //sub
	case 2: CompFPTriArith(op, &XEmitter::MULSS, false); break; //F(fd) = F(fs) * F(ft); //mul
	case 3: CompFPTriArith(op, &XEmitter::DIVSS, true); break;  //F(fd) = F(fs) / F(ft); //div
	default:
		_dbg_assert_msg_(CPU,0,"Trying to compile FPU3Op instruction that can't be interpreted");
		break;
	}
}

static u32 MEMORY_ALIGNED16(ssLoadStoreTemp);

void Jit::Comp_FPULS(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	s32 offset = _IMM16;
	int ft = _FT;
	MIPSGPReg rs = _RS;

	switch (op >> 26) {
	case 49: //FI(ft) = Memory::Read_U32(addr); break; //lwc1
		{
			gpr.Lock(rs);
			fpr.SpillLock(ft);
			fpr.MapReg(ft, false, true);

			JitSafeMem safe(this, rs, offset);
			OpArg src;
			if (safe.PrepareRead(src, 4))
				MOVSS(fpr.RX(ft), src);
			if (safe.PrepareSlowRead(safeMemFuncs.readU32))
				MOVD_xmm(fpr.RX(ft), R(EAX));
			safe.Finish();

			gpr.UnlockAll();
			fpr.ReleaseSpillLocks();
		}
		break;
	case 57: //Memory::Write_U32(FI(ft), addr); break; //swc1
		{
			gpr.Lock(rs);
			fpr.SpillLock(ft);
			fpr.MapReg(ft, true, false);

			JitSafeMem safe(this, rs, offset);
			OpArg dest;
			if (safe.PrepareWrite(dest, 4))
				MOVSS(dest, fpr.RX(ft));
			if (safe.PrepareSlowWrite())
			{
				MOVSS(M(&ssLoadStoreTemp), fpr.RX(ft));
				safe.DoSlowWrite(safeMemFuncs.writeU32, M(&ssLoadStoreTemp));
			}
			safe.Finish();

			gpr.UnlockAll();
			fpr.ReleaseSpillLocks();
		}
		break;

	default:
		_dbg_assert_msg_(CPU,0,"Trying to interpret FPULS instruction that can't be interpreted");
		break;
	}
}

static const u64 MEMORY_ALIGNED16(ssOneBits[2])	= {0x0000000100000001ULL, 0x0000000100000001ULL};
static const u64 MEMORY_ALIGNED16(ssSignBits2[2])	= {0x8000000080000000ULL, 0x8000000080000000ULL};
static const u64 MEMORY_ALIGNED16(ssNoSignMask[2]) = {0x7FFFFFFF7FFFFFFFULL, 0x7FFFFFFF7FFFFFFFULL};

void Jit::CompFPComp(int lhs, int rhs, u8 compare, bool allowNaN) {
	gpr.MapReg(MIPS_REG_FPCOND, false, true);

	// This means that NaN also means true, e.g. !<> or !>, etc.
	if (allowNaN) {
		CopyFPReg(XMM0, fpr.R(lhs));
		CopyFPReg(XMM1, fpr.R(lhs));
		CMPSS(XMM0, fpr.R(rhs), compare);
		CMPUNORDSS(XMM1, fpr.R(rhs));

		POR(XMM0, R(XMM1));
	} else {
		CopyFPReg(XMM0, fpr.R(lhs));
		CMPSS(XMM0, fpr.R(rhs), compare);
	}

	MOVD_xmm(gpr.R(MIPS_REG_FPCOND), XMM0);
}

void Jit::Comp_FPUComp(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int fs = _FS;
	int ft = _FT;

	switch (op & 0xf) {
	case 0: //f
	case 8: //sf
		gpr.SetImm(MIPS_REG_FPCOND, 0);
		break;

	case 1: //un
	case 9: //ngle
		CompFPComp(fs, ft, CMP_UNORD);
		break;

	case 2: //eq
	case 10: //seq
		CompFPComp(fs, ft, CMP_EQ);
		break;

	case 3: //ueq
	case 11: //ngl
		CompFPComp(fs, ft, CMP_EQ, true);
		break;

	case 4: //olt
	case 12: //lt
		CompFPComp(fs, ft, CMP_LT);
		break;

	case 5: //ult
	case 13: //nge
		CompFPComp(ft, fs, CMP_NLE);
		break;

	case 6: //ole
	case 14: //le
		CompFPComp(fs, ft, CMP_LE);
		break;

	case 7: //ule
	case 15: //ngt
		CompFPComp(ft, fs, CMP_NLT);
		break;

	default:
		DISABLE;
	}
}

static u32 mxcsrTemp;

void Jit::Comp_FPU2op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	
	int fs = _FS;
	int fd = _FD;

	auto execRounding = [&](void (XEmitter::*conv)(X64Reg, OpArg), int setMXCSR) {
		fpr.SpillLock(fd, fs);
		fpr.MapReg(fd, fs == fd, true);

		// Small optimization: 0 is our default mode anyway.
		if (setMXCSR == 0 && !js.hasSetRounding) {
			setMXCSR = -1;
		}
		if (setMXCSR != -1) {
			STMXCSR(M(&mxcsrTemp));
			MOV(32, R(TEMPREG), M(&mxcsrTemp));
			AND(32, R(TEMPREG), Imm32(~(3 << 13)));
			OR(32, R(TEMPREG), Imm32(setMXCSR << 13));
			MOV(32, M(&mips_->temp), R(TEMPREG));
			LDMXCSR(M(&mips_->temp));
		}

		(this->*conv)(TEMPREG, fpr.R(fs));

		// Did we get an indefinite integer value?
		CMP(32, R(TEMPREG), Imm32(0x80000000));
		FixupBranch skip = J_CC(CC_NE);
		if (fd != fs) {
			CopyFPReg(fpr.RX(fd), fpr.R(fs));
		}
		XORPS(XMM1, R(XMM1));
		CMPSS(fpr.RX(fd), R(XMM1), CMP_LT);

		// At this point, -inf = 0xffffffff, inf/nan = 0x00000000.
		// We want -inf to be 0x80000000 inf/nan to be 0x7fffffff, so we flip those bits.
		MOVD_xmm(R(TEMPREG), fpr.RX(fd));
		XOR(32, R(TEMPREG), Imm32(0x7fffffff));

		SetJumpTarget(skip);
		MOVD_xmm(fpr.RX(fd), R(TEMPREG));

		if (setMXCSR != -1) {
			LDMXCSR(M(&mxcsrTemp));
		}
	};

	switch (op & 0x3f) {
	case 5:	//F(fd)	= fabsf(F(fs)); break; //abs
		fpr.SpillLock(fd, fs);
		fpr.MapReg(fd, fd == fs, true);
		if (fd != fs && fpr.IsMapped(fs)) {
			MOVAPS(fpr.RX(fd), M(ssNoSignMask));
			ANDPS(fpr.RX(fd), fpr.R(fs));
		} else {
			if (fd != fs) {
				MOVSS(fpr.RX(fd), fpr.R(fs));
			}
			ANDPS(fpr.RX(fd), M(ssNoSignMask));
		}
		break;

	case 6:	//F(fd)	= F(fs);				break; //mov
		if (fd != fs) {
			fpr.SpillLock(fd, fs);
			fpr.MapReg(fd, fd == fs, true);
			CopyFPReg(fpr.RX(fd), fpr.R(fs));
		}
		break;

	case 7:	//F(fd)	= -F(fs);			 break; //neg
		fpr.SpillLock(fd, fs);
		fpr.MapReg(fd, fd == fs, true);
		if (fd != fs && fpr.IsMapped(fs)) {
			MOVAPS(fpr.RX(fd), M(ssSignBits2));
			XORPS(fpr.RX(fd), fpr.R(fs));
		} else {
			if (fd != fs) {
				MOVSS(fpr.RX(fd), fpr.R(fs));
			}
			XORPS(fpr.RX(fd), M(ssSignBits2));
		}
		break;


	case 4:	//F(fd)	= sqrtf(F(fs)); break; //sqrt
		fpr.SpillLock(fd, fs);
		fpr.MapReg(fd, fd == fs, true);
		SQRTSS(fpr.RX(fd), fpr.R(fs));
		break;

	case 13: //FsI(fd) = F(fs)>=0 ? (int)floorf(F(fs)) : (int)ceilf(F(fs)); break;//trunc.w.s
		execRounding(&XEmitter::CVTTSS2SI, -1);
		break;

	case 32: //F(fd)	= (float)FsI(fs);			break; //cvt.s.w
		fpr.SpillLock(fd, fs);
		fpr.MapReg(fd, fs == fd, true);
		if (fpr.IsMapped(fs)) {
			CVTDQ2PS(fpr.RX(fd), fpr.R(fs));
		} else {
			// If fs was fd, we'd be in the case above since we mapped fd.
			MOVSS(fpr.RX(fd), fpr.R(fs));
			CVTDQ2PS(fpr.RX(fd), fpr.R(fd));
		}
		break;

	case 36: //FsI(fd) = (int)	F(fs);			 break; //cvt.w.s
		// Uses the current rounding mode.
		execRounding(&XEmitter::CVTSS2SI, -1);
		break;

	case 12: //FsI(fd) = (int)floorf(F(fs)+0.5f); break; //round.w.s
		execRounding(&XEmitter::CVTSS2SI, 0);
		break;
	case 14: //FsI(fd) = (int)ceilf (F(fs)); break; //ceil.w.s
		execRounding(&XEmitter::CVTSS2SI, 2);
		break;
	case 15: //FsI(fd) = (int)floorf(F(fs)); break; //floor.w.s
		execRounding(&XEmitter::CVTSS2SI, 1);
		break;
	default:
		DISABLE;
		return;
	}
	fpr.ReleaseSpillLocks();
}

void Jit::Comp_mxc1(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int fs = _FS;
	MIPSGPReg rt = _RT;

	switch ((op >> 21) & 0x1f) {
	case 0: // R(rt) = FI(fs); break; //mfc1
		if (rt == MIPS_REG_ZERO)
			return;
		gpr.MapReg(rt, false, true);
		// If fs is not mapped, most likely it's being abandoned.
		// Just load from memory in that case.
		if (fpr.R(fs).IsSimpleReg()) {
			MOVD_xmm(gpr.R(rt), fpr.RX(fs));
		} else {
			MOV(32, gpr.R(rt), fpr.R(fs));
		}
		break;

	case 2: // R(rt) = currentMIPS->ReadFCR(fs); break; //cfc1
		if (rt == MIPS_REG_ZERO)
			return;
		if (fs == 31) {
			bool wasImm = gpr.IsImm(MIPS_REG_FPCOND);
			if (!wasImm) {
				gpr.Lock(rt, MIPS_REG_FPCOND);
				gpr.MapReg(MIPS_REG_FPCOND, true, false);
			}
			gpr.MapReg(rt, false, true);
			MOV(32, gpr.R(rt), M(&mips_->fcr31));
			if (wasImm) {
				if (gpr.GetImm(MIPS_REG_FPCOND) & 1) {
					OR(32, gpr.R(rt), Imm32(1 << 23));
				} else {
					AND(32, gpr.R(rt), Imm32(~(1 << 23)));
				}
			} else {
				AND(32, gpr.R(rt), Imm32(~(1 << 23)));
				MOV(32, R(TEMPREG), gpr.R(MIPS_REG_FPCOND));
				AND(32, R(TEMPREG), Imm32(1));
				SHL(32, R(TEMPREG), Imm8(23));
				OR(32, gpr.R(rt), R(TEMPREG));
			}
			gpr.UnlockAll();
		} else if (fs == 0) {
			gpr.SetImm(rt, MIPSState::FCR0_VALUE);
		} else {
			Comp_Generic(op);
		}
		return;

	case 4: //FI(fs) = R(rt);	break; //mtc1
		fpr.MapReg(fs, false, true);
		if (gpr.IsImm(rt) && gpr.GetImm(rt) == 0) {
			XORPS(fpr.RX(fs), fpr.R(fs));
		} else {
			gpr.KillImmediate(rt, true, false);
			MOVD_xmm(fpr.RX(fs), gpr.R(rt));
		}
		return;

	case 6: //currentMIPS->WriteFCR(fs, R(rt)); break; //ctc1
		if (fs == 31) {
			// Must clear before setting, since ApplyRoundingMode() assumes it was cleared.
			RestoreRoundingMode();
			if (gpr.IsImm(rt)) {
				gpr.SetImm(MIPS_REG_FPCOND, (gpr.GetImm(rt) >> 23) & 1);
				MOV(32, M(&mips_->fcr31), Imm32(gpr.GetImm(rt) & 0x0181FFFF));
				if ((gpr.GetImm(rt) & 0x1000003) == 0) {
					// Default nearest / no-flush mode, just leave it cleared.
				} else {
					UpdateRoundingMode();
					ApplyRoundingMode();
				}
			} else {
				gpr.Lock(rt, MIPS_REG_FPCOND);
				gpr.MapReg(rt, true, false);
				gpr.MapReg(MIPS_REG_FPCOND, false, true);
				MOV(32, gpr.R(MIPS_REG_FPCOND), gpr.R(rt));
				SHR(32, gpr.R(MIPS_REG_FPCOND), Imm8(23));
				AND(32, gpr.R(MIPS_REG_FPCOND), Imm32(1));
				MOV(32, M(&mips_->fcr31), gpr.R(rt));
				AND(32, M(&mips_->fcr31), Imm32(0x0181FFFF));
				gpr.UnlockAll();
				UpdateRoundingMode();
				ApplyRoundingMode();
			}
		} else {
			Comp_Generic(op);
		}
		return;
	}
}

}	// namespace MIPSComp
