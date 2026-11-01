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

// The registers EmitArm64VfpuDot() works in. x and y are only read. q are seven vector temps and w
// six GPR temps, none of them x or y, except that q[0] and q[1] may be x and y themselves.
struct Arm64VfpuDotRegs {
	Arm64Gen::ARM64Reg x;
	Arm64Gen::ARM64Reg y;
	Arm64Gen::ARM64Reg q[7];
	Arm64Gen::ARM64Reg w[6];
};

// Emits vfpu_dot() of the Vec4s x and y: vfpu_dot_neon(), scheduled after what clang makes of it.
// The result ends up in w[3]. Inf and NaN aren't handled: the returned branch is taken for them,
// and the caller sends those to GenerateArm64VfpuDotSpecial()'s routine. Clobbers the flags.
inline Arm64Gen::FixupBranch EmitArm64VfpuDot(Arm64Gen::ARM64XEmitter *e, Arm64Gen::ARM64FloatEmitter *fp, const Arm64VfpuDotRegs &r) {
	using namespace Arm64Gen;
	const ARM64Reg x = EncodeRegToQuad(r.x);
	const ARM64Reg y = EncodeRegToQuad(r.y);
	ARM64Reg q[7];
	for (int i = 0; i < 7; i++)
		q[i] = EncodeRegToQuad(r.q[i]);
	const ARM64Reg emax = DecodeReg(r.w[0]);
	const ARM64Reg sum = DecodeReg(r.w[1]);
	const ARM64Reg sign = DecodeReg(r.w[2]);
	const ARM64Reg m = DecodeReg(r.w[3]);
	const ARM64Reg t2 = DecodeReg(r.w[4]);
	const ARM64Reg t3 = DecodeReg(r.w[5]);

	// Exponents: a zero or denormal operand makes the product zero, and inf or NaN gets 0x7FF
	// added, above any real exponent sum, so one maximum finds the alignment and the specials
	// (q3 = e, q5 = normal).
	e->MOVI2R(emax, 0x7F800000);
	fp->DUP(32, q[2], emax);
	fp->AND(q[3], x, q[2]);
	fp->AND(q[4], y, q[2]);
	fp->CMTST(32, q[5], x, q[2]);
	fp->CMEQ(32, q[6], q[4]);
	fp->BIC(q[5], q[5], q[6]);
	fp->UMAX(32, q[6], q[3], q[4]);
	fp->CMEQ(32, q[6], q[6], q[2]);
	fp->ADD(32, q[3], q[3], q[4]);
	fp->USHR(32, q[3], q[3], 23);
	fp->AND(q[3], q[3], q[5]);
	fp->MOVI(32, q[2], 0x07, 8, true);  // 0x7FF
	fp->AND(q[6], q[6], q[2]);
	fp->ADD(32, q[3], q[3], q[6]);
	fp->UMAXP(32, q[2], q[3], q[3]);
	fp->UMAXP(32, q[2], q[2], q[2]);
	fp->UMOV(32, emax, q[2], 0);
	e->CMPI2R(emax, 0x7FF);
	FixupBranch special = e->B(CC_HS);

	// q4 = the signs. 24x24-bit products, kept to 2 extra bits with round-to-odd (q1 = p).
	fp->EOR(q[4], x, y);
	fp->SSHR(32, q[4], q[4], 31);
	fp->MOVI(32, q[6], 0x7F, 16, true);  // 0x7FFFFF
	fp->AND(q[0], x, q[6]);
	fp->ORR(32, q[0], 0x80, 16);
	fp->AND(q[1], y, q[6]);
	fp->ORR(32, q[1], 0x80, 16);
	fp->UMULL(32, q[6], EncodeRegToDouble(q[0]), EncodeRegToDouble(q[1]));
	fp->UMULL2(32, q[0], q[0], q[1]);
	fp->SHRN(32, EncodeRegToDouble(q[1]), q[6], 21);
	fp->SHRN2(32, q[1], q[0], 21);
	fp->UZP1(32, q[6], q[6], q[0]);
	fp->MOVI(32, q[0], 0x1F, 16, true);  // (1 << 21) - 1
	fp->AND(q[6], q[6], q[0]);
	fp->MOVI(32, q[0], 1);
	fp->UMIN(32, q[6], q[6], q[0]);
	fp->ORR(q[1], q[1], q[6]);
	fp->AND(q[1], q[1], q[5]);

	// Align to the largest exponent by truncation, apply the signs, and sum exactly.
	fp->DUP(32, q[2], q[2], 0);
	fp->SUB(32, q[3], q[3], q[2]);
	fp->MVNI(32, q[2], 27);  // -28
	fp->SMAX(32, q[3], q[3], q[2]);
	fp->USHL(32, q[1], q[1], q[3]);
	fp->EOR(q[1], q[1], q[4]);
	fp->SUB(32, q[1], q[1], q[4]);
	fp->ADDV(32, EncodeRegToSingle(q[1]), q[1]);
	fp->UMOV(32, sum, q[1], 0);

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
	e->SUB(emax, emax, t2);
	e->SUBI2R(emax, emax, 120);
	e->SXTW(EncodeRegTo64(emax), emax);
	e->ADD(EncodeRegTo64(emax), EncodeRegTo64(m), EncodeRegTo64(emax), ArithOption(EncodeRegTo64(emax), ST_LSL, 23));
	e->MOVI2R(m, 0x7F7FFFFF);
	e->CMP(EncodeRegTo64(emax), EncodeRegTo64(m));
	e->MOVI2R(m, 0x7F800000);
	e->CSEL(m, m, emax, CC_GT);
	// Zero if the result is below the smallest normal, or the sum (still scaled by 4) is.
	e->MOVI2R(t2, 0x00800000);
	e->CMP(EncodeRegTo64(emax), EncodeRegTo64(t2));
	e->CCMP(sum, 4, 0, CC_GE);
	e->CSEL(m, WZR, m, CC_LO);
	e->ORR(m, m, sign);
	return special;
}

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
// with special (from GenerateArm64VfpuDotSpecial()) for inf and NaN. It clobbers q0-q6, x0-x3, x16,
// x17, x30 and the flags, and saves nothing: the call site flushes whatever of those holds guest
// state, which in most blocks is nothing.
inline const u8 *GenerateArm64VfpuDotCall(Arm64Gen::ARM64XEmitter *e, Arm64Gen::ARM64FloatEmitter *fp, Arm64Gen::ARM64Reg ctx, const u8 *special) {
	using namespace Arm64Gen;
	const u8 *start = e->AlignCode16();
	const Arm64VfpuDotRegs regs{ Q0, Q1, { Q0, Q1, Q2, Q3, Q4, Q5, Q6 }, { W16, W17, W0, W1, W2, W3 } };
	FixupBranch specialBranch = EmitArm64VfpuDot(e, fp, regs);
	e->RET();

	// The operands are still in q0 and q1.
	e->SetJumpTarget(specialBranch);
	e->ADDI2R(X16, ctx, offsetof(MIPSState, vfpuDotArgs));
	fp->STP(128, INDEX_SIGNED, Q0, Q1, X16, 0);
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
	const ARM64Reg EXP = Q5, MANT = Q6, MASK21 = Q7, ONE = Q16, NEG28 = Q17, YM = Q18;
	const ARM64Reg E[4] = { Q19, Q20, Q21, Q22 };
	const ARM64Reg P[4] = { Q23, Q24, Q25, Q26 };
	const ARM64Reg XMAX = Q27, YSPECIAL = Q28, T0 = Q29, T1 = Q30, T2 = Q31;

	e->MOVI2R(W17, 0x7F800000);
	fp->DUP(32, EXP, W17);
	fp->MOVI(32, MANT, 0x7F, 16, true);  // 0x7FFFFF
	fp->MOVI(32, MASK21, 0x1F, 16, true);  // (1 << 21) - 1
	fp->MOVI(32, ONE, 1);
	fp->MVNI(32, NEG28, 27);
	// The vector's significands, and whether it has an inf or NaN.
	fp->AND(YM, Y, MANT);
	fp->ORR(32, YM, 0x80, 16);
	fp->AND(YSPECIAL, Y, EXP);
	fp->CMEQ(32, YSPECIAL, YSPECIAL, EXP);

	// Per term: the exponent sums (left in place, at bit 23), zero for a zero or denormal operand,
	// and the 24x24-bit products kept to 2 extra bits with round-to-odd.
	for (int i = 0; i < terms; i++) {
		const ARM64Reg x = X[i];
		fp->DUP(32, T0, Y, i);
		fp->AND(T0, T0, EXP);
		fp->AND(T1, x, EXP);
		fp->ADD(32, E[i], T1, T0);
		fp->UMIN(32, T0, T1, T0);
		fp->CMTST(32, T0, T0, T0);
		fp->AND(E[i], E[i], T0);
		if (i == 0)
			fp->MOV(XMAX, T1);
		else
			fp->UMAX(32, XMAX, XMAX, T1);
		fp->AND(T1, x, MANT);
		fp->ORR(32, T1, 0x80, 16);
		fp->UMULL(32, T2, D(T1), YM, i);
		fp->UMULL2(32, T1, T1, YM, i);
		fp->SHRN(32, D(P[i]), T2, 21);
		fp->SHRN2(32, P[i], T1, 21);
		fp->UZP1(32, T2, T2, T1);
		fp->AND(T2, T2, MASK21);
		fp->UMIN(32, T2, T2, ONE);
		fp->ORR(P[i], P[i], T2);
		fp->AND(P[i], P[i], T0);
	}

	// Inf or NaN anywhere sends the lot to the reference.
	const ARM64Reg emax = T0;
	fp->MOV(emax, E[0]);
	for (int i = 1; i < terms; i++)
		fp->UMAX(32, emax, emax, E[i]);
	fp->CMEQ(32, T1, XMAX, EXP);
	fp->ORR(T1, T1, YSPECIAL);
	fp->UMAXP(32, T1, T1, T1);
	fp->UMOV(64, X17, T1, 0);
	FixupBranch special = e->CBNZ(X17);

	// Align each to its lane's largest exponent by truncation, sign, and sum exactly. The exponent
	// sums are shifted down first, as their difference at bit 23 can overflow.
	const ARM64Reg sum = P[0];
	fp->USHR(32, emax, emax, 23);
	for (int i = 0; i < terms; i++) {
		fp->USHR(32, T1, E[i], 23);
		fp->SUB(32, T1, T1, emax);
		fp->SMAX(32, T1, T1, NEG28);
		fp->USHL(32, P[i], P[i], T1);
		fp->DUP(32, T2, Y, i);
		fp->EOR(T2, X[i], T2);
		fp->SSHR(32, T2, T2, 31);
		fp->EOR(P[i], P[i], T2);
		fp->SUB(32, P[i], P[i], T2);
		if (i > 0)
			fp->ADD(32, sum, sum, P[i]);
	}

	// vfpu_dot_finish(), per lane. In round-to-nearest (as nearly every game runs), UCVTF of m does
	// its rounding, and its exponent field plus emax - 277 is the result's. q1 = the sign, q3 = m,
	// q5 = the result, q6 = its exponent field.
	e->MRS(X17, FIELD_FPCR);
	e->TSTI2R(X17, 3 << 22);
	FixupBranch notNearest = e->B(CC_NEQ);
	fp->MOVI(32, Q1, 0x80, 24);
	fp->AND(Q1, sum, Q1);
	fp->ABS(32, Q3, sum);
	fp->USHR(32, Q3, Q3, 2);
	fp->UCVTF(32, Q5, Q3);
	fp->USHR(32, Q6, Q5, 23);
	fp->ADD(32, Q6, Q6, emax);
	e->MOVI2R(W17, 277);
	fp->DUP(32, Q7, W17);
	fp->SUB(32, Q6, Q6, Q7);
	fp->SLI(32, Q5, Q6, 23);
	// Inf past the largest exponent, zero at or below 0 or if the sum (still scaled by 4) is.
	fp->MOVI(32, Q7, 254);
	fp->CMGT(32, Q7, Q6, Q7);
	e->MOVI2R(W17, 0x7F800000);
	fp->DUP(32, Q2, W17);
	fp->BIT(Q5, Q2, Q7);
	fp->CMGT(32, Q6, Q6);
	fp->CMTST(32, Q3, Q3, Q3);
	fp->AND(Q6, Q6, Q3);
	fp->AND(Q5, Q5, Q6);
	fp->ORR(Q0, Q5, Q1);
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
