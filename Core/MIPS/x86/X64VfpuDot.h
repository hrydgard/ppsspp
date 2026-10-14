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
#include <vector>

#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSVFPUUtils.h"

namespace MIPSComp {

// Generates the VFPU dot product routine the x86-64 JITs CALL: vfpu_dot() of the two Vec4s in
// mips->vfpuDotArgs, written to mips->vfpuDotResult. ctx holds the MIPSState pointer. Every
// register is preserved except the flags. It's integer code, the algorithm of
// vfpu_dot_reference(), and leaves inf and NaN inputs to it.
inline const u8 *GenerateX64VfpuDot(Gen::XEmitter *e, Gen::X64Reg ctx) {
	using namespace Gen;
	const int args = (int)offsetof(MIPSState, vfpuDotArgs);
	const int result = (int)offsetof(MIPSState, vfpuDotResult);
	static const X64Reg saved[] = { RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11 };

	const u8 *start = e->AlignCode16();
	for (X64Reg r : saved)
		e->PUSH(r);

	// ESI/EDI: the lane's operands, R8D/R9D: their exponents, R10D: the largest exponent sum of a
	// normal product, R11D: the sum.
	auto loadLane = [&](int i) {
		e->MOV(32, R(ESI), MDisp(ctx, args + i * 4));
		e->MOV(32, R(EDI), MDisp(ctx, args + 16 + i * 4));
		e->MOV(32, R(R8), R(ESI));
		e->SHR(32, R(R8), Imm8(23));
		e->AND(32, R(R8), Imm32(255));
		e->MOV(32, R(R9), R(EDI));
		e->SHR(32, R(R9), Imm8(23));
		e->AND(32, R(R9), Imm32(255));
	};
	// A zero or denormal operand makes the product zero, which doesn't count.
	auto zeroUnlessNormal = [&](X64Reg r, X64Reg zero) {
		e->TEST(32, R(R8), R(R8));
		e->CMOVcc(32, r, R(zero), CC_Z);
		e->TEST(32, R(R9), R(R9));
		e->CMOVcc(32, r, R(zero), CC_Z);
	};

	// First pass: the largest exponent sum, and inf or NaN anywhere sends it to the slow path.
	std::vector<FixupBranch> special;
	e->XOR(32, R(R10), R(R10));
	for (int i = 0; i < 4; i++) {
		loadLane(i);
		e->CMP(32, R(R8), Imm32(255));
		special.push_back(e->J_CC(CC_E, true));
		e->CMP(32, R(R9), Imm32(255));
		special.push_back(e->J_CC(CC_E, true));
		e->LEA(32, EAX, MRegSum(R8, R9));
		e->XOR(32, R(EDX), R(EDX));
		zeroUnlessNormal(EAX, EDX);
		e->CMP(32, R(R10), R(EAX));
		e->CMOVcc(32, R10, R(EAX), CC_B);
	}

	// Second pass: the 24x24-bit products, kept to 2 extra bits with round-to-odd, aligned to the
	// largest by truncation, signed, and summed exactly.
	e->XOR(32, R(R11), R(R11));
	for (int i = 0; i < 4; i++) {
		loadLane(i);
		e->MOV(32, R(EAX), R(ESI));
		e->AND(32, R(EAX), Imm32(0x007FFFFF));
		e->OR(32, R(EAX), Imm32(0x00800000));
		e->MOV(32, R(EDX), R(EDI));
		e->AND(32, R(EDX), Imm32(0x007FFFFF));
		e->OR(32, R(EDX), Imm32(0x00800000));
		e->IMUL(64, RAX, R(RDX));
		e->MOV(64, R(RDX), R(RAX));
		e->SHR(64, R(RDX), Imm8(21));
		e->TEST(32, R(EAX), Imm32((1 << 21) - 1));
		e->SETcc(CC_NZ, R(EAX));
		e->MOVZX(32, 8, EAX, R(EAX));
		e->OR(32, R(EDX), R(EAX));
		// Shift right by the exponent difference, at most 28.
		e->LEA(32, ECX, MRegSum(R8, R9));
		e->MOV(32, R(EAX), R(R10));
		e->SUB(32, R(EAX), R(ECX));
		e->MOV(32, R(ECX), Imm32(28));
		e->CMP(32, R(EAX), R(ECX));
		e->CMOVcc(32, ECX, R(EAX), CC_B);
		e->SHR(32, R(EDX), R(CL));
		e->XOR(32, R(EAX), R(EAX));
		zeroUnlessNormal(EDX, EAX);
		e->MOV(32, R(EAX), R(ESI));
		e->XOR(32, R(EAX), R(EDI));
		e->SAR(32, R(EAX), Imm8(31));
		e->XOR(32, R(EDX), R(EAX));
		e->SUB(32, R(EDX), R(EAX));
		e->ADD(32, R(R11), R(EDX));
	}

	// vfpu_dot_finish(): drop the extra bits, round to 24 bits (nearest even), build the float.
	// ESI: the sign, EDI: the significand m, EDX: its leading zeros as 64 bits.
	e->MOV(32, R(ESI), R(R11));
	e->AND(32, R(ESI), Imm32(0x80000000));
	e->MOV(32, R(EAX), R(R11));
	e->CDQ();
	e->XOR(32, R(EAX), R(EDX));
	e->SUB(32, R(EAX), R(EDX));
	e->SHR(32, R(EAX), Imm8(2));
	e->MOV(32, R(EDI), R(EAX));
	e->MOV(32, R(ECX), R(EAX));
	e->OR(32, R(ECX), Imm32(1));
	e->BSR(32, ECX, R(ECX));
	e->MOV(32, R(EDX), Imm32(63));
	e->SUB(32, R(EDX), R(ECX));
	e->LEA(32, ECX, MDisp(EDX, -1));
	e->MOV(32, R(EAX), R(EDI));
	e->SHL(64, R(RAX), R(CL));
	e->MOV(64, R(R8), R(RAX));
	e->SHR(64, R(R8), Imm8(39));
	e->AND(32, R(R8), Imm32(1));
	e->ADD(64, R(RAX), R(R8));
	e->MOV(64, R(R8), Imm64((1ULL << 38) - 1));
	e->ADD(64, R(RAX), R(R8));
	e->SHR(64, R(RAX), Imm8(39));
	e->MOV(32, R(R8), R(R10));
	e->SUB(32, R(R8), R(EDX));
	e->SUB(32, R(R8), Imm32(88));
	e->MOVSX(64, 32, R8, R(R8));
	e->SHL(64, R(R8), Imm8(23));
	e->ADD(64, R(R8), R(RAX));
	e->MOV(64, R(RAX), Imm64(0x7F800000));
	e->CMP(64, R(R8), R(RAX));
	e->CMOVcc(64, R8, R(RAX), CC_GE);
	e->MOV(64, R(RAX), Imm64(0x00800000));
	e->XOR(32, R(ECX), R(ECX));
	e->CMP(64, R(R8), R(RAX));
	e->CMOVcc(64, R8, R(RCX), CC_L);
	e->TEST(32, R(EDI), R(EDI));
	e->CMOVcc(64, R8, R(RCX), CC_Z);
	e->OR(32, R(R8), R(ESI));
	e->MOV(32, MDisp(ctx, result), R(R8));

	const u8 *epilogue = e->GetCodePointer();
	for (int i = (int)(sizeof(saved) / sizeof(saved[0])) - 1; i >= 0; i--)
		e->POP(saved[i]);
	e->RET();

	// Inf or NaN: the C version, with the vector registers saved too, on an aligned stack.
	for (const FixupBranch &b : special)
		e->SetJumpTarget(b);
	// [RSP]: the shadow space Windows wants, then the 16 XMMs, then the unaligned RSP.
	const int xmmOffset = 32, savedRsp = 32 + 16 * 16, frame = savedRsp + 16;
	e->MOV(64, R(RAX), R(RSP));
	e->AND(64, R(RSP), Imm32(-16));
	e->SUB(64, R(RSP), Imm32(frame));
	e->MOV(64, MDisp(RSP, savedRsp), R(RAX));
	for (int i = 0; i < 16; i++)
		e->MOVUPS(MDisp(RSP, xmmOffset + i * 16), (X64Reg)(XMM0 + i));
#ifdef _WIN32
	e->LEA(64, RCX, MDisp(ctx, args));
	e->LEA(64, RDX, MDisp(ctx, args + 16));
#else
	e->LEA(64, RDI, MDisp(ctx, args));
	e->LEA(64, RSI, MDisp(ctx, args + 16));
#endif
	e->MOV(64, R(RAX), ImmPtr((const void *)&vfpu_dot_reference));
	e->CALLptr(R(RAX));
	e->MOVSS(MDisp(ctx, result), XMM0);
	for (int i = 0; i < 16; i++)
		e->MOVUPS((X64Reg)(XMM0 + i), MDisp(RSP, xmmOffset + i * 16));
	e->MOV(64, R(RSP), MDisp(RSP, savedRsp));
	e->JMP(epilogue, true);
	return start;
}

}  // namespace MIPSComp

#endif
