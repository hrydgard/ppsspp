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

#pragma once

#include <cstddef>
#include <vector>

#include "Common/Arm64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

namespace MIPSComp {

// Generates the VFPU dot product routine the ARM64 JITs call with BL: vfpu_dot() of the two
// Vec4s in mips->vfpuDotArgs, written to mips->vfpuDotResult. ctx holds the MIPSState pointer.
// Every register is preserved except x30 and the flags. It's integer code, the algorithm of
// vfpu_dot_reference(), and leaves inf and NaN inputs to it.
inline const u8 *GenerateArm64VfpuDot(Arm64Gen::ARM64XEmitter *e, Arm64Gen::ARM64FloatEmitter *fp, Arm64Gen::ARM64Reg ctx) {
	using namespace Arm64Gen;
	const s32 args = (s32)offsetof(MIPSState, vfpuDotArgs);
	const s32 result = (s32)offsetof(MIPSState, vfpuDotResult);

	const u8 *start = e->AlignCode16();
	e->STP(INDEX_PRE, X0, X1, SP, -16);
	e->STP(INDEX_PRE, X2, X3, SP, -16);
	e->STP(INDEX_PRE, X4, X5, SP, -16);
	e->STP(INDEX_PRE, X6, X7, SP, -16);
	e->STP(INDEX_PRE, X8, X9, SP, -16);
	e->STP(INDEX_PRE, X10, X11, SP, -16);
	e->STP(INDEX_PRE, X30, ZR, SP, -16);

	// W0/W1: the lane's operands, W2/W3: their exponents, W4: the largest exponent sum of a
	// normal product, W5: the sum.
	// First pass: the largest exponent sum, and inf or NaN anywhere sends it to the slow path.
	std::vector<FixupBranch> special;
	e->MOVI2R(W4, 0);
	for (int i = 0; i < 4; i++) {
		e->LDR(INDEX_UNSIGNED, W0, ctx, args + i * 4);
		e->LDR(INDEX_UNSIGNED, W1, ctx, args + 16 + i * 4);
		e->UBFX(W2, W0, 23, 8);
		e->UBFX(W3, W1, 23, 8);
		e->CMPI2R(W2, 255);
		special.push_back(e->B(CC_EQ));
		e->CMPI2R(W3, 255);
		special.push_back(e->B(CC_EQ));
		// A zero or denormal operand makes the product zero, which doesn't count.
		e->ADD(W7, W2, W3);
		e->CMPI2R(W2, 0);
		e->CCMP(W3, 0, 4, CC_NEQ);
		e->CSEL(W7, W7, WZR, CC_NEQ);
		e->CMP(W4, W7);
		e->CSEL(W4, W4, W7, CC_HS);
	}

	// Second pass: the 24x24-bit products, kept to 2 extra bits with round-to-odd, aligned to the
	// largest by truncation, signed, and summed exactly.
	e->MOVI2R(W5, 0);
	for (int i = 0; i < 4; i++) {
		e->LDR(INDEX_UNSIGNED, W0, ctx, args + i * 4);
		e->LDR(INDEX_UNSIGNED, W1, ctx, args + 16 + i * 4);
		e->UBFX(W2, W0, 23, 8);
		e->UBFX(W3, W1, 23, 8);
		e->ANDI2R(W9, W0, 0x007FFFFF);
		e->ORRI2R(W9, W9, 0x00800000);
		e->ANDI2R(W10, W1, 0x007FFFFF);
		e->ORRI2R(W10, W10, 0x00800000);
		e->UMULL(X6, W9, W10);
		e->LSR(X7, X6, 21);
		e->TSTI2R(X6, (1 << 21) - 1);
		e->CSET(W8, CC_NEQ);
		e->ORR(W7, W7, W8);
		// Shift right by the exponent difference, at most 28.
		e->ADD(W11, W2, W3);
		e->SUB(W11, W4, W11);
		e->MOVI2R(W8, 28);
		e->CMP(W11, W8);
		e->CSEL(W11, W11, W8, CC_LS);
		e->LSRV(W7, W7, W11);
		e->CMPI2R(W2, 0);
		e->CCMP(W3, 0, 4, CC_NEQ);
		e->CSEL(W7, W7, WZR, CC_NEQ);
		e->EOR(W8, W0, W1);
		e->TSTI2R(W8, 0x80000000);
		e->CSNEG(W7, W7, W7, CC_EQ);
		e->ADD(W5, W5, W7);
	}

	// vfpu_dot_finish(): drop the extra bits, round to 24 bits (nearest even), build the float.
	e->ANDI2R(W8, W5, 0x80000000);
	e->CMPI2R(W5, 0);
	e->CSNEG(W9, W5, W5, CC_GE);
	e->LSR(W9, W9, 2);
	e->ORRI2R(W10, W9, 1);
	e->CLZ(W10, W10);
	e->ADDI2R(W10, W10, 32);
	e->SUBI2R(W11, W10, 1);
	e->LSLV(X6, X9, X11);
	e->UBFX(X7, X6, 39, 1);
	e->ADD(X6, X6, X7);
	e->MOVI2R(X7, (1ULL << 38) - 1);
	e->ADD(X6, X6, X7);
	e->LSR(X6, X6, 39);
	e->SUB(W7, W4, W10);
	e->SUBI2R(W7, W7, 88);
	e->SXTW(X7, W7);
	e->LSL(X7, X7, 23);
	e->ADD(X7, X7, X6);
	e->MOVI2R(X6, 0x7F800000);
	e->CMP(X7, X6);
	e->CSEL(X7, X6, X7, CC_GE);
	e->MOVI2R(X6, 0x00800000);
	e->CMP(X7, X6);
	e->CSEL(X7, ZR, X7, CC_LT);
	e->CMPI2R(W9, 0);
	e->CSEL(W7, WZR, W7, CC_EQ);
	e->ORR(W7, W7, W8);
	e->STR(INDEX_UNSIGNED, W7, ctx, result);

	const u8 *epilogue = e->GetCodePointer();
	e->LDP(INDEX_POST, X30, ZR, SP, 16);
	e->LDP(INDEX_POST, X10, X11, SP, 16);
	e->LDP(INDEX_POST, X8, X9, SP, 16);
	e->LDP(INDEX_POST, X6, X7, SP, 16);
	e->LDP(INDEX_POST, X4, X5, SP, 16);
	e->LDP(INDEX_POST, X2, X3, SP, 16);
	e->LDP(INDEX_POST, X0, X1, SP, 16);
	e->RET();

	// Inf or NaN: the C version, with everything else a call may clobber saved too.
	for (const FixupBranch &b : special)
		e->SetJumpTarget(b);
	e->STP(INDEX_PRE, X12, X13, SP, -16);
	e->STP(INDEX_PRE, X14, X15, SP, -16);
	e->STP(INDEX_PRE, X16, X17, SP, -16);
	e->STP(INDEX_PRE, X18, X19, SP, -16);
	for (int i = 0; i < 32; i += 2)
		fp->STP(128, INDEX_PRE, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, -32);
	e->ADDI2R(X0, ctx, args);
	e->ADDI2R(X1, ctx, args + 16);
	e->MOVP2R(X16, &vfpu_dot_reference);
	e->BLR(X16);
	fp->STR(32, INDEX_UNSIGNED, S0, ctx, result);
	for (int i = 30; i >= 0; i -= 2)
		fp->LDP(128, INDEX_POST, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, 32);
	e->LDP(INDEX_POST, X18, X19, SP, 16);
	e->LDP(INDEX_POST, X16, X17, SP, 16);
	e->LDP(INDEX_POST, X14, X15, SP, 16);
	e->LDP(INDEX_POST, X12, X13, SP, 16);
	e->B(epilogue);
	return start;
}

}  // namespace MIPSComp
