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

#include "Common/CPUDetect.h"
#include "Common/LoongArch64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

namespace MIPSComp {

// What EmitLoongArch64VfpuDot() works on. The LSX version takes the operands in the vector regs x and
// y and works in those and v, the scalar one reads them as eight floats (x, then y) at base + offset.
// gpr are GPR temps, none of them base: the scalar version uses all nine, the LSX one the first five.
// The result ends up in ftemp, and the scalar version also clobbers ftemp2.
struct LoongArch64VfpuDotRegs {
	LoongArch64Gen::LoongArch64Reg base;
	s32 offset;
	LoongArch64Gen::LoongArch64Reg x;
	LoongArch64Gen::LoongArch64Reg y;
	LoongArch64Gen::LoongArch64Reg v[6];
	LoongArch64Gen::LoongArch64Reg gpr[9];
	LoongArch64Gen::LoongArch64Reg ftemp;
	LoongArch64Gen::LoongArch64Reg ftemp2;
};

// vfpu_dot_finish() from the sum val and the largest exponent sum ehi, into ftemp, and returns.
// Clobbers val, ehi, s, t and c.
inline void EmitLoongArch64VfpuDotFinish(LoongArch64Gen::LoongArch64Emitter *e, LoongArch64Gen::LoongArch64Reg val, LoongArch64Gen::LoongArch64Reg ehi, LoongArch64Gen::LoongArch64Reg s, LoongArch64Gen::LoongArch64Reg t, LoongArch64Gen::LoongArch64Reg c, LoongArch64Gen::LoongArch64Reg ftemp) {
	using namespace LoongArch64Gen;
	// val = m = |val| >> 2, s = the sign bit.
	e->SRAI_D(s, val, 63);
	e->XOR(val, val, s);
	e->SUB_D(val, val, s);
	e->SRLI_D(val, val, 2);
	e->SLLI_D(s, s, 31);
	FixupBranch zero = e->BEQZ(val);

	// Top bit to bit 62, then round to 24 bits (nearest even) by adding 2^38 - 1 plus the lowest kept
	// bit, and add the significand onto the exponent field, so a carry bumps the exponent.
	e->CLZ_D(c, val);
	e->SUB_D(ehi, ehi, c);
	e->ADDI_D(ehi, ehi, -88);
	e->SLLI_D(ehi, ehi, 23);
	e->ADDI_D(c, c, -1);
	e->SLL_D(val, val, c);
	e->BSTRPICK_D(c, val, 39, 39);
	e->ADD_D(val, val, c);
	e->LI(c, (1ULL << 38) - 1);
	e->ADD_D(val, val, c);
	e->SRLI_D(val, val, 39);
	e->ADD_D(t, ehi, val);

	// The result's exponent is normal when t - 0x00800000 is below 0x7F000000 (unsigned).
	e->LI(c, 0x00800000);
	e->SUB_D(c, t, c);
	e->SRLI_D(c, c, 24);
	e->ADDI_D(c, c, -0x7F);
	FixupBranch outOfRange = e->BGE(c, R_ZERO);
	e->OR(t, t, s);
	e->MOVGR2FR_W(ftemp, t);
	e->RET();

	// Below the smallest normal (t negative or small), it's a signed zero, and above, inf.
	e->SetJumpTarget(outOfRange);
	e->LI(c, 0x00800000);
	FixupBranch underflow = e->BLT(t, c);
	e->LI(c, 0x7F800000);
	e->OR(s, s, c);
	e->SetJumpTarget(zero);
	e->SetJumpTarget(underflow);
	e->MOVGR2FR_W(ftemp, s);
	e->RET();
}

// Emits vfpu_dot() of the operands, ending in a RET with the result in ftemp. Inf and NaN aren't
// handled: the returned branch is taken for them, with the operands untouched. The LSX version is
// vfpu_dot_lsx(), the scalar one vfpu_dot_scalar().
inline LoongArch64Gen::FixupBranch EmitLoongArch64VfpuDot(LoongArch64Gen::LoongArch64Emitter *e, const LoongArch64VfpuDotRegs &r, bool lsx) {
	using namespace LoongArch64Gen;
	const LoongArch64Reg c = r.gpr[0];
	const LoongArch64Reg a = r.gpr[1];
	const LoongArch64Reg b = r.gpr[2];
	const LoongArch64Reg emax = r.gpr[3];
	const LoongArch64Reg sum = r.gpr[4];

	if (lsx) {
		const LoongArch64Reg x = r.x;
		const LoongArch64Reg y = r.y;
		const LoongArch64Reg e1 = r.v[0], t2 = r.v[1], t3 = r.v[2], flush = r.v[3], t5 = r.v[4], sign = r.v[5];

		// Exponents: a zero or denormal operand makes the product zero (flush), and inf or NaN gets
		// 0x7FF, above any real exponent sum, so one maximum finds the alignment and the specials.
		e->LI(c, 0x7F800000);
		e->VREPLGR2VR_W(t5, c);
		e->VAND_V(e1, x, t5);
		e->VAND_V(t2, y, t5);
		e->VSEQ_W(t3, e1, t5);
		e->VSEQ_W(flush, t2, t5);
		e->VOR_V(t3, t3, flush);
		e->VSEQI_W(flush, e1, 0);
		e->VSEQI_W(t5, t2, 0);
		e->VOR_V(flush, flush, t5);
		e->VADD_W(e1, e1, t2);
		e->VSRLI_W(e1, e1, 23);
		e->VANDN_V(e1, flush, e1);
		e->VSRLI_W(t3, t3, 21);
		e->VOR_V(e1, e1, t3);
		e->VSHUF4I_W(t2, e1, 0x4E);
		e->VMAX_WU(t2, e1, t2);
		e->VSHUF4I_W(t3, t2, 0xB1);
		e->VMAX_WU(t2, t2, t3);
		e->VPICKVE2GR_W(emax, t2, 0);
		e->LI(c, 0x7FF);
		FixupBranch special = e->BGE(emax, c);

		// 24x24-bit products, even and odd lanes separately, kept to 2 extra bits with round-to-odd
		// (x = p).
		e->VXOR_V(sign, x, y);
		e->VSRAI_W(sign, sign, 31);
		e->VSLLI_W(x, x, 9);
		e->VSRLI_W(x, x, 9);
		e->VBITSETI_W(x, x, 23);
		e->VSLLI_W(y, y, 9);
		e->VSRLI_W(y, y, 9);
		e->VBITSETI_W(y, y, 23);
		e->VMULWEV_D_WU(t3, x, y);
		e->VMULWOD_D_WU(t5, x, y);
		e->VSRLI_D(x, t5, 21);
		e->VSRLI_D(y, t3, 21);
		e->VPACKEV_W(x, x, y);
		e->VPACKEV_W(y, t5, t3);
		e->VSLLI_W(y, y, 11);
		e->VMINI_WU(y, y, 1);
		e->VOR_V(x, x, y);
		e->VANDN_V(x, flush, x);

		// Align to the largest exponent by truncation (p is below 2^27, so 31 does for any larger
		// shift), apply the signs, and sum exactly.
		e->VSUB_W(t2, t2, e1);
		e->VMINI_WU(t2, t2, 31);
		e->VSRL_W(x, x, t2);
		e->VXOR_V(x, x, sign);
		e->VSUB_W(x, x, sign);
		e->VSHUF4I_W(y, x, 0x4E);
		e->VADD_W(x, x, y);
		e->VSHUF4I_W(y, x, 0xB1);
		e->VADD_W(x, x, y);
		e->VPICKVE2GR_W(sum, x, 0);
		EmitLoongArch64VfpuDotFinish(e, sum, emax, a, b, c, r.ftemp);
		return special;
	}

	// The exponent sums (ev, zero for a zero or denormal operand, whose product is cleared later), and
	// the largest (emax). spec gets bit 8 from any inf or NaN.
	auto x = [&](int i) { return (s16)(r.offset + i * 4); };
	auto y = [&](int i) { return (s16)(r.offset + 16 + i * 4); };
	const LoongArch64Reg spec = sum;
	const LoongArch64Reg ev[4] = { r.gpr[5], r.gpr[6], r.gpr[7], r.gpr[8] };
	for (int i = 0; i < 4; i++) {
		e->LD_W(a, r.base, x(i));
		e->LD_W(b, r.base, y(i));
		e->BSTRPICK_D(a, a, 30, 23);
		e->BSTRPICK_D(b, b, 30, 23);
		e->ADD_D(ev[i], a, b);
		e->ADDI_D(c, a, 1);
		e->OR(spec, i == 0 ? R_ZERO : spec, c);
		e->ADDI_D(c, b, 1);
		e->OR(spec, spec, c);
		e->MASKEQZ(ev[i], ev[i], a);
		e->MASKEQZ(ev[i], ev[i], b);
		if (i == 0) {
			e->MOVE(emax, ev[i]);
		} else {
			e->SLTU(c, emax, ev[i]);
			e->MASKEQZ(a, ev[i], c);
			e->MASKNEZ(b, emax, c);
			e->OR(emax, a, b);
		}
	}
	e->ANDI(c, spec, 0x100);
	FixupBranch special = e->BNEZ(c);

	// 24x24-bit products, kept to 2 extra bits with round-to-odd (a = p), aligned to the largest
	// exponent by truncation, and summed exactly. emax waits in ftemp2, for a register.
	const LoongArch64Reg k = emax;
	e->MOVGR2FR_D(r.ftemp2, emax);
	e->LI(k, 0x00800000);
	for (int i = 0; i < 4; i++) {
		e->LD_W(a, r.base, x(i));
		e->LD_W(b, r.base, y(i));
		e->XOR(c, a, b);
		e->BSTRPICK_D(a, a, 22, 0);
		e->OR(a, a, k);
		e->BSTRPICK_D(b, b, 22, 0);
		e->OR(b, b, k);
		e->MUL_D(a, a, b);
		e->BSTRPICK_D(b, a, 20, 0);
		e->SRLI_D(a, a, 21);
		e->SLTU(b, R_ZERO, b);
		e->OR(a, a, b);
		e->MASKEQZ(a, a, ev[i]);
		// Shifts of 64 and more clear p.
		e->MOVFR2GR_D(b, r.ftemp2);
		e->SUB_D(b, b, ev[i]);
		e->SRL_D(a, a, b);
		e->SLTUI(b, b, 64);
		e->MASKEQZ(a, a, b);
		e->SRAI_D(c, c, 63);
		e->XOR(a, a, c);
		e->SUB_D(a, a, c);
		if (i == 0)
			e->MOVE(sum, a);
		else
			e->ADD_D(sum, sum, a);
	}
	e->MOVFR2GR_D(emax, r.ftemp2);
	EmitLoongArch64VfpuDotFinish(e, sum, emax, a, b, c, r.ftemp);
	return special;
}

// Generates the routine for the inf and NaN dot products: vfpu_dot_reference() of the eight floats
// in mips->vfpuDotArgs, written to mips->vfpuDotResult. ctx holds the MIPSState pointer. It preserves
// every register (all of each vector with LSX).
inline const u8 *GenerateLoongArch64VfpuDotSpecial(LoongArch64Gen::LoongArch64Emitter *e, LoongArch64Gen::LoongArch64Reg ctx) {
	using namespace LoongArch64Gen;
	static constexpr LoongArch64Reg gprs[]{ R_RA, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R16, R17, R18, R19, R20 };
	const bool lsx = cpu_info.LOONGARCH_LSX;
	const int fprSize = lsx ? 32 * 16 : 24 * 8;
	const int size = ((int)ARRAY_SIZE(gprs) * 8 + fprSize + 15) & ~15;

	const u8 *start = e->AlignCode16();
	e->ADDI_D(R_SP, R_SP, -size);
	int offset = 0;
	for (LoongArch64Reg reg : gprs) {
		e->ST_D(reg, R_SP, offset);
		offset += 8;
	}
	// The C ABI keeps only the low 64 bits of f24-f31.
	for (int i = 0; i < (lsx ? 32 : 24); i++) {
		if (lsx) {
			e->VST((LoongArch64Reg)(V0 + i), R_SP, offset);
			offset += 16;
		} else {
			e->FST_D((LoongArch64Reg)(F0 + i), R_SP, offset);
			offset += 8;
		}
	}
	e->ADDI_D(R4, ctx, (s32)offsetof(MIPSState, vfpuDotArgs));
	e->ADDI_D(R5, R4, 16);
	e->QuickCallFunction(&vfpu_dot_reference, R12);
	e->FST_S(F0, ctx, (s32)offsetof(MIPSState, vfpuDotResult));
	offset = 0;
	for (LoongArch64Reg reg : gprs) {
		e->LD_D(reg, R_SP, offset);
		offset += 8;
	}
	for (int i = 0; i < (lsx ? 32 : 24); i++) {
		if (lsx) {
			e->VLD((LoongArch64Reg)(V0 + i), R_SP, offset);
			offset += 16;
		} else {
			e->FLD_D((LoongArch64Reg)(F0 + i), R_SP, offset);
			offset += 8;
		}
	}
	e->ADDI_D(R_SP, R_SP, size);
	e->RET();
	return start;
}

// The registers the routine from GenerateLoongArch64VfpuDot() works in. With LSX, x and y are v8
// and v9 (SCRATCHF1 and SCRATCHF2).
inline void GetLoongArch64VfpuDotRegs(LoongArch64Gen::LoongArch64Reg ctx, LoongArch64VfpuDotRegs *regs) {
	using namespace LoongArch64Gen;
	*regs = { ctx, (s32)offsetof(MIPSState, vfpuDotArgs), V8, V9, { V18, V19, V20, V21, V22, V23 },
		{ R12, R13, R20, R19, R18, R17, R16, R15, R14 }, F8, F9 };
}

// Generates the VFPU dot product routine the IR JIT calls: vfpu_dot() of v8 and v9 with LSX, or of
// the eight floats in mips->vfpuDotArgs without, into f8, with special (from
// GenerateLoongArch64VfpuDotSpecial()) for inf and NaN. It clobbers what GetLoongArch64VfpuDotRegs()
// lists for the version, and ra, and saves nothing: the call site flushes whatever of those holds
// guest state.
inline const u8 *GenerateLoongArch64VfpuDot(LoongArch64Gen::LoongArch64Emitter *e, LoongArch64Gen::LoongArch64Reg ctx, const u8 *special) {
	using namespace LoongArch64Gen;
	LoongArch64VfpuDotRegs regs;
	GetLoongArch64VfpuDotRegs(ctx, &regs);
	const bool lsx = cpu_info.LOONGARCH_LSX;
	const u8 *start = e->AlignCode16();
	FixupBranch specialBranch = EmitLoongArch64VfpuDot(e, regs, lsx);

	e->SetJumpTarget(specialBranch);
	if (lsx) {
		e->VST(regs.x, ctx, (s16)regs.offset);
		e->VST(regs.y, ctx, (s16)(regs.offset + 16));
	}
	e->MOVE(regs.gpr[0], R_RA);
	e->QuickCallFunction(special, regs.gpr[1]);
	e->MOVE(R_RA, regs.gpr[0]);
	e->FLD_S(regs.ftemp, ctx, (s32)offsetof(MIPSState, vfpuDotResult));
	e->RET();
	return start;
}

}  // namespace MIPSComp
