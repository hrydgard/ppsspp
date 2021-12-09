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
using namespace Rasterizer;

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
#endif

static const X64Reg fpScratchReg1 = XMM1;
static const X64Reg fpScratchReg2 = XMM2;
static const X64Reg fpScratchReg3 = XMM3;
static const X64Reg fpScratchReg4 = XMM4;
static const X64Reg fpScratchReg5 = XMM5;

NearestFunc SamplerJitCache::Compile(const SamplerID &id) {
	regCache_.SetupABI({
		RegCache::GEN_ARG_U,
		RegCache::GEN_ARG_V,
		RegCache::GEN_ARG_TEXPTR,
		RegCache::GEN_ARG_BUFW,
		RegCache::GEN_ARG_LEVEL,
	});
	regCache_.ChangeReg(RAX, RegCache::GEN_RESULT);
	regCache_.ChangeReg(XMM0, RegCache::VEC_RESULT);

	BeginWrite();
	const u8 *start = AlignCode16();

	// Early exit on !srcPtr.
	FixupBranch zeroSrc;
	if (id.hasInvalidPtr) {
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		CMP(PTRBITS, R(srcReg), Imm8(0));
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);

		FixupBranch nonZeroSrc = J_CC(CC_NZ);
		X64Reg vecResultReg = regCache_.Find(RegCache::VEC_RESULT);
		PXOR(vecResultReg, R(vecResultReg));
		regCache_.Unlock(vecResultReg, RegCache::VEC_RESULT);
		zeroSrc = J(true);
		SetJumpTarget(nonZeroSrc);
	}

	// This reads the pixel data into resultReg from the args.
	if (!Jit_ReadTextureFormat(id)) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(start));
		return nullptr;
	}

	X64Reg vecResultReg = regCache_.Find(RegCache::VEC_RESULT);

	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	MOVD_xmm(vecResultReg, R(resultReg));
	regCache_.Release(resultReg, RegCache::GEN_RESULT);

	if (cpu_info.bSSE4_1) {
		PMOVZXBD(vecResultReg, R(vecResultReg));
	} else {
		X64Reg vecTempReg = regCache_.Find(RegCache::VEC_TEMP0);
		PXOR(vecTempReg, R(vecTempReg));
		PUNPCKLBW(vecResultReg, R(vecTempReg));
		PUNPCKLWD(vecResultReg, R(vecTempReg));
		regCache_.Unlock(vecTempReg, RegCache::VEC_TEMP0);
	}
	regCache_.Unlock(vecResultReg, RegCache::VEC_RESULT);

	if (id.hasInvalidPtr) {
		SetJumpTarget(zeroSrc);
	}

	RET();

	regCache_.Reset(true);

	EndWrite();
	return (NearestFunc)start;
}

alignas(16) static const float by256[4] = { 1.0f / 256.0f, 1.0f / 256.0f, 1.0f / 256.0f, 1.0f / 256.0f, };
alignas(16) static const float ones[4] = { 1.0f, 1.0f, 1.0f, 1.0f, };

LinearFunc SamplerJitCache::CompileLinear(const SamplerID &id) {
	_assert_msg_(id.linear, "Linear should be set on sampler id");
	BeginWrite();

	regCache_.SetupABI({
		RegCache::GEN_ARG_U,
		RegCache::GEN_ARG_V,
		RegCache::GEN_ARG_TEXPTR,
		RegCache::GEN_ARG_BUFW,
	});
	regCache_.ChangeReg(RAX, RegCache::GEN_RESULT);

	// We'll first write the nearest sampler, which we will CALL.
	// This may differ slightly based on the "linear" flag.
	const u8 *nearest = AlignCode16();

	if (!Jit_ReadTextureFormat(id)) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(nearest));
		return nullptr;
	}

	RET();

	regCache_.Reset(true);

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
		PXOR(XMM0, R(XMM0));
		zeroSrc = J(true);
		SetJumpTarget(nonZeroSrc);
	}

	// At this point:
	// R12=uptr, R13=vptr, stack+24=frac_u, stack+32=frac_v, R14=src, R15=bufw, stack+X=level

	// This stores the result on the stack for later processing.
	auto doNearestCall = [&](int off) {
		static const X64Reg uReg = arg1Reg;
		static const X64Reg vReg = arg2Reg;
		static const X64Reg srcReg = arg3Reg;
		static const X64Reg bufwReg = arg4Reg;
		static const X64Reg resultReg = RAX;

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
		X64Reg tempReg = RAX;
		MOV(PTRBITS, R(tempReg), ImmPtr(by256));
		MULPS(fpScratchReg5, MatR(tempReg));
	}

	if (RipAccessible(ones)) {
		MOVAPS(XMM0, M(ones));
	} else {
		X64Reg tempReg = RAX;
		MOV(PTRBITS, R(tempReg), ImmPtr(ones));
		MOVAPS(XMM0, MatR(tempReg));
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
		X64Reg tempReg = RAX;
		MOV(PTRBITS, R(tempReg), ImmPtr(by256));
		MULPS(fpScratchReg5, MatR(tempReg));
	}
	if (RipAccessible(ones)) {
		MOVAPS(XMM0, M(ones));
	} else {
		X64Reg tempReg = RAX;
		MOV(PTRBITS, R(tempReg), ImmPtr(ones));
		MOVAPS(XMM0, MatR(tempReg));
	}
	SUBPS(XMM0, R(fpScratchReg5));

	MULPS(fpScratchReg1, R(XMM0));
	MULPS(fpScratchReg3, R(fpScratchReg5));

	// Still at the 255 scale, now we're interpolated.
	ADDPS(fpScratchReg1, R(fpScratchReg3));

	// Time to convert back to a single 32 bit value.
	CVTPS2DQ(XMM0, R(fpScratchReg1));

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
	GETextureFormat fmt = id.TexFmt();
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

	case GE_TFMT_DXT1:
		success = Jit_GetDXT1Color(id, 8, 255);
		break;

	case GE_TFMT_DXT3:
		success = Jit_GetDXT1Color(id, 16, 0);
		if (success)
			success = Jit_ApplyDXTAlpha(id);
		break;

	case GE_TFMT_DXT5:
		success = Jit_GetDXT1Color(id, 16, 0);
		if (success)
			success = Jit_ApplyDXTAlpha(id);
		break;

	default:
		success = false;
	}

	return success;
}

// Note: afterward, srcReg points at the block, and uReg/vReg have offset into block.
bool SamplerJitCache::Jit_GetDXT1Color(const SamplerID &id, int blockSize, int alpha) {
	// Like Jit_GetTexData, this gets the color into resultReg.
	// Note: color low bits are red, high bits are blue.
	_assert_msg_(blockSize == 8 || blockSize == 16, "Invalid DXT block size");

	// First, we need to get the block's offset, which is:
	// blockPos = src + (v/4 * bufw/4 + u/4) * blockSize
	// We distribute the blockSize constant for convenience:
	// blockPos = src + (blockSize*v/4 * bufw/4 + blockSize*u/4)

	// Copy u (we'll need it later), and round down to the nearest 4 after scaling.
	X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
	X64Reg srcBaseReg = regCache_.Alloc(RegCache::GEN_TEMP0);
	LEA(32, srcBaseReg, MScaled(uReg, blockSize / 4, 0));
	AND(32, R(srcBaseReg), Imm32(blockSize == 8 ? ~7 : ~15));
	// Add in srcReg already, since we'll be multiplying soon.
	X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	ADD(64, R(srcBaseReg), R(srcReg));

	X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);
	X64Reg srcOffsetReg = regCache_.Alloc(RegCache::GEN_TEMP1);
	LEA(32, srcOffsetReg, MScaled(vReg, blockSize / 4, 0));
	AND(32, R(srcOffsetReg), Imm32(blockSize == 8 ? ~7 : ~15));
	// Modify bufw in place and then multiply.
	X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW);
	SHR(32, R(bufwReg), Imm8(2));
	IMUL(32, srcOffsetReg, R(bufwReg));
	regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW);
	// We no longer need bufwReg.
	regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);

	// And now let's chop off the offset for u and v.
	AND(32, R(uReg), Imm32(3));
	AND(32, R(vReg), Imm32(3));

	// Okay, at this point srcBaseReg + srcOffsetReg = blockPos.  To free up regs, put back in srcReg.
	LEA(64, srcReg, MRegSum(srcBaseReg, srcOffsetReg));
	regCache_.Release(srcBaseReg, RegCache::GEN_TEMP0);
	regCache_.Release(srcOffsetReg, RegCache::GEN_TEMP1);

	// The colorIndex is simply the 2 bits at blockPos + (v & 3), shifted right by (u & 3) twice.
	X64Reg colorIndexReg = regCache_.Alloc(RegCache::GEN_TEMP0);
	MOVZX(32, 8, colorIndexReg, MRegSum(srcReg, vReg));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
	regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
	// Only DXT1 needs this reg later.
	if (id.TexFmt() == GE_TFMT_DXT1)
		regCache_.ForceRelease(RegCache::GEN_ARG_V);

	if (uReg == ECX) {
		SHR(32, R(colorIndexReg), R(CL));
		SHR(32, R(colorIndexReg), R(CL));
	} else {
		bool hasRCX = regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
		_assert_(hasRCX);
		LEA(32, ECX, MScaled(uReg, SCALE_2, 0));
		SHR(32, R(colorIndexReg), R(CL));
	}
	regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
	// If DXT1, there's no alpha and we can toss this reg.
	if (id.TexFmt() == GE_TFMT_DXT1)
		regCache_.ForceRelease(RegCache::GEN_ARG_U);
	AND(32, R(colorIndexReg), Imm32(3));

	X64Reg color1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg color2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);

	// For colorIndex 0 or 1, we'll simply take the 565 color and convert.
	CMP(32, R(colorIndexReg), Imm32(1));
	FixupBranch handleSimple565 = J_CC(CC_BE);

	// Otherwise, it depends if color1 or color2 is higher, so fetch them.
	srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	MOVZX(32, 16, color1Reg, MDisp(srcReg, 4));
	MOVZX(32, 16, color2Reg, MDisp(srcReg, 6));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);

	CMP(32, R(color1Reg), R(color2Reg));
	FixupBranch handleMix23 = J_CC(CC_A, true);

	// If we're still here, then colorIndex is either 3 for 0 (easy) or 2 for 50% mix.
	XOR(32, R(resultReg), R(resultReg));
	CMP(32, R(colorIndexReg), Imm32(3));
	FixupBranch finishZero = J_CC(CC_E, true);

	// We'll need more regs.  Grab two more.
	PUSH(R12);
	PUSH(R13);

	// At this point, resultReg, colorIndexReg, R12, and R13 can be used as temps.
	// We'll add, then shift from 565 a bit less to "divide" by 2 for a 50/50 mix.

	// Start with summing R, then shift into position.
	MOV(32, R(resultReg), R(color1Reg));
	AND(32, R(resultReg), Imm32(0x0000F800));
	MOV(32, R(colorIndexReg), R(color2Reg));
	AND(32, R(colorIndexReg), Imm32(0x0000F800));
	LEA(32, R12, MRegSum(resultReg, colorIndexReg));
	// The position is 9, instead of 8, due to doubling.
	SHR(32, R(R12), Imm8(9));

	// For G, summing leaves it 4 right (doubling made it not need more.)
	MOV(32, R(resultReg), R(color1Reg));
	AND(32, R(resultReg), Imm32(0x000007E0));
	MOV(32, R(colorIndexReg), R(color2Reg));
	AND(32, R(colorIndexReg), Imm32(0x000007E0));
	LEA(32, resultReg, MRegSum(resultReg, colorIndexReg));
	SHL(32, R(resultReg), Imm8(5 - 1));
	// Now add G and R together.
	OR(32, R(resultReg), R(R12));

	// At B, we're free to modify the regs in place, finally.
	AND(32, R(color1Reg), Imm32(0x0000001F));
	AND(32, R(color2Reg), Imm32(0x0000001F));
	LEA(32, colorIndexReg, MRegSum(color1Reg, color2Reg));
	// We shift left 2 into position (not 3 due to doubling), then 16 more into the B slot.
	SHL(32, R(colorIndexReg), Imm8(16 + 2));
	// And combine into the result.
	OR(32, R(resultReg), R(colorIndexReg));

	POP(R13);
	POP(R12);
	FixupBranch finishMix50 = J(true);

	// Simply load the 565 color, and convert to 0888.
	SetJumpTarget(handleSimple565);
	srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	MOVZX(32, 16, colorIndexReg, MComplex(srcReg, colorIndexReg, SCALE_2, 4));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
	// DXT1 is done with this reg.
	if (id.TexFmt() == GE_TFMT_DXT1)
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);

	// Start with R, shifting it into place.
	MOV(32, R(resultReg), R(colorIndexReg));
	AND(32, R(resultReg), Imm32(0x0000F800));
	SHR(32, R(resultReg), Imm8(8));

	// Then take G and shift it too.
	MOV(32, R(color2Reg), R(colorIndexReg));
	AND(32, R(color2Reg), Imm32(0x000007E0));
	SHL(32, R(color2Reg), Imm8(5));
	// And now combine with R, shifting that in the process.
	OR(32, R(resultReg), R(color2Reg));

	// Modify B in place and OR in.
	AND(32, R(colorIndexReg), Imm32(0x0000001F));
	SHL(32, R(colorIndexReg), Imm8(16 + 3));
	OR(32, R(resultReg), R(colorIndexReg));
	FixupBranch finish565 = J(true);

	// Here we'll mix color1 and color2 by 2/3 (which gets the 2 depends on colorIndexReg.)
	SetJumpTarget(handleMix23);
	// We'll need more regs.  Grab two more to keep the stack aligned.
	PUSH(R12);
	PUSH(R13);

	// If colorIndexReg is 2, it's color1Reg * 2 + color2Reg, but if colorIndexReg is 3, it's reversed.
	// Let's swap the regs in that case.
	CMP(32, R(colorIndexReg), Imm32(2));
	FixupBranch skipSwap23 = J_CC(CC_E);
	XCHG(32, R(color2Reg), R(color1Reg));
	SetJumpTarget(skipSwap23);

	// Start off with R, adding together first...
	MOV(32, R(resultReg), R(color1Reg));
	AND(32, R(resultReg), Imm32(0x0000F800));
	MOV(32, R(colorIndexReg), R(color2Reg));
	AND(32, R(colorIndexReg), Imm32(0x0000F800));
	LEA(32, resultReg, MComplex(colorIndexReg, resultReg, SCALE_2, 0));
	// We'll overflow if we divide here, so shift into place already.
	SHR(32, R(resultReg), Imm8(8));
	// Now we divide that by 3, by actually multiplying by AAAB and shifting off.
	IMUL(32, R12, R(resultReg), Imm32(0x0000AAAB));
	// Now we SHR off the extra bits we added on.
	SHR(32, R(R12), Imm8(17));

	// Now add up G.  We leave this in place and shift right more.
	MOV(32, R(resultReg), R(color1Reg));
	AND(32, R(resultReg), Imm32(0x000007E0));
	MOV(32, R(colorIndexReg), R(color2Reg));
	AND(32, R(colorIndexReg), Imm32(0x000007E0));
	LEA(32, resultReg, MComplex(colorIndexReg, resultReg, SCALE_2, 0));
	// Again, multiply and now we use AAAB, this time masking.
	IMUL(32, resultReg, R(resultReg), Imm32(0x0000AAAB));
	SHR(32, R(resultReg), Imm8(17 - 5));
	AND(32, R(resultReg), Imm32(0x0000FF00));
	// Let's combine R in already.
	OR(32, R(resultReg), R(R12));

	// Now for B, it starts in the lowest place so we'll need to mask.
	AND(32, R(color1Reg), Imm32(0x0000001F));
	AND(32, R(color2Reg), Imm32(0x0000001F));
	LEA(32, colorIndexReg, MComplex(color2Reg, color1Reg, SCALE_2, 0));
	// Instead of shifting left, though, we multiply by a bit more.
	IMUL(32, colorIndexReg, R(colorIndexReg), Imm32(0x0002AAAB));
	AND(32, R(colorIndexReg), Imm32(0x00FF0000));
	OR(32, R(resultReg), R(colorIndexReg));

	POP(R13);
	POP(R12);

	regCache_.Release(colorIndexReg, RegCache::GEN_TEMP0);
	regCache_.Release(color1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(color2Reg, RegCache::GEN_TEMP2);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);

	SetJumpTarget(finishMix50);
	SetJumpTarget(finish565);
	// In all these cases, it's time to add in alpha.  Zero doesn't get it.
	if (alpha != 0) {
		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
		OR(32, R(resultReg), Imm32(alpha << 24));
		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	}

	SetJumpTarget(finishZero);

	return true;
}

bool SamplerJitCache::Jit_ApplyDXTAlpha(const SamplerID &id) {
	GETextureFormat fmt = id.TexFmt();

	// At this point, srcReg points at the block, and u/v are offsets inside it.

	bool success = false;
	if (fmt == GE_TFMT_DXT3) {
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
		X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);

		if (uReg != RCX) {
			regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
			_assert_(regCache_.Has(RegCache::GEN_SHIFTVAL));
		}

		X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
		MOVZX(32, 16, temp1Reg, MComplex(srcReg, vReg, SCALE_2, 8));
		// Still depending on it being GEN_SHIFTVAL or GEN_ARG_U above.
		LEA(32, RCX, MScaled(uReg, SCALE_4, 0));
		SHR(32, R(temp1Reg), R(CL));
		SHL(32, R(temp1Reg), Imm8(28));
		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
		OR(32, R(resultReg), R(temp1Reg));
		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
		regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);

		success = true;

		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
		regCache_.ForceRelease(RegCache::GEN_ARG_U);
		regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
		regCache_.ForceRelease(RegCache::GEN_ARG_V);
	} else if (fmt == GE_TFMT_DXT5) {
		X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
		X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);

		if (uReg != RCX)
			regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);

		// Let's figure out the alphaIndex bit offset so we can read the right byte.
		// bitOffset = (u + v * 4) * 3;
		LEA(32, uReg, MComplex(uReg, vReg, SCALE_4, 0));
		LEA(32, uReg, MComplex(uReg, uReg, SCALE_2, 0));
		regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
		regCache_.ForceRelease(RegCache::GEN_ARG_V);

		X64Reg alphaIndexReg = regCache_.Alloc(RegCache::GEN_TEMP0);
		X64Reg alpha1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
		X64Reg alpha2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

		// And now the byte offset and bit from there, from those.
		MOV(32, R(alphaIndexReg), R(uReg));
		SHR(32, R(alphaIndexReg), Imm8(3));
		AND(32, R(uReg), Imm32(7));

		// Load 16 bits and mask, in case it straddles bytes.
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		MOVZX(32, 16, alphaIndexReg, MComplex(srcReg, alphaIndexReg, SCALE_1, 8));
		// If not, it's in what was bufwReg.
		if (uReg != RCX) {
			_assert_(regCache_.Has(RegCache::GEN_SHIFTVAL));
			MOV(32, R(RCX), R(uReg));
		}
		SHR(32, R(alphaIndexReg), R(CL));
		AND(32, R(alphaIndexReg), Imm32(7));

		regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
		regCache_.ForceRelease(RegCache::GEN_ARG_U);

		X64Reg temp3Reg = regCache_.Alloc(RegCache::GEN_TEMP3);

		// Okay, now check for 0 or 1 alphaIndex in alphaIndexReg, those are simple.
		CMP(32, R(alphaIndexReg), Imm32(1));
		FixupBranch handleSimple = J_CC(CC_BE, true);

		// Now load a1 and a2, since the rest depend on those values.  Frees up srcReg.
		MOVZX(32, 8, alpha1Reg, MDisp(srcReg, 14));
		MOVZX(32, 8, alpha2Reg, MDisp(srcReg, 15));

		CMP(32, R(alpha1Reg), R(alpha2Reg));
		FixupBranch handleLerp8 = J_CC(CC_A);

		// Okay, check for zero or full alpha, at alphaIndex 6 or 7.
		XOR(32, R(srcReg), R(srcReg));
		CMP(32, R(alphaIndexReg), Imm32(6));
		FixupBranch finishZero = J_CC(CC_E, true);
		// Remember, MOV doesn't affect flags.
		MOV(32, R(srcReg), Imm32(0xFF));
		FixupBranch finishFull = J_CC(CC_A, true);

		// At this point, we're handling a 6-step lerp between alpha1 and alpha2.
		SHL(32, R(alphaIndexReg), Imm8(8));
		// Prepare a multiplier in temp3Reg and multiply alpha1 by it.
		MOV(32, R(temp3Reg), Imm32(6 << 8));
		SUB(32, R(temp3Reg), R(alphaIndexReg));
		IMUL(32, alpha1Reg, R(temp3Reg));
		// And now the same for alpha2, using alphaIndexReg.
		SUB(32, R(alphaIndexReg), Imm32(1 << 8));
		IMUL(32, alpha2Reg, R(alphaIndexReg));

		// Let's skip a step and sum before dividing by 5, also adding the 31.
		LEA(32, srcReg, MComplex(alpha1Reg, alpha2Reg, SCALE_1, 5 * 31));
		// To divide by 5, we will actually multiply by 0x3334 and shift.
		IMUL(32, srcReg, Imm32(0x3334));
		SHR(32, R(srcReg), Imm8(24));
		FixupBranch finishLerp6 = J(true);

		// This will be a 8-step lerp between alpha1 and alpha2.
		SetJumpTarget(handleLerp8);
		SHL(32, R(alphaIndexReg), Imm8(8));
		// Prepare a multiplier in temp3Reg and multiply alpha1 by it.
		MOV(32, R(temp3Reg), Imm32(8 << 8));
		SUB(32, R(temp3Reg), R(alphaIndexReg));
		IMUL(32, alpha1Reg, R(temp3Reg));
		// And now the same for alpha2, using alphaIndexReg.
		SUB(32, R(alphaIndexReg), Imm32(1 << 8));
		IMUL(32, alpha2Reg, R(alphaIndexReg));

		// And divide by 7 together here too, also adding the 31.
		LEA(32, srcReg, MComplex(alpha1Reg, alpha2Reg, SCALE_1, 7 * 31));
		// Our magic constant here is 0x124A, but it's a bit more complex than just a shift.
		IMUL(32, alpha1Reg, R(srcReg), Imm32(0x124A));
		SHR(32, R(alpha1Reg), Imm8(15));
		SUB(32, R(srcReg), R(alpha1Reg));
		SHR(32, R(srcReg), Imm8(1));
		ADD(32, R(srcReg), R(alpha1Reg));
		SHR(32, R(srcReg), Imm8(10));

		FixupBranch finishLerp8 = J();

		SetJumpTarget(handleSimple);
		// Just load the specified alpha byte.
		MOVZX(32, 8, srcReg, MComplex(srcReg, alphaIndexReg, SCALE_1, 14));

		regCache_.Release(alphaIndexReg, RegCache::GEN_TEMP0);
		regCache_.Release(alpha1Reg, RegCache::GEN_TEMP1);
		regCache_.Release(alpha2Reg, RegCache::GEN_TEMP2);
		regCache_.Release(temp3Reg, RegCache::GEN_TEMP3);

		SetJumpTarget(finishFull);
		SetJumpTarget(finishZero);
		SetJumpTarget(finishLerp6);
		SetJumpTarget(finishLerp8);

		SHL(32, R(srcReg), Imm8(24));
		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
		OR(32, R(resultReg), R(srcReg));
		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
		success = true;

		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
	}

	_dbg_assert_(success);
	return success;
}

bool SamplerJitCache::Jit_GetTexData(const SamplerID &id, int bitsPerTexel) {
	if (id.swizzle) {
		return Jit_GetTexDataSwizzled(id, bitsPerTexel);
	}

	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

	// srcReg might be EDX, so let's copy and uReg that before we multiply.
	X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
	X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	bool success = true;
	switch (bitsPerTexel) {
	case 32:
	case 16:
	case 8:
		LEA(64, temp1Reg, MComplex(srcReg, uReg, bitsPerTexel / 8, 0));
		break;

	case 4: {
		XOR(32, R(temp2Reg), R(temp2Reg));
		SHR(32, R(uReg), Imm8(1));
		FixupBranch skip = J_CC(CC_NC);
		// Track whether we shifted a 1 off or not.
		MOV(32, R(temp2Reg), Imm32(4));
		SetJumpTarget(skip);
		LEA(64, temp1Reg, MRegSum(srcReg, uReg));
		break;
	}

	default:
		success = false;
		break;
	}
	// All done with u and texptr.
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
	regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
	regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
	regCache_.ForceRelease(RegCache::GEN_ARG_U);

	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);
	MOV(32, R(resultReg), R(vReg));
	regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
	regCache_.ForceRelease(RegCache::GEN_ARG_V);

	X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW);
	IMUL(32, resultReg, R(bufwReg));
	regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW);
	// We can throw bufw away, now.
	regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);

	switch (bitsPerTexel) {
	case 32:
	case 16:
	case 8:
		MOVZX(32, bitsPerTexel, resultReg, MComplex(temp1Reg, resultReg, bitsPerTexel / 8, 0));
		break;

	case 4: {
		SHR(32, R(resultReg), Imm8(1));
		MOV(8, R(resultReg), MRegSum(temp1Reg, resultReg));
		// RCX is now free.
		MOV(8, R(RCX), R(temp2Reg));
		SHR(8, R(resultReg), R(RCX));
		// Zero out any bits not shifted off.
		AND(32, R(resultReg), Imm8(0x0F));
		break;
	}

	default:
		success = false;
		break;
	}

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return success;
}

bool SamplerJitCache::Jit_GetTexDataSwizzled4() {
	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
	X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
	X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);

	// Get the horizontal tile pos into temp1Reg.
	LEA(32, temp1Reg, MScaled(uReg, SCALE_4, 0));
	// Note: imm8 sign extends negative.
	AND(32, R(temp1Reg), Imm8(~127));

	// Add vertical offset inside tile to temp1Reg.
	LEA(32, temp2Reg, MScaled(vReg, SCALE_4, 0));
	AND(32, R(temp2Reg), Imm8(31));
	LEA(32, temp1Reg, MComplex(temp1Reg, temp2Reg, SCALE_4, 0));
	// Add srcReg, since we'll need it at some point.
	X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	ADD(64, R(temp1Reg), R(srcReg));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
	regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);

	// Now find the vertical tile pos, and add to temp1Reg.
	SHR(32, R(vReg), Imm8(3));
	X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW);
	LEA(32, temp2Reg, MScaled(bufwReg, SCALE_4, 0));
	regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW);
	// We can throw bufw away, now.
	regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);

	IMUL(32, temp2Reg, R(vReg));
	ADD(64, R(temp1Reg), R(temp2Reg));
	// We no longer have a good value in vReg.
	regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
	regCache_.ForceRelease(RegCache::GEN_ARG_V);

	// Last and possible also least, the horizontal offset inside the tile.
	AND(32, R(uReg), Imm8(31));
	SHR(32, R(uReg), Imm8(1));
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	MOV(8, R(resultReg), MRegSum(temp1Reg, uReg));
	FixupBranch skipNonZero = J_CC(CC_NC);
	// If the horizontal offset was odd, take the upper 4.
	SHR(8, R(resultReg), Imm8(4));
	SetJumpTarget(skipNonZero);
	// Zero out the rest of the bits.
	AND(32, R(resultReg), Imm8(0x0F));
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);

	// This destroyed u as well.
	regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
	regCache_.ForceRelease(RegCache::GEN_ARG_U);

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	return true;
}

bool SamplerJitCache::Jit_GetTexDataSwizzled(const SamplerID &id, int bitsPerTexel) {
	if (bitsPerTexel == 4) {
		// Specialized implementation.
		return Jit_GetTexDataSwizzled4();
	}

	bool success = true;
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
	X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
	X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);

	LEA(32, temp1Reg, MScaled(vReg, SCALE_4, 0));
	AND(32, R(temp1Reg), Imm8(31));
	AND(32, R(vReg), Imm8(~7));

	MOV(32, R(temp2Reg), R(uReg));
	MOV(32, R(resultReg), R(uReg));
	switch (bitsPerTexel) {
	case 32:
		SHR(32, R(resultReg), Imm8(2));
		break;
	case 16:
		SHR(32, R(vReg), Imm8(1));
		SHR(32, R(temp2Reg), Imm8(1));
		SHR(32, R(resultReg), Imm8(3));
		break;
	case 8:
		SHR(32, R(vReg), Imm8(2));
		SHR(32, R(temp2Reg), Imm8(2));
		SHR(32, R(resultReg), Imm8(4));
		break;
	default:
		success = false;
		break;
	}
	AND(32, R(temp2Reg), Imm8(3));
	SHL(32, R(resultReg), Imm8(5));
	ADD(32, R(temp1Reg), R(temp2Reg));
	ADD(32, R(temp1Reg), R(resultReg));

	// We may clobber srcReg in the multiply, so let's grab it now.
	X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	LEA(64, temp1Reg, MComplex(srcReg, temp1Reg, SCALE_4, 0));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
	regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);

	X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW);
	LEA(32, resultReg, MScaled(bufwReg, SCALE_4, 0));
	regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW);
	// We can throw bufw away, now.
	regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);

	IMUL(32, resultReg, R(vReg));
	// We no longer have a good value in vReg.
	regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
	regCache_.ForceRelease(RegCache::GEN_ARG_V);

	switch (bitsPerTexel) {
	case 32:
		MOV(bitsPerTexel, R(resultReg), MRegSum(temp1Reg, resultReg));
		break;
	case 16:
		AND(32, R(uReg), Imm8(1));
		LEA(32, resultReg, MComplex(resultReg, uReg, SCALE_2, 0));
		MOVZX(32, bitsPerTexel, resultReg, MRegSum(temp1Reg, resultReg));
		break;
	case 8:
		AND(32, R(uReg), Imm8(3));
		ADD(32, R(resultReg), R(uReg));
		MOVZX(32, bitsPerTexel, resultReg, MRegSum(temp1Reg, resultReg));
		break;
	default:
		success = false;
		break;
	}

	regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
	regCache_.ForceRelease(RegCache::GEN_ARG_U);

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return success;
}

bool SamplerJitCache::Jit_Decode5650() {
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

	MOV(32, R(temp2Reg), R(resultReg));
	AND(32, R(temp2Reg), Imm32(0x0000001F));

	// B (we do R and B at the same time, they're both 5.)
	MOV(32, R(temp1Reg), R(resultReg));
	AND(32, R(temp1Reg), Imm32(0x0000F800));
	SHL(32, R(temp1Reg), Imm8(5));
	OR(32, R(temp2Reg), R(temp1Reg));

	// Expand 5 -> 8.  At this point we have 00BB00RR.
	MOV(32, R(temp1Reg), R(temp2Reg));
	SHL(32, R(temp2Reg), Imm8(3));
	SHR(32, R(temp1Reg), Imm8(2));
	OR(32, R(temp2Reg), R(temp1Reg));
	AND(32, R(temp2Reg), Imm32(0x00FF00FF));

	// Now's as good a time to put in A as any.
	OR(32, R(temp2Reg), Imm32(0xFF000000));

	// Last, we need to align, extract, and expand G.
	// 3 to align to G, and then 2 to expand to 8.
	SHL(32, R(resultReg), Imm8(3 + 2));
	AND(32, R(resultReg), Imm32(0x0000FC00));
	MOV(32, R(temp1Reg), R(resultReg));
	// 2 to account for resultReg being preshifted, 4 for expansion.
	SHR(32, R(temp1Reg), Imm8(2 + 4));
	OR(32, R(resultReg), R(temp1Reg));
	AND(32, R(resultReg), Imm32(0x0000FF00));
	OR(32, R(resultReg), R(temp2Reg));

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return true;
}

bool SamplerJitCache::Jit_Decode5551() {
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

	MOV(32, R(temp2Reg), R(resultReg));
	MOV(32, R(temp1Reg), R(resultReg));
	AND(32, R(temp2Reg), Imm32(0x0000001F));
	AND(32, R(temp1Reg), Imm32(0x000003E0));
	SHL(32, R(temp1Reg), Imm8(3));
	OR(32, R(temp2Reg), R(temp1Reg));

	MOV(32, R(temp1Reg), R(resultReg));
	AND(32, R(temp1Reg), Imm32(0x00007C00));
	SHL(32, R(temp1Reg), Imm8(6));
	OR(32, R(temp2Reg), R(temp1Reg));

	// Expand 5 -> 8.  After this is just A.
	MOV(32, R(temp1Reg), R(temp2Reg));
	SHL(32, R(temp2Reg), Imm8(3));
	SHR(32, R(temp1Reg), Imm8(2));
	// Chop off the bits that were shifted out.
	AND(32, R(temp1Reg), Imm32(0x00070707));
	OR(32, R(temp2Reg), R(temp1Reg));

	// For A, we shift it to a single bit, and then subtract and XOR.
	// That's probably the simplest way to expand it...
	SHR(32, R(resultReg), Imm8(15));
	// If it was 0, it's now -1, otherwise it's 0.  Easy.
	SUB(32, R(resultReg), Imm8(1));
	XOR(32, R(resultReg), Imm32(0xFF000000));
	AND(32, R(resultReg), Imm32(0xFF000000));
	OR(32, R(resultReg), R(temp2Reg));

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return true;
}

alignas(16) static const u32 color4444mask[4] = { 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, };

bool SamplerJitCache::Jit_Decode4444() {
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	X64Reg vecTemp1Reg = regCache_.Alloc(RegCache::VEC_TEMP1);
	X64Reg vecTemp2Reg = regCache_.Alloc(RegCache::VEC_TEMP2);
	X64Reg vecTemp3Reg = regCache_.Alloc(RegCache::VEC_TEMP3);

	MOVD_xmm(vecTemp1Reg, R(resultReg));
	PUNPCKLBW(vecTemp1Reg, R(vecTemp1Reg));
	if (RipAccessible(color4444mask)) {
		PAND(vecTemp1Reg, M(color4444mask));
	} else {
		X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
		MOV(PTRBITS, R(temp1Reg), ImmPtr(color4444mask));
		PAND(vecTemp1Reg, MatR(temp1Reg));
		regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	}
	MOVSS(vecTemp2Reg, R(vecTemp1Reg));
	MOVSS(vecTemp3Reg, R(vecTemp1Reg));
	PSRLW(vecTemp2Reg, 4);
	PSLLW(vecTemp3Reg, 4);
	POR(vecTemp1Reg, R(vecTemp2Reg));
	POR(vecTemp1Reg, R(vecTemp3Reg));
	MOVD_xmm(R(resultReg), vecTemp1Reg);

	regCache_.Release(vecTemp1Reg, RegCache::VEC_TEMP1);
	regCache_.Release(vecTemp2Reg, RegCache::VEC_TEMP2);
	regCache_.Release(vecTemp3Reg, RegCache::VEC_TEMP3);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return true;
}

bool SamplerJitCache::Jit_TransformClutIndex(const SamplerID &id, int bitsPerIndex) {
	GEPaletteFormat fmt = id.ClutFmt();
	if (!id.hasClutShift && !id.hasClutMask && !id.hasClutOffset) {
		// This is simple - just mask if necessary.
		if (bitsPerIndex > 8) {
			X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
			AND(32, R(resultReg), Imm32(0x000000FF));
			regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
		}
		return true;
	}

	bool hasRCX = regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
	_assert_msg_(hasRCX, "Could not obtain RCX, locked?");

	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	MOV(PTRBITS, R(temp1Reg), ImmPtr(&gstate.clutformat));
	MOV(32, R(temp1Reg), MatR(temp1Reg));

	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);

	// Shift = (clutformat >> 2) & 0x1F
	if (id.hasClutShift) {
		_assert_(regCache_.Has(RegCache::GEN_SHIFTVAL));
		MOV(32, R(RCX), R(temp1Reg));
		SHR(32, R(RCX), Imm8(2));
		AND(32, R(RCX), Imm8(0x1F));
		SHR(32, R(resultReg), R(RCX));
	}

	// Mask = (clutformat >> 8) & 0xFF
	if (id.hasClutMask) {
		X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
		MOV(32, R(temp2Reg), R(temp1Reg));
		SHR(32, R(temp2Reg), Imm8(8));
		AND(32, R(resultReg), R(temp2Reg));
		regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
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
		SHR(32, R(temp1Reg), Imm8(16));
		SHL(32, R(temp1Reg), Imm8(4));
		OR(32, R(resultReg), R(temp1Reg));
		AND(32, R(resultReg), Imm32(offsetMask));
	}

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return true;
}

bool SamplerJitCache::Jit_ReadClutColor(const SamplerID &id) {
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);

	if (!id.useSharedClut) {
		X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

		if (regCache_.Has(RegCache::GEN_ARG_LEVEL)) {
			X64Reg levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
			// We need to multiply by 16 and add, LEA allows us to copy too.
			LEA(32, temp2Reg, MScaled(levelReg, SCALE_4, 0));
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
			regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);
		} else {
			if (id.linear) {
#ifdef _WIN32
				const int argOffset = 24 + 48 + 8 + 32;
				// Extra 8 to account for CALL.
				MOV(32, R(temp2Reg), MDisp(RSP, argOffset + 16 + 8));
#else
				// Extra 8 to account for CALL.
				MOV(32, R(temp2Reg), MDisp(RSP, 24 + 48 + 8 + 8));
#endif
			} else {
#ifdef _WIN32
				// The argument was saved on the stack.
				MOV(32, R(temp2Reg), MDisp(RSP, 40));
#else
				_assert_(false);
#endif
			}
			LEA(32, temp2Reg, MScaled(temp2Reg, SCALE_4, 0));
		}

		// Second step of the multiply by 16 (since we only multiplied by 4 before.)
		LEA(64, resultReg, MComplex(resultReg, temp2Reg, SCALE_4, 0));
		regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	}

	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	MOV(PTRBITS, R(temp1Reg), ImmPtr(clut));

	switch (id.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		MOVZX(32, 16, resultReg, MComplex(temp1Reg, resultReg, SCALE_2, 0));
		break;

	case GE_CMODE_32BIT_ABGR8888:
		MOV(32, R(resultReg), MComplex(temp1Reg, resultReg, SCALE_4, 0));
		break;
	}

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);

	switch (id.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
		return Jit_Decode5650();

	case GE_CMODE_16BIT_ABGR5551:
		return Jit_Decode5551();

	case GE_CMODE_16BIT_ABGR4444:
		return Jit_Decode4444();

	case GE_CMODE_32BIT_ABGR8888:
		return true;

	default:
		return false;
	}
}

};

#endif
