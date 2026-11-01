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

#include "Core/MemMap.h"
#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"

// This file contains compilation for memory loads and stores.
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

// Puts the masked PSP address of src1 + constant in SCRATCH1, for use as an offset from
// MEMBASEREG. Destroys SCRATCH2.
void ArmJitBackend::PrepareSrc1Address(IRInst inst) {
	if (inst.src1 == MIPS_REG_ZERO) {
		MOVI2R(SCRATCH1, inst.constant & Memory::MEMVIEW32_MASK);
		return;
	}

	ARMReg src1 = regs_.MapGPR(inst.src1);
	// Clears bits 28-31 (see MEMVIEW32_MASK).
	if (inst.constant == 0) {
		BIC(SCRATCH1, src1, Operand2(0xF0, 4));
	} else {
		ADDI2R(SCRATCH1, src1, inst.constant, SCRATCH2);
		BIC(SCRATCH1, SCRATCH1, Operand2(0xF0, 4));
	}
}

void ArmJitBackend::CompIR_CondStore(IRInst inst) {
	CONDITIONAL_DISABLE;
	if (inst.op != IROp::Store32Conditional)
		INVALIDOP;

	regs_.SpillLockGPR(IRREG_LLBIT, inst.src3, inst.src1);
	PrepareSrc1Address(inst);
	// The result goes in the same register (dest == src3), so it must be flushed.
	ARMReg valueReg = regs_.MapGPR(inst.src3, inst.dest == MIPS_REG_ZERO ? MIPSMap::INIT : MIPSMap::DIRTY);
	regs_.MapGPR(IRREG_LLBIT, MIPSMap::INIT);

	CMP(regs_.R(IRREG_LLBIT), Operand2(0));
	FixupBranch condFailed = B_CC(CC_EQ);
	STR(valueReg, MEMBASEREG, SCRATCH1);

	if (inst.dest != MIPS_REG_ZERO) {
		MOVI2R(regs_.R(inst.dest), 1);
		FixupBranch finish = B();

		SetJumpTarget(condFailed);
		MOVI2R(regs_.R(inst.dest), 0);
		SetJumpTarget(finish);
	} else {
		SetJumpTarget(condFailed);
	}
}

void ArmJitBackend::CompIR_FLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::LoadFloat:
		// VLDR has no register offset form.
		PrepareSrc1Address(inst);
		ADD(SCRATCH1, SCRATCH1, MEMBASEREG);
		regs_.MapFPR(inst.dest, MIPSMap::NOINIT);
		VLDR(regs_.F(inst.dest), SCRATCH1, 0);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_FStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::StoreFloat:
		PrepareSrc1Address(inst);
		ADD(SCRATCH1, SCRATCH1, MEMBASEREG);
		regs_.MapFPR(inst.src3);
		VSTR(regs_.F(inst.src3), SCRATCH1, 0);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_Load(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.dest, inst.src1);
	PrepareSrc1Address(inst);
	// Only ll can load into $zero (for the llBit), and must not map it.
	ARMReg dest = INVALID_REG;
	if (inst.dest != MIPS_REG_ZERO)
		dest = regs_.MapGPR(inst.dest, MIPSMap::NOINIT);

	switch (inst.op) {
	case IROp::Load8:
		LDRB(dest, MEMBASEREG, SCRATCH1);
		break;

	case IROp::Load8Ext:
		LDRSB(dest, MEMBASEREG, SCRATCH1);
		break;

	case IROp::Load16:
		LDRH(dest, MEMBASEREG, SCRATCH1);
		break;

	case IROp::Load16Ext:
		LDRSH(dest, MEMBASEREG, SCRATCH1);
		break;

	case IROp::Load32:
		LDR(dest, MEMBASEREG, SCRATCH1);
		break;

	case IROp::Load32Linked:
		if (inst.dest != MIPS_REG_ZERO)
			LDR(dest, MEMBASEREG, SCRATCH1);
		regs_.SetGPRImm(IRREG_LLBIT, 1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_LoadShift(IRInst inst) {
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

void ArmJitBackend::CompIR_Store(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.src3, inst.src1);
	PrepareSrc1Address(inst);

	ARMReg valueReg = regs_.TryMapTempImm(inst.src3);
	if (valueReg == INVALID_REG)
		valueReg = regs_.MapGPR(inst.src3);

	switch (inst.op) {
	case IROp::Store8:
		STRB(valueReg, MEMBASEREG, SCRATCH1);
		break;

	case IROp::Store16:
		STRH(valueReg, MEMBASEREG, SCRATCH1);
		break;

	case IROp::Store32:
		STR(valueReg, MEMBASEREG, SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_StoreShift(IRInst inst) {
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

void ArmJitBackend::CompIR_VecLoad(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::LoadVec4:
		PrepareSrc1Address(inst);
		ADD(SCRATCH1, SCRATCH1, MEMBASEREG);
		// The lanes are in separate registers.
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i)
			VLDR(regs_.F(inst.dest + i), SCRATCH1, i * 4);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_VecStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::StoreVec4:
		PrepareSrc1Address(inst);
		ADD(SCRATCH1, SCRATCH1, MEMBASEREG);
		regs_.Map(inst);
		for (int i = 0; i < 4; ++i)
			VSTR(regs_.F(inst.src3 + i), SCRATCH1, i * 4);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
