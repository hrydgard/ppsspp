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
#include "GPU/GPUState.h"
#include "GPU/Software/Sampler.h"
#include "GPU/ge_constants.h"

using namespace Gen;

extern u32 clut[4096];

namespace Sampler {

#ifdef _WIN32
static const X64Reg resultReg = RAX;
static const X64Reg tempReg1 = R10;
static const X64Reg tempReg2 = R11;
static const X64Reg uReg = RCX;
static const X64Reg vReg = RDX;
static const X64Reg srcReg = R8;
static const X64Reg bufwReg = R9;
// TODO: levelReg on stack
#else
static const X64Reg resultReg = RAX;
static const X64Reg tempReg1 = R9;
static const X64Reg tempReg2 = R10;
static const X64Reg uReg = RDI;
static const X64Reg vReg = RSI
static const X64Reg srcReg = RDX;
static const X64Reg bufwReg = RCX;
static const X64Reg levelReg = R8;
#endif

static const X64Reg fpScratchReg1 = XMM1;
static const X64Reg fpScratchReg2 = XMM2;
static const X64Reg fpScratchReg3 = XMM3;
static const X64Reg fpScratchReg4 = XMM4;

NearestFunc SamplerJitCache::Compile(const SamplerID &id) {
	BeginWrite();
	const u8 *start = this->AlignCode16();

	SUB(PTRBITS, R(ESP), Imm8(64));
	MOVUPS(MDisp(ESP, 0), XMM4);
	MOVUPS(MDisp(ESP, 16), XMM5);
	MOVUPS(MDisp(ESP, 32), XMM6);
	MOVUPS(MDisp(ESP, 48), XMM7);

	// Early exit on !srcPtr.
	CMP(PTRBITS, R(srcReg), Imm32(0));
	FixupBranch nonZeroSrc = J_CC(CC_NZ);
	XOR(32, R(RAX), R(RAX));
	FixupBranch zeroSrc = J(true);
	SetJumpTarget(nonZeroSrc);

	if (!Jit_ReadTextureFormat(id)) {
		EndWrite();
		SetCodePtr(const_cast<u8 *>(start));
		return nullptr;
	}

	SetJumpTarget(zeroSrc);

	MOVUPS(XMM4, MDisp(ESP, 0));
	MOVUPS(XMM5, MDisp(ESP, 16));
	MOVUPS(XMM6, MDisp(ESP, 32));
	MOVUPS(XMM7, MDisp(ESP, 48));
	ADD(PTRBITS, R(ESP), Imm8(64));

	RET();

	EndWrite();
	return (NearestFunc)start;
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
		AND(32, R(resultReg), Imm32(0x0000000F));
		break;
	}

	default:
		return false;
	}

	return true;
}

bool SamplerJitCache::Jit_GetTexDataSwizzled(const SamplerID &id, int bitsPerTexel) {
	LEA(32, tempReg1, MScaled(vReg, SCALE_4, 0));
	AND(32, R(tempReg1), Imm32(31));
	if (bitsPerTexel != 4) {
		AND(32, R(vReg), Imm32(~7));
	}

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
	case 4:
		SHR(32, R(vReg), Imm8(3));
		SHR(32, R(tempReg2), Imm8(3));
		SHR(32, R(resultReg), Imm8(5));
		break;
	default:
		return false;
	}
	AND(32, R(tempReg2), Imm32(3));
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
		AND(32, R(uReg), Imm32(1));
		// Multiply by two by just adding twice.
		ADD(32, R(EAX), R(uReg));
		ADD(32, R(EAX), R(uReg));
		MOVZX(32, bitsPerTexel, resultReg, MRegSum(tempReg1, EAX));
		break;
	case 8:
		AND(32, R(uReg), Imm32(3));
		ADD(32, R(EAX), R(uReg));
		MOVZX(32, bitsPerTexel, resultReg, MRegSum(tempReg1, EAX));
		break;
	case 4: {
		AND(32, R(uReg), Imm32(7));
		SHR(32, R(uReg), Imm8(1));
		// Note: LEA/MOV do not affect flags (unlike ADD.)  uReg may be RCX.
		LEA(64, tempReg1, MRegSum(tempReg1, uReg));
		MOV(8, R(resultReg), MRegSum(tempReg1, EAX));
		FixupBranch skipNonZero = J_CC(CC_NC);
		SHR(8, R(resultReg), Imm8(4));
		SetJumpTarget(skipNonZero);
		// Zero out the rest.
		AND(32, R(resultReg), Imm32(0x0000000F));
		break;
	}
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

static const u32 MEMORY_ALIGNED16(color4444mask[4]) = { 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, };

bool SamplerJitCache::Jit_Decode4444() {
	MOVD_xmm(fpScratchReg1, R(resultReg));
	PUNPCKLBW(fpScratchReg1, R(fpScratchReg1));
	PAND(fpScratchReg1, M(color4444mask));
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
		AND(32, R(RCX), Imm32(0x0000001F));
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
		// TODO: Load level, SHL 4, and add to resultReg.
		return false;
	}

	MOV(PTRBITS, R(tempReg1), ImmPtr(clut));

	switch (gstate.getClutPaletteFormat()) {
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
