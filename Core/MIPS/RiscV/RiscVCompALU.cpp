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

#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"

// This file contains compilation for integer / arithmetic / logic related instructions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace RiscVGen;
using namespace RiscVJitConstants;

void RiscVJit::CompIR_Arith(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool allowPtrMath = true;
#ifndef MASKED_PSP_MEMORY
	// Since we modify it, we can't safely.
	allowPtrMath = false;
#endif

	// RISC-V only adds signed immediates, so rewrite a small enough subtract to an add.
	// We use -2047 and 2048 here because the range swaps.
	if (inst.op == IROp::SubConst && (int32_t)inst.constant >= -2047 && (int32_t)inst.constant <= 2048) {
		inst.op = IROp::AddConst;
		inst.constant = (uint32_t)-(int32_t)inst.constant;
	}

	switch (inst.op) {
	case IROp::Add:
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
		ADDW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		break;

	case IROp::Sub:
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
		SUBW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		break;

	case IROp::AddConst:
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			// Typical of stack pointer updates.
			if (gpr.IsMappedAsPointer(inst.src1) && inst.dest == inst.src1 && allowPtrMath) {
				gpr.MarkDirty(gpr.RPtr(inst.dest));
				ADDI(gpr.RPtr(inst.dest), gpr.RPtr(inst.dest), inst.constant);
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				ADDIW(gpr.R(inst.dest), gpr.R(inst.src1), inst.constant);
			}
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			LI(SCRATCH1, (s32)inst.constant, SCRATCH2);
			ADDW(gpr.R(inst.dest), gpr.R(inst.src1), SCRATCH1);
		}
		break;

	case IROp::SubConst:
		gpr.MapDirtyIn(inst.dest, inst.src1);
		LI(SCRATCH1, (s32)inst.constant, SCRATCH2);
		SUBW(gpr.R(inst.dest), gpr.R(inst.src1), SCRATCH1);
		break;

	case IROp::Neg:
		gpr.MapDirtyIn(inst.dest, inst.src1);
		SUBW(gpr.R(inst.dest), R_ZERO, gpr.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_Logic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::And:
	case IROp::Or:
	case IROp::Xor:
	case IROp::AndConst:
	case IROp::OrConst:
	case IROp::XorConst:
	case IROp::Not:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_Assign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Mov:
		gpr.MapDirtyIn(inst.dest, inst.src1);
		MV(gpr.R(inst.dest), gpr.R(inst.src1));
		break;

	case IROp::Ext8to32:
		if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			SEXT_B(gpr.R(inst.dest), gpr.R(inst.src1));
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			SLLI(gpr.R(inst.dest), gpr.R(inst.src1), 24);
			SRAIW(gpr.R(inst.dest), gpr.R(inst.dest), 24);
		}
		break;

	case IROp::Ext16to32:
		if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			SEXT_H(gpr.R(inst.dest), gpr.R(inst.src1));
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			SLLI(gpr.R(inst.dest), gpr.R(inst.src1), 16);
			SRAIW(gpr.R(inst.dest), gpr.R(inst.dest), 16);
		}
		break;

	case IROp::ReverseBits:
		CompIR_Generic(inst);
		break;

	case IROp::BSwap16:
		CompIR_Generic(inst);
		break;

	case IROp::BSwap32:
		if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			REV8(gpr.R(inst.dest), gpr.R(inst.src1));
			if (XLEN >= 64) {
				// REV8 swaps the entire register, so get the 32 highest bits.
				SRLI(gpr.R(inst.dest), gpr.R(inst.dest), XLEN - 32);
			}
		} else {
			CompIR_Generic(inst);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
