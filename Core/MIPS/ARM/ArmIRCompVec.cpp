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

#include "Common/CPUDetect.h"
#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"
#include "Core/MIPS/ARM/ArmVfpuDot.h"

// This file contains compilation for Vec4 operations.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace ArmGen;
using namespace ArmIRJitConstants;

// Floats aren't mapped as SIMD, so each lane of a Vec4 is its own S register and these work lane
// by lane. Vec4 regs are aligned to 4, so a dest and a source are either the same or disjoint,
// but a scalar source can be one of the dest's lanes.

void ArmJitBackend::CompIR_VecArith(IRInst inst) {
	CONDITIONAL_DISABLE;

	auto lanes3 = [&](void (ARMXEmitter::*op)(ARMReg, ARMReg, ARMReg)) {
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i)
			(this->*op)(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), regs_.F(inst.src2 + i));
	};
	auto lanes2 = [&](void (ARMXEmitter::*op)(ARMReg, ARMReg)) {
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i)
			(this->*op)(regs_.F(inst.dest + i), regs_.F(inst.src1 + i));
	};

	switch (inst.op) {
	case IROp::Vec4Add:
		lanes3(&ARMXEmitter::VADD);
		break;

	case IROp::Vec4Sub:
		lanes3(&ARMXEmitter::VSUB);
		break;

	case IROp::Vec4Mul:
		lanes3(&ARMXEmitter::VMUL);
		break;

	case IROp::Vec4Div:
		lanes3(&ARMXEmitter::VDIV);
		break;

	case IROp::Vec4Scale:
		regs_.Map(inst);
		// The factor may be one of the dest lanes.
		VMOV(SCRATCHF1, regs_.F(inst.src2));
		for (int i = 0; i < 4; ++i)
			VMUL(regs_.F(inst.dest + i), regs_.F(inst.src1 + i), SCRATCHF1);
		break;

	case IROp::Vec4Neg:
		lanes2(&ARMXEmitter::VNEG);
		break;

	case IROp::Vec4Abs:
		lanes2(&ARMXEmitter::VABS);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_VecAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4Init:
	{
		static const float values[7][4] = {
			{ 0.0f, 0.0f, 0.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ -1.0f, -1.0f, -1.0f, -1.0f },
			{ 1.0f, 0.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f },
		};
		if (inst.src1 >= ARRAY_SIZE(values)) {
			CompIR_Generic(inst);
			break;
		}
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i)
			MOVI2F(regs_.F(inst.dest + i), values[inst.src1][i], SCRATCH1);
		break;
	}

	case IROp::Vec4Shuffle:
		regs_.Map(inst);
		if (inst.dest == inst.src1) {
			// Go through the scratch regs, since a lane may be read after it's written.
			for (int i = 0; i < 4; ++i)
				VMOV((ARMReg)(SCRATCHF1 + i), regs_.F(inst.src1 + Vec4ShuffleLane(inst.src2, i)));
			for (int i = 0; i < 4; ++i)
				VMOV(regs_.F(inst.dest + i), (ARMReg)(SCRATCHF1 + i));
		} else {
			for (int i = 0; i < 4; ++i)
				VMOV(regs_.F(inst.dest + i), regs_.F(inst.src1 + Vec4ShuffleLane(inst.src2, i)));
		}
		break;

	case IROp::Vec4Blend:
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i) {
			IRReg src = ((inst.constant >> i) & 1) ? inst.src2 : inst.src1;
			if (src != inst.dest)
				VMOV(regs_.F(inst.dest + i), regs_.F(src + i));
		}
		break;

	case IROp::Vec4Mov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			for (int i = 0; i < 4; ++i)
				VMOV(regs_.F(inst.dest + i), regs_.F(inst.src1 + i));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_VecHoriz(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Vec4VfpuDot:
		if (cpu_info.bNEON) {
			CompVfpuDot(inst);
		} else {
			CompIR_Generic(inst);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompVfpuDot(IRInst inst) {
	regs_.Map(inst);
	// The lanes into Q8 and Q9, through S0-S3 (Q0), since Q8-Q15 have no S registers.
	for (int i = 0; i < 4; i++)
		VMOV((ARMReg)(S0 + i), regs_.F(inst.src1 + i));
	VMOV_neon(Q8, Q0);
	for (int i = 0; i < 4; i++)
		VMOV((ARMReg)(S0 + i), regs_.F(inst.src2 + i));
	VMOV_neon(Q9, Q0);

	// The shared routine clobbers these too, so whatever guest state is there gets flushed. They come
	// late in the allocation order, so usually that's nothing.
	regs_.FlushHostRegs({ R2, R3, R12, R14 });
	BL(vfpuDot_);
	VMOV(regs_.F(inst.dest), R3);
}

void ArmJitBackend::CompIR_VecPack(IRInst inst) {
	CONDITIONAL_DISABLE;

	// These are bit manipulations, so they go through the integer registers. Every source is read
	// before any dest lane is written.
	switch (inst.op) {
	case IROp::Vec2Unpack16To31:
	case IROp::Vec2Unpack16To32:
	{
		regs_.Map(inst);
		VMOV(SCRATCH1, regs_.F(inst.src1));
		bool to31 = inst.op == IROp::Vec2Unpack16To31;
		LSL(SCRATCH2, SCRATCH1, Operand2(16));
		if (to31)
			LSR(SCRATCH2, SCRATCH2, Operand2(1));
		VMOV(regs_.F(inst.dest), SCRATCH2);
		BIC(SCRATCH1, SCRATCH1, Operand2(0xFF, 0));
		BIC(SCRATCH1, SCRATCH1, Operand2(0xFF, 12));
		if (to31)
			LSR(SCRATCH1, SCRATCH1, Operand2(1));
		VMOV(regs_.F(inst.dest + 1), SCRATCH1);
		break;
	}

	case IROp::Vec4Unpack8To32:
		regs_.Map(inst);
		VMOV(SCRATCH1, regs_.F(inst.src1));
		for (int i = 0; i < 4; ++i) {
			// Byte i goes to the top of lane i.
			if (i < 3)
				LSL(SCRATCH2, SCRATCH1, Operand2(24 - i * 8));
			else
				MOV(SCRATCH2, SCRATCH1);
			if (i != 0)
				AND(SCRATCH2, SCRATCH2, Operand2(0xFF, 4));
			VMOV(regs_.F(inst.dest + i), SCRATCH2);
		}
		break;

	case IROp::Vec2Pack32To16:
		regs_.Map(inst);
		VMOV(SCRATCH1, regs_.F(inst.src1));
		VMOV(SCRATCH2, regs_.F(inst.src1 + 1));
		LSR(SCRATCH1, SCRATCH1, Operand2(16));
		// Keep the top half of the second.
		BFI(SCRATCH2, SCRATCH1, 0, 16);
		VMOV(regs_.F(inst.dest), SCRATCH2);
		break;

	case IROp::Vec2Pack31To16:
		// Negative lanes clamp to zero.
		regs_.Map(inst);
		VMOV(SCRATCH1, regs_.F(inst.src1));
		VMOV(SCRATCH2, regs_.F(inst.src1 + 1));
		CMP(SCRATCH1, Operand2(0));
		SetCC(CC_LT);
		MOV(SCRATCH1, Operand2(0));
		SetCC(CC_AL);
		CMP(SCRATCH2, Operand2(0));
		SetCC(CC_LT);
		MOV(SCRATCH2, Operand2(0));
		SetCC(CC_AL);
		LSR(SCRATCH1, SCRATCH1, Operand2(15));
		LSL(SCRATCH2, SCRATCH2, Operand2(1));
		BFI(SCRATCH2, SCRATCH1, 0, 16);
		VMOV(regs_.F(inst.dest), SCRATCH2);
		break;

	case IROp::Vec4Pack32To8:
	case IROp::Vec4Pack31To8:
	{
		// The top byte of each lane (bits 30-23 for 31, with negative lanes as zero.)
		bool from31 = inst.op == IROp::Vec4Pack31To8;
		ARMReg temp = regs_.GetAndLockTempGPR();
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i) {
			ARMReg target = i == 0 ? SCRATCH1 : SCRATCH2;
			VMOV(target, regs_.F(inst.src1 + i));
			if (from31) {
				CMP(target, Operand2(0));
				SetCC(CC_LT);
				MOV(target, Operand2(0));
				SetCC(CC_AL);
				UBFX(target, target, 23, 8);
			} else {
				LSR(target, target, Operand2(24));
			}
			if (i != 0) {
				// Accumulate into temp, then insert at the end.
				if (i == 1)
					MOV(temp, Operand2(target, ST_LSL, 8));
				else
					ORR(temp, temp, Operand2(target, ST_LSL, i * 8));
			}
		}
		ORR(SCRATCH1, SCRATCH1, temp);
		VMOV(regs_.F(inst.dest), SCRATCH1);
		break;
	}

	case IROp::Vec4DuplicateUpperBitsAndShift1:
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i)
			VMOV((ARMReg)(SCRATCHF1 + i), regs_.F(inst.src1 + i));
		for (int i = 0; i < 4; ++i) {
			VMOV(SCRATCH1, (ARMReg)(SCRATCHF1 + i));
			ORR(SCRATCH1, SCRATCH1, Operand2(SCRATCH1, ST_LSR, 8));
			ORR(SCRATCH1, SCRATCH1, Operand2(SCRATCH1, ST_LSR, 16));
			LSR(SCRATCH1, SCRATCH1, Operand2(1));
			VMOV(regs_.F(inst.dest + i), SCRATCH1);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
