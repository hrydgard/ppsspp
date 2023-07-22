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

#include "Core/MemMap.h"
#include "Core/MIPS/RiscV/RiscVJit.h"
#include "Core/MIPS/RiscV/RiscVRegCache.h"

// This file contains compilation for load/store instructions.
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

void RiscVJit::SetScratch1ToSrc1Address(IRReg src1) {
	gpr.MapReg(src1);
#ifdef MASKED_PSP_MEMORY
	SLLIW(SCRATCH1, gpr.R(src1), 2);
	SRLIW(SCRATCH1, SCRATCH1, 2);
	ADD(SCRATCH1, SCRATCH1, MEMBASEREG);
#else
	// Clear the top bits to be safe.
	if (cpu_info.RiscV_Zba) {
		ADD_UW(SCRATCH1, gpr.R(src1), MEMBASEREG);
	} else {
		_assert_(XLEN == 64);
		SLLI(SCRATCH1, gpr.R(src1), 32);
		SRLI(SCRATCH1, SCRATCH1, 32);
		ADD(SCRATCH1, SCRATCH1, MEMBASEREG);
	}
#endif
}

int32_t RiscVJit::AdjustForAddressOffset(RiscVGen::RiscVReg *reg, int32_t constant) {
	if (constant < -2048 || constant > 2047) {
		LI(SCRATCH2, constant);
		ADD(SCRATCH1, *reg, SCRATCH2);
		*reg = SCRATCH1;
		return 0;
	}
	return constant;
}

void RiscVJit::CompIR_Load(IRInst inst) {
	CONDITIONAL_DISABLE;

	gpr.SpillLock(inst.dest, inst.src1);
	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || gpr.IsMappedAsPointer(inst.src1)) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	// If they're the same, MapReg may subtract MEMBASEREG, so just mark dirty.
	if (inst.dest == inst.src1)
		gpr.MarkDirty(gpr.R(inst.dest));
	else
		gpr.MapReg(inst.dest, MAP_NOINIT);
	gpr.ReleaseSpillLock(inst.dest, inst.src1);

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Load8:
		LBU(gpr.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load8Ext:
		LB(gpr.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load16:
		LHU(gpr.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load16Ext:
		LH(gpr.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load32:
		LWU(gpr.R(inst.dest), addrReg, imm);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_LoadShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Load32Left:
	case IROp::Load32Right:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_FLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::LoadFloat:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_VecLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::LoadVec4:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_Store(IRInst inst) {
	CONDITIONAL_DISABLE;

	gpr.SpillLock(inst.src3, inst.src1);
	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if ((jo.cachePointers || gpr.IsMappedAsPointer(inst.src1)) && inst.src3 != inst.src1) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	RiscVReg valueReg = gpr.TryMapTempImm(inst.src3);
	if (valueReg == INVALID_REG)
		valueReg = gpr.MapReg(inst.src3);
	gpr.ReleaseSpillLock(inst.src3, inst.src1);

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Store8:
		SB(valueReg, addrReg, imm);
		break;

	case IROp::Store16:
		SH(valueReg, addrReg, imm);
		break;

	case IROp::Store32:
		SW(valueReg, addrReg, imm);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_StoreShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Store32Left:
	case IROp::Store32Right:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_FStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::StoreFloat:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJit::CompIR_VecStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::StoreVec4:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
