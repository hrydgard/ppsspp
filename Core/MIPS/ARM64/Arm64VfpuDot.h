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

}  // namespace MIPSComp
