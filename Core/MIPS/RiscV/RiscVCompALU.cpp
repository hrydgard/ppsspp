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

void RiscVJitBackend::CompIR_Arith(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool allowPtrMath = true;
#ifdef MASKED_PSP_MEMORY
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
		regs_.Map(inst);
		ADDW(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Sub:
		regs_.Map(inst);
		SUBW(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::AddConst:
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			// Typical of stack pointer updates.
			if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
				regs_.MarkGPRAsPointerDirty(inst.dest);
				ADDI(regs_.RPtr(inst.dest), regs_.RPtr(inst.dest), inst.constant);
			} else {
				regs_.Map(inst);
				ADDIW(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant);
				regs_.MarkGPRDirty(inst.dest, true);
			}
		} else {
			regs_.Map(inst);
			LI(SCRATCH1, (int32_t)inst.constant);
			ADDW(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::SubConst:
		regs_.Map(inst);
		LI(SCRATCH1, (int32_t)inst.constant);
		SUBW(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Neg:
		regs_.Map(inst);
		SUBW(regs_.R(inst.dest), R_ZERO, regs_.R(inst.src1));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Logic(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool resultNormalized = false;
	switch (inst.op) {
	case IROp::And:
		if (inst.src1 != inst.src2) {
			regs_.Map(inst);
			AND(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		} else if (inst.src1 != inst.dest) {
			regs_.Map(inst);
			MV(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Or:
		if (inst.src1 != inst.src2) {
			// If both were normalized before, the result is normalized.
			resultNormalized = regs_.IsNormalized32(inst.src1) && regs_.IsNormalized32(inst.src2);
			regs_.Map(inst);
			OR(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
			regs_.MarkGPRDirty(inst.dest, resultNormalized);
		} else if (inst.src1 != inst.dest) {
			regs_.Map(inst);
			MV(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Xor:
		if (inst.src1 == inst.src2) {
			regs_.SetGPRImm(inst.dest, 0);
		} else {
			regs_.Map(inst);
			XOR(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		}
		break;

	case IROp::AndConst:
		resultNormalized = regs_.IsNormalized32(inst.src1);
		regs_.Map(inst);
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			ANDI(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant);
		} else {
			LI(SCRATCH1, (int32_t)inst.constant);
			AND(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
		}
		// If the sign bits aren't cleared, and it was normalized before - it still is.
		if ((inst.constant & 0x80000000) != 0 && resultNormalized)
			regs_.MarkGPRDirty(inst.dest, true);
		// Otherwise, if we cleared the sign bits, it's naturally normalized.
		else if ((inst.constant & 0x80000000) == 0)
			regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::OrConst:
		resultNormalized = regs_.IsNormalized32(inst.src1);
		regs_.Map(inst);
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			ORI(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant);
		} else {
			LI(SCRATCH1, (int32_t)inst.constant);
			OR(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
		}
		// Since our constant is normalized, oring its bits in won't hurt normalization.
		regs_.MarkGPRDirty(inst.dest, resultNormalized);
		break;

	case IROp::XorConst:
		regs_.Map(inst);
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			XORI(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant);
		} else {
			LI(SCRATCH1, (int32_t)inst.constant);
			XOR(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
		}
		break;

	case IROp::Not:
		regs_.Map(inst);
		NOT(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Assign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Mov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MV(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Ext8to32:
		regs_.Map(inst);
		if (cpu_info.RiscV_Zbb) {
			SEXT_B(regs_.R(inst.dest), regs_.R(inst.src1));
		} else {
			SLLI(regs_.R(inst.dest), regs_.R(inst.src1), 24);
			SRAIW(regs_.R(inst.dest), regs_.R(inst.dest), 24);
		}
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Ext16to32:
		regs_.Map(inst);
		if (cpu_info.RiscV_Zbb) {
			SEXT_H(regs_.R(inst.dest), regs_.R(inst.src1));
		} else {
			SLLI(regs_.R(inst.dest), regs_.R(inst.src1), 16);
			SRAIW(regs_.R(inst.dest), regs_.R(inst.dest), 16);
		}
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Bits(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::ReverseBits:
		if (cpu_info.RiscV_Zbb) {
			regs_.Map(inst);
			// Start by reversing bytes (note: this puts in upper 32 of XLEN.)
			REV8(regs_.R(inst.dest), regs_.R(inst.src1));

			// Swap nibbles.
			LI(SCRATCH1, (s32)0xF0F0F0F0);
			SRLI(SCRATCH2, regs_.R(inst.dest), XLEN - 32 - 4);
			AND(SCRATCH2, SCRATCH2, SCRATCH1);
			if (XLEN >= 64)
				SRLI(regs_.R(inst.dest), regs_.R(inst.dest), XLEN - 28);
			else
				SLLI(regs_.R(inst.dest), regs_.R(inst.dest), 4);
			SRLIW(SCRATCH1, SCRATCH1, 4);
			AND(regs_.R(inst.dest), regs_.R(inst.dest), SCRATCH1);
			OR(regs_.R(inst.dest), regs_.R(inst.dest), SCRATCH2);

			// Now the consecutive pairs.
			LI(SCRATCH1, (s32)0x33333333);
			SRLI(SCRATCH2, regs_.R(inst.dest), 2);
			AND(SCRATCH2, SCRATCH2, SCRATCH1);
			AND(regs_.R(inst.dest), regs_.R(inst.dest), SCRATCH1);
			SLLIW(regs_.R(inst.dest), regs_.R(inst.dest), 2);
			OR(regs_.R(inst.dest), regs_.R(inst.dest), SCRATCH2);

			// And finally the even and odd bits.
			LI(SCRATCH1, (s32)0x55555555);
			SRLI(SCRATCH2, regs_.R(inst.dest), 1);
			AND(SCRATCH2, SCRATCH2, SCRATCH1);
			AND(regs_.R(inst.dest), regs_.R(inst.dest), SCRATCH1);
			SLLIW(regs_.R(inst.dest), regs_.R(inst.dest), 1);
			OR(regs_.R(inst.dest), regs_.R(inst.dest), SCRATCH2);
		} else {
			CompIR_Generic(inst);
		}
		break;

	case IROp::BSwap16:
		CompIR_Generic(inst);
		break;

	case IROp::BSwap32:
		if (cpu_info.RiscV_Zbb) {
			regs_.Map(inst);
			REV8(regs_.R(inst.dest), regs_.R(inst.src1));
			if (XLEN >= 64) {
				// REV8 swaps the entire register, so get the 32 highest bits.
				SRAI(regs_.R(inst.dest), regs_.R(inst.dest), XLEN - 32);
				regs_.MarkGPRDirty(inst.dest, true);
			}
		} else {
			CompIR_Generic(inst);
		}
		break;

	case IROp::Clz:
		if (cpu_info.RiscV_Zbb) {
			regs_.Map(inst);
			// This even sets to 32 when zero, perfect.
			CLZW(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, true);
		} else {
			CompIR_Generic(inst);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Shift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Shl:
		regs_.Map(inst);
		SLLW(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Shr:
		regs_.Map(inst);
		SRLW(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Sar:
		regs_.Map(inst);
		SRAW(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Ror:
		if (cpu_info.RiscV_Zbb) {
			regs_.Map(inst);
			RORW(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
			regs_.MarkGPRDirty(inst.dest, true);
		} else {
			CompIR_Generic(inst);
		}
		break;

	case IROp::ShlImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MV(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else {
			regs_.Map(inst);
			SLLIW(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::ShrImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MV(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else {
			regs_.Map(inst);
			SRLIW(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::SarImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.Map(inst);
			SRAIW(regs_.R(inst.dest), regs_.R(inst.src1), 31);
			regs_.MarkGPRDirty(inst.dest, true);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MV(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else {
			regs_.Map(inst);
			SRAIW(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::RorImm:
		if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MV(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else if (cpu_info.RiscV_Zbb) {
			regs_.Map(inst);
			RORIW(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2 & 31);
			regs_.MarkGPRDirty(inst.dest, true);
		} else {
			CompIR_Generic(inst);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Compare(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg lhs = INVALID_REG;
	RiscVReg rhs = INVALID_REG;
	switch (inst.op) {
	case IROp::Slt:
		regs_.Map(inst);
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);

		SLT(regs_.R(inst.dest), lhs, rhs);
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::SltConst:
		if (inst.constant == 0) {
			// Basically, getting the sign bit.  Let's shift instead.
			regs_.Map(inst);
			SRLIW(regs_.R(inst.dest), regs_.R(inst.src1), 31);
			regs_.MarkGPRDirty(inst.dest, true);
		} else {
			regs_.Map(inst);
			NormalizeSrc1(inst, &lhs, SCRATCH1, false);

			if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
				SLTI(regs_.R(inst.dest), lhs, (int32_t)inst.constant);
			} else {
				LI(SCRATCH2, (int32_t)inst.constant);
				SLT(regs_.R(inst.dest), lhs, SCRATCH2);
			}
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::SltU:
		regs_.Map(inst);
		// It's still fine to sign extend, the biggest just get even bigger.
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);

		SLTU(regs_.R(inst.dest), lhs, rhs);
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::SltUConst:
		if (inst.constant == 0) {
			regs_.SetGPRImm(inst.dest, 0);
		} else {
			regs_.Map(inst);
			NormalizeSrc1(inst, &lhs, SCRATCH1, false);

			// We sign extend because we're comparing against something normalized.
			// It's also the most efficient to set.
			if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
				SLTIU(regs_.R(inst.dest), lhs, (int32_t)inst.constant);
			} else {
				LI(SCRATCH2, (int32_t)inst.constant);
				SLTU(regs_.R(inst.dest), lhs, SCRATCH2);
			}
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_CondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg lhs = INVALID_REG;
	RiscVReg rhs = INVALID_REG;
	FixupBranch fixup;
	switch (inst.op) {
	case IROp::MovZ:
	case IROp::MovNZ:
		if (inst.dest == inst.src2)
			return;

		// We could have a "zero" with wrong upper due to XOR, so we have to normalize.
		regs_.Map(inst);
		NormalizeSrc1(inst, &lhs, SCRATCH1, true);

		switch (inst.op) {
		case IROp::MovZ:
			fixup = BNE(lhs, R_ZERO);
			break;
		case IROp::MovNZ:
			fixup = BEQ(lhs, R_ZERO);
			break;
		default:
			INVALIDOP;
			break;
		}

		MV(regs_.R(inst.dest), regs_.R(inst.src2));
		SetJumpTarget(fixup);
		break;

	case IROp::Max:
		if (inst.src1 != inst.src2) {
			if (cpu_info.RiscV_Zbb) {
				regs_.Map(inst);
				NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
				MAX(regs_.R(inst.dest), lhs, rhs);
				// Because we had to normalize the inputs, the output is normalized.
				regs_.MarkGPRDirty(inst.dest, true);
			} else {
				CompIR_Generic(inst);
			}
		} else if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MV(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Min:
		if (inst.src1 != inst.src2) {
			if (cpu_info.RiscV_Zbb) {
				regs_.Map(inst);
				NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
				MIN(regs_.R(inst.dest), lhs, rhs);
				// Because we had to normalize the inputs, the output is normalized.
				regs_.MarkGPRDirty(inst.dest, true);
			} else {
				CompIR_Generic(inst);
			}
		} else if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MV(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_HiLo(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MtLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		// First, clear the bits we're replacing.
		SRLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
		SLLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
		// And now, insert the low 32 bits of src1.
		if (cpu_info.RiscV_Zba) {
			ADD_UW(regs_.R(IRREG_LO), regs_.R(inst.src1), regs_.R(IRREG_LO));
		} else {
			SLLI(SCRATCH1, regs_.R(inst.src1), XLEN - 32);
			SRLI(SCRATCH1, SCRATCH1, XLEN - 32);
			ADD(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		}
		break;

	case IROp::MtHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		SLLI(SCRATCH1, regs_.R(inst.src1), XLEN - 32);
		if (cpu_info.RiscV_Zba) {
			ADD_UW(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		} else {
			SLLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
			SRLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
			ADD(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		}
		break;

	case IROp::MfLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		// It won't be normalized, but that's fine...
		MV(regs_.R(inst.dest), regs_.R(IRREG_LO));
		break;

	case IROp::MfHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		SRAI(regs_.R(inst.dest), regs_.R(IRREG_LO), 32);
		if (XLEN == 64)
			regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Mult(IRInst inst) {
	CONDITIONAL_DISABLE;

	auto makeArgsUnsigned = [&](RiscVReg *lhs, RiscVReg *rhs) {
		if (cpu_info.RiscV_Zba) {
			ZEXT_W(SCRATCH1, regs_.R(inst.src1));
			ZEXT_W(SCRATCH2, regs_.R(inst.src2));
		} else {
			SLLI(SCRATCH1, regs_.R(inst.src1), XLEN - 32);
			SRLI(SCRATCH1, SCRATCH1, XLEN - 32);
			SLLI(SCRATCH2, regs_.R(inst.src2), XLEN - 32);
			SRLI(SCRATCH2, SCRATCH2, XLEN - 32);
		}
		*lhs = SCRATCH1;
		*rhs = SCRATCH2;
	};

	RiscVReg lhs = INVALID_REG;
	RiscVReg rhs = INVALID_REG;
	switch (inst.op) {
	case IROp::Mult:
		// TODO: Maybe IR could simplify when HI is not needed or clobbered?
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL(regs_.R(IRREG_LO), lhs, rhs);
		break;

	case IROp::MultU:
		// This is an "anti-norm32" case.  Let's just zero always.
		// TODO: If we could know that LO was only needed, we could use MULW.
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		makeArgsUnsigned(&lhs, &rhs);
		MUL(regs_.R(IRREG_LO), lhs, rhs);
		break;

	case IROp::Madd:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL(SCRATCH1, lhs, rhs);
		ADD(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	case IROp::MaddU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		makeArgsUnsigned(&lhs, &rhs);
		MUL(SCRATCH1, lhs, rhs);
		ADD(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	case IROp::Msub:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL(SCRATCH1, lhs, rhs);
		SUB(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	case IROp::MsubU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		makeArgsUnsigned(&lhs, &rhs);
		MUL(SCRATCH1, lhs, rhs);
		SUB(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Div(IRInst inst) {
	CONDITIONAL_DISABLE;

	RiscVReg numReg, denomReg;
	switch (inst.op) {
	case IROp::Div:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		// We have to do this because of the divide by zero and overflow checks below.
		NormalizeSrc12(inst, &numReg, &denomReg, SCRATCH1, SCRATCH2, true);
		DIVW(regs_.R(IRREG_LO), numReg, denomReg);
		REMW(R_RA, numReg, denomReg);
		// Now to combine them.  We'll do more with them below...
		SLLI(R_RA, R_RA, 32);
		if (cpu_info.RiscV_Zba) {
			ADD_UW(regs_.R(IRREG_LO), regs_.R(IRREG_LO), R_RA);
		} else {
			SLLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
			SRLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
			ADD(regs_.R(IRREG_LO), regs_.R(IRREG_LO), R_RA);
		}

		// Now some tweaks for divide by zero and overflow.
		{
			// Start with divide by zero, remainder is fine.
			FixupBranch skipNonZero = BNE(denomReg, R_ZERO);
			FixupBranch keepNegOne = BGE(numReg, R_ZERO);
			// Clear the -1 and replace it with 1.
			SRLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), 32);
			SLLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), 32);
			ADDI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), 1);
			SetJumpTarget(keepNegOne);
			SetJumpTarget(skipNonZero);

			// For overflow, RISC-V sets LO right, but remainder to zero.
			// Cheating a bit by using R_RA as a temp...
			LI(R_RA, (int32_t)0x80000000);
			FixupBranch notMostNegative = BNE(numReg, R_RA);
			LI(R_RA, -1);
			FixupBranch notNegativeOne = BNE(denomReg, R_RA);
			// Take our R_RA and put it in the high bits.
			SLLI(R_RA, R_RA, 32);
			OR(regs_.R(IRREG_LO), regs_.R(IRREG_LO), R_RA);
			SetJumpTarget(notNegativeOne);
			SetJumpTarget(notMostNegative);
		}
		break;

	case IROp::DivU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		// We have to do this because of the divide by zero check below.
		NormalizeSrc12(inst, &numReg, &denomReg, SCRATCH1, SCRATCH2, true);
		DIVUW(regs_.R(IRREG_LO), numReg, denomReg);
		REMUW(R_RA, numReg, denomReg);

		// On divide by zero, everything is correct already except the 0xFFFF case.
		{
			FixupBranch skipNonZero = BNE(denomReg, R_ZERO);
			// Luckily, we don't need SCRATCH2/denomReg anymore.
			LI(SCRATCH2, 0xFFFF);
			FixupBranch keepNegOne = BLTU(SCRATCH2, numReg);
			MV(regs_.R(IRREG_LO), SCRATCH2);
			SetJumpTarget(keepNegOne);
			SetJumpTarget(skipNonZero);
		}

		// Now combine the remainder in.
		SLLI(R_RA, R_RA, 32);
		if (cpu_info.RiscV_Zba) {
			ADD_UW(regs_.R(IRREG_LO), regs_.R(IRREG_LO), R_RA);
		} else {
			SLLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
			SRLI(regs_.R(IRREG_LO), regs_.R(IRREG_LO), XLEN - 32);
			ADD(regs_.R(IRREG_LO), regs_.R(IRREG_LO), R_RA);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
