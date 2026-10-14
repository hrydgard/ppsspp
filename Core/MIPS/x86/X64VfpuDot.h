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

#include "ppsspp_config.h"

#if PPSSPP_ARCH(AMD64)

#include <cstddef>
#include <cstring>
#include <vector>

#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

namespace MIPSComp {

// Generates the VFPU dot product routine the x86-64 IR JIT CALLs: vfpu_dot() of xmm0 and xmm1, into
// edi. ctx points at mips->f[0], as CTXREG does. It's vfpu_dot_sse2(), SSE2 only, and leaves inf
// and NaN to vfpu_dot_reference(). It clobbers xmm0-xmm5, xmm14, xmm15, rax, rcx, rdx, rsi, rdi and
// the flags, and saves nothing: the call site flushes whatever of those holds guest state. The xmms
// are the last ones allocated, and rsi and rdi are never allocated outside Windows.
inline const u8 *GenerateX64VfpuDot(Gen::XEmitter *e, Gen::X64Reg ctx) {
	using namespace Gen;
	const int args = (int)(offsetof(MIPSState, vfpuDotArgs) - offsetof(MIPSState, f));

	// The constants, RIP-relative.
	auto constant4 = [&](u32 a, u32 b, u32 c, u32 d) {
		const u8 *p = e->AlignCode16();
		u8 *writable = e->GetWritableCodePtr();
		e->ReserveCodeSpace(16);
		const u32 values[4] = { a, b, c, d };
		memcpy(writable, values, sizeof(values));
		return p;
	};
	auto splat = [&](u32 v) { return constant4(v, v, v, v); };
	const u8 *expBits = splat(0x7F800000);
	const u8 *mantMask = splat(0x007FFFFF);
	const u8 *implicitBit = splat(0x00800000);
	const u8 *lowMask = constant4((1 << 21) - 1, 0, (1 << 21) - 1, 0);
	const u8 *ones = splat(1);
	const u8 *max28 = splat(28);
	const u8 *scaleBase = splat(127 + 28);

	const u8 *start = e->AlignCode16();

	// xmm0 = x, xmm1 = y. Exponents: a zero or denormal operand makes the product zero (xmm4 =
	// flush), and inf or NaN gets 0x7FF, above any real exponent sum, so one maximum finds the
	// alignment and the specials (xmm5 = e). They all fit in 15 bits, so the 16-bit max does.
	e->PXOR(XMM15, R(XMM15));
	e->MOVDQA(XMM2, R(XMM0));
	e->PAND(XMM2, M(expBits));
	e->MOVDQA(XMM3, R(XMM1));
	e->PAND(XMM3, M(expBits));
	e->MOVDQA(XMM4, R(XMM2));
	e->PCMPEQD(XMM4, R(XMM15));
	e->MOVDQA(XMM5, R(XMM3));
	e->PCMPEQD(XMM5, R(XMM15));
	e->POR(XMM4, R(XMM5));
	e->MOVDQA(XMM14, R(XMM2));
	e->PCMPEQD(XMM14, M(expBits));
	e->MOVDQA(XMM5, R(XMM3));
	e->PCMPEQD(XMM5, M(expBits));
	e->POR(XMM14, R(XMM5));
	e->PSRLD(XMM14, 21);
	e->MOVDQA(XMM5, R(XMM2));
	e->PADDD(XMM5, R(XMM3));
	e->PSRLD(XMM5, 23);
	e->MOVDQA(XMM2, R(XMM4));
	e->PANDN(XMM2, R(XMM5));
	e->MOVDQA(XMM5, R(XMM2));
	e->POR(XMM5, R(XMM14));
	e->PSHUFD(XMM2, R(XMM5), 0x4E);
	e->PMAXSW(XMM2, R(XMM5));
	e->PSHUFD(XMM3, R(XMM2), 0xB1);
	e->PMAXSW(XMM2, R(XMM3));
	e->MOVD_xmm(R(EAX), XMM2);
	e->CMP(32, R(EAX), Imm32(0x7FF));
	FixupBranch special = e->J_CC(CC_AE, true);

	// 24x24-bit products, even and odd lanes separately, kept to 2 extra bits with round-to-odd
	// (xmm4 = p). xmm0 = the signs, while x and y are still around.
	e->MOVDQA(XMM3, R(XMM0));
	e->PAND(XMM3, M(mantMask));
	e->POR(XMM3, M(implicitBit));
	e->MOVDQA(XMM14, R(XMM1));
	e->PAND(XMM14, M(mantMask));
	e->POR(XMM14, M(implicitBit));
	e->PXOR(XMM0, R(XMM1));
	e->PSRAD(XMM0, 31);
	e->MOVDQA(XMM15, R(XMM3));
	e->PMULUDQ(XMM15, R(XMM14));  // even products
	e->PSRLQ(XMM3, 32);
	e->PSRLQ(XMM14, 32);
	e->PMULUDQ(XMM3, R(XMM14));  // odd products
	e->MOVDQA(XMM14, R(XMM15));
	e->PSRLQ(XMM14, 21);
	e->PAND(XMM15, M(lowMask));
	e->MOVDQA(XMM1, R(XMM3));
	e->PSRLQ(XMM1, 21);
	e->PSLLQ(XMM1, 32);
	e->POR(XMM14, R(XMM1));  // kept
	e->PAND(XMM3, M(lowMask));
	e->PSLLQ(XMM3, 32);
	e->POR(XMM3, R(XMM15));  // dropped
	e->PXOR(XMM15, R(XMM15));
	e->PCMPEQD(XMM3, R(XMM15));
	e->PANDN(XMM3, M(ones));  // sticky
	e->POR(XMM3, R(XMM14));
	e->PANDN(XMM4, R(XMM3));

	// Align to the largest exponent by truncation. SSE2 has no per-lane shift, so p >> d is done
	// as (p * 2^(28 - d)) >> 28, with the power of two built as float bits and converted by
	// truncation (always a normal float, so DAZ doesn't matter).
	e->PSHUFD(XMM2, R(XMM2), 0x00);
	e->PSUBD(XMM2, R(XMM5));
	e->PMINSW(XMM2, M(max28));
	e->MOVDQA(XMM3, M(scaleBase));
	e->PSUBD(XMM3, R(XMM2));
	e->PSLLD(XMM3, 23);
	e->CVTTPS2DQ(XMM3, R(XMM3));
	e->MOVDQA(XMM5, R(XMM4));
	e->PMULUDQ(XMM5, R(XMM3));
	e->PSRLQ(XMM5, 28);
	e->PSRLQ(XMM4, 32);
	e->PSRLQ(XMM3, 32);
	e->PMULUDQ(XMM4, R(XMM3));
	e->PSRLQ(XMM4, 28);
	e->PSLLQ(XMM4, 32);
	e->POR(XMM4, R(XMM5));

	// Apply the signs and sum exactly.
	e->PXOR(XMM4, R(XMM0));
	e->PSUBD(XMM4, R(XMM0));
	e->PSHUFD(XMM0, R(XMM4), 0x4E);
	e->PADDD(XMM4, R(XMM0));
	e->PSHUFD(XMM0, R(XMM4), 0xB1);
	e->PADDD(XMM4, R(XMM0));

	// vfpu_dot_finish(): drop the extra bits, round to 24 bits (nearest even), build the float.
	// esi = the sum, then its sign. edi = the largest exponent sum, then the result. edx = the
	// significand m. ecx = 62 - its top bit, the shift that puts that at bit 62.
	e->MOVD_xmm(R(ESI), XMM4);
	e->MOV(32, R(EDI), R(EAX));
	e->MOV(32, R(EAX), R(ESI));
	e->CDQ();
	e->XOR(32, R(EAX), R(EDX));
	e->SUB(32, R(EAX), R(EDX));
	e->AND(32, R(ESI), Imm32(0x80000000));
	e->SHR(32, R(EAX), Imm8(2));
	e->MOV(32, R(EDX), R(EAX));
	e->MOV(32, R(ECX), R(EAX));
	e->OR(32, R(ECX), Imm32(1));
	e->BSR(32, ECX, R(ECX));
	e->NEG(32, R(ECX));
	e->ADD(32, R(ECX), Imm32(62));
	e->SHL(64, R(RAX), R(CL));
	e->SUB(32, R(EDI), R(ECX));
	e->SUB(32, R(EDI), Imm32(89));
	e->MOV(64, R(RCX), R(RAX));
	e->SHR(64, R(RCX), Imm8(39));
	e->AND(32, R(ECX), Imm32(1));
	e->ADD(64, R(RAX), R(RCX));
	e->MOV(64, R(RCX), Imm64((1ULL << 38) - 1));
	e->ADD(64, R(RAX), R(RCX));
	e->SHR(64, R(RAX), Imm8(39));
	e->MOVSX(64, 32, RDI, R(EDI));
	e->SHL(64, R(RDI), Imm8(23));
	e->ADD(64, R(RDI), R(RAX));
	e->MOV(32, R(EAX), Imm32(0x7F800000));
	e->CMP(64, R(RDI), R(RAX));
	e->CMOVcc(64, RDI, R(RAX), CC_GE);
	e->XOR(32, R(ECX), R(ECX));
	e->CMP(64, R(RDI), Imm32(0x00800000));
	e->CMOVcc(32, EDI, R(ECX), CC_L);
	e->TEST(32, R(EDX), R(EDX));
	e->CMOVcc(32, EDI, R(ECX), CC_Z);
	e->OR(32, R(EDI), R(ESI));
	e->RET();

	// Inf or NaN: the C version, with x and y still in xmm0 and xmm1. The call can clobber registers
	// that the call site didn't flush, so those are saved, around an aligned stack.
	e->SetJumpTarget(special);
	e->MOVUPS(MDisp(ctx, args), XMM0);
	e->MOVUPS(MDisp(ctx, args + 16), XMM1);
	e->PUSH(R8);
	e->PUSH(R9);
	e->PUSH(R10);
	e->PUSH(R11);
	e->PUSH(RBP);
	e->MOV(64, R(RBP), R(RSP));
	e->AND(64, R(RSP), Imm32(-16));
	// xmm6-xmm13, and the shadow space Windows wants.
	e->SUB(64, R(RSP), Imm32(8 * 16 + 32));
	for (int i = 6; i < 14; i++)
		e->MOVUPS(MDisp(RSP, 32 + (i - 6) * 16), (X64Reg)(XMM0 + i));
#ifdef _WIN32
	e->LEA(64, RCX, MDisp(ctx, args));
	e->LEA(64, RDX, MDisp(ctx, args + 16));
#else
	e->LEA(64, RDI, MDisp(ctx, args));
	e->LEA(64, RSI, MDisp(ctx, args + 16));
#endif
	e->MOV(64, R(RAX), ImmPtr((const void *)&vfpu_dot_reference));
	e->CALLptr(R(RAX));
	e->MOVD_xmm(R(EDI), XMM0);
	for (int i = 6; i < 14; i++)
		e->MOVUPS((X64Reg)(XMM0 + i), MDisp(RSP, 32 + (i - 6) * 16));
	e->MOV(64, R(RSP), R(RBP));
	e->POP(RBP);
	e->POP(R11);
	e->POP(R10);
	e->POP(R9);
	e->POP(R8);
	e->RET();
	return start;
}

}  // namespace MIPSComp

#endif
