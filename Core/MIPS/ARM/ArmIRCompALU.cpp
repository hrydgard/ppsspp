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

#include "Common/CPUDetect.h"
#include "Core/MIPS/ARM/ArmIRJit.h"
#include "Core/MIPS/ARM/ArmIRRegCache.h"

// This file contains compilation for integer arithmetic, logic, shifts, compares, and multiplies.
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

void ArmJitBackend::CompIR_Arith(IRInst inst) {
	CONDITIONAL_DISABLE;

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
		regs_.Map(inst);
		ADDI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
		break;

	case IROp::SubConst:
		regs_.Map(inst);
		SUBI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
		break;

	case IROp::Neg:
		regs_.Map(inst);
		RSB(regs_.R(inst.dest), regs_.R(inst.src1), Operand2(0));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_Assign(IRInst inst) {
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

void ArmJitBackend::CompIR_Bits(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::BSwap16:
		regs_.Map(inst);
		REV16(regs_.R(inst.dest), regs_.R(inst.src1));
		break;

	case IROp::BSwap32:
		regs_.Map(inst);
		REV(regs_.R(inst.dest), regs_.R(inst.src1));
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

void ArmJitBackend::CompIR_Compare(IRInst inst) {
	CONDITIONAL_DISABLE;

	// Sets dest to 1 if cc holds, else 0. The MOVs don't touch the flags, so dest may be a source.
	auto setCC = [&](ARMReg dest, CCFlags cc) {
		MOV(dest, Operand2(0));
		SetCC(cc);
		MOV(dest, Operand2(1));
		SetCC(CC_AL);
	};

	switch (inst.op) {
	case IROp::Slt:
		regs_.Map(inst);
		CMP(regs_.R(inst.src1), regs_.R(inst.src2));
		setCC(regs_.R(inst.dest), CC_LT);
		break;

	case IROp::SltConst:
		if (inst.constant == 0) {
			// Basically, getting the sign bit.
			regs_.Map(inst);
			LSR(regs_.R(inst.dest), regs_.R(inst.src1), Operand2(31));
		} else {
			regs_.Map(inst);
			CMPI2R(regs_.R(inst.src1), inst.constant, SCRATCH1);
			setCC(regs_.R(inst.dest), CC_LT);
		}
		break;

	case IROp::SltU:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			// This is kinda common, same as != 0.  Avoid flushing src1.
			regs_.SpillLockGPR(inst.src2, inst.dest);
			regs_.MapGPR(inst.src2);
			regs_.MapGPR(inst.dest, MIPSMap::NOINIT);
			CMP(regs_.R(inst.src2), Operand2(0));
			setCC(regs_.R(inst.dest), CC_NEQ);
		} else {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), regs_.R(inst.src2));
			setCC(regs_.R(inst.dest), CC_LO);
		}
		break;

	case IROp::SltUConst:
		if (inst.constant == 0) {
			regs_.SetGPRImm(inst.dest, 0);
		} else {
			regs_.Map(inst);
			CMPI2R(regs_.R(inst.src1), inst.constant, SCRATCH1);
			setCC(regs_.R(inst.dest), CC_LO);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_CondAssign(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MovZ:
	case IROp::MovNZ:
		if (inst.dest != inst.src2) {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), Operand2(0));
			SetCC(inst.op == IROp::MovZ ? CC_EQ : CC_NEQ);
			MOV(regs_.R(inst.dest), regs_.R(inst.src2));
			SetCC(CC_AL);
		}
		break;

	case IROp::Max:
	case IROp::Min:
		if (inst.src1 != inst.src2) {
			regs_.Map(inst);
			CMP(regs_.R(inst.src1), regs_.R(inst.src2));
			// Exactly one of these happens, so dest may be either source.
			CCFlags pickSrc1 = inst.op == IROp::Max ? CC_GE : CC_LE;
			SetCC(pickSrc1);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1));
			SetCC(InvertCond(pickSrc1));
			MOV(regs_.R(inst.dest), regs_.R(inst.src2));
			SetCC(CC_AL);
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

void ArmJitBackend::CompIR_Div(IRInst inst) {
	CONDITIONAL_DISABLE;
	if (!cpu_info.bIDIVa) {
		CompIR_Generic(inst);
		return;
	}

	switch (inst.op) {
	case IROp::Div:
	{
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::NOINIT }, { 'G', IRREG_HI, 1, MIPSMap::NOINIT } });
		ARMReg lo = regs_.R(IRREG_LO);
		ARMReg hi = regs_.R(IRREG_HI);
		// INT_MIN divided by -1 = INT_MIN with remainder 0, anything divided by 0 = 0. The first is
		// what the hardware does too (cpu/cpu_alu/cpu_div), so only the second needs tweaking.
		SDIV(lo, regs_.R(inst.src1), regs_.R(inst.src2));
		MUL(SCRATCH1, lo, regs_.R(inst.src2));
		SUB(hi, regs_.R(inst.src1), SCRATCH1);

		// Divide by zero, remainder is fine. mips->lo = numerator < 0 ? 1 : -1
		CMP(regs_.R(inst.src2), Operand2(0));
		FixupBranch skipNonZero = B_CC(CC_NEQ);
		CMP(regs_.R(inst.src1), Operand2(0));
		MOV(lo, Operand2(1));
		SetCC(CC_GE);
		MVN(lo, Operand2(0));
		SetCC(CC_AL);
		SetJumpTarget(skipNonZero);
		break;
	}

	case IROp::DivU:
	{
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::NOINIT }, { 'G', IRREG_HI, 1, MIPSMap::NOINIT } });
		ARMReg lo = regs_.R(IRREG_LO);
		ARMReg hi = regs_.R(IRREG_HI);
		// Anything divided by 0 = 0.
		UDIV(lo, regs_.R(inst.src1), regs_.R(inst.src2));
		MUL(SCRATCH1, lo, regs_.R(inst.src2));
		SUB(hi, regs_.R(inst.src1), SCRATCH1);

		// On divide by zero, we have to update LO - remainder is correct.
		// If the numerator is <= 0xFFFF (unsigned), it's 0xFFFF, otherwise -1.
		CMP(regs_.R(inst.src2), Operand2(0));
		FixupBranch skipNonZero = B_CC(CC_NEQ);
		MOVI2R(lo, 0xFFFF);
		CMP(regs_.R(inst.src1), lo);
		SetCC(CC_HI);
		MVN(lo, Operand2(0));
		SetCC(CC_AL);
		SetJumpTarget(skipNonZero);
		break;
	}

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_HiLo(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::MtLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::NOINIT } });
		MOV(regs_.R(IRREG_LO), regs_.R(inst.src1));
		break;

	case IROp::MtHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_HI, 1, MIPSMap::NOINIT } });
		MOV(regs_.R(IRREG_HI), regs_.R(inst.src1));
		break;

	case IROp::MfLo:
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::INIT } });
		MOV(regs_.R(inst.dest), regs_.R(IRREG_LO));
		break;

	case IROp::MfHi:
		regs_.MapWithExtra(inst, { { 'G', IRREG_HI, 1, MIPSMap::INIT } });
		MOV(regs_.R(inst.dest), regs_.R(IRREG_HI));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_Logic(IRInst inst) {
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
		ORI2R(regs_.R(inst.dest), regs_.R(inst.src1), inst.constant, SCRATCH1);
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

void ArmJitBackend::CompIR_Mult(IRInst inst) {
	CONDITIONAL_DISABLE;

	auto mapHiLo = [&](MIPSMap flags) {
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, flags }, { 'G', IRREG_HI, 1, flags } });
	};

	switch (inst.op) {
	case IROp::Mult:
		mapHiLo(MIPSMap::NOINIT);
		SMULL(regs_.R(IRREG_LO), regs_.R(IRREG_HI), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::MultU:
		mapHiLo(MIPSMap::NOINIT);
		UMULL(regs_.R(IRREG_LO), regs_.R(IRREG_HI), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::Madd:
		mapHiLo(MIPSMap::DIRTY);
		SMLAL(regs_.R(IRREG_LO), regs_.R(IRREG_HI), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::MaddU:
		mapHiLo(MIPSMap::DIRTY);
		UMLAL(regs_.R(IRREG_LO), regs_.R(IRREG_HI), regs_.R(inst.src1), regs_.R(inst.src2));
		break;

	case IROp::Msub:
	case IROp::MsubU:
		// No multiply-subtract-long, so do the product and a 64-bit subtract.
		mapHiLo(MIPSMap::DIRTY);
		if (inst.op == IROp::Msub)
			SMULL(SCRATCH1, SCRATCH2, regs_.R(inst.src1), regs_.R(inst.src2));
		else
			UMULL(SCRATCH1, SCRATCH2, regs_.R(inst.src1), regs_.R(inst.src2));
		SUBS(regs_.R(IRREG_LO), regs_.R(IRREG_LO), SCRATCH1);
		SBC(regs_.R(IRREG_HI), regs_.R(IRREG_HI), SCRATCH2);
		break;

	default:
		INVALIDOP;
		break;
	}
}

void ArmJitBackend::CompIR_Shift(IRInst inst) {
	CONDITIONAL_DISABLE;

	// A shift by register uses the bottom byte of it, but MIPS only the bottom five bits.
	auto shiftByReg = [&](ShiftType type) {
		regs_.Map(inst);
		AND(SCRATCH1, regs_.R(inst.src2), Operand2(31));
		MOV(regs_.R(inst.dest), Operand2(regs_.R(inst.src1), type, SCRATCH1));
	};
	auto shiftByImm = [&](ShiftType type, int amount) {
		regs_.Map(inst);
		MOV(regs_.R(inst.dest), Operand2(regs_.R(inst.src1), type, (u8)amount));
	};
	auto movOrNothing = [&]() {
		if (inst.dest != inst.src1) {
			regs_.Map(inst);
			MOV(regs_.R(inst.dest), regs_.R(inst.src1));
		}
	};

	switch (inst.op) {
	case IROp::Shl:
		shiftByReg(ST_LSL);
		break;

	case IROp::Shr:
		shiftByReg(ST_LSR);
		break;

	case IROp::Sar:
		shiftByReg(ST_ASR);
		break;

	case IROp::Ror:
		// Rotation by the bottom byte is the same as by its bottom five bits.
		regs_.Map(inst);
		MOV(regs_.R(inst.dest), Operand2(regs_.R(inst.src1), ST_ROR, regs_.R(inst.src2)));
		break;

	case IROp::ShlImm:
		// Shouldn't happen, but let's be safe of any passes that modify the ops.
		if (inst.src2 >= 32)
			regs_.SetGPRImm(inst.dest, 0);
		else if (inst.src2 == 0)
			movOrNothing();
		else
			shiftByImm(ST_LSL, inst.src2);
		break;

	case IROp::ShrImm:
		if (inst.src2 >= 32)
			regs_.SetGPRImm(inst.dest, 0);
		else if (inst.src2 == 0)
			movOrNothing();
		else
			shiftByImm(ST_LSR, inst.src2);
		break;

	case IROp::SarImm:
		if (inst.src2 >= 32)
			shiftByImm(ST_ASR, 31);
		else if (inst.src2 == 0)
			movOrNothing();
		else
			shiftByImm(ST_ASR, inst.src2);
		break;

	case IROp::RorImm:
		if ((inst.src2 & 31) == 0)
			movOrNothing();
		else
			shiftByImm(ST_ROR, inst.src2 & 31);
		break;

	default:
		INVALIDOP;
		break;
	}
}

} // namespace MIPSComp

#endif
