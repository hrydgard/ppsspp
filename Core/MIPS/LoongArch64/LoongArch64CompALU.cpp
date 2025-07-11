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
#include "Core/MIPS/LoongArch64/LoongArch64Jit.h"
#include "Core/MIPS/LoongArch64/LoongArch64RegCache.h"

// This file contains compilation for integer / arithmetic / logic related instructions.
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

void LoongArch64JitBackend::CompIR_Arith(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool allowPtrMath = true;
#ifdef MASKED_PSP_MEMORY
	// Since we modify it, we can't safely.
	allowPtrMath = false;
#endif

	// LoongArch64 only adds signed immediates, so rewrite a small enough subtract to an add.
	// We use -2047 and 2048 here because the range swaps.
	if (inst.op == IROp::SubConst && (int32_t)inst.constant >= -2047 && (int32_t)inst.constant <= 2048) {
		inst.op = IROp::AddConst;
		inst.constant = (uint32_t)-(int32_t)inst.constant;
	}

	switch (inst.op) {
	case IROp::Add:
		regs_.Map(inst);
		ADD_W(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Sub:
		regs_.Map(inst);
		SUB_W(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::AddConst:
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			// Typical of stack pointer updates.
			if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
				regs_.MarkGPRAsPointerDirty(inst.dest);
				ADDI_D(regs_.RPtr(inst.dest), regs_.RPtr(inst.dest), inst.constant);
			} else {
				regs_.Map(inst);
				ADDI_W(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant);
				regs_.MarkGPRDirty(inst.dest, true);
			}
		} else {
			regs_.Map(inst);
			LI(SCRATCH1, (int32_t)inst.constant);
			ADD_W(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::SubConst:
		regs_.Map(inst);
		LI(SCRATCH1, (int32_t)inst.constant);
		SUB_W(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Neg:
		regs_.Map(inst);
		SUB_W(regs_.R(inst.dest), R_ZERO, regs_.R(inst.src1));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Logic(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool resultNormalized = false;
	switch (inst.op) {
	case IROp::And:
		if (inst.src1 != inst.src2) {
			regs_.Map(inst);
			AND(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		} else if (inst.src1 != inst.dest) {
			regs_.Map(inst);
			MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
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
			MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
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
        // LoongArch64's ANDI use unsigned 12-bit immediate
		if ((int32_t)inst.constant >= 0 && (int32_t)inst.constant < 4096) {
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
		if ((int32_t)inst.constant >= 0 && (int32_t)inst.constant < 4096) {
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
		if ((int32_t)inst.constant >= 0 && (int32_t)inst.constant < 4096) {
			XORI(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant);
		} else {
			LI(SCRATCH1, (int32_t)inst.constant);
			XOR(regs_.R(inst.dest), regs_.R(inst.src1), SCRATCH1);
		}
		break;

	case IROp::Not:
		regs_.Map(inst);
		ORN(regs_.R(inst.dest), R_ZERO, regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Assign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Mov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Ext8to32:
		regs_.Map(inst);
		EXT_W_B(regs_.R(inst.dest), regs_.R(inst.src1));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Ext16to32:
		regs_.Map(inst);
		EXT_W_H(regs_.R(inst.dest), regs_.R(inst.src1));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Bits(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::ReverseBits:
        regs_.Map(inst);
		BITREV_W(regs_.R(inst.dest), regs_.R(inst.src1));
        regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::BSwap16:
        regs_.Map(inst);
		REVB_2H(regs_.R(inst.dest), regs_.R(inst.src1));
        regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::BSwap32:
        regs_.Map(inst);
		REVB_2W(regs_.R(inst.dest), regs_.R(inst.src1));
        regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Clz:
        regs_.Map(inst);
		CLZ_W(regs_.R(inst.dest), regs_.R(inst.src1));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Shift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Shl:
		regs_.Map(inst);
		SLL_W(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Shr:
		regs_.Map(inst);
		SRL_W(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Sar:
		regs_.Map(inst);
		SRA_W(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::Ror:
        regs_.Map(inst);
        ROTR_W(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
        regs_.MarkGPRDirty(inst.dest, true);
		break;

	case IROp::ShlImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else {
			regs_.Map(inst);
			SLLI_W(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2);
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
				MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else {
			regs_.Map(inst);
			SRLI_W(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::SarImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.Map(inst);
			SRAI_W(regs_.R(inst.dest), regs_.R(inst.src1), 31);
			regs_.MarkGPRDirty(inst.dest, true);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else {
			regs_.Map(inst);
			SRAI_W(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	case IROp::RorImm:
		if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
				regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
			}
		} else {
			regs_.Map(inst);
			ROTRI_W(regs_.R(inst.dest), regs_.R(inst.src1), inst.src2 & 31);
			regs_.MarkGPRDirty(inst.dest, true);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Compare(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoongArch64Reg lhs = INVALID_REG;
	LoongArch64Reg rhs = INVALID_REG;
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
			SRLI_W(regs_.R(inst.dest), regs_.R(inst.src1), 31);
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
				SLTUI(regs_.R(inst.dest), lhs, (int32_t)inst.constant);
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

void LoongArch64JitBackend::CompIR_CondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoongArch64Reg lhs = INVALID_REG;
	LoongArch64Reg rhs = INVALID_REG;
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
			fixup = BNEZ(lhs);
			break;
		case IROp::MovNZ:
			fixup = BEQZ(lhs);
			break;
		default:
			INVALIDOP;
			break;
		}

		MOVE(regs_.R(inst.dest), regs_.R(inst.src2));
		SetJumpTarget(fixup);
		break;

	case IROp::Max:
		if (inst.src1 != inst.src2) {
			CompIR_Generic(inst);
		} else if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Min:
		if (inst.src1 != inst.src2) {
			CompIR_Generic(inst);
		} else if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOVE(regs_.R(inst.dest), regs_.R(inst.src1));
			regs_.MarkGPRDirty(inst.dest, regs_.IsNormalized32(inst.src1));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_HiLo(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MtLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
        // 32-63 bits of IRREG_LO + 0-31 bits of inst.src1
        BSTRINS_D(regs_.R(IRREG_LO), regs_.R(inst.src1), 31, 0);
		break;

	case IROp::MtHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
        BSTRINS_D(regs_.R(IRREG_LO), regs_.R(inst.src1), 63, 32);
		break;

	case IROp::MfLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		// It won't be normalized, but that's fine...
		MOVE(regs_.R(inst.dest), regs_.R(IRREG_LO));
		break;

	case IROp::MfHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		SRAI_D(regs_.R(inst.dest), regs_.R(IRREG_LO), 32);
		regs_.MarkGPRDirty(inst.dest, true);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Mult(IRInst inst) {
	CONDITIONAL_DISABLE;

	auto putArgsIntoScratches = [&](LoongArch64Reg *lhs, LoongArch64Reg *rhs) {
		MOVE(SCRATCH1, regs_.R(inst.src1));
		MOVE(SCRATCH2, regs_.R(inst.src2));
		*lhs = SCRATCH1;
		*rhs = SCRATCH2;
	};

	LoongArch64Reg lhs = INVALID_REG;
	LoongArch64Reg rhs = INVALID_REG;
	switch (inst.op) {
	case IROp::Mult:
		// TODO: Maybe IR could simplify when HI is not needed or clobbered?
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL_D(regs_.R(IRREG_LO), lhs, rhs);
		break;

	case IROp::MultU:
		// This is an "anti-norm32" case.  Let's just zero always.
		// TODO: If we could know that LO was only needed, we could use MULW.
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		putArgsIntoScratches(&lhs, &rhs);
		MULW_D_WU(regs_.R(IRREG_LO), lhs, rhs);
		break;

	case IROp::Madd:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL_D(SCRATCH1, lhs, rhs);
		ADD_D(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	case IROp::MaddU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		putArgsIntoScratches(&lhs, &rhs);
		MULW_D_WU(SCRATCH1, lhs, rhs);
		ADD_D(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	case IROp::Msub:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL_D(SCRATCH1, lhs, rhs);
		SUB_D(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	case IROp::MsubU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		putArgsIntoScratches(&lhs, &rhs);
		MULW_D_WU(SCRATCH1, lhs, rhs);
		SUB_D(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void LoongArch64JitBackend::CompIR_Div(IRInst inst) {
	CONDITIONAL_DISABLE;

	LoongArch64Reg numReg, denomReg;
	switch (inst.op) {
	case IROp::Div:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		// We have to do this because of the divide by zero and overflow checks below.
		NormalizeSrc12(inst, &numReg, &denomReg, SCRATCH1, SCRATCH2, true);
		DIV_W(regs_.R(IRREG_LO), numReg, denomReg);
		MOD_W(R_RA, numReg, denomReg);
		// Now to combine them.  We'll do more with them below...
		BSTRINS_D(regs_.R(IRREG_LO), R_RA, 63, 32);

		// Now some tweaks for divide by zero and overflow.
		{
			// Start with divide by zero, the quotient and remainder are arbitrary numbers.
			FixupBranch skipNonZero = BNEZ(denomReg);
            // Clear the arbitrary number
            XOR(regs_.R(IRREG_LO), regs_.R(IRREG_LO), regs_.R(IRREG_LO));
            // Replace remainder to numReg
            BSTRINS_D(regs_.R(IRREG_LO), numReg, 63, 32);
			FixupBranch keepNegOne = BGE(numReg, R_ZERO);
			// Replace quotient with 1.
			ADDI_D(regs_.R(IRREG_LO), regs_.R(IRREG_LO), 1);
			SetJumpTarget(keepNegOne);
            // Replace quotient with -1.
            ADDI_D(regs_.R(IRREG_LO), regs_.R(IRREG_LO), -1);
			SetJumpTarget(skipNonZero);

			// For overflow, LoongArch sets LO right, but remainder to zero.
			// Cheating a bit by using R_RA as a temp...
			LI(R_RA, (int32_t)0x80000000);
			FixupBranch notMostNegative = BNE(numReg, R_RA);
			LI(R_RA, -1);
			FixupBranch notNegativeOne = BNE(denomReg, R_RA);
			// Take our R_RA and put it in the high bits.
			SLLI_D(R_RA, R_RA, 32);
			OR(regs_.R(IRREG_LO), regs_.R(IRREG_LO), R_RA);
			SetJumpTarget(notNegativeOne);
			SetJumpTarget(notMostNegative);
		}
		break;

	case IROp::DivU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		// We have to do this because of the divide by zero check below.
		NormalizeSrc12(inst, &numReg, &denomReg, SCRATCH1, SCRATCH2, true);
		DIV_WU(regs_.R(IRREG_LO), numReg, denomReg);
		MOD_WU(R_RA, numReg, denomReg);

		// On divide by zero, special dealing with the 0xFFFF case.
		{
			FixupBranch skipNonZero = BNEZ(denomReg);
            // Move -1 to quotient.
            ADDI_D(regs_.R(IRREG_LO), R_ZERO, -1);
            // Move numReg to remainder (stores in RA currently).
            MOVE(R_RA, numReg);
			// Luckily, we don't need SCRATCH2/denomReg anymore.
			LI(SCRATCH2, 0xFFFF);
			FixupBranch keepNegOne = BLTU(SCRATCH2, numReg);
			MOVE(regs_.R(IRREG_LO), SCRATCH2);
			SetJumpTarget(keepNegOne);
			SetJumpTarget(skipNonZero);
		}

		// Now combine the remainder in.
		BSTRINS_D(regs_.R(IRREG_LO), R_RA, 63, 32);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
