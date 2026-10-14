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
#include "Common/RiscVEmitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

namespace MIPSComp {

// What EmitRiscVVfpuDot() works on. The operands are eight floats (x, then y) at base + offset, which
// are only read. gpr are GPR temps, none of them base: the scalar version uses all eleven, the vector
// one the first seven and v0-v9. The result ends up in ftemp.
struct RiscVVfpuDotRegs {
	RiscVGen::RiscVReg base;
	s32 offset;
	RiscVGen::RiscVReg gpr[11];
	RiscVGen::RiscVReg ftemp;
};

// vfpu_dot_finish() from the sum val and the largest exponent sum ehi (scaled by 2^23), into ftemp,
// and returns. The rounding is FCVT.S.LU's, nearest even whatever the guest's rounding mode, of
// m = |val| >> 2: that gives the significand and its exponent, which only needs ehi added. Clobbers
// val, ehi, s, t and c.
inline void EmitRiscVVfpuDotFinish(RiscVGen::RiscVEmitter *e, RiscVGen::RiscVReg val, RiscVGen::RiscVReg ehi, RiscVGen::RiscVReg s, RiscVGen::RiscVReg t, RiscVGen::RiscVReg c, RiscVGen::RiscVReg ftemp) {
	using namespace RiscVGen;
	e->SRAI(s, val, 63);
	e->XOR(val, val, s);
	e->SUB(val, val, s);
	e->SRLI(val, val, 2);
	e->SLLI(s, s, 31);
	FixupBranch zero = e->BEQ(val, R_ZERO);
	e->FCVT(FConv::S, FConv::LU, ftemp, val, Round::NEAREST_EVEN);
	e->FMV(FMv::X, FMv::W, t, ftemp);
	e->ADD(t, t, ehi);
	e->LI(c, 277 << 22);
	e->SLLI(c, c, 1);
	e->SUB(t, t, c);
	// The result's exponent is normal when t - 0x00800000 is below 0x7F000000 (unsigned).
	e->LI(c, 0x00800000);
	e->SUB(c, t, c);
	e->SRLI(c, c, 24);
	e->ADDI(c, c, -0x7F);
	FixupBranch outOfRange = e->BGE(c, R_ZERO);
	e->OR(t, t, s);
	e->FMV(FMv::W, FMv::X, ftemp, t);
	e->RET();

	// Below the smallest normal (t negative or small), it's a signed zero, and above, inf.
	e->SetJumpTarget(outOfRange);
	e->LI(c, 0x00800000);
	FixupBranch underflow = e->BLT(t, c);
	e->LI(c, 0x7F800000);
	e->OR(s, s, c);
	e->SetJumpTarget(zero);
	e->SetJumpTarget(underflow);
	e->FMV(FMv::W, FMv::X, ftemp, s);
	e->RET();
}

// Emits vfpu_dot() of the operands, ending in a RET with the result in ftemp. Inf and NaN aren't
// handled: the returned branch is taken for them, with the operands untouched. The scalar version
// is vfpu_dot_scalar(), the vector one (which needs the V extension) vfpu_dot_sse2() in RVV.
inline RiscVGen::FixupBranch EmitRiscVVfpuDot(RiscVGen::RiscVEmitter *e, const RiscVVfpuDotRegs &r, bool vector) {
	using namespace RiscVGen;
	const RiscVReg c = r.gpr[0];
	const RiscVReg a = r.gpr[1];
	const RiscVReg b = r.gpr[2];
	const RiscVReg k1 = r.gpr[3];
	const RiscVReg k2 = r.gpr[4];
	const RiscVReg emax = r.gpr[5];
	const RiscVReg sum = r.gpr[6];
	auto x = [&](int i) { return r.offset + i * 4; };
	auto y = [&](int i) { return r.offset + 16 + i * 4; };

	if (vector) {
		e->VSETIVLI(R_ZERO, 4, VType(VSew::E32, VLMul::M1, VTail::A, VMask::A));
		e->ADDI(a, r.base, x(0));
		e->VLE_V(32, V1, a);
		e->ADDI(a, r.base, y(0));
		e->VLE_V(32, V2, a);

		// Exponents (v3 = x, v4 = y): inf and NaN go to the reference, and v0 = zero and denormal
		// lanes, whose products are zero.
		e->LI(k1, 0x7F800000);
		e->VAND_VX(V3, V1, k1);
		e->VAND_VX(V4, V2, k1);
		e->VMAXU_VV(V5, V3, V4);
		e->VMSEQ_VX(V6, V5, k1);
		e->VFIRST_M(c, V6);
		FixupBranch special = e->BGE(c, R_ZERO);
		e->VMINU_VV(V5, V3, V4);
		e->VMSEQ_VI(V0, V5, 0);

		// v3 = the exponent sums, emax = the largest, and v6 = the signs.
		e->VADD_VV(V3, V3, V4);
		e->VSRL_VI(V3, V3, 23);
		e->VMERGE_VIM(V3, V3, 0, V0);
		e->VMV_S_X(V7, R_ZERO);
		e->VREDMAXU_VS(V5, V3, V7);
		e->VMV_X_S(emax, V5);
		e->VXOR_VV(V6, V1, V2);
		e->VSRA_VI(V6, V6, 31);

		// 24x24-bit products, kept to 2 extra bits with round-to-odd (v1 = p).
		e->LI(k2, 0x00800000);
		e->ADDI(k1, k2, -1);
		e->VAND_VX(V1, V1, k1);
		e->VOR_VX(V1, V1, k2);
		e->VAND_VX(V2, V2, k1);
		e->VOR_VX(V2, V2, k2);
		e->VWMULU_VV(V8, V1, V2);
		e->VNSRL_WI(V1, V8, 21);
		e->VNSRL_WI(V2, V8, 0);
		e->VSLL_VI(V2, V2, 11);
		e->LI(c, 1);
		e->VMINU_VX(V2, V2, c);
		e->VOR_VV(V1, V1, V2);
		e->VMERGE_VIM(V1, V1, 0, V0);

		// Align to the largest exponent by truncation (p is below 2^27, so 31 does for any larger
		// shift), apply the signs, and sum exactly.
		e->VRSUB_VX(V2, V3, emax);
		e->LI(c, 31);
		e->VMINU_VX(V2, V2, c);
		e->VSRL_VV(V1, V1, V2);
		e->VXOR_VV(V1, V1, V6);
		e->VSUB_VV(V1, V1, V6);
		e->VREDSUM_VS(V5, V1, V7);
		e->VMV_X_S(sum, V5);
		e->SLLI(emax, emax, 23);
		EmitRiscVVfpuDotFinish(e, sum, emax, a, b, c, r.ftemp);
		return special;
	}

	// The exponent sums, scaled by 2^23 (ev), and the largest (emax). A zero or denormal operand makes
	// the product zero: with Zicond, its sum is zero and the product is cleared later, otherwise its
	// sum is negative enough that the shift clears it. spec gets bit 31 from any inf or NaN.
	const RiscVReg spec = sum;
	const RiscVReg ev[4] = { r.gpr[7], r.gpr[8], r.gpr[9], r.gpr[10] };
	const bool zicond = cpu_info.RiscV_Zicond;
	e->LI(k1, 0x7F800000);
	e->LI(k2, 0x00800000);
	for (int i = 0; i < 4; i++) {
		e->LW(a, r.base, x(i));
		e->LW(b, r.base, y(i));
		e->AND(a, a, k1);
		e->AND(b, b, k1);
		e->ADD(ev[i], a, b);
		e->ADD(c, a, k2);
		e->OR(spec, i == 0 ? R_ZERO : spec, c);
		e->ADD(c, b, k2);
		e->OR(spec, spec, c);
		if (zicond) {
			e->CZERO_EQZ(ev[i], ev[i], a);
			e->CZERO_EQZ(ev[i], ev[i], b);
		} else {
			e->SEQZ(a, a);
			e->SEQZ(b, b);
			e->OR(a, a, b);
			e->SLLI(a, a, 40);
			e->SUB(ev[i], ev[i], a);
		}
		const RiscVReg prev = i == 0 ? R_ZERO : emax;
		if (cpu_info.RiscV_Zbb) {
			e->MAX(emax, prev, ev[i]);
		} else {
			e->SUB(c, ev[i], prev);
			e->SRAI(a, c, 63);
			e->AND(c, c, a);
			e->SUB(emax, ev[i], c);
		}
	}
	e->SRLI(c, spec, 31);
	FixupBranch special = e->BNE(c, R_ZERO);

	// 24x24-bit products, kept to 2 extra bits with round-to-odd (b = p), aligned to the largest
	// exponent by truncation, and summed exactly.
	e->ADDI(k1, k2, -1);
	for (int i = 0; i < 4; i++) {
		e->LW(a, r.base, x(i));
		e->LW(b, r.base, y(i));
		e->XOR(c, a, b);
		e->AND(a, a, k1);
		e->OR(a, a, k2);
		e->AND(b, b, k1);
		e->OR(b, b, k2);
		e->MUL(a, a, b);
		e->SRLI(b, a, 21);
		e->SLLI(a, a, 43);
		e->SNEZ(a, a);
		e->OR(b, b, a);
		if (zicond)
			e->CZERO_EQZ(b, b, ev[i]);
		// Shifts of 64 and more clear p.
		e->SUB(a, emax, ev[i]);
		e->SRLI(a, a, 23);
		e->SRL(b, b, a);
		e->SLTIU(a, a, 64);
		if (zicond) {
			e->CZERO_EQZ(b, b, a);
		} else {
			e->NEG(a, a);
			e->AND(b, b, a);
		}
		e->SRAI(c, c, 63);
		e->XOR(b, b, c);
		e->SUB(b, b, c);
		if (i == 0)
			e->MV(sum, b);
		else
			e->ADD(sum, sum, b);
	}
	EmitRiscVVfpuDotFinish(e, sum, emax, a, b, c, r.ftemp);
	return special;
}

// Generates the routine for the inf and NaN dot products: vfpu_dot_reference() of the eight floats
// in mips->vfpuDotArgs, written to mips->vfpuDotResult. ctx holds the MIPSState pointer. It preserves
// every register.
inline const u8 *GenerateRiscVVfpuDotSpecial(RiscVGen::RiscVEmitter *e, RiscVGen::RiscVReg ctx) {
	using namespace RiscVGen;
	static constexpr RiscVReg gprs[]{ R_RA, X5, X6, X7, X10, X11, X12, X13, X14, X15, X16, X17, X28, X29, X30, X31 };
	static constexpr RiscVReg fprs[]{ F0, F1, F2, F3, F4, F5, F6, F7, F10, F11, F12, F13, F14, F15, F16, F17, F28, F29, F30, F31 };
	const int size = (int)(ARRAY_SIZE(gprs) + ARRAY_SIZE(fprs)) * 8;
	static_assert(((ARRAY_SIZE(gprs) + ARRAY_SIZE(fprs)) & 1) == 0, "Stack must be kept aligned");

	const u8 *start = e->AlignCode16();
	e->ADDI(R_SP, R_SP, -size);
	int offset = 0;
	for (RiscVReg reg : gprs) {
		e->SD(reg, R_SP, offset);
		offset += 8;
	}
	for (RiscVReg reg : fprs) {
		e->FS(64, reg, R_SP, offset);
		offset += 8;
	}
	e->ADDI(X10, ctx, (s32)offsetof(MIPSState, vfpuDotArgs));
	e->ADDI(X11, X10, 16);
	e->QuickCallFunction(&vfpu_dot_reference, X5);
	e->FS(32, F10, ctx, (s32)offsetof(MIPSState, vfpuDotResult));
	offset = 0;
	for (RiscVReg reg : gprs) {
		e->LD(reg, R_SP, offset);
		offset += 8;
	}
	for (RiscVReg reg : fprs) {
		e->FL(64, reg, R_SP, offset);
		offset += 8;
	}
	e->ADDI(R_SP, R_SP, size);
	e->RET();
	return start;
}

// The registers the routine from GenerateRiscVVfpuDot() clobbers, besides ra.
inline void GetRiscVVfpuDotRegs(RiscVGen::RiscVReg ctx, RiscVVfpuDotRegs *regs) {
	using namespace RiscVGen;
	*regs = { ctx, (s32)offsetof(MIPSState, vfpuDotArgs), { X10, X11, X5, X6, X7, X28, X29, X30, X31, X16, X17 }, F31 };
}

// Generates the VFPU dot product routine the IR JIT calls with JAL: vfpu_dot() of the eight floats in
// mips->vfpuDotArgs, into f31, with special (from GenerateRiscVVfpuDotSpecial()) for inf and NaN. The
// vector version is used with the V extension. It clobbers what GetRiscVVfpuDotRegs() lists for the
// version, ra, and with the V extension, v0-v9, and saves nothing: the call site flushes whatever of
// those holds guest state.
inline const u8 *GenerateRiscVVfpuDot(RiscVGen::RiscVEmitter *e, RiscVGen::RiscVReg ctx, const u8 *special) {
	using namespace RiscVGen;
	RiscVVfpuDotRegs regs;
	GetRiscVVfpuDotRegs(ctx, &regs);
	const u8 *start = e->AlignCode16();
	FixupBranch specialBranch = EmitRiscVVfpuDot(e, regs, cpu_info.RiscV_V);

	e->SetJumpTarget(specialBranch);
	e->MV(regs.gpr[0], R_RA);
	e->QuickCallFunction(special, regs.gpr[1]);
	e->MV(R_RA, regs.gpr[0]);
	e->FL(32, regs.ftemp, ctx, (s32)offsetof(MIPSState, vfpuDotResult));
	e->RET();
	return start;
}

}  // namespace MIPSComp
