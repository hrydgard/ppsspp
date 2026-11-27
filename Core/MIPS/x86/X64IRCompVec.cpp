// Copyright (c) 2023- PPSSPP Project.

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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <algorithm>
#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

// This file contains compilation for vector instructions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace Gen;
using namespace X64IRJitConstants;

static bool Overlap(IRReg r1, int l1, IRReg r2, int l2) {
	return r1 < r2 + l2 && r1 + l1 > r2;
}

void X64JitBackend::EmitVecConstants() {
	static const float vec4InitData[8][4] = {
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ -1.0f, -1.0f, -1.0f, -1.0f },
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f },
	};

	constants.vec4InitValues = (const Float4Constant *)GetCodePointer();
	for (size_t type = 0; type < ARRAY_SIZE(vec4InitData); ++type) {
		for (int i = 0; i < 4; ++i) {
			uint32_t val;
			memcpy(&val, &vec4InitData[type][i], sizeof(val));
			Write32(val);
		}
	}
}

void X64JitBackend::CompIR_VecArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Add:
		regs_.Map(inst);
		if (inst.dest == inst.src1) {
			ADDPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			ADDPS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else if (cpu_info.bAVX) {
			VADDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else {
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			ADDPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::Vec4Sub:
		if (inst.dest == inst.src1) {
			regs_.Map(inst);
			SUBPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (cpu_info.bAVX) {
			regs_.Map(inst);
			VSUBPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			X64Reg tempReg = regs_.MapWithFPRTemp(inst);
			MOVAPS(tempReg, regs_.F(inst.src2));
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			SUBPS(regs_.FX(inst.dest), R(tempReg));
		} else {
			regs_.Map(inst);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			SUBPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::Vec4Mul:
		regs_.Map(inst);
		if (inst.dest == inst.src1) {
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else if (cpu_info.bAVX) {
			VMULPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else {
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::Vec4Div:
		if (inst.dest == inst.src1) {
			regs_.Map(inst);
			DIVPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (cpu_info.bAVX) {
			regs_.Map(inst);
			VDIVPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			X64Reg tempReg = regs_.MapWithFPRTemp(inst);
			MOVAPS(tempReg, regs_.F(inst.src2));
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			DIVPS(regs_.FX(inst.dest), R(tempReg));
		} else {
			regs_.Map(inst);
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			DIVPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::Vec4Scale:
		// TODO: Handle "aliasing" of sizes.
		if (Overlap(inst.dest, 4, inst.src2, 1) || Overlap(inst.src1, 4, inst.src2, 1))
			DISABLE;

		regs_.Map(inst);
		SHUFPS(regs_.FX(inst.src2), regs_.F(inst.src2), 0);
		if (inst.dest == inst.src1) {
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else if (cpu_info.bAVX) {
			VMULPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else {
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}
		break;

	case IROp::Vec4Neg:
		regs_.Map(inst);
		if (cpu_info.bAVX) {
			VXORPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(constants.signBitAll));  // rip accessible
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			XORPS(regs_.FX(inst.dest), M(constants.signBitAll));  // rip accessible
		}
		break;

	case IROp::Vec4Abs:
		regs_.Map(inst);
		if (cpu_info.bAVX) {
			VANDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(constants.noSignMask));  // rip accessible
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			ANDPS(regs_.FX(inst.dest), M(constants.noSignMask));  // rip accessible
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_VecAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Init:
		regs_.Map(inst);
		if (inst.src1 == (int)Vec4Init::AllZERO) {
			XORPS(regs_.FX(inst.dest), regs_.F(inst.dest));
		} else  {
			MOVAPS(regs_.FX(inst.dest), M(&constants.vec4InitValues[inst.src1]));  // rip accessible
		}
		break;

	case IROp::Vec4Shuffle:
		if (regs_.GetFPRLaneCount(inst.src1) == 1 && (inst.src1 & 3) == 0 && inst.src2 == 0) {
			// This is a broadcast.  If dest == src1, this won't clear it.
			regs_.SpillLockFPR(inst.src1);
			regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
		} else {
			regs_.Map(inst);
		}
		if (cpu_info.bAVX) {
			VPERMILPS(128, regs_.FX(inst.dest), regs_.F(inst.src1), inst.src2);
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			SHUFPS(regs_.FX(inst.dest), regs_.F(inst.dest), inst.src2);
		}
		break;

	case IROp::Vec4Blend:
		if (cpu_info.bAVX) {
			regs_.Map(inst);
			VBLENDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2), (uint8_t)inst.constant);
		} else if (cpu_info.bSSE4_1) {
			regs_.Map(inst);
			if (inst.dest == inst.src1) {
				BLENDPS(regs_.FX(inst.dest), regs_.F(inst.src2), (uint8_t)inst.constant);
			} else if (inst.dest == inst.src2) {
				BLENDPS(regs_.FX(inst.dest), regs_.F(inst.src1), (uint8_t)~inst.constant);
			} else {
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
				BLENDPS(regs_.FX(inst.dest), regs_.F(inst.src2), (uint8_t)inst.constant);
			}
		} else {
			// Could use some shuffles...
			DISABLE;
		}
		break;

	case IROp::Vec4Mov:
		regs_.Map(inst);
		MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_VecHoriz(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4VfpuDot:
	{
#if PPSSPP_ARCH(AMD64)
		if (!vfpuDot_ || Overlap(inst.dest, 1, inst.src1, 4) || Overlap(inst.dest, 1, inst.src2, 4)) {
			CompIR_Generic(inst);
			break;
		}
		// The operands go to the shared routine in xmm0 and xmm1, and the result comes back in edi.
		// Whatever guest state is in the registers it clobbers gets flushed first: these come last in
		// the allocation order, so usually that's nothing. Then again after the operands are moved,
		// in case they were mapped into one of them.
		auto flushClobbered = [&]() {
			regs_.FlushHostRegs({ RCX, RDX, RSI, RDI }, { XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM14, XMM15 });
		};
		flushClobbered();
		regs_.SpillLockFPR(inst.src1, inst.src2);
		const X64Reg x = regs_.MapVec4(inst.src1);
		const X64Reg y = regs_.MapVec4(inst.src2);
		if (x == XMM1 && y == XMM0) {
			MOVAPS(XMM2, R(XMM0));
			MOVAPS(XMM0, R(XMM1));
			MOVAPS(XMM1, R(XMM2));
		} else if (y == XMM0) {
			MOVAPS(XMM1, R(y));
			MOVAPS(XMM0, R(x));
		} else {
			if (x != XMM0)
				MOVAPS(XMM0, R(x));
			if (y != XMM1)
				MOVAPS(XMM1, R(y));
		}
		regs_.ReleaseSpillLockFPR(inst.src1, inst.src2);
		flushClobbered();
		CALL(vfpuDot_);
		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		MOVD_xmm(regs_.FX(inst.dest), R(EDI));
#else
		// 32-bit x86 has too few registers for the shared routine.
		CompIR_Generic(inst);
#endif
		break;
	}

	case IROp::Vec4VfpuMultiDot:
	{
#if PPSSPP_ARCH(AMD64)
		if (!vfpuDot_) {
			CompIR_Generic(inst);
			break;
		}
		// Four calls of the dot routine, with the operands and the results in MIPSState's scratch space, as the
		// routine clobbers most of what could hold them. What it saves over four Vec4VfpuDots is the moves the IR
		// needed to line their operands up.
		const int terms = IRMULTIDOT_TERMS(inst.constant);
		const bool transposed = (inst.constant & IRMULTIDOT_TRANSPOSED) != 0;
		const int args = (int)(offsetof(MIPSState, vfpuMultiDotArgs) - offsetof(MIPSState, f));
		const int result = (int)(offsetof(MIPSState, vfpuMultiDotResult) - offsetof(MIPSState, f));
		for (int j = 0; j < 4; j++)
			MOVUPS(MDisp(CTXREG, args + 16 * j), regs_.MapVec4(inst.src1 + 4 * j));
		MOVUPS(MDisp(CTXREG, args + 64), regs_.MapVec4(inst.src2));
		regs_.FlushHostRegs({ RCX, RDX, RSI, RDI }, { XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM14, XMM15 });

		if (!transposed) {
			// Lane k of the result is against the block's row k: transpose it, so each is one Vec4.
			MOVUPS(XMM0, MDisp(CTXREG, args));
			MOVUPS(XMM1, MDisp(CTXREG, args + 16));
			MOVUPS(XMM2, MDisp(CTXREG, args + 32));
			MOVUPS(XMM3, MDisp(CTXREG, args + 48));
			MOVAPS(XMM4, R(XMM0));
			UNPCKLPS(XMM0, R(XMM1));
			UNPCKHPS(XMM4, R(XMM1));
			MOVAPS(XMM5, R(XMM2));
			UNPCKLPS(XMM2, R(XMM3));
			UNPCKHPS(XMM5, R(XMM3));
			MOVAPS(XMM1, R(XMM0));
			MOVLHPS(XMM0, XMM2);
			MOVHLPS(XMM2, XMM1);
			MOVAPS(XMM1, R(XMM4));
			MOVLHPS(XMM4, XMM5);
			MOVHLPS(XMM5, XMM1);
			MOVUPS(MDisp(CTXREG, args), XMM0);
			MOVUPS(MDisp(CTXREG, args + 16), XMM2);
			MOVUPS(MDisp(CTXREG, args + 32), XMM4);
			MOVUPS(MDisp(CTXREG, args + 48), XMM5);
		}
		// The lanes past the terms count as zero in both operands.
		for (int j = 0; j < 5; j++) {
			for (int i = terms; i < 4; i++)
				MOV(32, MDisp(CTXREG, args + 16 * j + 4 * i), Imm32(0));
		}
		for (int k = 0; k < 4; k++) {
			if (IRMULTIDOT_SKIP(inst.constant) & (1 << k)) {
				MOV(32, MDisp(CTXREG, result + 4 * k), Imm32(0));
				continue;
			}
			MOVUPS(XMM0, MDisp(CTXREG, args + 16 * k));
			MOVUPS(XMM1, MDisp(CTXREG, args + 64));
			CALL(vfpuDot_);
			MOV(32, MDisp(CTXREG, result + 4 * k), R(EDI));
		}
		regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
		MOVUPS(regs_.FX(inst.dest), MDisp(CTXREG, result));
#else
		CompIR_Generic(inst);
#endif
		break;
	}

	case IROp::Vec4VfpuCross:
	{
#if PPSSPP_ARCH(AMD64)
		if (!vfpuDot_) {
			CompIR_Generic(inst);
			break;
		}
		// vfpu_cross(): three calls of the dot routine, s against permuted and negated copies of t, which are
		// put together in MIPSState's scratch space.
		const int args = (int)(offsetof(MIPSState, vfpuMultiDotArgs) - offsetof(MIPSState, f));
		const int result = (int)(offsetof(MIPSState, vfpuMultiDotResult) - offsetof(MIPSState, f));
		const int sOffset = args + 64;
		const int tOffset = args + 48;
		MOVUPS(MDisp(CTXREG, sOffset), regs_.MapVec4(inst.src1));
		MOVUPS(MDisp(CTXREG, tOffset), regs_.MapVec4(inst.src2));
		regs_.FlushHostRegs({ RCX, RDX, RSI, RDI }, { XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM14, XMM15 });

		MOV(32, MDisp(CTXREG, sOffset + 12), Imm32(0));
		// Row k is at args + 16 * k: (0, t2, -t1, 0), (-t2, 0, t0, 0), (t1, -t0, 0, t2). The last has t2 where s
		// has nothing, which still makes the third dot NaN if t2 is inf or NaN.
		auto row = [&](int k, int lane) { return MDisp(CTXREG, args + 16 * k + 4 * lane); };
		MOV(32, R(SCRATCH1), MDisp(CTXREG, tOffset));
		MOV(32, row(1, 2), R(SCRATCH1));
		XOR(32, R(SCRATCH1), Imm32(0x80000000));
		MOV(32, row(2, 1), R(SCRATCH1));
		MOV(32, R(SCRATCH1), MDisp(CTXREG, tOffset + 4));
		MOV(32, row(2, 0), R(SCRATCH1));
		XOR(32, R(SCRATCH1), Imm32(0x80000000));
		MOV(32, row(0, 2), R(SCRATCH1));
		MOV(32, R(SCRATCH1), MDisp(CTXREG, tOffset + 8));
		MOV(32, row(0, 1), R(SCRATCH1));
		MOV(32, row(2, 3), R(SCRATCH1));
		XOR(32, R(SCRATCH1), Imm32(0x80000000));
		MOV(32, row(1, 0), R(SCRATCH1));
		MOV(32, row(0, 0), Imm32(0));
		MOV(32, row(0, 3), Imm32(0));
		MOV(32, row(1, 1), Imm32(0));
		MOV(32, row(1, 3), Imm32(0));
		MOV(32, row(2, 2), Imm32(0));
		for (int k = 0; k < 3; k++) {
			MOVUPS(XMM0, MDisp(CTXREG, sOffset));
			MOVUPS(XMM1, MDisp(CTXREG, args + 16 * k));
			CALL(vfpuDot_);
			MOV(32, MDisp(CTXREG, result + 4 * k), R(EDI));
		}
		MOV(32, MDisp(CTXREG, result + 12), Imm32(0));
		regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
		MOVUPS(regs_.FX(inst.dest), MDisp(CTXREG, result));
#else
		CompIR_Generic(inst);
#endif
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	// Only SSE2 here. A destination overlapping the source goes the slow way, it's rare.
	switch (inst.op) {
	case IROp::Vec4Pack32To8:
	case IROp::Vec4Pack31To8:
	{
		if (Overlap(inst.dest, 1, inst.src1, 4)) {
			DISABLE;
		}
		// The top byte of each lane, packed into one lane. For 31, the byte below the sign, with
		// negative lanes clamped to zero: the arithmetic shift makes them negative, and PACKUSWB
		// saturates them to 0.
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);
		MOVDQA(tempReg, regs_.F(inst.src1));
		if (inst.op == IROp::Vec4Pack31To8)
			PSRAD(tempReg, 23);
		else
			PSRLD(tempReg, 24);
		PACKSSDW(tempReg, R(tempReg));
		PACKUSWB(tempReg, R(tempReg));
		MOVDQA(regs_.FX(inst.dest), R(tempReg));
		break;
	}

	case IROp::Vec2Pack32To16:
	case IROp::Vec2Pack31To16:
	{
		if (Overlap(inst.dest, 1, inst.src1, 2)) {
			DISABLE;
		}
		// The top 16 bits of each lane (for 31, the 16 below the sign, with negative lanes clamped
		// to zero), packed into one lane. The arithmetic shift keeps them in PACKSSDW's range, so
		// the bits come through unchanged.
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);
		MOVDQA(tempReg, regs_.F(inst.src1));
		if (inst.op == IROp::Vec2Pack31To16) {
			PSRAD(tempReg, 31);
			PANDN(tempReg, regs_.F(inst.src1));
			PSLLD(tempReg, 1);
		}
		PSRAD(tempReg, 16);
		PACKSSDW(tempReg, R(tempReg));
		MOVDQA(regs_.FX(inst.dest), R(tempReg));
		break;
	}

	case IROp::Vec4Unpack8To32:
	{
		if (Overlap(inst.dest, 4, inst.src1, 1)) {
			DISABLE;
		}
		// Each byte into the top of a lane: duplicate bytes, then words, then shift the rest out.
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);
		MOVDQA(tempReg, regs_.F(inst.src1));
		PUNPCKLBW(tempReg, R(tempReg));
		PUNPCKLWD(tempReg, R(tempReg));
		PSLLD(tempReg, 24);
		MOVDQA(regs_.FX(inst.dest), R(tempReg));
		break;
	}

	case IROp::Vec2Unpack16To32:
	case IROp::Vec2Unpack16To31:
	{
		if (Overlap(inst.dest, 2, inst.src1, 1)) {
			DISABLE;
		}
		// Each half into the top of a lane, shifted down one more for 31.
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);
		MOVDQA(tempReg, regs_.F(inst.src1));
		PUNPCKLWD(tempReg, R(tempReg));
		PSLLD(tempReg, 16);
		if (inst.op == IROp::Vec2Unpack16To31)
			PSRLD(tempReg, 1);
		MOVDQA(regs_.FX(inst.dest), R(tempReg));
		break;
	}

	case IROp::Vec4DuplicateUpperBitsAndShift1:
	{
		if (inst.dest != inst.src1 && Overlap(inst.dest, 4, inst.src1, 4)) {
			DISABLE;
		}
		// v |= v >> 8; v |= v >> 16; v >>= 1, which turns 0xFF000000 into 0x7FFFFFFF.
		X64Reg tempReg = regs_.MapWithFPRTemp(inst);
		MOVDQA(tempReg, regs_.F(inst.src1));
		PSRLD(tempReg, 8);
		POR(tempReg, regs_.F(inst.src1));
		MOVDQA(regs_.FX(inst.dest), R(tempReg));
		PSRLD(tempReg, 16);
		POR(regs_.FX(inst.dest), R(tempReg));
		PSRLD(regs_.FX(inst.dest), 1);
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
