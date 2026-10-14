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
#include <cstring>

#include "Common/ArmEmitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

namespace MIPSComp {

// The core registers EmitArmVfpuDot() works in, all different. The result ends up in result.
struct ArmVfpuDotRegs {
	ArmGen::ARMReg sum;
	ArmGen::ARMReg ehi;
	ArmGen::ARMReg sign;
	ArmGen::ARMReg result;
	ArmGen::ARMReg lz;
	ArmGen::ARMReg temp;
};

// Emits vfpu_dot() of the Vec4s in Q8 (x) and Q9 (y): vfpu_dot_neon(), in ARMv7 NEON after what clang
// makes of it, with Q0 and Q8-Q15 as temps. The end is in 32-bit integers rather than
// vfpu_dot_finish()'s 64-bit ones (checked bit-exact against it): the significand goes to bit 30
// instead of 62, the exponent is clamped to [-2, 254], and the zero test is on the result plus 2^24, so
// that it and the inf test are unsigned compares. Inf and NaN aren't handled: the returned branch is
// taken for them, with Q8 and Q9 intact. Clobbers the flags.
inline ArmGen::FixupBranch EmitArmVfpuDot(ArmGen::ARMXEmitter *e, const ArmVfpuDotRegs &r) {
	using namespace ArmGen;
	const ARMReg x = Q8, y = Q9;

	// Exponents: a zero or denormal operand makes the product zero, and inf or NaN gets 0x7FF added,
	// above any real exponent sum, so one maximum finds the alignment and the specials (q11 = e,
	// q13 = normal).
	e->MOVI2R(r.ehi, 0x7F800000);
	e->VDUP(I_32, Q10, r.ehi);
	e->VAND(Q11, x, Q10);
	e->VAND(Q12, y, Q10);
	e->VTST(I_32, Q13, x, Q10);
	e->VTST(I_32, Q14, y, Q10);
	e->VAND(Q13, Q13, Q14);
	e->VMAX(I_32 | I_UNSIGNED, Q14, Q11, Q12);
	e->VCEQ(I_32, Q14, Q14, Q10);
	e->VADD(I_32, Q11, Q11, Q12);
	e->VSHR(I_32 | I_UNSIGNED, Q11, Q11, 23);
	e->VAND(Q11, Q11, Q13);
	e->VMOV_imm(I_32, Q12, VIMM__x1__x1, 0x07);  // 0x7FF
	e->VAND(Q14, Q14, Q12);
	e->VADD(I_32, Q11, Q11, Q14);
	e->VPMAX(I_32 | I_UNSIGNED, D24, D22, D23);
	e->VPMAX(I_32 | I_UNSIGNED, D24, D24, D24);
	e->VMOV_neon(I_32, r.ehi, D24, 0);
	// Real exponent sums are at most 508.
	e->CMP(r.ehi, AssumeMakeOperand2(0x7F0));
	FixupBranch special = e->B_CC(CC_HS);

	// q0 = the signs. 24x24-bit products, kept to 2 extra bits with round-to-odd (q10 = p).
	e->VEOR(Q0, x, y);
	e->VSHR(I_32 | I_SIGNED, Q0, Q0, 31);
	e->VMOV_imm(I_32, Q10, VIMM_x11_x11, 0x7F);  // 0x7FFFFF
	e->VAND(Q15, x, Q10);
	e->VORR_imm(I_32, Q15, VIMM_x___x__, 0x80);
	e->VAND(Q14, y, Q10);
	e->VORR_imm(I_32, Q14, VIMM_x___x__, 0x80);
	e->VMULL(I_32 | I_UNSIGNED, Q8, D30, D28);
	e->VMULL(I_32 | I_UNSIGNED, Q9, D31, D29);
	e->VSHRN(I_64, D20, Q8, 21);
	e->VSHRN(I_64, D21, Q9, 21);
	e->VMOVN(I_64, D28, Q8);
	e->VMOVN(I_64, D29, Q9);
	e->VMOV_imm(I_32, Q15, VIMM_x11_x11, 0x1F);  // (1 << 21) - 1
	e->VAND(Q14, Q14, Q15);
	e->VMOV_imm(I_32, Q15, VIMM___x___x, 1);
	e->VMIN(I_32 | I_UNSIGNED, Q14, Q14, Q15);
	e->VORR(Q10, Q10, Q14);
	e->VAND(Q10, Q10, Q13);

	// Align to the largest exponent by truncation, apply the signs, and sum exactly.
	e->VDUP(I_32, Q12, D24, 0);
	e->VSUB(I_32, Q11, Q11, Q12);
	e->VMVN_imm(I_32, Q15, VIMM___x___x, 27);  // -28
	e->VMAX(I_32 | I_SIGNED, Q11, Q11, Q15);
	e->VSHL(I_32 | I_UNSIGNED, Q10, Q10, Q11);
	e->VEOR(Q10, Q10, Q0);
	e->VSUB(I_32, Q10, Q10, Q0);
	e->VADD(I_32, D20, D20, D21);
	e->VPADD(I_32, D20, D20, D20);
	e->VMOV_neon(I_32, r.sum, D20, 0);

	// The end of vfpu_dot(): ehi = the largest exponent sum, sum = the sum, still scaled by 4.
	e->AND(r.sign, r.sum, AssumeMakeOperand2(0x80000000));
	e->CMP(r.sum, Operand2(0));
	e->SetCC(CC_LT);
	e->RSB(r.sum, r.sum, Operand2(0));
	e->SetCC(CC_AL);
	e->MOV(r.result, Operand2(r.sum, ST_LSR, 2));
	e->ORR(r.lz, r.result, Operand2(1));
	e->CLZ(r.lz, r.lz);
	// Round to 24 bits, nearest even, with the top bit at bit 30.
	e->SUB(r.temp, r.lz, Operand2(1));
	e->MOV(r.result, Operand2(r.result, ST_LSL, r.temp));
	e->UBFX(r.temp, r.result, 7, 1);
	e->ADD(r.result, r.result, r.temp);
	e->ADD(r.result, r.result, Operand2(0x3F));
	e->MOV(r.result, Operand2(r.result, ST_LSR, 7));
	// The exponent, clamped to [-2, 254], which doesn't change the result.
	e->SUB(r.ehi, r.ehi, r.lz);
	e->SUB(r.ehi, r.ehi, Operand2(120));
	e->CMN(r.ehi, Operand2(2));
	e->SetCC(CC_LT);
	e->MVN(r.ehi, Operand2(1));
	e->SetCC(CC_AL);
	e->CMP(r.ehi, Operand2(254));
	e->SetCC(CC_GT);
	e->MOV(r.ehi, Operand2(254));
	e->SetCC(CC_AL);
	// Adding the significand, implicit bit and all, lets a carry from the rounding bump the exponent.
	e->ADD(r.result, r.result, Operand2(r.ehi, ST_LSL, 23));
	e->ADD(r.temp, r.result, AssumeMakeOperand2(0x01000000));
	// 0x7F800000 isn't an immediate, so compare the exponent field.
	e->MOV(r.lz, Operand2(r.result, ST_LSR, 23));
	e->CMP(r.lz, Operand2(0xFF));
	e->SetCC(CC_HS);
	e->MOVI2R(r.result, 0x7F800000);
	e->SetCC(CC_AL);
	// Zero if the result is below the smallest normal, or the sum is.
	e->CMP(r.temp, AssumeMakeOperand2(0x01800000));
	e->SetCC(CC_LO);
	e->MOV(r.result, Operand2(0));
	e->SetCC(CC_AL);
	e->CMP(r.sum, Operand2(4));
	e->SetCC(CC_LO);
	e->MOV(r.result, Operand2(0));
	e->SetCC(CC_AL);
	e->ORR(r.result, r.result, r.sign);
	return special;
}

static void ArmVfpuDotSpecialC(MIPSState *mips) {
	const float *args = (const float *)mips->vfpuDotArgs;
	const float result = vfpu_dot_reference(args, args + 4);
	memcpy(&mips->vfpuDotResult, &result, sizeof(result));
}

// Generates the routine for the inf and NaN dot products: vfpu_dot_reference() of the two Vec4s in
// mips->vfpuDotArgs, written to mips->vfpuDotResult. ctx holds the MIPSState pointer. Called with the
// stack aligned to 8, and preserves everything that a call may clobber, but Q8-Q15.
inline const u8 *GenerateArmVfpuDotSpecial(ArmGen::ARMXEmitter *e, ArmGen::ARMReg ctx) {
	using namespace ArmGen;
	const u8 *start = e->AlignCode16();
	e->PUSH(8, R0, R1, R2, R3, R4, R5, R12, R_LR);
	e->VPUSH(D0, 8);
	e->QuickCallFunctionR(R12, &ArmVfpuDotSpecialC, ctx);
	e->VPOP(D0, 8);
	e->POP(8, R0, R1, R2, R3, R4, R5, R12, R_PC);
	return start;
}

// Generates the VFPU dot product routine the IR JIT calls with BL: vfpu_dot() of Q8 and Q9, into R3,
// with special (from GenerateArmVfpuDotSpecial()) for inf and NaN. It clobbers Q0, Q8-Q15, R0-R3, R12,
// LR and the flags, and saves only R4: the call site flushes whatever of the others holds guest
// state, which in most blocks is nothing. Q8-Q15 are never allocated.
inline const u8 *GenerateArmVfpuDotCall(ArmGen::ARMXEmitter *e, ArmGen::ARMReg ctx, const u8 *special) {
	using namespace ArmGen;
	const u8 *start = e->AlignCode16();
	e->PUSH(2, R4, R_LR);
	const ArmVfpuDotRegs regs{ R0, R1, R2, R3, R12, R4 };
	FixupBranch specialBranch = EmitArmVfpuDot(e, regs);
	e->POP(2, R4, R_PC);

	// The operands are still in Q8 and Q9.
	e->SetJumpTarget(specialBranch);
	e->ADDI2R(R0, ctx, offsetof(MIPSState, vfpuDotArgs), R1);
	for (int i = 0; i < 4; i++)
		e->VSTR((ARMReg)(D16 + i), R0, i * 8);
	e->BL(special);
	e->LDR(R3, ctx, offsetof(MIPSState, vfpuDotResult));
	e->POP(2, R4, R_PC);
	return start;
}

}  // namespace MIPSComp
