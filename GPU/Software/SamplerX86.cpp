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
#include "Common/BitScan.h"
#include "Common/CPUDetect.h"
#include "GPU/GPUState.h"
#include "GPU/Software/Sampler.h"
#include "GPU/ge_constants.h"

using namespace Gen;
using namespace Rasterizer;

extern u32 clut[4096];

namespace Sampler {

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
		RegCache::GEN_ARG_LEVEL,
		// Avoid clobber.
		RegCache::GEN_ARG_LEVELFRAC,
	});
	regCache_.ChangeReg(RAX, RegCache::GEN_RESULT);
	regCache_.ChangeReg(XMM0, RegCache::VEC_ARG_U);
	regCache_.ForceRetain(RegCache::VEC_ARG_U);
	regCache_.ChangeReg(XMM1, RegCache::VEC_ARG_V);
	regCache_.ForceRetain(RegCache::VEC_ARG_V);
	regCache_.ChangeReg(XMM5, RegCache::VEC_RESULT);
	regCache_.ForceRetain(RegCache::VEC_RESULT);

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

	regCache_.ForceRelease(RegCache::VEC_ARG_U);
	regCache_.ForceRelease(RegCache::VEC_ARG_V);
	regCache_.ForceRelease(RegCache::VEC_RESULT);
	if (regCache_.Has(RegCache::GEN_ARG_LEVEL))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);
	if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVELFRAC);
	regCache_.Reset(true);

	// Let's drop some helpful constants here.
	const u8 *const10All = AlignCode16();
	Write16(0x10); Write16(0x10); Write16(0x10); Write16(0x10);
	Write16(0x10); Write16(0x10); Write16(0x10); Write16(0x10);

	const u8 *const10Low = AlignCode16();
	Write16(0x10); Write16(0x10); Write16(0x10); Write16(0x10);
	Write16(0); Write16(0); Write16(0); Write16(0);

	if (!id.hasAnyMips) {
		constWidth256f_ = AlignCode16();
		float w256f = (1 << id.width0Shift) * 256;
		Write32(*(uint32_t *)&w256f); Write32(*(uint32_t *)&w256f);
		Write32(*(uint32_t *)&w256f); Write32(*(uint32_t *)&w256f);

		constHeight256f_ = AlignCode16();
		float h256f = (1 << id.height0Shift) * 256;
		Write32(*(uint32_t *)&h256f); Write32(*(uint32_t *)&h256f);
		Write32(*(uint32_t *)&h256f); Write32(*(uint32_t *)&h256f);

		constWidthMinus1i_ = AlignCode16();
		Write32((1 << id.width0Shift) - 1); Write32((1 << id.width0Shift) - 1);
		Write32((1 << id.width0Shift) - 1); Write32((1 << id.width0Shift) - 1);

		constHeightMinus1i_ = AlignCode16();
		Write32((1 << id.height0Shift) - 1); Write32((1 << id.height0Shift) - 1);
		Write32((1 << id.height0Shift) - 1); Write32((1 << id.height0Shift) - 1);
	} else {
		constWidth256f_ = nullptr;
		constHeight256f_ = nullptr;
		constWidthMinus1i_ = nullptr;
		constHeightMinus1i_ = nullptr;
	}

	constUNext_ = AlignCode16();
	Write32(0); Write32(1); Write32(0); Write32(1);

	constVNext_ = AlignCode16();
	Write32(0); Write32(0); Write32(1); Write32(1);

	// Now the actual linear func, which is exposed externally.
	const u8 *start = AlignCode16();

	regCache_.SetupABI({
		RegCache::VEC_ARG_S,
		RegCache::VEC_ARG_T,
		RegCache::GEN_ARG_X,
		RegCache::GEN_ARG_Y,
		RegCache::VEC_ARG_COLOR,
		RegCache::GEN_ARG_TEXPTR,
		RegCache::GEN_ARG_BUFW,
		RegCache::GEN_ARG_LEVEL,
		RegCache::GEN_ARG_LEVELFRAC,
	});

#if PPSSPP_PLATFORM(WINDOWS)
	// RET + shadow space + 8 byte space for color arg (the Win32 ABI is kinda ugly.)
	stackArgPos_ = 8 + 32 + 8;

	// Positions: stackArgPos_+0=src, stackArgPos_+8=bufw, stackArgPos_+16=level, stackArgPos_+24=levelFrac
#else
	stackArgPos_ = 0;
#endif

	// Start out by saving some registers, since we'll need more.
	PUSH(R15);
	PUSH(R14);
	PUSH(R13);
	PUSH(R12);
	regCache_.Add(R15, RegCache::GEN_INVALID);
	regCache_.Add(R14, RegCache::GEN_INVALID);
	// This is what we'll put in them, anyway...
	regCache_.Add(R13, RegCache::GEN_ARG_FRAC_V);
	regCache_.Add(R12, RegCache::GEN_ARG_FRAC_U);
	stackArgPos_ += 32;

	// Our first goal is to convert S/T and X/Y into U/V and frac_u/frac_v.
	if (!Jit_GetTexelCoordsQuad(id)) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(nearest));
		return nullptr;
	}

	// We also want to save src and bufw for later.  Might be in a reg already.
	if (regCache_.Has(RegCache::GEN_ARG_TEXPTR)) {
		_assert_(regCache_.Has(RegCache::GEN_ARG_BUFW));
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW);
		MOV(64, R(R14), R(srcReg));
		MOV(64, R(R15), R(bufwReg));
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);
	} else {
		MOV(64, R(R14), MDisp(RSP, stackArgPos_ + 0));
		MOV(64, R(R15), MDisp(RSP, stackArgPos_ + 8));
	}

	// Okay, and now remember we moved to R14/R15.
	regCache_.ChangeReg(R14, RegCache::GEN_ARG_TEXPTR);
	regCache_.ChangeReg(R15, RegCache::GEN_ARG_BUFW);
	regCache_.ForceRetain(RegCache::GEN_ARG_TEXPTR);
	regCache_.ForceRetain(RegCache::GEN_ARG_BUFW);

	// Early exit on !srcPtr.
	FixupBranch zeroSrc;
	if (id.hasInvalidPtr) {
		// TODO: Change when texptr is an array.
		CMP(PTRBITS, R(R14), Imm8(0));
		FixupBranch nonZeroSrc = J_CC(CC_NZ);
		PXOR(XMM0, R(XMM0));
		zeroSrc = J(true);
		SetJumpTarget(nonZeroSrc);
	}

	// TODO: Save color or put it somewhere... or reserve the reg?
	// For now, throwing away to avoid confusion.
	regCache_.ForceRelease(RegCache::VEC_ARG_COLOR);

	if (!Jit_PrepareDataOffsets(id)) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(nearest));
		return nullptr;
	}

	regCache_.ChangeReg(XMM5, RegCache::VEC_RESULT);
	regCache_.ForceRetain(RegCache::VEC_RESULT);

	// This stores the result in an XMM for later processing.
	// We map lookups to nearest CALLs, with arg order: u, v, src, bufw, level
	auto doNearestCall = [&](int off) {
#if PPSSPP_PLATFORM(WINDOWS)
		static const X64Reg uArgReg = RCX;
		static const X64Reg vArgReg = RDX;
		static const X64Reg srcArgReg = R8;
		static const X64Reg bufwArgReg = R9;
#else
		static const X64Reg uArgReg = RDI;
		static const X64Reg vArgReg = RSI;
		static const X64Reg srcArgReg = RDX;
		static const X64Reg bufwArgReg = RCX;
#endif
		static const X64Reg resultReg = RAX;

		X64Reg uReg = regCache_.Find(RegCache::VEC_ARG_U);
		X64Reg vReg = regCache_.Find(RegCache::VEC_ARG_V);
		// Otherwise, we'll overwrite them...
		_assert_(uReg == XMM0 && vReg == XMM1);

		MOVD_xmm(R(uArgReg), uReg);
		MOVD_xmm(R(vArgReg), vReg);

		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW);
		// TODO: Change when texptr is an array.
		MOV(64, R(srcArgReg), R(srcReg));
		MOV(32, R(bufwArgReg), R(bufwReg));
		// Leave level/levelFrac, we just always load from RAM on Windows and lock on POSIX.
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW);

		PSRLDQ(uReg, 4);
		PSRLDQ(vReg, 4);
		regCache_.Unlock(uReg, RegCache::VEC_ARG_U);
		regCache_.Unlock(vReg, RegCache::VEC_ARG_V);

		CALL(nearest);

		X64Reg vecResultReg = regCache_.Find(RegCache::VEC_RESULT);
		if (off == 0) {
			MOVD_xmm(vecResultReg, R(resultReg));
		} else {
			X64Reg tempReg = regCache_.Alloc(RegCache::VEC_TEMP0);
			MOVD_xmm(tempReg, R(resultReg));
			PSLLDQ(tempReg, off);
			POR(vecResultReg, R(tempReg));
			regCache_.Release(tempReg, RegCache::VEC_TEMP0);
		}
		regCache_.Unlock(vecResultReg, RegCache::VEC_RESULT);
	};

	doNearestCall(0);
	doNearestCall(4);
	doNearestCall(8);
	doNearestCall(12);

	// We're done with these now.
	regCache_.ForceRelease(RegCache::VEC_ARG_U);
	regCache_.ForceRelease(RegCache::VEC_ARG_V);
	regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
	regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);
	if (regCache_.Has(RegCache::GEN_ARG_LEVEL))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);

	// TODO: Convert to reg cache.
	regCache_.ForceRelease(RegCache::VEC_RESULT);
	static const X64Reg fpScratchReg1 = XMM1;
	static const X64Reg fpScratchReg2 = XMM2;
	static const X64Reg fpScratchReg3 = XMM3;
	static const X64Reg fpScratchReg4 = XMM4;
	static const X64Reg fpScratchReg5 = XMM5;

	// First put the top RRRRRRRR LLLLLLLL into fpScratchReg1, bottom into fpScratchReg2.
	// Start with XXXX XXXX RRRR LLLL, and then expand 8 bits to 16 bits.
	if (!cpu_info.bSSE4_1) {
		PXOR(fpScratchReg3, R(fpScratchReg3));
		PSHUFD(fpScratchReg1, R(XMM5), _MM_SHUFFLE(0, 0, 1, 0));
		PSHUFD(fpScratchReg2, R(XMM5), _MM_SHUFFLE(0, 0, 3, 2));
		PUNPCKLBW(fpScratchReg1, R(fpScratchReg3));
		PUNPCKLBW(fpScratchReg2, R(fpScratchReg3));
	} else {
		PSHUFD(fpScratchReg2, R(XMM5), _MM_SHUFFLE(0, 0, 3, 2));
		PMOVZXBW(fpScratchReg1, R(XMM5));
		PMOVZXBW(fpScratchReg2, R(fpScratchReg2));
	}

	// Grab frac_u and spread to lower (L) lanes.
	X64Reg fracUReg = regCache_.Find(RegCache::GEN_ARG_FRAC_U);
	MOVD_xmm(fpScratchReg5, R(fracUReg));
	regCache_.Unlock(fracUReg, RegCache::GEN_ARG_FRAC_U);
	regCache_.ForceRelease(RegCache::GEN_ARG_FRAC_U);
	PSHUFLW(fpScratchReg5, R(fpScratchReg5), _MM_SHUFFLE(0, 0, 0, 0));
	// Now subtract 0x10 - frac_u in the L lanes only: 00000000 LLLLLLLL.
	MOVDQA(fpScratchReg3, M(const10Low));
	PSUBW(fpScratchReg3, R(fpScratchReg5));
	// Then we just shift and OR in the original frac_u.
	PSLLDQ(fpScratchReg5, 8);
	POR(fpScratchReg3, R(fpScratchReg5));

	// Okay, we have 8-bits in the top and bottom rows for the color.
	// Multiply by frac to get 12, which we keep for the next stage.
	PMULLW(fpScratchReg1, R(fpScratchReg3));
	PMULLW(fpScratchReg2, R(fpScratchReg3));

	// Time for frac_v.  This time, we want it in all 8 lanes.
	X64Reg fracVReg = regCache_.Find(RegCache::GEN_ARG_FRAC_V);
	MOVD_xmm(fpScratchReg5, R(fracVReg));
	regCache_.Unlock(fracVReg, RegCache::GEN_ARG_FRAC_V);
	regCache_.ForceRelease(RegCache::GEN_ARG_FRAC_V);
	PSHUFLW(fpScratchReg5, R(fpScratchReg5), _MM_SHUFFLE(0, 0, 0, 0));
	PSHUFD(fpScratchReg5, R(fpScratchReg5), _MM_SHUFFLE(0, 0, 0, 0));

	// Now, inverse fpScratchReg5 into fpScratchReg3 for the top row.
	MOVDQA(fpScratchReg3, M(const10All));
	PSUBW(fpScratchReg3, R(fpScratchReg5));

	// We had 12, plus 4 frac, that gives us 16.
	PMULLW(fpScratchReg2, R(fpScratchReg5));
	PMULLW(fpScratchReg1, R(fpScratchReg3));

	// Finally, time to sum them all up and divide by 256 to get back to 8 bits.
	PADDUSW(fpScratchReg2, R(fpScratchReg1));
	PSHUFD(XMM0, R(fpScratchReg2), _MM_SHUFFLE(3, 2, 3, 2));
	PADDUSW(XMM0, R(fpScratchReg2));
	PSRLW(XMM0, 8);

	// Last of all, convert to 32-bit channels.
	if (cpu_info.bSSE4_1) {
		PMOVZXWD(XMM0, R(XMM0));
	} else {
		PXOR(fpScratchReg1, R(fpScratchReg1));
		PUNPCKLWD(XMM0, R(fpScratchReg1));
	}

	// TODO: Actually use this (and color) at some point.
	if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVELFRAC);

	if (id.hasInvalidPtr) {
		SetJumpTarget(zeroSrc);
	}

	POP(R12);
	POP(R13);
	POP(R14);
	POP(R15);

	RET();

	regCache_.Reset(true);

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
	if (id.linear) {
		// We can throw away bufw immediately.  Maybe even earlier?
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);

		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);

		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		X64Reg byteIndexReg = regCache_.Find(RegCache::GEN_ARG_V);
		bool success = true;
		switch (bitsPerTexel) {
		case 32:
		case 16:
		case 8:
			MOVZX(32, bitsPerTexel, resultReg, MRegSum(srcReg, byteIndexReg));
			break;

		case 4:
			MOV(8, R(resultReg), MRegSum(srcReg, byteIndexReg));
			break;

		default:
			success = false;
			break;
		}
		// Okay, srcReg and byteIndexReg have done their jobs.
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(byteIndexReg, RegCache::GEN_ARG_V);
		regCache_.ForceRelease(RegCache::GEN_ARG_V);

		if (bitsPerTexel == 4) {
			X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);

			SHR(32, R(uReg), Imm8(1));
			FixupBranch skip = J_CC(CC_NC);
			SHR(32, R(resultReg), Imm8(4));
			SetJumpTarget(skip);
			// Zero out any bits not shifted off.
			AND(32, R(resultReg), Imm8(0x0F));

			regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
		}
		regCache_.ForceRelease(RegCache::GEN_ARG_U);

		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
		return success;
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

	if (bitsPerTexel == 4) {
		bool hasRCX = regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
		_assert_(hasRCX);
	}

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

bool SamplerJitCache::Jit_GetTexDataSwizzled4(const SamplerID &id) {
	if (id.linear) {
		// We can throw away bufw immediately.  Maybe even earlier?
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);

		X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
		X64Reg byteIndexReg = regCache_.Find(RegCache::GEN_ARG_V);
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);

		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
		MOV(8, R(resultReg), MRegSum(srcReg, byteIndexReg));

		SHR(32, R(uReg), Imm8(1));
		FixupBranch skipNonZero = J_CC(CC_NC);
		// If the horizontal offset was odd, take the upper 4.
		SHR(8, R(resultReg), Imm8(4));
		SetJumpTarget(skipNonZero);
		// Zero out the rest of the bits.
		AND(32, R(resultReg), Imm8(0x0F));
		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);

		// We're all done with each of these regs, now.
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
		regCache_.ForceRelease(RegCache::GEN_ARG_U);
		regCache_.Unlock(byteIndexReg, RegCache::GEN_ARG_V);
		regCache_.ForceRelease(RegCache::GEN_ARG_V);

		return true;
	}

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
		return Jit_GetTexDataSwizzled4(id);
	}

	bool success = true;
	if (id.linear) {
		// We can throw away bufw immediately.  Maybe even earlier?
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);
		// We've also baked uReg into vReg.
		regCache_.ForceRelease(RegCache::GEN_ARG_U);

		X64Reg byteIndexReg = regCache_.Find(RegCache::GEN_ARG_V);
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);

		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
		switch (bitsPerTexel) {
		case 32:
			MOV(bitsPerTexel, R(resultReg), MRegSum(srcReg, byteIndexReg));
			break;
		case 16:
			MOVZX(32, bitsPerTexel, resultReg, MRegSum(srcReg, byteIndexReg));
			break;
		case 8:
			MOVZX(32, bitsPerTexel, resultReg, MRegSum(srcReg, byteIndexReg));
			break;
		default:
			success = false;
			break;
		}
		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);

		// The pointer and offset have done their duty.
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(byteIndexReg, RegCache::GEN_ARG_V);
		regCache_.ForceRelease(RegCache::GEN_ARG_V);

		return success;
	}

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

bool SamplerJitCache::Jit_GetTexelCoordsQuad(const SamplerID &id) {
	// TODO: Handle grabbing w/h and generating constants.
	if (constWidth256f_ == nullptr)
		return false;

	X64Reg sReg = regCache_.Find(RegCache::VEC_ARG_S);
	X64Reg tReg = regCache_.Find(RegCache::VEC_ARG_T);

	// Start by multiplying with the width and converting to integers.
	MULSS(sReg, M(constWidth256f_));
	MULSS(tReg, M(constHeight256f_));
	CVTPS2DQ(sReg, R(sReg));
	CVTPS2DQ(tReg, R(tReg));

	// Now adjust X and Y...
	X64Reg xReg = regCache_.Find(RegCache::GEN_ARG_X);
	X64Reg yReg = regCache_.Find(RegCache::GEN_ARG_Y);
	NEG(32, R(xReg));
	SUB(32, R(xReg), Imm8(128 - 12));
	NEG(32, R(yReg));
	SUB(32, R(yReg), Imm8(128 - 12));

	// Add them in.  We do this in the SSE because we have more to do there...
	X64Reg tempXYReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	MOVD_xmm(tempXYReg, R(xReg));
	PADDD(sReg, R(tempXYReg));
	MOVD_xmm(tempXYReg, R(yReg));
	PADDD(tReg, R(tempXYReg));
	regCache_.Release(tempXYReg, RegCache::VEC_TEMP0);

	regCache_.Unlock(xReg, RegCache::GEN_ARG_X);
	regCache_.Unlock(yReg, RegCache::GEN_ARG_Y);
	regCache_.ForceRelease(RegCache::GEN_ARG_X);
	regCache_.ForceRelease(RegCache::GEN_ARG_Y);

	// We do want the fraction, though, so extract that.
	X64Reg fracUReg = regCache_.Find(RegCache::GEN_ARG_FRAC_U);
	X64Reg fracVReg = regCache_.Find(RegCache::GEN_ARG_FRAC_V);
	MOVD_xmm(R(fracUReg), sReg);
	MOVD_xmm(R(fracVReg), tReg);
	SHR(32, R(fracUReg), Imm8(4));
	AND(32, R(fracUReg), Imm8(0x0F));
	SHR(32, R(fracVReg), Imm8(4));
	AND(32, R(fracVReg), Imm8(0x0F));
	regCache_.Unlock(fracUReg, RegCache::GEN_ARG_FRAC_U);
	regCache_.Unlock(fracVReg, RegCache::GEN_ARG_FRAC_V);

	// Get rid of the fractional bits, and spread out.
	PSRAD(sReg, 8);
	PSRAD(tReg, 8);
	PSHUFD(sReg, R(sReg), _MM_SHUFFLE(0, 0, 0, 0));
	PSHUFD(tReg, R(tReg), _MM_SHUFFLE(0, 0, 0, 0));

	// Add U/V values for the next coords.
	PADDD(sReg, M(constUNext_));
	PADDD(tReg, M(constVNext_));

	X64Reg temp0ClampReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	bool temp0ClampZero = false;

	auto doClamp = [&](X64Reg stReg, const u8 *bound) {
		if (!temp0ClampZero)
			PXOR(temp0ClampReg, R(temp0ClampReg));
		temp0ClampZero = true;

		if (cpu_info.bSSE4_1) {
			PMINSD(stReg, M(bound));
			PMAXSD(stReg, R(temp0ClampReg));
		} else {
			temp0ClampZero = false;
			// Set temp to max(0, stReg) = AND(NOT(0 > stReg), stReg).
			PCMPGTD(temp0ClampReg, R(stReg));
			PANDN(temp0ClampReg, R(stReg));

			// Now make a mask where bound is greater than the ST value in temp0ClampReg.
			MOVDQA(stReg, M(bound));
			PCMPGTD(stReg, R(temp0ClampReg));
			// Throw away the values that are greater in our temp0ClampReg in progress result.
			PAND(temp0ClampReg, R(stReg));

			// Now, set bound only where ST was too high.
			PANDN(stReg, M(bound));
			// And put in the values that were fine.
			POR(stReg, R(temp0ClampReg));
		}
	};

	if (id.clampS)
		doClamp(sReg, constWidthMinus1i_);
	else
		PAND(sReg, M(constWidthMinus1i_));

	if (id.clampT)
		doClamp(tReg, constHeightMinus1i_);
	else
		PAND(tReg, M(constHeightMinus1i_));

	regCache_.Release(temp0ClampReg, RegCache::VEC_TEMP0);

	regCache_.Unlock(sReg, RegCache::VEC_ARG_S);
	regCache_.Unlock(tReg, RegCache::VEC_ARG_T);
	regCache_.Change(RegCache::VEC_ARG_S, RegCache::VEC_ARG_U);
	regCache_.Change(RegCache::VEC_ARG_T, RegCache::VEC_ARG_V);
	return true;
}

bool SamplerJitCache::Jit_PrepareDataOffsets(const SamplerID &id) {
	_assert_(id.linear);

	// TODO: Use reg cache to avoid overwriting color...

	bool success = true;
	int bits = -1;
	switch (id.TexFmt()) {
	case GE_TFMT_5650:
	case GE_TFMT_5551:
	case GE_TFMT_4444:
	case GE_TFMT_CLUT16:
		bits = 16;
		break;

	case GE_TFMT_8888:
	case GE_TFMT_CLUT32:
		bits = 32;
		break;

	case GE_TFMT_CLUT8:
		bits = 8;
		break;

	case GE_TFMT_CLUT4:
		bits = 4;
		break;

	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
		break;

	default:
		success = false;
	}

	if (success && bits != -1) {
		if (id.swizzle) {
			success = Jit_PrepareDataSwizzledOffsets(id, bits);
		} else {
			if (!id.useStandardBufw || id.hasAnyMips) {
				// Spread bufw into each lane.
				MOVD_xmm(XMM2, R(R15));
				PSHUFD(XMM2, R(XMM2), _MM_SHUFFLE(0, 0, 0, 0));

				if (bits == 4)
					PSRLD(XMM2, 1);
				else if (bits == 16)
					PSLLD(XMM2, 1);
				else if (bits == 32)
					PSLLD(XMM2, 2);
			}

			if (id.useStandardBufw && !id.hasAnyMips) {
				int amt = id.width0Shift;
				if (bits == 4)
					amt -= 1;
				else if (bits == 16)
					amt += 1;
				else if (bits == 32)
					amt += 2;
				// It's aligned to 16 bytes, so must at least be 16.
				PSLLD(XMM1, std::max(4, amt));
			} else if (cpu_info.bSSE4_1) {
				// And now multiply.  This is slow, but not worse than the SSE2 version...
				PMULLD(XMM1, R(XMM2));
			} else {
				// Copy that into another temp for multiply.
				MOVDQA(XMM3, R(XMM1));

				// Okay, first, multiply to get XXXX CCCC XXXX AAAA.
				PMULUDQ(XMM1, R(XMM2));
				PSRLDQ(XMM3, 4);
				PSRLDQ(XMM2, 4);
				// And now get XXXX DDDD XXXX BBBB.
				PMULUDQ(XMM3, R(XMM2));

				// We know everything is positive, so XXXX must be zero.  Let's combine.
				PSLLDQ(XMM3, 4);
				POR(XMM1, R(XMM3));
			}

			if (bits == 4) {
				// Need to keep uvec for the odd bit.
				MOVDQA(XMM2, R(XMM0));
				PSRLD(XMM2, 1);
				PADDD(XMM1, R(XMM2));
			} else {
				// Destroy uvec, we won't use it again.
				if (bits == 16)
					PSLLD(XMM0, 1);
				else if (bits == 32)
					PSLLD(XMM0, 2);
				PADDD(XMM1, R(XMM0));
			}
		}
	}

	return success;
}

bool SamplerJitCache::Jit_PrepareDataSwizzledOffsets(const SamplerID &id, int bitsPerTexel) {
	// See Jit_GetTexDataSwizzled() for usage of this offset.

	// TODO: Use reg cache to avoid overwriting color...

	if (!id.useStandardBufw || id.hasAnyMips) {
		// Spread bufw into each lane.
		MOVD_xmm(XMM2, R(R15));
		PSHUFD(XMM2, R(XMM2), _MM_SHUFFLE(0, 0, 0, 0));
	}

	// Divide vvec by 8 in a temp.
	MOVDQA(XMM3, R(XMM1));
	PSRLD(XMM3, 3);

	// And now multiply by bufw.  May be able to use a shift in a common case.
	int shiftAmount = 32 - clz32_nonzero(bitsPerTexel - 1);
	if (id.useStandardBufw && !id.hasAnyMips) {
		int amt = id.width0Shift;
		// Account for 16 byte minimum.
		amt = std::max(7 - shiftAmount, amt);
		shiftAmount += amt;
	} else if (cpu_info.bSSE4_1) {
		// And now multiply.  This is slow, but not worse than the SSE2 version...
		PMULLD(XMM3, R(XMM2));
	} else {
		// Copy that into another temp for multiply.
		MOVDQA(XMM4, R(XMM3));

		// Okay, first, multiply to get XXXX CCCC XXXX AAAA.
		PMULUDQ(XMM3, R(XMM2));
		PSRLDQ(XMM4, 4);
		PSRLDQ(XMM2, 4);
		// And now get XXXX DDDD XXXX BBBB.
		PMULUDQ(XMM4, R(XMM2));

		// We know everything is positive, so XXXX must be zero.  Let's combine.
		PSLLDQ(XMM4, 4);
		POR(XMM3, R(XMM4));
	}
	// Multiply the result by bitsPerTexel using a shift.
	PSLLD(XMM3, shiftAmount);

	// Now we're adding (v & 7) * 16.  Use a 16-bit wall.
	PSLLW(XMM1, 13);
	PSRLD(XMM1, 9);
	PADDD(XMM1, R(XMM3));

	// Now get ((uvec / texels_per_tile) / 4) * 32 * 4 aka (uvec / (128 / bitsPerTexel)) << 7.
	MOVDQA(XMM2, R(XMM0));
	PSRLD(XMM2, 7 + clz32_nonzero(bitsPerTexel - 1) - 32);
	PSLLD(XMM2, 7);
	// Add it in to our running total.
	PADDD(XMM1, R(XMM2));

	if (bitsPerTexel == 4) {
		// Finally, we want (uvec & 31) / 2.  Use a 16-bit wall.
		MOVDQA(XMM2, R(XMM0));
		PSLLW(XMM2, 11);
		PSRLD(XMM2, 12);
		// With that, this is our byte offset.  uvec & 1 has which half.
		PADDD(XMM1, R(XMM2));
	} else {
		// We can destroy uvec in this path.  Clear all but 2 bits for 32, 3 for 16, or 4 for 8.
		PSLLW(XMM0, 32 - clz32_nonzero(bitsPerTexel - 1) + 9);
		// Now that it's at the top of the 16 bits, we always shift that to the top of 4 bits.
		PSRLD(XMM0, 12);
		PADDD(XMM1, R(XMM0));
	}

	return true;
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
			// Don't release if we're reusing it.
			if (!regCache_.Has(RegCache::VEC_ARG_U))
				regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);
		} else {
#if PPSSPP_PLATFORM(WINDOWS)
			if (id.linear) {
				MOV(32, R(temp2Reg), MDisp(RSP, stackArgPos_ + 16));
			} else {
				// The argument was saved on the stack.
				MOV(32, R(temp2Reg), MDisp(RSP, 40));
			}
#else
			_assert_(false);
#endif
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
