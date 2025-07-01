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
#include "Core/MIPS/LoongArch64/LoongArch64Jit.h"
#include "Core/MIPS/LoongArch64/LoongArch64RegCache.h"

// This file contains compilation for load/store instructions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace LoongArch64Gen;
using namespace LoongArch64JitConstants;

void LoongArch64JitBackend::SetScratch1ToSrc1Address(IRReg src1) {
	regs_.MapGPR(src1);
#ifdef MASKED_PSP_MEMORY
	SLLI_W(SCRATCH1, regs_.R(src1), 2);
	SRLI_W(SCRATCH1, SCRATCH1, 2);
	ADD_D(SCRATCH1, SCRATCH1, MEMBASEREG);
#else
	// Clear the top bits to be safe.
	SLLI_D(SCRATCH1, regs_.R(src1), 32);
	SRLI_D(SCRATCH1, SCRATCH1, 32);
	ADD_D(SCRATCH1, SCRATCH1, MEMBASEREG);
#endif
}

int32_t LoongArch64JitBackend::AdjustForAddressOffset(LoongArch64Gen::LoongArch64Reg *reg, int32_t constant, int32_t range) {
	if (constant < -2048 || constant + range > 2047) {
#ifdef MASKED_PSP_MEMORY
		if (constant > 0)
			constant &= Memory::MEMVIEW32_MASK;
#endif
		// It can't be this negative, must be a constant with top bit set.
		if ((constant & 0xC0000000) == 0x80000000) {
			LI(SCRATCH2, (uint32_t)constant);
			ADD_D(SCRATCH1, *reg, SCRATCH2);
		} else {
			LI(SCRATCH2, constant);
			ADD_D(SCRATCH1, *reg, SCRATCH2);
		}
		*reg = SCRATCH1;
		return 0;
	}
	return constant;
}

void LoongArch64JitBackend::CompIR_Load(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.dest, inst.src1);
	LoongArch64Reg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = regs_.MapGPRAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	// With NOINIT, MapReg won't subtract MEMBASEREG even if dest == src1.
	regs_.MapGPR(inst.dest, MIPSMap::NOINIT);
	regs_.MarkGPRDirty(inst.dest, true);

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Load8:
		LD_BU(regs_.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load8Ext:
		LD_B(regs_.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load16:
		LD_HU(regs_.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load16Ext:
		LD_H(regs_.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load32:
		LD_W(regs_.R(inst.dest), addrReg, imm);
		break;

	case IROp::Load32Linked:
		if (inst.dest != MIPS_REG_ZERO)
			LD_W(regs_.R(inst.dest), addrReg, imm);
		regs_.SetGPRImm(IRREG_LLBIT, 1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_LoadShift(IRInst inst) {
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

void LoongArch64JitBackend::CompIR_FLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoongArch64Reg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = regs_.MapGPRAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::LoadFloat:
		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		FLD_S(regs_.F(inst.dest), addrReg, imm);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_VecLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoongArch64Reg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = regs_.MapGPRAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	// We need to be able to address the whole 16 bytes, so offset of 12.
	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant, 12);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::LoadVec4:
		if (cpu_info.LOONGARCH_LSX) {
			regs_.MapVec4(inst.dest, MIPSMap::NOINIT);
			VLD(regs_.V(inst.dest), addrReg, imm);
		} else {
			for (int i = 0; i < 4; ++i) {
				// Spilling is okay.
				regs_.MapFPR(inst.dest + i, MIPSMap::NOINIT);
				FLD_S(regs_.F(inst.dest + i), addrReg, imm + 4 * i);
			}
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Store(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.src3, inst.src1);
	LoongArch64Reg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if ((jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) && inst.src3 != inst.src1) {
		addrReg = regs_.MapGPRAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	LoongArch64Reg valueReg = regs_.TryMapTempImm(inst.src3);
	if (valueReg == INVALID_REG)
		valueReg = regs_.MapGPR(inst.src3);

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Store8:
		ST_B(valueReg, addrReg, imm);
		break;

	case IROp::Store16:
		ST_H(valueReg, addrReg, imm);
		break;

	case IROp::Store32:
		ST_W(valueReg, addrReg, imm);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_CondStore(IRInst inst) {
	CONDITIONAL_DISABLE;
	if (inst.op != IROp::Store32Conditional)
		INVALIDOP;

	regs_.SpillLockGPR(IRREG_LLBIT, inst.src3, inst.src1);
	LoongArch64Reg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if ((jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) && inst.src3 != inst.src1) {
		addrReg = regs_.MapGPRAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}
	regs_.MapGPR(inst.src3, inst.dest == MIPS_REG_ZERO ? MIPSMap::INIT : MIPSMap::DIRTY);
	regs_.MapGPR(IRREG_LLBIT);

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	FixupBranch condFailed = BEQZ(regs_.R(IRREG_LLBIT));
	ST_W(regs_.R(inst.src3), addrReg, imm);

	if (inst.dest != MIPS_REG_ZERO) {
		LI(regs_.R(inst.dest), 1);
		FixupBranch finish = B();

		SetJumpTarget(condFailed);
		LI(regs_.R(inst.dest), 0);
		SetJumpTarget(finish);
	} else {
		SetJumpTarget(condFailed);
	}
}

void LoongArch64JitBackend::CompIR_StoreShift(IRInst inst) {
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

void LoongArch64JitBackend::CompIR_FStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoongArch64Reg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = regs_.MapGPRAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::StoreFloat:
		regs_.MapFPR(inst.src3);
		FST_S(regs_.F(inst.src3), addrReg, imm);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_VecStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoongArch64Reg addrReg = INVALID_REG;
	if (inst.src1 == MIPS_REG_ZERO) {
		// This will get changed by AdjustForAddressOffset.
		addrReg = MEMBASEREG;
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
	} else if (jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) {
		addrReg = regs_.MapGPRAsPointer(inst.src1);
	} else {
		SetScratch1ToSrc1Address(inst.src1);
		addrReg = SCRATCH1;
	}

	// We need to be able to address the whole 16 bytes, so offset of 12.
	s32 imm = AdjustForAddressOffset(&addrReg, inst.constant, 12);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::StoreVec4:
		if (cpu_info.LOONGARCH_LSX) {
			regs_.MapVec4(inst.src3);
			VST(regs_.V(inst.src3), addrReg, imm);
		} else {
			for (int i = 0; i < 4; ++i) {
				// Spilling is okay, though not ideal.
				regs_.MapFPR(inst.src3 + i);
				FST_S(regs_.F(inst.src3 + i), addrReg, imm + 4 * i);
			}
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
