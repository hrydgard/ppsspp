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

void RiscVJitBackend::SetScratch1ToSrc1Address(IRReg src1) {
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

int32_t RiscVJitBackend::AdjustForAddressOffset(RiscVGen::RiscVReg *reg, int32_t constant, int32_t range) {
	if (constant < -2048 || constant + range > 2047) {
		LI(SCRATCH2, constant);
		ADD(SCRATCH1, *reg, SCRATCH2);
		*reg = SCRATCH1;
		return 0;
	}
	return constant;
}

void RiscVJitBackend::CompIR_Load(IRInst inst) {
	CONDITIONAL_DISABLE;

	gpr.SpillLockGPR(inst.dest, inst.src1);
	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || gpr.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	// With NOINIT, MapReg won't subtract MEMBASEREG even if dest == src1.
	gpr.MapReg(inst.dest, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);

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
		LW(gpr.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load32Linked:
		if (inst.dest != MIPS_REG_ZERO)
			LW(gpr.R(inst.dest), addrReg, imm);
		gpr.SetGPRImm(IRREG_LLBIT, 1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_LoadShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Load32Left:
	case IROp::Load32Right:
		// Should not happen if the pass to split is active.
		DISABLE;
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_FLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || gpr.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::LoadFloat:
		fpr.MapReg(inst.dest, MIPSMap::NOINIT);
		FL(32, fpr.R(inst.dest), addrReg, imm);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_VecLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || gpr.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	// We need to be able to address the whole 16 bytes, so offset of 12.
	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant, 12);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::LoadVec4:
		for (int i = 0; i < 4; ++i) {
			// Spilling is okay.
			fpr.MapReg(inst.dest + i, MIPSMap::NOINIT);
			FL(32, fpr.R(inst.dest + i), addrReg, imm + 4 * i);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Store(IRInst inst) {
	CONDITIONAL_DISABLE;

	gpr.SpillLockGPR(inst.src3, inst.src1);
	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if ((jo.cachePointers || gpr.IsGPRMappedAsPointer(inst.src1)) && inst.src3 != inst.src1) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	RiscVReg valueReg = gpr.TryMapTempImm(inst.src3);
	if (valueReg == INVALID_REG)
		valueReg = gpr.MapReg(inst.src3);

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

void RiscVJitBackend::CompIR_CondStore(IRInst inst) {
	CONDITIONAL_DISABLE;
	if (inst.op != IROp::Store32Conditional)
		INVALIDOP;

	gpr.SpillLockGPR(IRREG_LLBIT, inst.src3, inst.src1);
	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if ((jo.cachePointers || gpr.IsGPRMappedAsPointer(inst.src1)) && inst.src3 != inst.src1) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	gpr.MapReg(inst.src3, inst.dest == MIPS_REG_ZERO ? MIPSMap::INIT : MIPSMap::DIRTY);
	gpr.MapReg(IRREG_LLBIT);

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	FixupBranch condFailed = BEQ(gpr.R(IRREG_LLBIT), R_ZERO);
	SW(gpr.R(inst.src3), addrReg, imm);

	if (inst.dest != MIPS_REG_ZERO) {
		LI(gpr.R(inst.dest), 1);
		FixupBranch finish = J();

		SetJumpTarget(condFailed);
		LI(gpr.R(inst.dest), 0);
		SetJumpTarget(finish);
	} else {
		SetJumpTarget(condFailed);
	}
}

void RiscVJitBackend::CompIR_StoreShift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Store32Left:
	case IROp::Store32Right:
		// Should not happen if the pass to split is active.
		DISABLE;
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_FStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || gpr.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::StoreFloat:
		fpr.MapReg(inst.src3);
		FS(32, fpr.R(inst.src3), addrReg, imm);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_VecStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || gpr.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = gpr.MapRegAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	// We need to be able to address the whole 16 bytes, so offset of 12.
	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant, 12);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::StoreVec4:
		for (int i = 0; i < 4; ++i) {
			// Spilling is okay, though not ideal.
			fpr.MapReg(inst.src3 + i);
			FS(32, fpr.R(inst.src3 + i), addrReg, imm + 4 * i);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
