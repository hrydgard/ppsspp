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

// Generates the VFPU dot product routine the ARM64 JITs call with BL: vfpu_dot() of the two Vec4s
// in mips->vfpuDotArgs, written to mips->vfpuDotResult. ctx holds the MIPSState pointer. It's
// vfpu_dot_neon(), scheduled after what clang makes of it, and leaves inf and NaN to
// vfpu_dot_reference(). It clobbers q0-q3, x16, x17, x30 and the flags, which both ARM64 JITs keep
// free of guest state, and preserves everything else.
inline const u8 *GenerateArm64VfpuDot(Arm64Gen::ARM64XEmitter *e, Arm64Gen::ARM64FloatEmitter *fp, Arm64Gen::ARM64Reg ctx) {
	using namespace Arm64Gen;
	const s32 args = (s32)offsetof(MIPSState, vfpuDotArgs);
	const s32 result = (s32)offsetof(MIPSState, vfpuDotResult);

	const u8 *start = e->AlignCode16();
	fp->STP(128, INDEX_PRE, Q4, Q5, SP, -32);
	fp->STP(128, INDEX_PRE, Q6, Q7, SP, -32);
	e->STP(INDEX_PRE, X0, X1, SP, -16);
	e->STP(INDEX_PRE, X2, X3, SP, -16);

	// q0 = x, q1 = y. Exponents: a zero or denormal operand makes the product zero, and inf or NaN
	// gets 0x7FF added, above any real exponent sum, so one maximum finds the alignment and the
	// specials (q3 = e, q5 = normal).
	fp->LDR(128, INDEX_UNSIGNED, Q0, ctx, args);
	fp->LDR(128, INDEX_UNSIGNED, Q1, ctx, args + 16);
	e->MOVI2R(W16, 0x7F800000);
	fp->DUP(32, Q2, W16);
	fp->AND(Q3, Q0, Q2);
	fp->AND(Q4, Q1, Q2);
	fp->CMTST(32, Q5, Q0, Q2);
	fp->CMEQ(32, Q6, Q4);
	fp->BIC(Q5, Q5, Q6);
	fp->UMAX(32, Q6, Q3, Q4);
	fp->CMEQ(32, Q6, Q6, Q2);
	fp->ADD(32, Q3, Q3, Q4);
	fp->USHR(32, Q3, Q3, 23);
	fp->AND(Q3, Q3, Q5);
	fp->MOVI(32, Q2, 0x07, 8, true);  // 0x7FF
	fp->AND(Q6, Q6, Q2);
	fp->ADD(32, Q3, Q3, Q6);
	fp->UMAXP(32, Q2, Q3, Q3);
	fp->UMAXP(32, Q2, Q2, Q2);
	fp->UMOV(32, W16, Q2, 0);
	e->CMPI2R(W16, 0x7FF);
	FixupBranch special = e->B(CC_HS);

	// q4 = the signs. 24x24-bit products, kept to 2 extra bits with round-to-odd (q1 = p).
	fp->EOR(Q4, Q0, Q1);
	fp->SSHR(32, Q4, Q4, 31);
	fp->MOVI(32, Q6, 0x7F, 16, true);  // 0x7FFFFF
	fp->AND(Q0, Q0, Q6);
	fp->ORR(32, Q0, 0x80, 16);
	fp->AND(Q1, Q1, Q6);
	fp->ORR(32, Q1, 0x80, 16);
	fp->UMULL(32, Q6, D0, D1);
	fp->UMULL2(32, Q0, Q0, Q1);
	fp->SHRN(32, D1, Q6, 21);
	fp->SHRN2(32, Q1, Q0, 21);
	fp->UZP1(32, Q6, Q6, Q0);
	fp->MOVI(32, Q0, 0x1F, 16, true);  // (1 << 21) - 1
	fp->AND(Q6, Q6, Q0);
	fp->MOVI(32, Q0, 1);
	fp->UMIN(32, Q6, Q6, Q0);
	fp->ORR(Q1, Q1, Q6);
	fp->AND(Q1, Q1, Q5);

	// Align to the largest exponent by truncation, apply the signs, and sum exactly.
	fp->DUP(32, Q2, Q2, 0);
	fp->SUB(32, Q3, Q3, Q2);
	fp->MVNI(32, Q2, 27);  // -28
	fp->SMAX(32, Q3, Q3, Q2);
	fp->USHL(32, Q1, Q1, Q3);
	fp->EOR(Q1, Q1, Q4);
	fp->SUB(32, Q1, Q1, Q4);
	fp->ADDV(32, S1, Q1);
	fp->UMOV(32, W17, Q1, 0);

	// vfpu_dot_finish(): w16 = the largest exponent sum, w17 = the sum.
	e->ANDI2R(W0, W17, 0x80000000);
	e->CMPI2R(W17, 0);
	e->CSNEG(W17, W17, W17, CC_GE);
	e->LSR(W1, W17, 2);
	e->ORRI2R(W2, W1, 1);
	e->CLZ(W2, W2);
	e->ADDI2R(W3, W2, 31);
	e->LSLV(X1, X1, X3);
	e->UBFX(X3, X1, 39, 1);
	e->ADD(X1, X1, X3);
	e->MOVI2R(X3, (1ULL << 38) - 1);
	e->ADD(X1, X1, X3);
	e->LSR(X1, X1, 39);
	e->SUB(W16, W16, W2);
	e->SUBI2R(W16, W16, 120);
	e->SXTW(X16, W16);
	e->ADD(X16, X1, X16, ArithOption(X16, ST_LSL, 23));
	e->MOVI2R(W1, 0x7F7FFFFF);
	e->CMP(X16, X1);
	e->MOVI2R(W1, 0x7F800000);
	e->CSEL(W1, W1, W16, CC_GT);
	// Zero if the result is below the smallest normal, or the sum (in w17, still scaled by 4) is.
	e->MOVI2R(W2, 0x00800000);
	e->CMP(X16, X2);
	e->CCMP(W17, 4, 0, CC_GE);
	e->CSEL(W1, WZR, W1, CC_LO);
	e->ORR(W1, W1, W0);
	e->STR(INDEX_UNSIGNED, W1, ctx, result);

	const u8 *epilogue = e->GetCodePointer();
	e->LDP(INDEX_POST, X2, X3, SP, 16);
	e->LDP(INDEX_POST, X0, X1, SP, 16);
	fp->LDP(128, INDEX_POST, Q6, Q7, SP, 32);
	fp->LDP(128, INDEX_POST, Q4, Q5, SP, 32);
	e->RET();

	// Inf or NaN: the C version, with everything a call may clobber saved too.
	e->SetJumpTarget(special);
	e->STP(INDEX_PRE, X4, X5, SP, -16);
	e->STP(INDEX_PRE, X6, X7, SP, -16);
	e->STP(INDEX_PRE, X8, X9, SP, -16);
	e->STP(INDEX_PRE, X10, X11, SP, -16);
	e->STP(INDEX_PRE, X12, X13, SP, -16);
	e->STP(INDEX_PRE, X14, X15, SP, -16);
	e->STP(INDEX_PRE, X18, X30, SP, -16);
	for (int i = 8; i < 32; i += 2)
		fp->STP(128, INDEX_PRE, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, -32);
	e->ADDI2R(X0, ctx, args);
	e->ADDI2R(X1, ctx, args + 16);
	e->MOVP2R(X16, &vfpu_dot_reference);
	e->BLR(X16);
	fp->STR(32, INDEX_UNSIGNED, S0, ctx, result);
	for (int i = 30; i >= 8; i -= 2)
		fp->LDP(128, INDEX_POST, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, 32);
	e->LDP(INDEX_POST, X18, X30, SP, 16);
	e->LDP(INDEX_POST, X14, X15, SP, 16);
	e->LDP(INDEX_POST, X12, X13, SP, 16);
	e->LDP(INDEX_POST, X10, X11, SP, 16);
	e->LDP(INDEX_POST, X8, X9, SP, 16);
	e->LDP(INDEX_POST, X6, X7, SP, 16);
	e->LDP(INDEX_POST, X4, X5, SP, 16);
	e->B(epilogue);
	return start;
}

}  // namespace MIPSComp
