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
		if (inst.src1 == inst.src2) {
			LEA(32, regs_.RX(inst.dest), MScaled(regs_.RX(inst.src1), 2, 0));
		} else if (inst.dest == inst.src2) {
			ADD(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else if (inst.dest == inst.src1) {
			ADD(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			ADD(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
		break;

	case IROp::Sub:
		regs_.Map(inst);
		if (inst.src1 == inst.src2) {
			regs_.SetGPRImm(inst.dest, 0);
		} else if (inst.dest == inst.src2) {
			NEG(32, regs_.R(inst.src2));
			ADD(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else if (inst.dest == inst.src1) {
			SUB(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SUB(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
		break;

	case IROp::AddConst:
		if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
			regs_.MarkGPRAsPointerDirty(inst.dest);
			LEA(PTRBITS, regs_.RXPtr(inst.dest), MDisp(regs_.RXPtr(inst.dest), inst.constant));
		} else {
			regs_.Map(inst);
			LEA(32, regs_.RX(inst.dest), MDisp(regs_.RX(inst.src1), inst.constant));
		}
		break;

	case IROp::SubConst:
		if (regs_.IsGPRMappedAsPointer(inst.dest) && inst.dest == inst.src1 && allowPtrMath) {
			regs_.MarkGPRAsPointerDirty(inst.dest);
			LEA(PTRBITS, regs_.RXPtr(inst.dest), MDisp(regs_.RXPtr(inst.dest), -(int)inst.constant));
		} else {
			regs_.Map(inst);
			LEA(32, regs_.RX(inst.dest), MDisp(regs_.RX(inst.src1), -(int)inst.constant));
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
		regs_.MapWithFlags(inst, X64Map::NONE, X64Map::LOW_SUBREG);
		MOVSX(32, 8, regs_.RX(inst.dest), regs_.R(inst.src1));
		break;

	case IROp::Ext16to32:
		regs_.Map(inst);
		MOVSX(32, 16, regs_.RX(inst.dest), regs_.R(inst.src1));
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Bits(IRInst inst) {
	CONDITIONAL_DISABLE;

	switch (inst.op) {
	case IROp::BSwap32:
		regs_.Map(inst);
		if (inst.src1 != inst.dest) {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		}
		BSWAP(32, regs_.RX(inst.dest));
		break;

	case IROp::ReverseBits:
		regs_.Map(inst);
		if (inst.src1 != inst.dest) {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		}

		// Swap even/odd bits (in bits: 0123 -> 1032.)
		LEA(32, SCRATCH1, MScaled(regs_.RX(inst.dest), 2, 0));
		SHR(32, regs_.R(inst.dest), Imm8(1));
		XOR(32, regs_.R(inst.dest), R(SCRATCH1));
		AND(32, regs_.R(inst.dest), Imm32(0x55555555));
		XOR(32, regs_.R(inst.dest), R(SCRATCH1));

		// Swap pairs of bits (in bits: 10325476 -> 32107654.)
		LEA(32, SCRATCH1, MScaled(regs_.RX(inst.dest), 4, 0));
		SHR(32, regs_.R(inst.dest), Imm8(2));
		XOR(32, regs_.R(inst.dest), R(SCRATCH1));
		AND(32, regs_.R(inst.dest), Imm32(0x33333333));
		XOR(32, regs_.R(inst.dest), R(SCRATCH1));

		// Swap nibbles (in nibbles: ABCD -> BADC.)
		MOV(32, R(SCRATCH1), regs_.R(inst.dest));
		SHL(32, R(SCRATCH1), Imm8(4));
		SHR(32, regs_.R(inst.dest), Imm8(4));
		XOR(32, regs_.R(inst.dest), R(SCRATCH1));
		AND(32, regs_.R(inst.dest), Imm32(0x0F0F0F0F));
		XOR(32, regs_.R(inst.dest), R(SCRATCH1));

		// Finally, swap the bytes to drop everything into place (nibbles: BADCFEHG -> HGFEDCBA.)
		BSWAP(32, regs_.RX(inst.dest));
		break;

	case IROp::BSwap16:
		regs_.Map(inst);
		if (cpu_info.bBMI2) {
			// Rotate to put it into the correct register, then swap.
			if (inst.dest != inst.src1)
				RORX(32, regs_.RX(inst.dest), regs_.R(inst.src1), 16);
			else
				ROR(32, regs_.R(inst.dest), Imm8(16));
			BSWAP(32, regs_.RX(inst.dest));
		} else {
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			BSWAP(32, regs_.RX(inst.dest));
			ROR(32, regs_.R(inst.dest), Imm8(16));
		}
		break;

	case IROp::Clz:
		regs_.Map(inst);
		if (cpu_info.bLZCNT) {
			LZCNT(32, regs_.RX(inst.dest), regs_.R(inst.src1));
		} else {
			BSR(32, regs_.RX(inst.dest), regs_.R(inst.src1));
			FixupBranch notFound = J_CC(CC_Z);

			// Since one of these bits must be set, and none outside, this subtracts from 31.
			XOR(32, regs_.R(inst.dest), Imm8(31));
			FixupBranch skip = J();

			SetJumpTarget(notFound);
			MOV(32, regs_.R(inst.dest), Imm32(32));

			SetJumpTarget(skip);
		}
		break;

	default:
		INVALIDOP;
		break;
	}
}

void X64JitBackend::CompIR_Compare(IRInst inst) {
	CONDITIONAL_DISABLE;

	auto setCC = [&](const OpArg &arg, CCFlags cc) {
		// If it's carry, we can take advantage of ADC to avoid subregisters.
		if (cc == CC_C && inst.dest != inst.src1 && inst.dest != inst.src2) {
			XOR(32, regs_.R(inst.dest), regs_.R(inst.dest));
			CMP(32, regs_.R(inst.src1), arg);
			ADC(32, regs_.R(inst.dest), Imm8(0));
		} else if (regs_.HasLowSubregister(regs_.RX(inst.dest)) && inst.dest != inst.src1 && inst.dest != inst.src2) {
			XOR(32, regs_.R(inst.dest), regs_.R(inst.dest));
			CMP(32, regs_.R(inst.src1), arg);
			SETcc(cc, regs_.R(inst.dest));
		} else {
			CMP(32, regs_.R(inst.src1), arg);
			SETcc(cc, R(SCRATCH1));
			MOVZX(32, 8, regs_.RX(inst.dest), R(SCRATCH1));
		}
	};

	switch (inst.op) {
	case IROp::Slt:
		regs_.Map(inst);
		setCC(regs_.R(inst.src2), CC_L);
		break;

	case IROp::SltConst:
		if (inst.constant == 0) {
			// Basically, getting the sign bit.  Let's shift instead.
			regs_.Map(inst);
			if (inst.dest != inst.src1)
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			SHR(32, regs_.R(inst.dest), Imm8(31));
		} else {
			regs_.Map(inst);
			setCC(Imm32(inst.constant), CC_L);
		}
		break;

	case IROp::SltU:
		if (regs_.IsGPRImm(inst.src1) && regs_.GetGPRImm(inst.src1) == 0) {
			// This is kinda common, same as != 0.  Avoid flushing src1.
			regs_.SpillLockGPR(inst.src2, inst.dest);
			regs_.MapGPR(inst.src2);
			regs_.MapGPR(inst.dest, MIPSMap::NOINIT);
			if (inst.dest != inst.src2 && regs_.HasLowSubregister(regs_.RX(inst.dest))) {
				XOR(32, regs_.R(inst.dest), regs_.R(inst.dest));
				TEST(32, regs_.R(inst.src2), regs_.R(inst.src2));
				SETcc(CC_NE, regs_.R(inst.dest));
			} else {
				CMP(32, regs_.R(inst.src2), Imm8(0));
				SETcc(CC_NE, R(SCRATCH1));
				MOVZX(32, 8, regs_.RX(inst.dest), R(SCRATCH1));
			}
		} else {
			regs_.Map(inst);
			setCC(regs_.R(inst.src2), CC_B);
		}
		break;

	case IROp::SltUConst:
		if (inst.constant == 0) {
			regs_.SetGPRImm(inst.dest, 0);
		} else {
			regs_.Map(inst);
			setCC(Imm32(inst.constant), CC_B);
		}
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
		if (inst.dest != inst.src2) {
			regs_.Map(inst);
			TEST(32, regs_.R(inst.src1), regs_.R(inst.src1));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src2), CC_Z);
		}
		break;

	case IROp::MovNZ:
		if (inst.dest != inst.src2) {
			regs_.Map(inst);
			TEST(32, regs_.R(inst.src1), regs_.R(inst.src1));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src2), CC_NZ);
		}
		break;

	case IROp::Max:
		regs_.Map(inst);
		if (inst.src1 == inst.src2) {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else if (inst.dest == inst.src1) {
			CMP(32, regs_.R(inst.src1), regs_.R(inst.src2));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src2), CC_L);
		} else if (inst.dest == inst.src2) {
			CMP(32, regs_.R(inst.src1), regs_.R(inst.src2));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src1), CC_G);
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			CMP(32, regs_.R(inst.dest), regs_.R(inst.src2));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src2), CC_L);
		}
		break;

	case IROp::Min:
		regs_.Map(inst);
		if (inst.src1 == inst.src2) {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		} else if (inst.dest == inst.src1) {
			CMP(32, regs_.R(inst.src1), regs_.R(inst.src2));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src2), CC_G);
		} else if (inst.dest == inst.src2) {
			CMP(32, regs_.R(inst.src1), regs_.R(inst.src2));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src1), CC_L);
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			CMP(32, regs_.R(inst.dest), regs_.R(inst.src2));
			CMOVcc(32, regs_.RX(inst.dest), regs_.R(inst.src2), CC_G);
		}
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
#if PPSSPP_ARCH(AMD64)
		// We need EDX specifically, so force a spill (before spill locks happen.)
		regs_.MapGPR2(IRREG_LO, MIPSMap::NOINIT | X64Map::HIGH_DATA);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT | X64Map::HIGH_DATA } });
#else // PPSSPP_ARCH(X86)
		// Force a spill, it's HI in this path.
		regs_.MapGPR(IRREG_HI, MIPSMap::NOINIT | X64Map::HIGH_DATA);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::NOINIT }, { 'G', IRREG_HI, 1, MIPSMap::NOINIT | X64Map::HIGH_DATA } });
#endif
		{
			TEST(32, regs_.R(inst.src2), regs_.R(inst.src2));
			FixupBranch divideByZero = J_CC(CC_E, false);

			// Sign extension sets HI to -1 for us on x64.
			MOV(PTRBITS, regs_.R(IRREG_LO), Imm32(0x80000000));
#if PPSSPP_ARCH(X86)
			MOV(PTRBITS, regs_.R(IRREG_HI), Imm32(-1));
#endif
			CMP(32, regs_.R(inst.src1), regs_.R(IRREG_LO));
			FixupBranch numeratorNotOverflow = J_CC(CC_NE, false);
			CMP(32, regs_.R(inst.src2), Imm32(-1));
			FixupBranch denominatorOverflow = J_CC(CC_E, false);

			SetJumpTarget(numeratorNotOverflow);

			// It's finally time to actually divide.
			MOV(32, R(EAX), regs_.R(inst.src1));
			CDQ();
			IDIV(32, regs_.R(inst.src2));
#if PPSSPP_ARCH(AMD64)
			// EDX == RX(IRREG_LO).  Put the remainder in the upper bits, done.
			SHL(64, R(EDX), Imm8(32));
			OR(64, R(EDX), R(EAX));
#else // PPSSPP_ARCH(X86)
			// EDX is already good (HI), just move EAX into place.
			MOV(32, regs_.R(IRREG_LO), R(EAX));
#endif
			FixupBranch done = J(false);

			SetJumpTarget(divideByZero);
			X64Reg loReg = SCRATCH1;
#if PPSSPP_ARCH(X86)
			if (regs_.HasLowSubregister(regs_.RX(IRREG_LO)))
				loReg = regs_.RX(IRREG_LO);
#endif
			// Set to -1 if numerator positive using SF.
			XOR(32, R(loReg), R(loReg));
			TEST(32, regs_.R(inst.src1), regs_.R(inst.src1));
			SETcc(CC_NS, R(loReg));
			NEG(32, R(loReg));
			// If it was negative, OR in 1 (so we get -1 or 1.)
			OR(32, R(loReg), Imm8(1));

#if PPSSPP_ARCH(AMD64)
			// Move the numerator into the high bits.
			MOV(32, regs_.R(IRREG_LO), regs_.R(inst.src1));
			SHL(64, regs_.R(IRREG_LO), Imm8(32));
			OR(64, regs_.R(IRREG_LO), R(loReg));
#else // PPSSPP_ARCH(X86)
			// If we didn't have a subreg, move into place.
			if (loReg != regs_.RX(IRREG_LO))
				MOV(32, regs_.R(IRREG_LO), R(loReg));
			MOV(32, regs_.R(IRREG_HI), regs_.R(inst.src1));
#endif

			SetJumpTarget(denominatorOverflow);
			SetJumpTarget(done);
		}
		break;

	case IROp::DivU:
#if PPSSPP_ARCH(AMD64)
		// We need EDX specifically, so force a spill (before spill locks happen.)
		regs_.MapGPR2(IRREG_LO, MIPSMap::NOINIT | X64Map::HIGH_DATA);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT | X64Map::HIGH_DATA } });
#else // PPSSPP_ARCH(X86)
		// Force a spill, it's HI in this path.
		regs_.MapGPR(IRREG_HI, MIPSMap::NOINIT | X64Map::HIGH_DATA);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::NOINIT }, { 'G', IRREG_HI, 1, MIPSMap::NOINIT | X64Map::HIGH_DATA } });
#endif
		{
			TEST(32, regs_.R(inst.src2), regs_.R(inst.src2));
			FixupBranch divideByZero = J_CC(CC_E, false);

			MOV(32, R(EAX), regs_.R(inst.src1));
			XOR(32, R(EDX), R(EDX));
			DIV(32, regs_.R(inst.src2));
#if PPSSPP_ARCH(AMD64)
			// EDX == RX(IRREG_LO).  Put the remainder in the upper bits, done.
			SHL(64, R(EDX), Imm8(32));
			OR(64, R(EDX), R(EAX));
#else // PPSSPP_ARCH(X86)
			// EDX is already good (HI), just move EAX into place.
			MOV(32, regs_.R(IRREG_LO), R(EAX));
#endif
			FixupBranch done = J(false);

			SetJumpTarget(divideByZero);
			// First, set LO to 0xFFFF if numerator was <= that value.
			MOV(32, regs_.R(IRREG_LO), Imm32(0xFFFF));
			XOR(32, R(SCRATCH1), R(SCRATCH1));
			CMP(32, regs_.R(IRREG_LO), regs_.R(inst.src1));
			// If 0xFFFF was less, CF was set - SBB will subtract 1 from 0, netting -1.
			SBB(32, R(SCRATCH1), Imm8(0));
			OR(32, regs_.R(IRREG_LO), R(SCRATCH1));

#if PPSSPP_ARCH(AMD64)
			// Move the numerator into the high bits.
			MOV(32, R(SCRATCH1), regs_.R(inst.src1));
			SHL(64, R(SCRATCH1), Imm8(32));
			OR(64, regs_.R(IRREG_LO), R(SCRATCH1));
#else // PPSSPP_ARCH(X86)
			MOV(32, regs_.R(IRREG_HI), regs_.R(inst.src1));
#endif

			SetJumpTarget(done);
		}
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
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		// First, clear the bits we're replacing.
		SHR(64, regs_.R(IRREG_LO), Imm8(32));
		SHL(64, regs_.R(IRREG_LO), Imm8(32));
		// Now clear the high bits and merge.
		MOVZX(64, 32, regs_.RX(inst.src1), regs_.R(inst.src1));
		OR(64, regs_.R(IRREG_LO), regs_.R(inst.src1));
#else // PPSSPP_ARCH(X86)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::DIRTY } });
		MOV(32, regs_.R(IRREG_LO), regs_.R(inst.src1));
#endif
		break;

	case IROp::MtHi:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		// First, clear the bits we're replacing.
		MOVZX(64, 32, regs_.RX(IRREG_LO), regs_.R(IRREG_LO));
		// Then move the new bits into place.
		MOV(32, R(SCRATCH1), regs_.R(inst.src1));
		SHL(64, R(SCRATCH1), Imm8(32));
		OR(64, regs_.R(IRREG_LO), R(SCRATCH1));
#else // PPSSPP_ARCH(X86)
		regs_.MapWithExtra(inst, { { 'G', IRREG_HI, 1, MIPSMap::DIRTY } });
		MOV(32, regs_.R(IRREG_HI), regs_.R(inst.src1));
#endif
		break;

	case IROp::MfLo:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		MOV(32, regs_.R(inst.dest), regs_.R(IRREG_LO));
#else // PPSSPP_ARCH(X86)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::INIT } });
		MOV(32, regs_.R(inst.dest), regs_.R(IRREG_LO));
#endif
		break;

	case IROp::MfHi:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::INIT } });
		MOV(64, regs_.R(inst.dest), regs_.R(IRREG_LO));
		SHR(64, regs_.R(inst.dest), Imm8(32));
#else // PPSSPP_ARCH(X86)
		regs_.MapWithExtra(inst, { { 'G', IRREG_HI, 1, MIPSMap::INIT } });
		MOV(32, regs_.R(inst.dest), regs_.R(IRREG_HI));
#endif
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
		AND(32, regs_.R(inst.dest), SImmAuto((s32)inst.constant));
		break;

	case IROp::OrConst:
		regs_.Map(inst);
		if (inst.dest != inst.src1)
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		OR(32, regs_.R(inst.dest), SImmAuto((s32)inst.constant));
		break;

	case IROp::XorConst:
		regs_.Map(inst);
		if (inst.dest != inst.src1)
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		XOR(32, regs_.R(inst.dest), SImmAuto((s32)inst.constant));
		break;

	case IROp::Not:
		regs_.Map(inst);
		if (inst.dest != inst.src1)
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
		NOT(32, regs_.R(inst.dest));
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
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		MOVSX(64, 32, regs_.RX(IRREG_LO), regs_.R(inst.src1));
		MOVSX(64, 32, regs_.RX(inst.src2), regs_.R(inst.src2));
		IMUL(64, regs_.RX(IRREG_LO), regs_.R(inst.src2));
#else // PPSSPP_ARCH(X86)
		// Force a spill (before spill locks.)
		regs_.MapGPR(IRREG_HI, MIPSMap::NOINIT | X64Map::HIGH_DATA);
		// We keep it here so it stays locked.
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::NOINIT }, { 'G', IRREG_HI, 1, MIPSMap::NOINIT | X64Map::HIGH_DATA } });
		MOV(32, R(EAX), regs_.R(inst.src1));
		IMUL(32, regs_.R(inst.src2));
		MOV(32, regs_.R(IRREG_LO), R(EAX));
		// IRREG_HI was mapped to EDX.
#endif
		break;

	case IROp::MultU:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::NOINIT } });
		MOVZX(64, 32, regs_.RX(IRREG_LO), regs_.R(inst.src1));
		MOVZX(64, 32, regs_.RX(inst.src2), regs_.R(inst.src2));
		IMUL(64, regs_.RX(IRREG_LO), regs_.R(inst.src2));
#else // PPSSPP_ARCH(X86)
		// Force a spill (before spill locks.)
		regs_.MapGPR(IRREG_HI, MIPSMap::NOINIT | X64Map::HIGH_DATA);
		// We keep it here so it stays locked.
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::NOINIT }, { 'G', IRREG_HI, 1, MIPSMap::NOINIT | X64Map::HIGH_DATA } });
		MOV(32, R(EAX), regs_.R(inst.src1));
		MUL(32, regs_.R(inst.src2));
		MOV(32, regs_.R(IRREG_LO), R(EAX));
		// IRREG_HI was mapped to EDX.
#endif
		break;

	case IROp::Madd:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		MOVSX(64, 32, SCRATCH1, regs_.R(inst.src1));
		MOVSX(64, 32, regs_.RX(inst.src2), regs_.R(inst.src2));
		IMUL(64, SCRATCH1, regs_.R(inst.src2));
		ADD(64, regs_.R(IRREG_LO), R(SCRATCH1));
#else // PPSSPP_ARCH(X86)
		// For ones that modify LO/HI, we can't have anything else in EDX.
		regs_.ReserveAndLockXGPR(EDX);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::DIRTY }, { 'G', IRREG_HI, 1, MIPSMap::DIRTY } });
		MOV(32, R(EAX), regs_.R(inst.src1));
		IMUL(32, regs_.R(inst.src2));
		ADD(32, regs_.R(IRREG_LO), R(EAX));
		ADC(32, regs_.R(IRREG_HI), R(EDX));
#endif
		break;

	case IROp::MaddU:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		MOVZX(64, 32, SCRATCH1, regs_.R(inst.src1));
		MOVZX(64, 32, regs_.RX(inst.src2), regs_.R(inst.src2));
		IMUL(64, SCRATCH1, regs_.R(inst.src2));
		ADD(64, regs_.R(IRREG_LO), R(SCRATCH1));
#else // PPSSPP_ARCH(X86)
		// For ones that modify LO/HI, we can't have anything else in EDX.
		regs_.ReserveAndLockXGPR(EDX);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::DIRTY }, { 'G', IRREG_HI, 1, MIPSMap::DIRTY } });
		MOV(32, R(EAX), regs_.R(inst.src1));
		MUL(32, regs_.R(inst.src2));
		ADD(32, regs_.R(IRREG_LO), R(EAX));
		ADC(32, regs_.R(IRREG_HI), R(EDX));
#endif
		break;

	case IROp::Msub:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		MOVSX(64, 32, SCRATCH1, regs_.R(inst.src1));
		MOVSX(64, 32, regs_.RX(inst.src2), regs_.R(inst.src2));
		IMUL(64, SCRATCH1, regs_.R(inst.src2));
		SUB(64, regs_.R(IRREG_LO), R(SCRATCH1));
#else // PPSSPP_ARCH(X86)
		// For ones that modify LO/HI, we can't have anything else in EDX.
		regs_.ReserveAndLockXGPR(EDX);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::DIRTY }, { 'G', IRREG_HI, 1, MIPSMap::DIRTY } });
		MOV(32, R(EAX), regs_.R(inst.src1));
		IMUL(32, regs_.R(inst.src2));
		SUB(32, regs_.R(IRREG_LO), R(EAX));
		SBB(32, regs_.R(IRREG_HI), R(EDX));
#endif
		break;

	case IROp::MsubU:
#if PPSSPP_ARCH(AMD64)
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 2, MIPSMap::DIRTY } });
		MOVZX(64, 32, SCRATCH1, regs_.R(inst.src1));
		MOVZX(64, 32, regs_.RX(inst.src2), regs_.R(inst.src2));
		IMUL(64, SCRATCH1, regs_.R(inst.src2));
		SUB(64, regs_.R(IRREG_LO), R(SCRATCH1));
#else // PPSSPP_ARCH(X86)
		// For ones that modify LO/HI, we can't have anything else in EDX.
		regs_.ReserveAndLockXGPR(EDX);
		regs_.MapWithExtra(inst, { { 'G', IRREG_LO, 1, MIPSMap::DIRTY }, { 'G', IRREG_HI, 1, MIPSMap::DIRTY } });
		MOV(32, R(EAX), regs_.R(inst.src1));
		MUL(32, regs_.R(inst.src2));
		SUB(32, regs_.R(IRREG_LO), R(EAX));
		SBB(32, regs_.R(IRREG_HI), R(EDX));
#endif
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
		if (cpu_info.bBMI2) {
			regs_.Map(inst);
			SHLX(32, regs_.RX(inst.dest), regs_.R(inst.src1), regs_.RX(inst.src2));
		} else {
			regs_.MapWithFlags(inst, X64Map::NONE, X64Map::NONE, X64Map::SHIFT);
			if (inst.dest == inst.src1) {
				SHL(32, regs_.R(inst.dest), regs_.R(inst.src2));
			} else if (inst.dest == inst.src2) {
				MOV(32, R(SCRATCH1), regs_.R(inst.src1));
				SHL(32, R(SCRATCH1), regs_.R(inst.src2));
				MOV(32, regs_.R(inst.dest), R(SCRATCH1));
			} else {
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
				SHL(32, regs_.R(inst.dest), regs_.R(inst.src2));
			}
		}
		break;

	case IROp::Shr:
		if (cpu_info.bBMI2) {
			regs_.Map(inst);
			SHRX(32, regs_.RX(inst.dest), regs_.R(inst.src1), regs_.RX(inst.src2));
		} else {
			regs_.MapWithFlags(inst, X64Map::NONE, X64Map::NONE, X64Map::SHIFT);
			if (inst.dest == inst.src1) {
				SHR(32, regs_.R(inst.dest), regs_.R(inst.src2));
			} else if (inst.dest == inst.src2) {
				MOV(32, R(SCRATCH1), regs_.R(inst.src1));
				SHR(32, R(SCRATCH1), regs_.R(inst.src2));
				MOV(32, regs_.R(inst.dest), R(SCRATCH1));
			} else {
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
				SHR(32, regs_.R(inst.dest), regs_.R(inst.src2));
			}
		}
		break;

	case IROp::Sar:
		if (cpu_info.bBMI2) {
			regs_.Map(inst);
			SARX(32, regs_.RX(inst.dest), regs_.R(inst.src1), regs_.RX(inst.src2));
		} else {
			regs_.MapWithFlags(inst, X64Map::NONE, X64Map::NONE, X64Map::SHIFT);
			if (inst.dest == inst.src1) {
				SAR(32, regs_.R(inst.dest), regs_.R(inst.src2));
			} else if (inst.dest == inst.src2) {
				MOV(32, R(SCRATCH1), regs_.R(inst.src1));
				SAR(32, R(SCRATCH1), regs_.R(inst.src2));
				MOV(32, regs_.R(inst.dest), R(SCRATCH1));
			} else {
				MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
				SAR(32, regs_.R(inst.dest), regs_.R(inst.src2));
			}
		}
		break;

	case IROp::Ror:
		regs_.MapWithFlags(inst, X64Map::NONE, X64Map::NONE, X64Map::SHIFT);
		if (inst.dest == inst.src1) {
			ROR(32, regs_.R(inst.dest), regs_.R(inst.src2));
		} else if (inst.dest == inst.src2) {
			MOV(32, R(SCRATCH1), regs_.R(inst.src1));
			ROR(32, R(SCRATCH1), regs_.R(inst.src2));
			MOV(32, regs_.R(inst.dest), R(SCRATCH1));
		} else {
			MOV(32, regs_.R(inst.dest), regs_.R(inst.src1));
			ROR(32, regs_.R(inst.dest), regs_.R(inst.src2));
		}
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
		} else if (inst.src2 <= 3) {
			regs_.Map(inst);
			LEA(32, regs_.RX(inst.dest), MScaled(regs_.RX(inst.src1), 1 << inst.src2, 0));
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
		} else if (cpu_info.bBMI2) {
			regs_.Map(inst);
			RORX(32, regs_.RX(inst.dest), regs_.R(inst.src1), inst.src2 & 31);
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
