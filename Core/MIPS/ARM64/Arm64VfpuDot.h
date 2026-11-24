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

#include <algorithm>
#include <cstddef>
#include <vector>

#include "Common/Arm64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

namespace MIPSComp {

// Generates the routine for the inf and NaN dot products: vfpu_dot_reference() of the two Vec4s in
// mips->vfpuDotArgs, written to mips->vfpuDotResult. ctx holds the MIPSState pointer. It clobbers
// q0-q3, x16, x17, x30 and the flags, which the IR JIT keeps free of guest state, and preserves
// everything else.
inline const u8 *GenerateArm64VfpuDotSpecial(Arm64Gen::ARM64XEmitter *e, Arm64Gen::ARM64FloatEmitter *fp, Arm64Gen::ARM64Reg ctx) {
	using namespace Arm64Gen;
	const s32 args = (s32)offsetof(MIPSState, vfpuDotArgs);
	const s32 result = (s32)offsetof(MIPSState, vfpuDotResult);

	const u8 *start = e->AlignCode16();
	for (int i = 0; i < 16; i += 2)
		e->STP(INDEX_PRE, (ARM64Reg)(X0 + i), (ARM64Reg)(X0 + i + 1), SP, -16);
	e->STP(INDEX_PRE, X18, X30, SP, -16);
	for (int i = 4; i < 32; i += 2)
		fp->STP(128, INDEX_PRE, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, -32);
	e->ADDI2R(X0, ctx, args);
	e->ADDI2R(X1, ctx, args + 16);
	e->MOVP2R(X16, &vfpu_dot_reference);
	e->BLR(X16);
	fp->STR(32, INDEX_UNSIGNED, S0, ctx, result);
	for (int i = 30; i >= 4; i -= 2)
		fp->LDP(128, INDEX_POST, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, 32);
	e->LDP(INDEX_POST, X18, X30, SP, 16);
	for (int i = 14; i >= 0; i -= 2)
		e->LDP(INDEX_POST, (ARM64Reg)(X0 + i), (ARM64Reg)(X0 + i + 1), SP, 16);
	e->RET();
	return start;
}

// Generates the VFPU dot product routine the IR JIT calls with BL: vfpu_dot() of q0 and q1, into w1,
// with special (from GenerateArm64VfpuDotSpecial()) for inf and NaN. It's vfpu_dot_neon(), ordered
// for an in-order core like the Cortex-A53, which waits on latency: the operands are stored for the
// inf and NaN path first, so the branch to it can come last, and the multiplies start right away.
// It clobbers q0-q6, x0-x3, x16, x17, x30 and the flags, and saves nothing: the call site flushes
// whatever of those holds guest state, which in most blocks is nothing.
inline const u8 *GenerateArm64VfpuDotCall(Arm64Gen::ARM64XEmitter *e, Arm64Gen::ARM64FloatEmitter *fp, Arm64Gen::ARM64Reg ctx, const u8 *special) {
	using namespace Arm64Gen;
	auto D = [](ARM64Reg q) { return EncodeRegToDouble(q); };
	const ARM64Reg emax = W16, sum = W17, sign = W0, m = W1, t2 = W2, t3 = W3;

	const u8 *start = e->AlignCode16();
	e->ADDI2R(X16, ctx, offsetof(MIPSState, vfpuDotArgs));
	fp->STP(128, INDEX_SIGNED, Q0, Q1, X16, 0);
	e->MOVI2R(W17, 0x7F800000);

	// 24x24-bit products kept to 2 extra bits with round-to-odd. The significands are shifted up by 8
	// and 3, so the 64-bit products are shifted by 11, and the bits kept are exactly the top half
	// (v >> 21) and those dropped the bottom half (q4 = the bottom halves, q2 = the top).
	fp->SHL(32, Q2, Q0, 8);
	fp->ORR(32, Q2, 0x80, 24);
	fp->SHL(32, Q3, Q1, 8);
	fp->ORR(32, Q3, 0x80, 24);
	fp->USHR(32, Q3, Q3, 5);
	fp->UMULL(32, Q4, D(Q2), D(Q3));
	fp->UMULL2(32, Q2, Q2, Q3);

	// While they finish, the signs (q6) and the exponents: a zero or denormal operand makes the
	// product zero (q5 = normal), and inf or NaN gets 0x7FF added, above any real exponent sum, so
	// one maximum finds the alignment and the specials (q0 = the exponent sums, q3 = special).
	fp->DUP(32, Q5, W17);
	fp->EOR(Q6, Q0, Q1);
	fp->SSHR(32, Q6, Q6, 31);
	fp->AND(Q0, Q0, Q5);
	fp->AND(Q1, Q1, Q5);
	fp->UMAX(32, Q3, Q0, Q1);
	fp->CMEQ(32, Q3, Q3, Q5);
	fp->UMIN(32, Q5, Q0, Q1);
	fp->CMTST(32, Q5, Q5, Q5);
	fp->ADD(32, Q0, Q0, Q1);
	fp->USHR(32, Q0, Q0, 23);
	fp->AND(Q0, Q0, Q5);
	fp->MOVI(32, Q1, 0x07, 8, true);  // 0x7FF
	fp->AND(Q3, Q3, Q1);
	fp->ADD(32, Q0, Q0, Q3);
	fp->UMAXP(32, Q1, Q0, Q0);
	fp->UMAXP(32, Q1, Q1, Q1);
	fp->UMOV(32, emax, Q1, 0);
	fp->DUP(32, Q1, Q1, 0);

	// The products' round-to-odd, zeroed for a zero or denormal operand (q2 = p).
	fp->UZP1(32, Q3, Q4, Q2);
	fp->UZP2(32, Q2, Q4, Q2);
	fp->MOVI(32, Q4, 1);
	fp->UMIN(32, Q3, Q3, Q4);
	fp->ORR(Q2, Q2, Q3);
	fp->AND(Q2, Q2, Q5);

	// Align to the largest exponent by truncation, apply the signs, and sum exactly.
	fp->SUB(32, Q0, Q0, Q1);
	fp->MVNI(32, Q3, 27);  // -28
	fp->SMAX(32, Q0, Q0, Q3);
	fp->USHL(32, Q2, Q2, Q0);
	fp->EOR(Q2, Q2, Q6);
	fp->SUB(32, Q2, Q2, Q6);
	fp->ADDV(32, EncodeRegToSingle(Q2), Q2);
	fp->UMOV(32, sum, Q2, 0);
	e->CMPI2R(emax, 0x7FF);
	FixupBranch specialBranch = e->B(CC_HS);

	// vfpu_dot_finish(): emax = the largest exponent sum, sum = the sum.
	e->ANDI2R(sign, sum, 0x80000000);
	e->CMPI2R(sum, 0);
	e->CSNEG(sum, sum, sum, CC_GE);
	e->LSR(m, sum, 2);
	e->ORRI2R(t2, m, 1);
	e->CLZ(t2, t2);
	e->ADDI2R(t3, t2, 31);
	e->LSLV(EncodeRegTo64(m), EncodeRegTo64(m), EncodeRegTo64(t3));
	e->UBFX(EncodeRegTo64(t3), EncodeRegTo64(m), 39, 1);
	e->ADD(EncodeRegTo64(m), EncodeRegTo64(m), EncodeRegTo64(t3));
	e->MOVI2R(EncodeRegTo64(t3), (1ULL << 38) - 1);
	e->ADD(EncodeRegTo64(m), EncodeRegTo64(m), EncodeRegTo64(t3));
	e->LSR(EncodeRegTo64(m), EncodeRegTo64(m), 39);
	// In 64 bits, as it can go negative (emax and t2 are zero-extended).
	e->SUB(EncodeRegTo64(emax), EncodeRegTo64(emax), EncodeRegTo64(t2));
	e->SUBI2R(EncodeRegTo64(emax), EncodeRegTo64(emax), 120);
	e->ADD(EncodeRegTo64(emax), EncodeRegTo64(m), EncodeRegTo64(emax), ArithOption(EncodeRegTo64(emax), ST_LSL, 23));
	e->MOVI2R(m, 0x7F800000);
	e->CMP(EncodeRegTo64(emax), EncodeRegTo64(m));
	e->CSEL(m, m, emax, CC_GE);
	// Zero if the result is below the smallest normal, or the sum (still scaled by 4) is.
	e->MOVI2R(t2, 0x00800000);
	e->CMP(EncodeRegTo64(emax), EncodeRegTo64(t2));
	e->CCMP(sum, 4, 0, CC_GE);
	e->CSEL(m, WZR, m, CC_LO);
	e->ORR(m, m, sign);
	e->RET();

	// The operands are already in mips->vfpuDotArgs.
	e->SetJumpTarget(specialBranch);
	e->STR(INDEX_PRE, X30, SP, -16);
	e->BL(special);
	e->LDR(INDEX_POST, X30, SP, 16);
	e->LDR(INDEX_UNSIGNED, W1, ctx, offsetof(MIPSState, vfpuDotResult));
	e->RET();
	return start;
}

// Generates the routine the IR JIT calls for Vec4VfpuMultiDot over terms terms: four vfpu_dot()s at
// once, lane k of q0..q(terms-1) (the terms) against the lanes of q4, into q0. q4's lanes past the
// terms must be zero. It's vfpu_dot_neon() with the dots in lanes instead of the terms, so nothing is
// horizontal, and vfpu_dot_finish() done in 32-bit lanes: the significand, normalized to bit 31,
// keeps all of m (below 2^27), and rounding its low 8 bits away is the 64-bit version's rounding of
// 39. The exponent is clamped to [-2, 253] so the sum fits, with larger ones made inf separately.
// It clobbers q0-q7, the q16-q31 in Arm64VfpuMultiDotClobbers(terms), x16, x17, x30 and the flags,
// and saves nothing: the call site keeps whatever of those holds guest state.
inline u32 Arm64VfpuMultiDotClobbers(int terms) {
	// q16-q18 and q27-q31, and the exponent sums and products of each term (q19+i, q23+i).
	u32 mask = 0xF807;
	for (int i = 0; i < terms; i++)
		mask |= (1 << (3 + i)) | (1 << (7 + i));
	return mask;
}

inline const u8 *GenerateArm64VfpuMultiDot(Arm64Gen::ARM64XEmitter *e, Arm64Gen::ARM64FloatEmitter *fp, Arm64Gen::ARM64Reg ctx, int terms) {
	using namespace Arm64Gen;
	const s32 args = (s32)offsetof(MIPSState, vfpuMultiDotArgs);
	const s32 result = (s32)offsetof(MIPSState, vfpuMultiDotResult);
	auto D = [](ARM64Reg q) { return EncodeRegToDouble(q); };

	const u8 *start = e->AlignCode16();
	const ARM64Reg X[4] = { Q0, Q1, Q2, Q3 };
	const ARM64Reg Y = Q4;

	// It's latency that limits this on an in-order core like the Cortex-A53, so each step is done for
	// all the terms before the next, as they're independent.
	const ARM64Reg EXP = Q5, YE = Q6, ONE = Q16, NEG28 = Q17, YM = Q18;
	const ARM64Reg E[4] = { Q19, Q20, Q21, Q22 };
	const ARM64Reg P[4] = { Q23, Q24, Q25, Q26 };
	const ARM64Reg XMAX = Q27, ADJ = Q28;
	const ARM64Reg T[4] = { Q29, Q30, Q31, Q7 };

	// For the rounding mode check at the end.
	e->MRS(X16, FIELD_FPCR);
	e->MOVI2R(W17, 0x7F800000);
	fp->DUP(32, EXP, W17);
	fp->MOVI(32, ONE, 1);
	fp->MVNI(32, NEG28, 27);
	// The vector's exponents, and its significands with the implicit bit, shifted up by 3. The
	// terms' are shifted up by 8, so the 64-bit products are shifted by 11, and the bits kept are
	// exactly the top half (v >> 21) and those dropped the bottom half.
	fp->AND(YE, Y, EXP);
	fp->SHL(32, YM, Y, 8);
	fp->ORR(32, YM, 0x80, 24);
	fp->USHR(32, YM, YM, 5);

	// The 24x24-bit products kept to 2 extra bits with round-to-odd.
	for (int i = 0; i < terms; i++)
		fp->SHL(32, P[i], X[i], 8);
	for (int i = 0; i < terms; i++)
		fp->ORR(32, P[i], 0x80, 24);
	for (int i = 0; i < terms; i++) {
		fp->UMULL(32, E[i], D(P[i]), YM, i);
		fp->UMULL2(32, P[i], P[i], YM, i);
	}
	for (int i = 0; i < terms; i++)
		fp->UZP1(32, T[i], E[i], P[i]);
	for (int i = 0; i < terms; i++)
		fp->UZP2(32, P[i], E[i], P[i]);
	for (int i = 0; i < terms; i++)
		fp->UMIN(32, T[i], T[i], ONE);
	for (int i = 0; i < terms; i++)
		fp->ORR(P[i], P[i], T[i]);
	// 277 for the finish (ONE is no longer needed), before x17 holds the inf and NaN flag.
	e->MOVI2R(W17, 277);
	fp->DUP(32, ONE, W17);

	// The exponent sums (left in place, at bit 23), and both they and the products zeroed for a
	// zero or denormal operand, two terms at a time. XMAX, the largest exponent among the vector and
	// the terms, finds an inf or NaN anywhere.
	for (int i = 0; i < terms; i += 2) {
		const int n = std::min(2, terms - i);
		for (int j = 0; j < n; j++)
			fp->AND(E[i + j], X[i + j], EXP);
		for (int j = 0; j < n; j++)
			fp->DUP(32, T[2 * j], YE, i + j);
		for (int j = 0; j < n; j++)
			fp->UMIN(32, T[2 * j + 1], E[i + j], T[2 * j]);
		for (int j = 0; j < n; j++)
			fp->UMAX(32, XMAX, i + j == 0 ? YE : XMAX, E[i + j]);
		for (int j = 0; j < n; j++)
			fp->ADD(32, E[i + j], E[i + j], T[2 * j]);
		for (int j = 0; j < n; j++)
			fp->CMTST(32, T[2 * j + 1], T[2 * j + 1], T[2 * j + 1]);
		for (int j = 0; j < n; j++)
			fp->AND(E[i + j], E[i + j], T[2 * j + 1]);
		for (int j = 0; j < n; j++)
			fp->AND(P[i + j], P[i + j], T[2 * j + 1]);
	}

	// Inf or NaN anywhere sends the lot to the reference. The branch waits for the flag, so it's
	// taken after the shifts, which leave q0-q4 alone for it.
	fp->CMEQ(32, T[1], XMAX, EXP);
	fp->UMAXP(32, T[1], T[1], T[1]);
	fp->UMOV(64, X17, T[1], 0);

	// Align each to its lane's largest exponent by truncation. The exponent sums are shifted down
	// first, as their difference at bit 23 can overflow.
	const ARM64Reg emax = T[0];
	for (int i = 0; i < terms; i++)
		fp->USHR(32, E[i], E[i], 23);
	if (terms == 2) {
		fp->UMAX(32, emax, E[0], E[1]);
	} else {
		fp->UMAX(32, emax, E[0], E[1]);
		fp->UMAX(32, T[2], E[2], terms == 4 ? E[3] : E[2]);
		fp->UMAX(32, emax, emax, T[2]);
	}
	for (int i = 0; i < terms; i++)
		fp->SUB(32, E[i], E[i], emax);
	fp->SUB(32, ADJ, emax, ONE);
	for (int i = 0; i < terms; i++)
		fp->SMAX(32, E[i], E[i], NEG28);
	for (int i = 0; i < terms; i++)
		fp->USHL(32, P[i], P[i], E[i]);
	FixupBranch special = e->CBNZ(X17);

	// Sign and sum exactly. The terms (no longer needed) turn into the signs.
	const ARM64Reg sum = P[0];
	const ARM64Reg Ylane[4] = { T[1], T[2], T[3], YE };
	for (int i = 0; i < terms; i++)
		fp->DUP(32, Ylane[i], Y, i);
	for (int i = 0; i < terms; i++)
		fp->EOR(X[i], X[i], Ylane[i]);
	for (int i = 0; i < terms; i++)
		fp->SSHR(32, X[i], X[i], 31);
	for (int i = 0; i < terms; i++)
		fp->EOR(P[i], P[i], X[i]);
	for (int i = 0; i < terms; i++)
		fp->SUB(32, P[i], P[i], X[i]);
	if (terms == 4) {
		fp->ADD(32, P[0], P[0], P[1]);
		fp->ADD(32, P[2], P[2], P[3]);
		fp->ADD(32, sum, P[0], P[2]);
	} else {
		for (int i = 1; i < terms; i++)
			fp->ADD(32, sum, sum, P[i]);
	}

	// vfpu_dot_finish(), per lane. In round-to-nearest (as nearly every game runs), UCVTF of m does
	// its rounding, and its exponent field plus emax - 277 is the result's. q1 = the sign bit mask,
	// q3 = m, q2 = the result, q6 = its exponent field, q7 = 254, q30 = whether m is nonzero.
	e->TSTI2R(X16, 3 << 22);
	FixupBranch notNearest = e->B(CC_NEQ);
	fp->ABS(32, Q3, sum);
	fp->MOVI(32, Q1, 0x80, 24);
	fp->USHR(32, Q3, Q3, 2);
	fp->MOVI(32, Q7, 254);
	fp->UCVTF(32, Q2, Q3);
	fp->CMTST(32, Q30, Q3, Q3);
	fp->USHR(32, Q6, Q2, 23);
	fp->ADD(32, Q6, Q6, ADJ);
	fp->SLI(32, Q2, Q6, 23);
	// Inf past the largest exponent, zero at or below 0 or if the sum (still scaled by 4) is.
	fp->CMGT(32, Q7, Q6, Q7);
	fp->CMGT(32, Q6, Q6);
	fp->BIT(Q2, EXP, Q7);
	fp->AND(Q6, Q6, Q30);
	fp->AND(Q0, Q2, Q6);
	fp->BIT(Q0, sum, Q1);
	const u8 *done = e->GetCodePointer();
	e->RET();

	// The same, in integers, for the other rounding modes. q1 = the sign, q3 = m, q4 = its leading
	// zeros, q6 = the rounded significand, q16 = the exponent, q17 = the bits.
	e->SetJumpTarget(notNearest);
	fp->MOVI(32, Q1, 0x80, 24);
	fp->AND(Q1, sum, Q1);
	fp->SSHR(32, Q2, sum, 31);
	fp->EOR(Q3, sum, Q2);
	fp->SUB(32, Q3, Q3, Q2);
	fp->USHR(32, Q3, Q3, 2);
	fp->MOV(Q4, Q3);
	fp->ORR(32, Q4, 0x01, 0);
	fp->CLZ(32, Q4, Q4);
	fp->USHL(32, Q5, Q3, Q4);
	fp->USHR(32, Q6, Q5, 8);
	fp->MOVI(32, Q7, 0xFF);
	fp->AND(Q7, Q5, Q7);
	fp->MOVI(32, Q2, 1);
	fp->AND(Q2, Q6, Q2);
	fp->ADD(32, Q7, Q7, Q2);
	fp->MOVI(32, Q2, 0x7F);
	fp->ADD(32, Q7, Q7, Q2);
	fp->USHR(32, Q7, Q7, 8);
	fp->ADD(32, Q6, Q6, Q7);
	fp->SUB(32, Q16, emax, Q4);
	fp->MOVI(32, Q2, 120);
	fp->SUB(32, Q16, Q16, Q2);
	fp->MOVI(32, Q2, 253);
	fp->SMIN(32, Q17, Q16, Q2);
	fp->MVNI(32, Q18, 1);  // -2
	fp->SMAX(32, Q17, Q17, Q18);
	fp->SHL(32, Q17, Q17, 23);
	fp->ADD(32, Q17, Q17, Q6);
	// Inf if the exponent was clamped from above or the rounding carried into it.
	fp->CMGT(32, Q18, Q16, Q2);
	e->MOVI2R(W17, 0x7F800000);
	fp->DUP(32, Q19, W17);
	fp->CMGE(32, Q20, Q17, Q19);
	fp->ORR(Q18, Q18, Q20);
	// Zero below the smallest normal, or if the sum (still scaled by 4) is.
	fp->MOVI(32, Q20, 0x80, 16);
	fp->CMGT(32, Q21, Q20, Q17);
	fp->CMEQ(32, Q2, Q3);
	fp->ORR(Q21, Q21, Q2);
	fp->MOV(Q0, Q17);
	fp->BIT(Q0, Q19, Q18);
	fp->BIC(Q0, Q0, Q21);
	fp->ORR(Q0, Q0, Q1);

	e->B(done);

	// Inf or NaN somewhere: vfpu_multi_dot() of the operands, still in q0-q4, saving what C may
	// clobber.
	e->SetJumpTarget(special);
	e->ADDI2R(X16, ctx, args);
	fp->STP(128, INDEX_SIGNED, Q0, Q1, X16, 0);
	fp->STP(128, INDEX_SIGNED, Q2, Q3, X16, 32);
	fp->STR(128, INDEX_UNSIGNED, Q4, X16, 64);
	for (int i = 0; i < 16; i += 2)
		e->STP(INDEX_PRE, (ARM64Reg)(X0 + i), (ARM64Reg)(X0 + i + 1), SP, -16);
	e->STP(INDEX_PRE, X18, X30, SP, -16);
	for (int i = 8; i < 32; i += 2)
		fp->STP(128, INDEX_PRE, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, -32);
	e->ADDI2R(X0, ctx, result);
	e->ADDI2R(X1, ctx, args);
	e->ADDI2R(X2, ctx, args + 64);
	e->MOVI2R(W3, terms);
	e->MOVI2R(W4, 0);
	e->MOVP2R(X16, &vfpu_multi_dot);
	e->BLR(X16);
	for (int i = 30; i >= 8; i -= 2)
		fp->LDP(128, INDEX_POST, (ARM64Reg)(Q0 + i), (ARM64Reg)(Q0 + i + 1), SP, 32);
	e->LDP(INDEX_POST, X18, X30, SP, 16);
	for (int i = 14; i >= 0; i -= 2)
		e->LDP(INDEX_POST, (ARM64Reg)(X0 + i), (ARM64Reg)(X0 + i + 1), SP, 16);
	e->ADDI2R(X16, ctx, result);
	fp->LDR(128, INDEX_UNSIGNED, Q0, X16, 0);
	e->B(done);
	return start;
}

}  // namespace MIPSComp
