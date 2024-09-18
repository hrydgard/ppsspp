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
// In other words, PPSSPP_ARCH(ARM64) || DISASM_ALL.
#if PPSSPP_ARCH(ARM64) || (PPSSPP_PLATFORM(WINDOWS) && !defined(__LIBRETRO__))

#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/ARM64/Arm64IRJit.h"
#include "Core/MIPS/ARM64/Arm64IRRegCache.h"

// This file contains compilation for integer / arithmetic / logic related instructions.
//
// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.  No flags because that's in IR already.

// #define CONDITIONAL_DISABLE { CompIR_Generic(inst); return; }
#define CONDITIONAL_DISABLE {}
#define DISABLE { CompIR_Generic(inst); return; }
#define INVALIDOP { _assert_msg_(false, "Invalid IR inst %d", (int)inst.op); CompIR_Generic(inst); return; }

namespace MIPSComp {

using namespace Arm64Gen;
using namespace Arm64IRJitConstants;

void Arm64JitBackend::CompIR_Arith(IRInst inst) {
	CONDITIONAL_DISABLE;

	bool allowPtrMath = inst.constant <= 0x7FFFFFFF;
#ifdef MASKED_PSP_MEMORY
	// Since we modify it, we can't safely.
	allowPtrMath = false;
#endif

	switch (inst.op) {
	case IROp::Add:
		regs_.Map(inst);
		ADD(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::Sub:
		regs_.Map(inst);
		SUB(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::AddConst:
		if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
			regs_.MarkGPRAsPointerDirty(inst.dest);
			ADDI2R(regs_.RPtr(inst.dest), regs_.RPtr(inst.src1), (int)inst.constant, SCRATCH1_64);
		} else {
			regs_.Map(inst);
			ADDI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
		}
		break;

	case IROp::SubConst:
		if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
			regs_.MarkGPRAsPointerDirty(inst.dest);
			SUBI2R(regs_.RPtr(inst.dest), regs_.RPtr(inst.src1), (int)inst.constant, SCRATCH1_64);
		} else {
			regs_.Map(inst);
			SUBI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
		}
		break;

	case IROp::Neg:
		regs_.Map(inst);
		NEG(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Assign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Mov:
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::Ext8to32:
		regs_.Map(inst);
		SXTB(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	case IROp::Ext16to32:
		regs_.Map(inst);
		SXTH(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Bits(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::BSwap16:
		regs_.Map(inst);
		REV16(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	case IROp::BSwap32:
		regs_.Map(inst);
		REV32(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	case IROp::Clz:
		regs_.Map(inst);
		CLZ(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	case IROp::ReverseBits:
		regs_.Map(inst);
		RBIT(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Compare(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Slt:
		regs_.Map(inst);
		CMP(regs_.R(inst.src1), regs_.R(inst.src2));
		CSET(regs_.R(inst.dest), CC_LT);
		break;

	case IROp::SltConst:
		if (inst.constant == 0) {
			// Basically, getting the sign bit.
			regs_.Map(inst);
			UBFX(regs_.R(inst.dest), regs_.R(inst.src1), 31, 1);
		} else {
			regs_.Map(inst);
			CMPI2R(regs_.R(inst.src1), (int32_t)inst.constant, SCRATCH1);
			CSET(regs_.R(inst.dest), CC_LT);
		}
		break;

	case IROp::SltU:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			// This is kinda common, same as != 0.  Avoid flushing src1.
			regs_.SpillLockGPR(inst.src2, inst.dest);
			regs_.MapGPR(inst.src2);
			regs_.MapGPR(inst.dest, MIPSMap::NOINIT);
			CMP(regs_.R(inst.src2), 0);
			CSET(regs_.R(inst.dest), CC_NEQ);
		} else {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), regs_.R(inst.src2));
			CSET(regs_.R(inst.dest), CC_LO);
		}
		break;

	case IROp::SltUConst:
		if (inst.constant == 0) {
			regs_.SetGPRImm(inst.dest, 0);
		} else {
			regs_.Map(inst);
			CMPI2R(regs_.R(inst.src1), (int32_t)inst.constant, SCRATCH1);
			CSET(regs_.R(inst.dest), CC_LO);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_CondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MovZ:
		if (inst.dest != inst.src2) {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), 0);
			CSEL(regs_.R(inst.dest), regs_.R(inst.src2), regs_.R(inst.dest), CC_EQ);
		}
		break;

	case IROp::MovNZ:
		if (inst.dest != inst.src2) {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), 0);
			CSEL(regs_.R(inst.dest), regs_.R(inst.src2), regs_.R(inst.dest), CC_NEQ);
		}
		break;

	case IROp::Max:
		if (inst.src1 != inst.src2) {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), regs_.R(inst.src2));
			CSEL(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2), CC_GE);
		} else if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::Min:
		if (inst.src1 != inst.src2) {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), regs_.R(inst.src2));
			CSEL(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2), CC_LE);
		} else if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Div(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Div:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		// INT_MIN divided by -1 = INT_MIN, anything divided by 0 = 0.
		SDIV(regs_.R(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2));
		MSUB(SCRATCH1, regs_.R(inst.src2), regs_.R(IRREG_LO), regs_.R(inst.src1));

		// Now some tweaks for divide by zero and overflow.
		{
			// Start with divide by zero, remainder is fine.
			FixupBranch skipNonZero = CBNZ(regs_.R(inst.src2));
			MOVI2R(regs_.R(IRREG_LO), 1);
			CMP(regs_.R(inst.src1), 0);
			// mips->lo = numerator < 0 ? 1 : -1
			CSNEG(regs_.R(IRREG_LO), regs_.R(IRREG_LO), regs_.R(IRREG_LO), CC_LT);
			SetJumpTarget(skipNonZero);

			// For overflow, we end up with remainder as zero, need to fix.
			MOVI2R(SCRATCH2, 0x80000000);
			CMP(regs_.R(inst.src1), SCRATCH2);
			FixupBranch notMostNegative = B(CC_NEQ);
			CMPI2R(regs_.R(inst.src2), -1);
			// If it's not equal, keep SCRATCH1.  Otherwise (equal), invert 0 = -1.
			CSINV(SCRATCH1, SCRATCH1, WZR, CC_NEQ);
			SetJumpTarget(notMostNegative);
		}

		BFI(regs_.R64(IRREG_LO), SCRATCH1_64, 32, 32);
		break;

	case IROp::DivU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		// Anything divided by 0 = 0.
		UDIV(regs_.R(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2));
		MSUB(SCRATCH1, regs_.R(inst.src2), regs_.R(IRREG_LO), regs_.R(inst.src1));

		// On divide by zero, we have to update LO - remainder is correct.
		{
			FixupBranch skipNonZero = CBNZ(regs_.R(inst.src2));
			MOVI2R(regs_.R(IRREG_LO), 0xFFFF);
			CMP(regs_.R(inst.src1), regs_.R(IRREG_LO));
			// If it's <= 0xFFFF, keep 0xFFFF.  Otherwise, invert 0 = -1.
			CSINV(regs_.R(IRREG_LO), regs_.R(IRREG_LO), WZR, CC_LE);
			SetJumpTarget(skipNonZero);
		}

		BFI(regs_.R64(IRREG_LO), SCRATCH1_64, 32, 32);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_HiLo(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MtLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		BFI(regs_.R64(IRREG_LO), regs_.R64(inst.src1), 0, 32);
		break;

	case IROp::MtHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		BFI(regs_.R64(IRREG_LO), regs_.R64(inst.src1), 32, 32);
		break;

	case IROp::MfLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		MOV(regs_.R(inst.dest), regs_.R(IRREG_LO));
		break;

	case IROp::MfHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		UBFX(regs_.R64(inst.dest), regs_.R64(IRREG_LO), 32, 32);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Logic(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::And:
		if (inst.src1 != inst.src2) {
			regs_.Map(inst);
			AND(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		} else if (inst.src1 != inst.dest) {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::Or:
		if (inst.src1 != inst.src2) {
			regs_.Map(inst);
			ORR(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		} else if (inst.src1 != inst.dest) {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1));
		}
		break;

	case IROp::Xor:
		if (inst.src1 == inst.src2) {
			regs_.SetGPRImm(inst.dest, 0);
		} else {
			regs_.Map(inst);
			EOR(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		}
		break;

	case IROp::AndConst:
		regs_.Map(inst);
		ANDI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
		break;

	case IROp::OrConst:
		regs_.Map(inst);
		ORRI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
		break;

	case IROp::XorConst:
		regs_.Map(inst);
		EORI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
		break;

	case IROp::Not:
		regs_.Map(inst);
		MVN(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Mult(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Mult:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		SMULL(regs_.R64(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::MultU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		UMULL(regs_.R64(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::Madd:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		// Accumulator is at the end, "standard" syntax.
		SMADDL(regs_.R64(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2), regs_.R64(IRREG_LO));
		break;

	case IROp::MaddU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		// Accumulator is at the end, "standard" syntax.
		UMADDL(regs_.R64(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2), regs_.R64(IRREG_LO));
		break;

	case IROp::Msub:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		// Accumulator is at the end, "standard" syntax.
		SMSUBL(regs_.R64(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2), regs_.R64(IRREG_LO));
		break;

	case IROp::MsubU:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		// Accumulator is at the end, "standard" syntax.
		UMSUBL(regs_.R64(IRREG_LO), regs_.R(inst.src1), regs_.R(inst.src2), regs_.R64(IRREG_LO));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void Arm64JitBackend::CompIR_Shift(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::Shl:
		regs_.Map(inst);
		LSLV(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::Shr:
		regs_.Map(inst);
		LSRV(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::Sar:
		regs_.Map(inst);
		ASRV(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::Ror:
		regs_.Map(inst);
		RORV(regs_.R(inst.dest), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::ShlImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1), ArithOption(regs_.R(inst.src1), ST_LSL, inst.src2));
		}
		break;

	case IROp::ShrImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1), ArithOption(regs_.R(inst.src1), ST_LSR, inst.src2));
		}
		break;

	case IROp::SarImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32) {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1), ArithOption(regs_.R(inst.src1), ST_ASR, 31));
		} else if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1), ArithOption(regs_.R(inst.src1), ST_ASR, inst.src2));
		}
		break;

	case IROp::RorImm:
		if (inst.src2 == 0) {
			if (inst.dest != inst.src1) {
				regs_.Map(inst);
				MOV(regs_.R(inst.dest), regs_.R(inst.src1));
			}
		} else {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1), ArithOption(regs_.R(inst.src1), ST_ROR, inst.src2 & 31));
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
