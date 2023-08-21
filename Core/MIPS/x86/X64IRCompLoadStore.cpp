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

#include "Core/MemMap.h"
#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

// This file contains compilation for load/store instructions.
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

Gen::OpArg X64JitBackend::PrepareSrc1Address(IRInst inst) {
	const IRMeta *m = GetIRMeta(inst.op);
	bool readsFromSrc1 = inst.src1 == inst.src3 && (m->flags & (IRFLAG_SRC3 | IRFLAG_SRC3DST)) != 0;

	OpArg addrArg;
	if (inst.src1 == MIPS_REG_ZERO) {
#ifdef MASKED_PSP_MEMORY
		inst.constant &= Memory::MEMVIEW32_MASK;
#endif
#if PPSSPP_ARCH(AMD64)
		addrArg = MDisp(MEMBASEREG, inst.constant & 0x7FFFFFFF);
#else
		addrArg = M(Memory::base + inst.constant);
#endif
	} else if ((jo.cachePointers || regs_.IsGPRMappedAsPointer(inst.src1)) && !readsFromSrc1) {
		X64Reg src1 = regs_.MapGPRAsPointer(inst.src1);
		addrArg = MDisp(src1, (int)inst.constant);
	} else {
		regs_.MapGPR(inst.src1);
#ifdef MASKED_PSP_MEMORY
		LEA(PTRBITS, SCRATCH1, MDisp(regs_.RX(inst.src1), (int)inst.constant));
		AND(PTRBITS, R(SCRATCH1), Imm32(Memory::MEMVIEW32_MASK));
		ADD(PTRBITS, R(SCRATCH1), ImmPtr(Memory::base));
		addrArg = MatR(SCRATCH1);
#else
#if PPSSPP_ARCH(AMD64)
		addrArg = MComplex(MEMBASEREG, regs_.RX(inst.src1), SCALE_1, (int)inst.constant);
#else
		addrArg = MDisp(regs_.RX(inst.src1), Memory::base + inst.constant);
#endif
#endif
	}

	return addrArg;
}

void X64JitBackend::CompIR_CondStore(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Store32Conditional:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_FLoad(IRInst inst) {
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

void X64JitBackend::CompIR_FStore(IRInst inst) {
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

void X64JitBackend::CompIR_Load(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.dest, inst.src1);
	OpArg addrArg = PrepareSrc1Address(inst);
	// With NOINIT, MapReg won't subtract MEMBASEREG even if dest == src1.
	regs_.MapGPR(inst.dest, MIPSMap::NOINIT);

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Load8:
		MOVZX(32, 8, regs_.RX(inst.dest), addrArg);
		break;

	case IROp::Load8Ext:
		MOVSX(32, 8, regs_.RX(inst.dest), addrArg);
		break;

	case IROp::Load16:
		MOVZX(32, 16, regs_.RX(inst.dest), addrArg);
		break;

	case IROp::Load16Ext:
		MOVSX(32, 16, regs_.RX(inst.dest), addrArg);
		break;

	case IROp::Load32:
		MOV(32, regs_.R(inst.dest), addrArg);
		break;

	case IROp::Load32Linked:
		if (inst.dest != MIPS_REG_ZERO)
			MOV(32, regs_.R(inst.dest), addrArg);
		regs_.SetGPRImm(IRREG_LLBIT, 1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_LoadShift(IRInst inst) {
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

void X64JitBackend::CompIR_Store(IRInst inst) {
	CONDITIONAL_DISABLE;

	regs_.SpillLockGPR(inst.src3, inst.src1);
	OpArg addrArg = PrepareSrc1Address(inst);

	OpArg valueArg;
	X64Reg valueReg = regs_.TryMapTempImm(inst.src3);
	if (valueReg != INVALID_REG) {
		valueArg = R(valueReg);
	} else if (regs_.IsGPRImm(inst.src3)) {
		u32 imm = regs_.GetGPRImm(inst.src3);
		switch (inst.op) {
		case IROp::Store8: valueArg = Imm8((u8)imm); break;
		case IROp::Store16: valueArg = Imm16((u16)imm); break;
		case IROp::Store32: valueArg = Imm32(imm); break;
		default:
			INVALIDOP;
			break;
		}
	} else {
		valueArg = R(regs_.MapGPR(inst.src3));
	}

	// TODO: Safe memory?  Or enough to have crash handler + validate?

	switch (inst.op) {
	case IROp::Store8:
		MOV(8, addrArg, valueArg);
		break;

	case IROp::Store16:
		MOV(16, addrArg, valueArg);
		break;

	case IROp::Store32:
		MOV(32, addrArg, valueArg);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_StoreShift(IRInst inst) {
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

void X64JitBackend::CompIR_VecLoad(IRInst inst) {
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

void X64JitBackend::CompIR_VecStore(IRInst inst) {
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

#endif
