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
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
		ADDW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		break;

	case IROp::Sub:
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
		SUBW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		break;

	case IROp::AddConst:
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			// Typical of stack pointer updates.
			if (gpr.IsMappedAsPointer(inst.src1) && inst.dest == inst.src1 && allowPtrMath) {
				gpr.MarkPtrDirty(gpr.RPtr(inst.dest));
				ADDI(gpr.RPtr(inst.dest), gpr.RPtr(inst.dest), inst.constant);
			} else {
				gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
				ADDIW(gpr.R(inst.dest), gpr.R(inst.src1), inst.constant);
			}
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			LI(SCRATCH1, (int32_t)inst.constant);
			ADDW(gpr.R(inst.dest), gpr.R(inst.src1), SCRATCH1);
		}
		break;

	case IROp::SubConst:
		gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
		LI(SCRATCH1, (int32_t)inst.constant);
		SUBW(gpr.R(inst.dest), gpr.R(inst.src1), SCRATCH1);
		break;

	case IROp::Neg:
		gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
		SUBW(gpr.R(inst.dest), R_ZERO, gpr.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void RiscVJitBackend::CompIR_Logic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::And:
		if (inst.src1 != inst.src2) {
			gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
			AND(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		} else if (inst.src1 != inst.dest) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			MV(gpr.R(inst.dest), gpr.R(inst.src1));
			gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Or:
		if (inst.src1 != inst.src2) {
			gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
			OR(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
			// If both were normalized before, the result is normalized.
			if (gpr.IsNormalized32(inst.src1) && gpr.IsNormalized32(inst.src2))
				gpr.MarkDirty(gpr.R(inst.dest), true);
		} else if (inst.src1 != inst.dest) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			MV(gpr.R(inst.dest), gpr.R(inst.src1));
			gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Xor:
		if (inst.src1 == inst.src2) {
			gpr.SetImm(inst.dest, 0);
		} else {
			gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
			XOR(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		}
		break;

	case IROp::AndConst:
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			ANDI(gpr.R(inst.dest), gpr.R(inst.src1), inst.constant);
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			LI(SCRATCH1, (int32_t)inst.constant);
			AND(gpr.R(inst.dest), gpr.R(inst.src1), SCRATCH1);
		}
		// If the sign bits aren't cleared, and it was normalized before - it still is.
		if ((inst.constant & 0x80000000) != 0 && gpr.IsNormalized32(inst.src1))
			gpr.MarkDirty(gpr.R(inst.dest), true);
		// Otherwise, if we cleared the sign bits, it's naturally normalized.
		else if ((inst.constant & 0x80000000) == 0)
			gpr.MarkDirty(gpr.R(inst.dest), true);
		break;

	case IROp::OrConst:
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			ORI(gpr.R(inst.dest), gpr.R(inst.src1), inst.constant);
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			LI(SCRATCH1, (int32_t)inst.constant);
			OR(gpr.R(inst.dest), gpr.R(inst.src1), SCRATCH1);
		}
		// Since our constant is normalized, oring its bits in won't hurt normalization.
		if (gpr.IsNormalized32(inst.src1))
			gpr.MarkDirty(gpr.R(inst.dest), true);
		break;

	case IROp::XorConst:
		if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			XORI(gpr.R(inst.dest), gpr.R(inst.src1), inst.constant);
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			LI(SCRATCH1, (int32_t)inst.constant);
			XOR(gpr.R(inst.dest), gpr.R(inst.src1), SCRATCH1);
		}
		break;

	case IROp::Not:
		gpr.MapDirtyIn(inst.dest, inst.src1);
		NOT(gpr.R(inst.dest), gpr.R(inst.src1));
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
			gpr.MapDirtyIn(inst.dest, inst.src1);
			MV(gpr.R(inst.dest), gpr.R(inst.src1));
			gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Ext8to32:
		if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SEXT_B(gpr.R(inst.dest), gpr.R(inst.src1));
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SLLI(gpr.R(inst.dest), gpr.R(inst.src1), 24);
			SRAIW(gpr.R(inst.dest), gpr.R(inst.dest), 24);
		}
		break;

	case IROp::Ext16to32:
		if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SEXT_H(gpr.R(inst.dest), gpr.R(inst.src1));
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SLLI(gpr.R(inst.dest), gpr.R(inst.src1), 16);
			SRAIW(gpr.R(inst.dest), gpr.R(inst.dest), 16);
		}
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
			gpr.MapDirtyIn(inst.dest, inst.src1);
			// Start by reversing bytes (note: this puts in upper 32 of XLEN.)
			REV8(gpr.R(inst.dest), gpr.R(inst.src1));

			// Swap nibbles.
			LI(SCRATCH1, (s32)0xF0F0F0F0);
			SRLI(SCRATCH2, gpr.R(inst.dest), XLEN - 32 - 4);
			AND(SCRATCH2, SCRATCH2, SCRATCH1);
			if (XLEN >= 64)
				SRLI(gpr.R(inst.dest), gpr.R(inst.dest), XLEN - 28);
			else
				SLLI(gpr.R(inst.dest), gpr.R(inst.dest), 4);
			SRLIW(SCRATCH1, SCRATCH1, 4);
			AND(gpr.R(inst.dest), gpr.R(inst.dest), SCRATCH1);
			OR(gpr.R(inst.dest), gpr.R(inst.dest), SCRATCH2);

			// Now the consecutive pairs.
			LI(SCRATCH1, (s32)0x33333333);
			SRLI(SCRATCH2, gpr.R(inst.dest), 2);
			AND(SCRATCH2, SCRATCH2, SCRATCH1);
			AND(gpr.R(inst.dest), gpr.R(inst.dest), SCRATCH1);
			SLLIW(gpr.R(inst.dest), gpr.R(inst.dest), 2);
			OR(gpr.R(inst.dest), gpr.R(inst.dest), SCRATCH2);

			// And finally the even and odd bits.
			LI(SCRATCH1, (s32)0x55555555);
			SRLI(SCRATCH2, gpr.R(inst.dest), 1);
			AND(SCRATCH2, SCRATCH2, SCRATCH1);
			AND(gpr.R(inst.dest), gpr.R(inst.dest), SCRATCH1);
			SLLIW(gpr.R(inst.dest), gpr.R(inst.dest), 1);
			OR(gpr.R(inst.dest), gpr.R(inst.dest), SCRATCH2);
		} else {
			CompIR_Generic(inst);
		}
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
				SRAI(gpr.R(inst.dest), gpr.R(inst.dest), XLEN - 32);
				gpr.MarkDirty(gpr.R(inst.dest), true);
			}
		} else {
			CompIR_Generic(inst);
		}
		break;

	case IROp::Clz:
		if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			// This even sets to 32 when zero, perfect.
			CLZW(gpr.R(inst.dest), gpr.R(inst.src1));
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
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
		SLLW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		break;

	case IROp::Shr:
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
		SRLW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		break;

	case IROp::Sar:
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
		SRAW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		break;

	case IROp::Ror:
		if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
			RORW(gpr.R(inst.dest), gpr.R(inst.src1), gpr.R(inst.src2));
		} else {
			CompIR_Generic(inst);
		}
		break;

	case IROp::ShlImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			gpr.SetImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				MV(gpr.R(inst.dest), gpr.R(inst.src1));
				gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
			}
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SLLIW(gpr.R(inst.dest), gpr.R(inst.src1), inst.src2);
		}
		break;

	case IROp::ShrImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			gpr.SetImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				MV(gpr.R(inst.dest), gpr.R(inst.src1));
				gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
			}
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SRLIW(gpr.R(inst.dest), gpr.R(inst.src1), inst.src2);
		}
		break;

	case IROp::SarImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SRAIW(gpr.R(inst.dest), gpr.R(inst.src1), 31);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				MV(gpr.R(inst.dest), gpr.R(inst.src1));
				gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
			}
		} else {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SRAIW(gpr.R(inst.dest), gpr.R(inst.src1), inst.src2);
		}
		break;

	case IROp::RorImm:
		if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				gpr.MapDirtyIn(inst.dest, inst.src1);
				MV(gpr.R(inst.dest), gpr.R(inst.src1));
				gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
			}
		} else if (cpu_info.RiscV_Zbb) {
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			RORIW(gpr.R(inst.dest), gpr.R(inst.src1), inst.src2 & 31);
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
		gpr.SpillLock(inst.dest, inst.src1, inst.src2);
		gpr.MapReg(inst.src1);
		gpr.MapReg(inst.src2);
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		gpr.MapReg(inst.dest, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
		gpr.ReleaseSpillLock(inst.dest, inst.src1, inst.src2);

		SLT(gpr.R(inst.dest), lhs, rhs);
		break;

	case IROp::SltConst:
		if (inst.constant == 0) {
			// Basically, getting the sign bit.  Let's shift instead.
			gpr.MapDirtyIn(inst.dest, inst.src1, MapType::AVOID_LOAD_MARK_NORM32);
			SRLIW(gpr.R(inst.dest), gpr.R(inst.src1), 31);
		} else {
			gpr.SpillLock(inst.dest, inst.src1);
			gpr.MapReg(inst.src1);
			NormalizeSrc1(inst, &lhs, SCRATCH1, false);
			gpr.MapReg(inst.dest, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			gpr.ReleaseSpillLock(inst.dest, inst.src1);

			if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
				SLTI(gpr.R(inst.dest), lhs, (int32_t)inst.constant);
			} else {
				LI(SCRATCH2, (int32_t)inst.constant);
				SLT(gpr.R(inst.dest), lhs, SCRATCH2);
			}
			gpr.MarkDirty(gpr.R(inst.dest), true);
		}
		break;

	case IROp::SltU:
		gpr.SpillLock(inst.dest, inst.src1, inst.src2);
		gpr.MapReg(inst.src1);
		gpr.MapReg(inst.src2);
		// It's still fine to sign extend, the biggest just get even bigger.
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		gpr.MapReg(inst.dest, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
		gpr.ReleaseSpillLock(inst.dest, inst.src1, inst.src2);

		SLTU(gpr.R(inst.dest), lhs, rhs);
		break;

	case IROp::SltUConst:
		if (inst.constant == 0) {
			gpr.SetImm(inst.dest, 0);
		} else {
			gpr.SpillLock(inst.dest, inst.src1);
			gpr.MapReg(inst.src1);
			NormalizeSrc1(inst, &lhs, SCRATCH1, false);
			gpr.MapReg(inst.dest, MIPSMap::NOINIT | MIPSMap::MARK_NORM32);
			gpr.ReleaseSpillLock(inst.dest, inst.src1);

			// We sign extend because we're comparing against something normalized.
			// It's also the most efficient to set.
			if ((int32_t)inst.constant >= -2048 && (int32_t)inst.constant <= 2047) {
				SLTIU(gpr.R(inst.dest), lhs, (int32_t)inst.constant);
			} else {
				LI(SCRATCH2, (int32_t)inst.constant);
				SLTU(gpr.R(inst.dest), lhs, SCRATCH2);
			}
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

		// We could have a "zero" that with wrong upper due to XOR, so we have to normalize.
		gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2, MapType::ALWAYS_LOAD);
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

		MV(gpr.R(inst.dest), gpr.R(inst.src2));
		SetJumpTarget(fixup);
		break;

	case IROp::Max:
		if (inst.src1 != inst.src2) {
			if (cpu_info.RiscV_Zbb) {
				gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
				NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
				MAX(gpr.R(inst.dest), lhs, rhs);
				// Because we had to normalize the inputs, the output is normalized.
				gpr.MarkDirty(gpr.R(inst.dest), true);
			} else {
				CompIR_Generic(inst);
			}
		} else if (inst.dest != inst.src1) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			MV(gpr.R(inst.dest), gpr.R(inst.src1));
			gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
		}
		break;

	case IROp::Min:
		if (inst.src1 != inst.src2) {
			if (cpu_info.RiscV_Zbb) {
				gpr.MapDirtyInIn(inst.dest, inst.src1, inst.src2);
				NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
				MIN(gpr.R(inst.dest), lhs, rhs);
				// Because we had to normalize the inputs, the output is normalized.
				gpr.MarkDirty(gpr.R(inst.dest), true);
			} else {
				CompIR_Generic(inst);
			}
		} else if (inst.dest != inst.src1) {
			gpr.MapDirtyIn(inst.dest, inst.src1);
			MV(gpr.R(inst.dest), gpr.R(inst.src1));
			gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(inst.src1));
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
		gpr.MapDirtyIn(IRREG_LO, inst.src1);
		MV(gpr.R(IRREG_LO), gpr.R(inst.src1));
		gpr.MarkDirty(gpr.R(IRREG_LO), gpr.IsNormalized32(inst.src1));
		break;

	case IROp::MtHi:
		gpr.MapDirtyIn(IRREG_HI, inst.src1);
		MV(gpr.R(IRREG_HI), gpr.R(inst.src1));
		gpr.MarkDirty(gpr.R(IRREG_HI), gpr.IsNormalized32(inst.src1));
		break;

	case IROp::MfLo:
		gpr.MapDirtyIn(inst.dest, IRREG_LO);
		MV(gpr.R(inst.dest), gpr.R(IRREG_LO));
		gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(IRREG_LO));
		break;

	case IROp::MfHi:
		gpr.MapDirtyIn(inst.dest, IRREG_HI);
		MV(gpr.R(inst.dest), gpr.R(IRREG_HI));
		gpr.MarkDirty(gpr.R(inst.dest), gpr.IsNormalized32(IRREG_HI));
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
			ZEXT_W(SCRATCH1, gpr.R(inst.src1));
			ZEXT_W(SCRATCH2, gpr.R(inst.src2));
		} else {
			SLLI(SCRATCH1, gpr.R(inst.src1), XLEN - 32);
			SRLI(SCRATCH1, SCRATCH1, XLEN - 32);
			SLLI(SCRATCH2, gpr.R(inst.src2), XLEN - 32);
			SRLI(SCRATCH2, SCRATCH2, XLEN - 32);
		}
		*lhs = SCRATCH1;
		*rhs = SCRATCH2;
	};
	auto combinePrevMulResult = [&] {
		// TODO: Using a single reg for HI/LO would make this less ugly.
		if (cpu_info.RiscV_Zba) {
			ZEXT_W(gpr.R(IRREG_LO), gpr.R(IRREG_LO));
		} else {
			SLLI(gpr.R(IRREG_LO), gpr.R(IRREG_LO), XLEN - 32);
			SRLI(gpr.R(IRREG_LO), gpr.R(IRREG_LO), XLEN - 32);
		}
		SLLI(gpr.R(IRREG_HI), gpr.R(IRREG_HI), 32);
		OR(gpr.R(IRREG_LO), gpr.R(IRREG_LO), gpr.R(IRREG_HI));
	};
	auto splitMulResult = [&] {
		SRAI(gpr.R(IRREG_HI), gpr.R(IRREG_LO), 32);
		gpr.MarkDirty(gpr.R(IRREG_HI), true);
	};

	RiscVReg lhs = INVALID_REG;
	RiscVReg rhs = INVALID_REG;
	switch (inst.op) {
	case IROp::Mult:
		// TODO: Maybe IR could simplify when HI is not needed or clobbered?
		// TODO: HI/LO merge optimization?  Have to be careful of passes that split them...
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2);
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL(gpr.R(IRREG_LO), lhs, rhs);
		splitMulResult();
		break;

	case IROp::MultU:
		// This is an "anti-norm32" case.  Let's just zero always.
		// TODO: If we could know that LO was only needed, we could use MULW and be done.
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2);
		makeArgsUnsigned(&lhs, &rhs);
		MUL(gpr.R(IRREG_LO), lhs, rhs);
		splitMulResult();
		break;

	case IROp::Madd:
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2, MapType::ALWAYS_LOAD);
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL(SCRATCH1, lhs, rhs);

		combinePrevMulResult();
		ADD(gpr.R(IRREG_LO), gpr.R(IRREG_LO), SCRATCH1);
		splitMulResult();
		break;

	case IROp::MaddU:
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2, MapType::ALWAYS_LOAD);
		makeArgsUnsigned(&lhs, &rhs);
		MUL(SCRATCH1, lhs, rhs);

		combinePrevMulResult();
		ADD(gpr.R(IRREG_LO), gpr.R(IRREG_LO), SCRATCH1);
		splitMulResult();
		break;

	case IROp::Msub:
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2, MapType::ALWAYS_LOAD);
		NormalizeSrc12(inst, &lhs, &rhs, SCRATCH1, SCRATCH2, true);
		MUL(SCRATCH1, lhs, rhs);

		combinePrevMulResult();
		SUB(gpr.R(IRREG_LO), gpr.R(IRREG_LO), SCRATCH1);
		splitMulResult();
		break;

	case IROp::MsubU:
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2, MapType::ALWAYS_LOAD);
		makeArgsUnsigned(&lhs, &rhs);
		MUL(SCRATCH1, lhs, rhs);

		combinePrevMulResult();
		SUB(gpr.R(IRREG_LO), gpr.R(IRREG_LO), SCRATCH1);
		splitMulResult();
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
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
		// We have to do this because of the divide by zero and overflow checks below.
		NormalizeSrc12(inst, &numReg, &denomReg, SCRATCH1, SCRATCH2, true);
		DIVW(gpr.R(IRREG_LO), numReg, denomReg);
		REMW(gpr.R(IRREG_HI), numReg, denomReg);

		// Now some tweaks for divide by zero and overflow.
		{
			// Start with divide by zero, remainder is fine.
			FixupBranch skipNonZero = BNE(denomReg, R_ZERO);
			FixupBranch keepNegOne = BGE(numReg, R_ZERO);
			LI(gpr.R(IRREG_LO), 1);
			SetJumpTarget(keepNegOne);
			SetJumpTarget(skipNonZero);

			// For overflow, RISC-V sets LO right, but remainder to zero.
			// Cheating a bit by using R_RA as a temp...
			LI(R_RA, (int32_t)0x80000000);
			FixupBranch notMostNegative = BNE(numReg, R_RA);
			LI(R_RA, -1);
			FixupBranch notNegativeOne = BNE(denomReg, R_RA);
			LI(gpr.R(IRREG_HI), -1);
			SetJumpTarget(notNegativeOne);
			SetJumpTarget(notMostNegative);
		}
		break;

	case IROp::DivU:
		gpr.MapDirtyDirtyInIn(IRREG_LO, IRREG_HI, inst.src1, inst.src2, MapType::AVOID_LOAD_MARK_NORM32);
		// We have to do this because of the divide by zero check below.
		NormalizeSrc12(inst, &numReg, &denomReg, SCRATCH1, SCRATCH2, true);
		DIVUW(gpr.R(IRREG_LO), numReg, denomReg);
		REMUW(gpr.R(IRREG_HI), numReg, denomReg);

		// On divide by zero, everything is correct already except the 0xFFFF case.
		{
			FixupBranch skipNonZero = BNE(denomReg, R_ZERO);
			// Luckily, we don't need SCRATCH2/denomReg anymore.
			LI(SCRATCH2, 0xFFFF);
			FixupBranch keepNegOne = BLTU(SCRATCH2, numReg);
			MV(gpr.R(IRREG_LO), SCRATCH2);
			SetJumpTarget(keepNegOne);
			SetJumpTarget(skipNonZero);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp
