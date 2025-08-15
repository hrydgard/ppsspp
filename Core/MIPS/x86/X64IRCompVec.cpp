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
	{
		// TODO: Handle "aliasing" of sizes.  In theory it should be fine if not dirty...
		if (Overlap(inst.dest, 1, inst.src1, 4) || Overlap(inst.dest, 1, inst.src2, 4))
			DISABLE;

		X64Reg tempReg = regs_.MapWithFPRTemp(inst);

		if (inst.dest == inst.src1) {
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		} else if (inst.dest == inst.src2) {
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src1));
		} else if (cpu_info.bAVX) {
			VMULPS(128, regs_.FX(inst.dest), regs_.FX(inst.src1), regs_.F(inst.src2));
		} else if (cpu_info.bSSE4_1) {
			MOVAPS(regs_.FX(inst.dest), regs_.F(inst.src1));
			MULPS(regs_.FX(inst.dest), regs_.F(inst.src2));
		}

		// This shuffle can be done in one op for SSE3/AVX, but it's not always faster.
		MOVAPS(tempReg, regs_.F(inst.dest));
		SHUFPS(tempReg, regs_.F(inst.dest), VFPU_SWIZZLE(1, 0, 3, 2));
		ADDPS(regs_.FX(inst.dest), R(tempReg));
		MOVHLPS(tempReg, regs_.FX(inst.dest));
		ADDSS(regs_.FX(inst.dest), R(tempReg));
		break;
	}

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
