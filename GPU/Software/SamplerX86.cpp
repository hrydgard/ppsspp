// Copyright (c) 2017- PPSSPP Project.

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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <emmintrin.h>
#include "Common/x64Emitter.h"
#include "Common/CPUDetect.h"
#include "GPU/GPUState.h"
#include "GPU/Software/Sampler.h"
#include "GPU/ge_constants.h"

using namespace Gen;

extern u32 clut[4096];

namespace Sampler {

#ifdef _WIN32
static const X64Reg arg1Reg = RCX;
static const X64Reg arg2Reg = RDX;
static const X64Reg arg3Reg = R8;
static const X64Reg arg4Reg = R9;
// 5 and 6 are on the stack.
#else
static const X64Reg arg1Reg = RDI;
static const X64Reg arg2Reg = RSI;
static const X64Reg arg3Reg = RDX;
static const X64Reg arg4Reg = RCX;
static const X64Reg arg5Reg = R8;
static const X64Reg arg6Reg = R9;

static const X64Reg levelReg = arg5Reg;
#endif

static const X64Reg resultReg = RAX;
static const X64Reg tempReg1 = R10;
static const X64Reg tempReg2 = R11;

static const X64Reg uReg = arg1Reg;
static const X64Reg vReg = arg2Reg;
static const X64Reg srcReg = arg3Reg;
static const X64Reg bufwReg = arg4Reg;

static const X64Reg fpScratchReg1 = XMM1;
static const X64Reg fpScratchReg2 = XMM2;
static const X64Reg fpScratchReg3 = XMM3;
static const X64Reg fpScratchReg4 = XMM4;
static const X64Reg fpScratchReg5 = XMM5;

NearestFunc SamplerJitCache::Compile(const SamplerID &id) {
	BeginWrite();
	const u8 *start = AlignCode16();

	// Early exit on !srcPtr.
	FixupBranch zeroSrc;
	if (id.hasInvalidPtr) {
		CMP(PTRBITS, R(srcReg), Imm8(0));
		FixupBranch nonZeroSrc = J_CC(CC_NZ);
		XOR(32, R(RAX), R(RAX));
		zeroSrc = J(true);
		SetJumpTarget(nonZeroSrc);
	}

	if (!Jit_ReadTextureFormat(id)) {
		EndWrite();
		SetCodePtr(const_cast<u8 *>(start));
		return nullptr;
	}

	if (id.hasInvalidPtr) {
		SetJumpTarget(zeroSrc);
	}

	RET();

	EndWrite();
	return (NearestFunc)start;
}

alignas(16) static const float by256[4] = { 1.0f / 256.0f, 1.0f / 256.0f, 1.0f / 256.0f, 1.0f / 256.0f, };
alignas(16) static const float ones[4] = { 1.0f, 1.0f, 1.0f, 1.0f, };

LinearFunc SamplerJitCache::CompileLinear(const SamplerID &id) {
	_assert_msg_(G3D, id.linear, "Linear should be set on sampler id");
	BeginWrite();

	// We'll first write the nearest sampler, which we will CALL.
	// This may differ slightly based on the "linear" flag.
	const u8 *nearest = AlignCode16();

	if (!Jit_ReadTextureFormat(id)) {
		EndWrite();
		SetCodePtr(const_cast<u8 *>(nearest));
		return nullptr;
	}

	RET();

	// Now the actual linear func, which is exposed externally.
	const u8 *start = AlignCode16();

	// NOTE: This doesn't use the general register mapping.
	// POSIX: arg1=uptr, arg2=vptr, arg3=frac_u, arg4=frac_v, arg5=src, arg6=bufw, stack+8=level
	// Win64: arg1=uptr, arg2=vptr, arg3=frac_u, arg4=frac_v, stack+40=src, stack+48=bufw, stack+56=level
	//
	// We map these to nearest CALLs, with order: u, v, src, bufw, level

	// Let's start by saving a bunch of registers.
	PUSH(R15);
	PUSH(R14);
	PUSH(R13);
	PUSH(R12);
	// Won't need frac_u/frac_v for a while.
	PUSH(arg4Reg);
	PUSH(arg3Reg);
	// Extra space to restore alignment and save resultReg for lerp.
	// TODO: Maybe use XMMs instead?
	SUB(64, R(RSP), Imm8(24));

	MOV(64, R(R12), R(arg1Reg));
	MOV(64, R(R13), R(arg2Reg));
#ifdef _WIN32
	// First arg now starts at 24 (extra space) + 48 (pushed stack) + 8 (ret address) + 32 (shadow space)
	const int argOffset = 24 + 48 + 8 + 32;
	MOV(64, R(R14), MDisp(RSP, argOffset));
	MOV(32, R(R15), MDisp(RSP, argOffset + 8));
	// level is at argOffset + 16.
#else
	MOV(64, R(R14), R(arg5Reg));
	MOV(32, R(R15), R(arg6Reg));
	// level is at 24 + 48 + 8.
#endif

	// Early exit on !srcPtr.
	FixupBranch zeroSrc;
	if (id.hasInvalidPtr) {
		CMP(PTRBITS, R(R14), Imm8(0));
		FixupBranch nonZeroSrc = J_CC(CC_NZ);
		XOR(32, R(RAX), R(RAX));
		zeroSrc = J(true);
		SetJumpTarget(nonZeroSrc);
	}

	// At this point:
	// R12=uptr, R13=vptr, stack+24=frac_u, stack+32=frac_v, R14=src, R15=bufw, stack+X=level

	auto doNearestCall = [&](int off) {
		MOV(32, R(uReg), MDisp(R12, off));
		MOV(32, R(vReg), MDisp(R13, off));
		MOV(64, R(srcReg), R(R14));
		MOV(32, R(bufwReg), R(R15));
		// Leave level, we just always load from RAM.  Separate CLUTs is uncommon.

		CALL(nearest);
		MOV(32, MDisp(RSP, off), R(resultReg));
	};

	doNearestCall(0);
	doNearestCall(4);
	doNearestCall(8);
	doNearestCall(12);

	// Convert TL, TR, BL, BR to floats for easier blending.
	if (!cpu_info.bSSE4_1) {
		PXOR(XMM0, R(XMM0));
	}

	MOVD_xmm(fpScratchReg1, MDisp(RSP, 0));
	MOVD_xmm(fpScratchReg2, MDisp(RSP, 4));
	MOVD_xmm(fpScratchReg3, MDisp(RSP, 8));
	MOVD_xmm(fpScratchReg4, MDisp(RSP, 12));

	if (cpu_info.bSSE4_1) {
		PMOVZXBD(fpScratchReg1, R(fpScratchReg1));
		PMOVZXBD(fpScratchReg2, R(fpScratchReg2));
		PMOVZXBD(fpScratchReg3, R(fpScratchReg3));
		PMOVZXBD(fpScratchReg4, R(fpScratchReg4));
	} else {
		PUNPCKLBW(fpScratchReg1, R(XMM0));
		PUNPCKLBW(fpScratchReg2, R(XMM0));
		PUNPCKLBW(fpScratchReg3, R(XMM0));
		PUNPCKLBW(fpScratchReg4, R(XMM0));
		PUNPCKLWD(fpScratchReg1, R(XMM0));
		PUNPCKLWD(fpScratchReg2, R(XMM0));
		PUNPCKLWD(fpScratchReg3, R(XMM0));
		PUNPCKLWD(fpScratchReg4, R(XMM0));
	}
	CVTDQ2PS(fpScratchReg1, R(fpScratchReg1));
	CVTDQ2PS(fpScratchReg2, R(fpScratchReg2));
	CVTDQ2PS(fpScratchReg3, R(fpScratchReg3));
	CVTDQ2PS(fpScratchReg4, R(fpScratchReg4));

	// Okay, now multiply the R sides by frac_u, and L by (256 - frac_u)...
	MOVD_xmm(fpScratchReg5, MDisp(RSP, 24));
	CVTDQ2PS(fpScratchReg5, R(fpScratchReg5));
	SHUFPS(fpScratchReg5, R(fpScratchReg5), _MM_SHUFFLE(0, 0, 0, 0));
	if (RipAccessible(by256)) {
		MULPS(fpScratchReg5, M(by256));
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(by256));
		MULPS(fpScratchReg5, MatR(tempReg1));
	}

	if (RipAccessible(ones)) {
		MOVAPS(XMM0, M(ones));
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(ones));
		MOVAPS(XMM0, MatR(tempReg1));
	}
	SUBPS(XMM0, R(fpScratchReg5));

	MULPS(fpScratchReg1, R(XMM0));
	MULPS(fpScratchReg2, R(fpScratchReg5));
	MULPS(fpScratchReg3, R(XMM0));
	MULPS(fpScratchReg4, R(fpScratchReg5));

	// Now set top=fpScratchReg1, bottom=fpScratchReg3.
	ADDPS(fpScratchReg1, R(fpScratchReg2));
	ADDPS(fpScratchReg3, R(fpScratchReg4));

	// Next, time for frac_v.
	MOVD_xmm(fpScratchReg5, MDisp(RSP, 32));
	CVTDQ2PS(fpScratchReg5, R(fpScratchReg5));
	SHUFPS(fpScratchReg5, R(fpScratchReg5), _MM_SHUFFLE(0, 0, 0, 0));
	if (RipAccessible(ones)) {
		MULPS(fpScratchReg5, M(by256));
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(by256));
		MULPS(fpScratchReg5, MatR(tempReg1));
	}
	if (RipAccessible(ones)) {
		MOVAPS(XMM0, M(ones));
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(ones));
		MOVAPS(XMM0, MatR(tempReg1));
	}
	SUBPS(XMM0, R(fpScratchReg5));

	MULPS(fpScratchReg1, R(XMM0));
	MULPS(fpScratchReg3, R(fpScratchReg5));

	// Still at the 255 scale, now we're interpolated.
	ADDPS(fpScratchReg1, R(fpScratchReg3));

	// Time to convert back to a single 32 bit value.
	CVTPS2DQ(fpScratchReg1, R(fpScratchReg1));
	PACKSSDW(fpScratchReg1, R(fpScratchReg1));
	PACKUSWB(fpScratchReg1, R(fpScratchReg1));
	MOVD_xmm(R(resultReg), fpScratchReg1);

	if (id.hasInvalidPtr) {
		SetJumpTarget(zeroSrc);
	}

	ADD(64, R(RSP), Imm8(24));
	POP(arg3Reg);
	POP(arg4Reg);
	POP(R12);
	POP(R13);
	POP(R14);
	POP(R15);

	RET();

	EndWrite();
	return (LinearFunc)start;
}

bool SamplerJitCache::Jit_ReadTextureFormat(const SamplerID &id) {
	GETextureFormat fmt = (GETextureFormat)id.texfmt;
	bool success = true;
	switch (fmt) {
	case GE_TFMT_5650:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode5650();
		break;

	case GE_TFMT_5551:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode5551();
		break;

	case GE_TFMT_4444:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode4444();
		break;

	case GE_TFMT_8888:
		success = Jit_GetTexData(id, 32);
		break;

	case GE_TFMT_CLUT32:
		success = Jit_GetTexData(id, 32);
		if (success)
			success = Jit_TransformClutIndex(id, 32);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	case GE_TFMT_CLUT16:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_TransformClutIndex(id, 16);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	case GE_TFMT_CLUT8:
		success = Jit_GetTexData(id, 8);
		if (success)
			success = Jit_TransformClutIndex(id, 8);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	case GE_TFMT_CLUT4:
		success = Jit_GetTexData(id, 4);
		if (success)
			success = Jit_TransformClutIndex(id, 4);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	// TODO: DXT?
	default:
		success = false;
	}

	return success;
}

bool SamplerJitCache::Jit_GetTexData(const SamplerID &id, int bitsPerTexel) {
	if (id.swizzle) {
		return Jit_GetTexDataSwizzled(id, bitsPerTexel);
	}

	// srcReg might be EDX, so let's copy that before we multiply.
	switch (bitsPerTexel) {
	case 32:
	case 16:
	case 8:
		LEA(64, tempReg1, MComplex(srcReg, uReg, bitsPerTexel / 8, 0));
		break;

	case 4: {
		XOR(32, R(tempReg2), R(tempReg2));
		SHR(32, R(uReg), Imm8(1));
		FixupBranch skip = J_CC(CC_NC);
		// Track whether we shifted a 1 off or not.
		MOV(32, R(tempReg2), Imm32(4));
		SetJumpTarget(skip);
		LEA(64, tempReg1, MRegSum(srcReg, uReg));
		break;
	}

	default:
		return false;
	}

	MOV(32, R(EAX), R(vReg));
	MUL(32, R(bufwReg));

	switch (bitsPerTexel) {
	case 32:
	case 16:
	case 8:
		MOVZX(32, bitsPerTexel, resultReg, MComplex(tempReg1, RAX, bitsPerTexel / 8, 0));
		break;

	case 4: {
		SHR(32, R(RAX), Imm8(1));
		MOV(8, R(resultReg), MRegSum(tempReg1, RAX));
		// RCX is now free.
		MOV(8, R(RCX), R(tempReg2));
		SHR(8, R(resultReg), R(RCX));
		// Zero out any bits not shifted off.
		AND(32, R(resultReg), Imm8(0x0F));
		break;
	}

	default:
		return false;
	}

	return true;
}

bool SamplerJitCache::Jit_GetTexDataSwizzled4() {
	// Get the horizontal tile pos into tempReg1.
	LEA(32, tempReg1, MScaled(uReg, SCALE_4, 0));
	// Note: imm8 sign extends negative.
	AND(32, R(tempReg1), Imm8(~127));

	// Add vertical offset inside tile to tempReg1.
	LEA(32, tempReg2, MScaled(vReg, SCALE_4, 0));
	AND(32, R(tempReg2), Imm8(31));
	LEA(32, tempReg1, MComplex(tempReg1, tempReg2, SCALE_4, 0));
	// Add srcReg, since we'll need it at some point.
	ADD(64, R(tempReg1), R(srcReg));

	// Now find the vertical tile pos, and add to tempReg1.
	SHR(32, R(vReg), Imm8(3));
	LEA(32, EAX, MScaled(bufwReg, SCALE_4, 0));
	MUL(32, R(vReg));
	ADD(64, R(tempReg1), R(EAX));

	// Last and possible also least, the horizontal offset inside the tile.
	AND(32, R(uReg), Imm8(31));
	SHR(32, R(uReg), Imm8(1));
	MOV(8, R(resultReg), MRegSum(tempReg1, uReg));
	FixupBranch skipNonZero = J_CC(CC_NC);
	// If the horizontal offset was odd, take the upper 4.
	SHR(8, R(resultReg), Imm8(4));
	SetJumpTarget(skipNonZero);
	// Zero out the rest of the bits.
	AND(32, R(resultReg), Imm8(0x0F));

	return true;
}

bool SamplerJitCache::Jit_GetTexDataSwizzled(const SamplerID &id, int bitsPerTexel) {
	if (bitsPerTexel == 4) {
		// Specialized implementation.
		return Jit_GetTexDataSwizzled4();
	}

	LEA(32, tempReg1, MScaled(vReg, SCALE_4, 0));
	AND(32, R(tempReg1), Imm8(31));
	AND(32, R(vReg), Imm8(~7));

	MOV(32, R(tempReg2), R(uReg));
	MOV(32, R(resultReg), R(uReg));
	switch (bitsPerTexel) {
	case 32:
		SHR(32, R(resultReg), Imm8(2));
		break;
	case 16:
		SHR(32, R(vReg), Imm8(1));
		SHR(32, R(tempReg2), Imm8(1));
		SHR(32, R(resultReg), Imm8(3));
		break;
	case 8:
		SHR(32, R(vReg), Imm8(2));
		SHR(32, R(tempReg2), Imm8(2));
		SHR(32, R(resultReg), Imm8(4));
		break;
	default:
		return false;
	}
	AND(32, R(tempReg2), Imm8(3));
	SHL(32, R(resultReg), Imm8(5));
	ADD(32, R(tempReg1), R(tempReg2));
	ADD(32, R(tempReg1), R(resultReg));

	// We may clobber srcReg in the MUL, so let's grab it now.
	LEA(64, tempReg1, MComplex(srcReg, tempReg1, SCALE_4, 0));

	LEA(32, EAX, MScaled(bufwReg, SCALE_4, 0));
	MUL(32, R(vReg));

	switch (bitsPerTexel) {
	case 32:
		MOV(bitsPerTexel, R(resultReg), MRegSum(tempReg1, EAX));
		break;
	case 16:
		AND(32, R(uReg), Imm8(1));
		// Multiply by two by just adding twice.
		ADD(32, R(EAX), R(uReg));
		ADD(32, R(EAX), R(uReg));
		MOVZX(32, bitsPerTexel, resultReg, MRegSum(tempReg1, EAX));
		break;
	case 8:
		AND(32, R(uReg), Imm8(3));
		ADD(32, R(EAX), R(uReg));
		MOVZX(32, bitsPerTexel, resultReg, MRegSum(tempReg1, EAX));
		break;
	default:
		return false;
	}

	return true;
}

bool SamplerJitCache::Jit_Decode5650() {
	MOV(32, R(tempReg2), R(resultReg));
	AND(32, R(tempReg2), Imm32(0x0000001F));

	// B (we do R and B at the same time, they're both 5.)
	MOV(32, R(tempReg1), R(resultReg));
	AND(32, R(tempReg1), Imm32(0x0000F800));
	SHL(32, R(tempReg1), Imm8(5));
	OR(32, R(tempReg2), R(tempReg1));

	// Expand 5 -> 8.  At this point we have 00BB00RR.
	MOV(32, R(tempReg1), R(tempReg2));
	SHL(32, R(tempReg2), Imm8(3));
	SHR(32, R(tempReg1), Imm8(2));
	OR(32, R(tempReg2), R(tempReg1));
	AND(32, R(tempReg2), Imm32(0x00FF00FF));

	// Now's as good a time to put in A as any.
	OR(32, R(tempReg2), Imm32(0xFF000000));

	// Last, we need to align, extract, and expand G.
	// 3 to align to G, and then 2 to expand to 8.
	SHL(32, R(resultReg), Imm8(3 + 2));
	AND(32, R(resultReg), Imm32(0x0000FC00));
	MOV(32, R(tempReg1), R(resultReg));
	// 2 to account for resultReg being preshifted, 4 for expansion.
	SHR(32, R(tempReg1), Imm8(2 + 4));
	OR(32, R(resultReg), R(tempReg1));
	AND(32, R(resultReg), Imm32(0x0000FF00));
	OR(32, R(resultReg), R(tempReg2));;

	return true;
}

bool SamplerJitCache::Jit_Decode5551() {
	MOV(32, R(tempReg2), R(resultReg));
	MOV(32, R(tempReg1), R(resultReg));
	AND(32, R(tempReg2), Imm32(0x0000001F));
	AND(32, R(tempReg1), Imm32(0x000003E0));
	SHL(32, R(tempReg1), Imm8(3));
	OR(32, R(tempReg2), R(tempReg1));

	MOV(32, R(tempReg1), R(resultReg));
	AND(32, R(tempReg1), Imm32(0x00007C00));
	SHL(32, R(tempReg1), Imm8(6));
	OR(32, R(tempReg2), R(tempReg1));

	// Expand 5 -> 8.  After this is just A.
	MOV(32, R(tempReg1), R(tempReg2));
	SHL(32, R(tempReg2), Imm8(3));
	SHR(32, R(tempReg1), Imm8(2));
	// Chop off the bits that were shifted out.
	AND(32, R(tempReg1), Imm32(0x00070707));
	OR(32, R(tempReg2), R(tempReg1));

	// For A, we shift it to a single bit, and then subtract and XOR.
	// That's probably the simplest way to expand it...
	SHR(32, R(resultReg), Imm8(15));
	// If it was 0, it's now -1, otherwise it's 0.  Easy.
	SUB(32, R(resultReg), Imm8(1));
	XOR(32, R(resultReg), Imm32(0xFF000000));
	AND(32, R(resultReg), Imm32(0xFF000000));
	OR(32, R(resultReg), R(tempReg2));

	return true;
}

alignas(16) static const u32 color4444mask[4] = { 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, };

bool SamplerJitCache::Jit_Decode4444() {
	MOVD_xmm(fpScratchReg1, R(resultReg));
	PUNPCKLBW(fpScratchReg1, R(fpScratchReg1));
	if (RipAccessible(color4444mask)) {
		PAND(fpScratchReg1, M(color4444mask));
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(color4444mask));
		PAND(fpScratchReg1, MatR(tempReg1));
	}
	MOVSS(fpScratchReg2, R(fpScratchReg1));
	MOVSS(fpScratchReg3, R(fpScratchReg1));
	PSRLW(fpScratchReg2, 4);
	PSLLW(fpScratchReg3, 4);
	POR(fpScratchReg1, R(fpScratchReg2));
	POR(fpScratchReg1, R(fpScratchReg3));
	MOVD_xmm(R(resultReg), fpScratchReg1);
	return true;
}

bool SamplerJitCache::Jit_TransformClutIndex(const SamplerID &id, int bitsPerIndex) {
	GEPaletteFormat fmt = (GEPaletteFormat)id.clutfmt;
	if (!id.hasClutShift && !id.hasClutMask && !id.hasClutOffset) {
		// This is simple - just mask if necessary.
		if (bitsPerIndex > 8) {
			AND(32, R(resultReg), Imm32(0x000000FF));
		}
		return true;
	}

	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate.clutformat));
	MOV(32, R(tempReg1), MatR(tempReg1));

	// Shift = (clutformat >> 2) & 0x1F
	if (id.hasClutShift) {
		MOV(32, R(RCX), R(tempReg1));
		SHR(32, R(RCX), Imm8(2));
		AND(32, R(RCX), Imm8(0x1F));
		SHR(32, R(resultReg), R(RCX));
	}

	// Mask = (clutformat >> 8) & 0xFF
	if (id.hasClutMask) {
		MOV(32, R(tempReg2), R(tempReg1));
		SHR(32, R(tempReg2), Imm8(8));
		AND(32, R(resultReg), R(tempReg2));
	}

	// We need to wrap any entries beyond the first 1024 bytes.
	u32 offsetMask = fmt == GE_CMODE_32BIT_ABGR8888 ? 0x00FF : 0x01FF;

	// We must mask to 0xFF before ORing 0x100 in 16 bit CMODEs.
	// But skip if we'll mask 0xFF after offset anyway.
	if (bitsPerIndex > 8 && (!id.hasClutOffset || offsetMask != 0x00FF)) {
		AND(32, R(resultReg), Imm32(0x000000FF));
	}

	// Offset = (clutformat >> 12) & 0x01F0
	if (id.hasClutOffset) {
		SHR(32, R(tempReg1), Imm8(16));
		SHL(32, R(tempReg1), Imm8(4));
		OR(32, R(resultReg), R(tempReg1));
		AND(32, R(resultReg), Imm32(offsetMask));
	}
	return true;
}

bool SamplerJitCache::Jit_ReadClutColor(const SamplerID &id) {
	if (!id.useSharedClut) {
		// TODO: Need to load from RAM, always.
		if (id.linear) {
#ifdef _WIN32
			const int argOffset = 24 + 48 + 8 + 32;
			// Extra 8 to account for CALL.
			MOV(32, R(tempReg2), MDisp(RSP, argOffset + 16 + 8));
#else
			// Extra 8 to account for CALL.
			MOV(32, R(tempReg2), MDisp(RSP, 24 + 48 + 8 + 8));
#endif
			LEA(32, tempReg2, MScaled(tempReg2, SCALE_4, 0));
		} else {
#ifdef _WIN32
			// The argument was saved on the stack.
			MOV(32, R(tempReg2), MDisp(RSP, 40));
			LEA(32, tempReg2, MScaled(tempReg2, SCALE_4, 0));
#else
			// We need to multiply by 16 and add, LEA allows us to copy too.
			LEA(32, tempReg2, MScaled(levelReg, SCALE_4, 0));
#endif
		}

		// Second step of the multiply by 16 (since we only multiplied by 4 before.)
		LEA(64, resultReg, MComplex(resultReg, tempReg2, SCALE_4, 0));
	}

	MOV(PTRBITS, R(tempReg1), ImmPtr(clut));

	switch ((GEPaletteFormat)id.clutfmt) {
	case GE_CMODE_16BIT_BGR5650:
		MOVZX(32, 16, resultReg, MComplex(tempReg1, resultReg, SCALE_2, 0));
		return Jit_Decode5650();

	case GE_CMODE_16BIT_ABGR5551:
		MOVZX(32, 16, resultReg, MComplex(tempReg1, resultReg, SCALE_2, 0));
		return Jit_Decode5551();

	case GE_CMODE_16BIT_ABGR4444:
		MOVZX(32, 16, resultReg, MComplex(tempReg1, resultReg, SCALE_2, 0));
		return Jit_Decode4444();

	case GE_CMODE_32BIT_ABGR8888:
		MOV(32, R(resultReg), MComplex(tempReg1, resultReg, SCALE_4, 0));
		return true;

	default:
		return false;
	}
}

};

#endif
