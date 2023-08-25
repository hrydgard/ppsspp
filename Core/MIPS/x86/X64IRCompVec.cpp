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

static struct SimdConstants {
	alignas(16) const u32 noSignMask[4] = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };
	alignas(16) const u32 signBitAll[4] = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
} simdConstants;

alignas(16) static const float vec4InitValues[8][4] = {
	{ 0.0f, 0.0f, 0.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ -1.0f, -1.0f, -1.0f, -1.0f },
	{ 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 1.0f },
};

static bool Overlap(IRReg r1, int l1, IRReg r2, int l2) {
	return r1 < r2 + l2 && r1 + l1 > r2;
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
			if (RipAccessible(&simdConstants.signBitAll)) {
				VXORPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(&simdConstants.signBitAll));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.signBitAll));
				VXORPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), MatR(SCRATCH1));
			}
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			if (RipAccessible(&simdConstants.signBitAll)) {
				XORPS(regs_.FX(inst.dest), M(&simdConstants.signBitAll));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.signBitAll));
				XORPS(regs_.FX(inst.dest), MatR(SCRATCH1));
			}
		}
		break;

	case IROp::Vec4Abs:
		regs_.Map(inst);
		if (cpu_info.bAVX) {
			if (RipAccessible(&simdConstants.noSignMask)) {
				VANDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), M(&simdConstants.noSignMask));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.noSignMask));
				VANDPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), MatR(SCRATCH1));
			}
		} else {
			if (inst.dest != inst.src1)
				MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			if (RipAccessible(&simdConstants.noSignMask)) {
				ANDPS(regs_.FX(inst.dest), M(&simdConstants.noSignMask));  // rip accessible
			} else {
				MOV(PTRBITS, R(SCRATCH1), ImmPtr(&simdConstants.noSignMask));
				ANDPS(regs_.FX(inst.dest), MatR(SCRATCH1));
			}
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
		} else if (RipAccessible(&vec4InitValues[inst.src1])) {
			MOVAPS(regs_.FX(inst.dest), M(&vec4InitValues[inst.src1]));  // rip accessible
		} else {
			MOV(PTRBITS, R(SCRATCH1), ImmPtr(&vec4InitValues[inst.src1]));
			MOVAPS(regs_.FX(inst.dest), MatR(SCRATCH1));
		}
		break;

	case IROp::Vec4Shuffle:
		CompIR_Generic(inst);
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

void X64JitBackend::CompIR_VecClamp(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4ClampToZero:
	case IROp::Vec2ClampToZero:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_VecHoriz(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Dot:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec2Unpack16To31:
	case IROp::Vec4Pack32To8:
	case IROp::Vec2Pack31To16:
	case IROp::Vec4Unpack8To32:
	case IROp::Vec2Unpack16To32:
	case IROp::Vec4DuplicateUpperBitsAndShift1:
	case IROp::Vec4Pack31To8:
	case IROp::Vec2Pack32To16:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
