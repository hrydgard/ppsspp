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

#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/x86/X64IRJit.h"
#include "Core/MIPS/x86/X64IRRegCache.h"

// This file contains compilation for integer / arithmetic / logic related instructions.
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

void X64JitBackend::CompIR_Arith(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool allowPtrMath = inst.constant <= 0x7FFFFFFF;
#ifdef MASKED_PSP_MEMORY
	// Since we modify it, we can't safely.
	allowPtrMath = false;
#endif

	switch (inst.op) {
	case IROp::Add:
		regs_.Map(inst);
		if (inst.dest == inst.src2) {
			ADD(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else if (inst.dest == inst.src1) {
			ADD(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			ADD(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
		break;

	case IROp::Sub:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			// TODO: Might be nice to have a pass to turn this into Neg.
			// Special cased to avoid wasting a reg on zero.
			regs_.SpillLockGPR(inst.dest, inst.src2);
			regs_.MapGPR(inst.src2);
			regs_.MapGPR(inst.dest, MIPSMap::NOINIT);
			if (inst.dest != inst.src2)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src2));
			NEG(32, regs_.R(inst.dest));
		} else if (inst.dest == inst.src2) {
			regs_.Map(inst);
			MOV(32, R(SCRATCH1), regs_.R(inst.src2));
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SUB(32, regs_.R(inst.dest), R(SCRATCH1));
		} else if (inst.dest == inst.src1) {
			regs_.Map(inst);
			SUB(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else {
			regs_.Map(inst);
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SUB(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
		break;

	case IROp::AddConst:
		if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
			regs_.MarkGPRAsPointerDirty(inst.dest);
			ADD(PTRBITS, regs_.RPtr(inst.dest), SImmAuto(inst.constant));
		} else {
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			ADD(32, regs_.R(inst.dest), SImmAuto(inst.constant));
		}
		break;

	case IROp::SubConst:
		if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
			regs_.MarkGPRAsPointerDirty(inst.dest);
			SUB(PTRBITS, regs_.RPtr(inst.dest), SImmAuto(inst.constant));
		} else {
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SUB(32, regs_.R(inst.dest), SImmAuto(inst.constant));
		}
		break;

	case IROp::Neg:
		regs_.Map(inst);
		if (inst.dest != inst.src1) {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		}
		NEG(32, regs_.R(inst.dest));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Assign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Mov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::Ext8to32:
#if PPSSPP_ARCH(X86)
		DISABLE;  // 8-bit registers need special handling
#endif
		regs_.Map(inst);
		MOVZX(32, 8, regs_.RX(inst.dest), regs_.R(inst.src1));
		break;

	case IROp::Ext16to32:
#if PPSSPP_ARCH(X86)
		DISABLE;  // 8-bit registers need special handling
#endif
		regs_.Map(inst);
		MOVZX(32, 16, regs_.RX(inst.dest), regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Bits(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::ReverseBits:
	case IROp::BSwap16:
	case IROp::BSwap32:
	case IROp::Clz:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Compare(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Slt:
	case IROp::SltConst:
	case IROp::SltU:
	case IROp::SltUConst:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_CondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MovZ:
	case IROp::MovNZ:
	case IROp::Max:
	case IROp::Min:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Div(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Div:
	case IROp::DivU:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_HiLo(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MtLo:
	case IROp::MtHi:
	case IROp::MfLo:
	case IROp::MfHi:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Logic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::And:
		regs_.Map(inst);
		if (inst.dest == inst.src1) {
			AND(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else if (inst.dest == inst.src2) {
			AND(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			AND(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
		break;
	case IROp::Or:
		regs_.Map(inst);
		if (inst.dest == inst.src1) {
			OR(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else if (inst.dest == inst.src2) {
			OR(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			OR(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
		break;

	case IROp::Xor:
		regs_.Map(inst);
		if (inst.dest == inst.src1) {
			XOR(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else if (inst.dest == inst.src2) {
			XOR(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			XOR(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
		break;

	case IROp::AndConst:
		regs_.Map(inst);
		if (inst.dest != inst.src1)
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		AND(32, regs_.R(inst.dest), UImmAuto(inst.constant));
		break;

	case IROp::OrConst:
		regs_.Map(inst);
		if (inst.dest != inst.src1)
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		OR(32, regs_.R(inst.dest), UImmAuto(inst.constant));
		break;

	case IROp::XorConst:
	case IROp::Not:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Mult(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Mult:
	case IROp::MultU:
	case IROp::Madd:
	case IROp::MaddU:
	case IROp::Msub:
	case IROp::MsubU:
		CompIR_Generic(inst);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Shift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Shl:
	case IROp::Shr:
	case IROp::Sar:
	case IROp::Ror:
		CompIR_Generic(inst);
		break;

	case IROp::ShlImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SHL(32, regs_.R(inst.dest), Imm8(inst.src2));
		}
		break;

	case IROp::ShrImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SHR(32, regs_.R(inst.dest), Imm8(inst.src2));
		}
		break;

	case IROp::SarImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SAR(32, regs_.R(inst.dest), Imm8(31));
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SAR(32, regs_.R(inst.dest), Imm8(inst.src2));
		}
		break;

	case IROp::RorImm:
		if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			ROR(32, regs_.R(inst.dest), Imm8(inst.src2 & 31));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
