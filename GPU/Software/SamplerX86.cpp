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

FetchFunc SamplerJitCache::CompileFetch(const SamplerID &id) {
	_assert_msg_(id.fetch && !id.linear, "Only fetch should be set on sampler id");
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
	Describe("Init");
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

	Describe("Init");
	if (id.hasInvalidPtr) {
		SetJumpTarget(zeroSrc);
	}

	RET();

	regCache_.Reset(true);

	EndWrite();
	return (FetchFunc)start;
}

NearestFunc SamplerJitCache::CompileNearest(const SamplerID &id) {
	_assert_msg_(!id.fetch && !id.linear, "Fetch and linear should be cleared on sampler id");

	// TODO: Actual implementation with new args.
	return nullptr;
}

LinearFunc SamplerJitCache::CompileLinear(const SamplerID &id) {
	_assert_msg_(id.linear && !id.fetch, "Only linear should be set on sampler id");
	BeginWrite();
	Describe("Init");

	// Set the stackArgPos_ so we can use it in the nearest part too.
#if PPSSPP_PLATFORM(WINDOWS)
	// RET + shadow space + 8 byte space for color arg (the Win32 ABI is kinda ugly.)
	stackArgPos_ = 8 + 32 + 8;
	// Plus 32 for R12-R15.
	stackArgPos_ += 32;
	// Plus XMM6-XMM9 and 8 to align.
	stackArgPos_ += 16 * 4 + 8;
#else
	stackArgPos_ = 32;
#endif

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
	auto lockReg = [&](X64Reg r, RegCache::Purpose p) {
		regCache_.ChangeReg(r, p);
		regCache_.ForceRetain(p);
	};
	lockReg(XMM0, RegCache::VEC_ARG_U);
	lockReg(XMM1, RegCache::VEC_ARG_V);
	lockReg(XMM5, RegCache::VEC_RESULT);
#if !PPSSPP_PLATFORM(WINDOWS)
	if (id.hasAnyMips) {
		lockReg(XMM6, RegCache::VEC_U1);
		lockReg(XMM7, RegCache::VEC_V1);
		lockReg(XMM8, RegCache::VEC_RESULT1);
	}
	lockReg(XMM9, RegCache::VEC_ARG_COLOR);
#endif

	// Let's drop some helpful constants here.
	WriteConstantPool(id);

	// We'll first write the nearest sampler, which we will CALL.
	// This may differ slightly based on the "linear" flag.
	const u8 *nearest = AlignCode16();

	if (!Jit_ReadTextureFormat(id)) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(nearest));
		return nullptr;
	}

	Describe("Init");
	RET();

	regCache_.ForceRelease(RegCache::VEC_ARG_U);
	regCache_.ForceRelease(RegCache::VEC_ARG_V);
	regCache_.ForceRelease(RegCache::VEC_RESULT);

	auto unlockOptReg = [&](RegCache::Purpose p) {
		if (regCache_.Has(p))
			regCache_.ForceRelease(p);
	};
	unlockOptReg(RegCache::GEN_ARG_LEVEL);
	unlockOptReg(RegCache::GEN_ARG_LEVELFRAC);
	unlockOptReg(RegCache::VEC_U1);
	unlockOptReg(RegCache::VEC_V1);
	unlockOptReg(RegCache::VEC_RESULT1);
	unlockOptReg(RegCache::VEC_ARG_COLOR);
	regCache_.Reset(true);

	// Now the actual linear func, which is exposed externally.
	const u8 *start = AlignCode16();
	Describe("Init");

	regCache_.SetupABI({
		RegCache::VEC_ARG_S,
		RegCache::VEC_ARG_T,
		RegCache::GEN_ARG_X,
		RegCache::GEN_ARG_Y,
		RegCache::VEC_ARG_COLOR,
		RegCache::GEN_ARG_TEXPTR_PTR,
		RegCache::GEN_ARG_BUFW_PTR,
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

#if PPSSPP_PLATFORM(WINDOWS)
	// Free up some more vector regs on Windows, where we're a bit tight.
	SUB(64, R(RSP), Imm8(16 * 4 + 8));
	stackArgPos_ += 16 * 4 + 8;
	MOVDQA(MDisp(RSP, 0), XMM6);
	MOVDQA(MDisp(RSP, 16), XMM7);
	MOVDQA(MDisp(RSP, 32), XMM8);
	MOVDQA(MDisp(RSP, 48), XMM9);
	regCache_.Add(XMM6, RegCache::VEC_INVALID);
	regCache_.Add(XMM7, RegCache::VEC_INVALID);
	regCache_.Add(XMM8, RegCache::VEC_INVALID);
	regCache_.Add(XMM9, RegCache::VEC_INVALID);

	// Store frac UV in the gap.
	stackFracUV1Offset_ = -stackArgPos_ + 16 * 4;
#else
	// Use the red zone.
	stackFracUV1Offset_ = -stackArgPos_ - 8;
#endif

	// Reserve a couple regs that the nearest CALL won't use.
	if (id.hasAnyMips) {
		regCache_.ChangeReg(XMM6, RegCache::VEC_U1);
		regCache_.ChangeReg(XMM7, RegCache::VEC_V1);
		regCache_.ForceRetain(RegCache::VEC_U1);
		regCache_.ForceRetain(RegCache::VEC_V1);
	} else if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVELFRAC);
	}

	// Save prim color for later in a different XMM too.
	X64Reg primColorReg = regCache_.Find(RegCache::VEC_ARG_COLOR);
	MOVDQA(XMM9, R(primColorReg));
	regCache_.Unlock(primColorReg, RegCache::VEC_ARG_COLOR);
	regCache_.ForceRelease(RegCache::VEC_ARG_COLOR);
	regCache_.ChangeReg(XMM9, RegCache::VEC_ARG_COLOR);
	regCache_.ForceRetain(RegCache::VEC_ARG_COLOR);

	// We also want to save src and bufw for later.  Might be in a reg already.
	if (regCache_.Has(RegCache::GEN_ARG_TEXPTR_PTR)) {
		_assert_(regCache_.Has(RegCache::GEN_ARG_BUFW_PTR));
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		MOV(64, R(R14), R(srcReg));
		MOV(64, R(R15), R(bufwReg));
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR_PTR);
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW_PTR);
	} else {
		MOV(64, R(R14), MDisp(RSP, stackArgPos_ + 0));
		MOV(64, R(R15), MDisp(RSP, stackArgPos_ + 8));
	}

	// Okay, and now remember we moved to R14/R15.
	regCache_.ChangeReg(R14, RegCache::GEN_ARG_TEXPTR_PTR);
	regCache_.ChangeReg(R15, RegCache::GEN_ARG_BUFW_PTR);
	regCache_.ForceRetain(RegCache::GEN_ARG_TEXPTR_PTR);
	regCache_.ForceRetain(RegCache::GEN_ARG_BUFW_PTR);

	bool success = true;

	// Our first goal is to convert S/T and X/Y into U/V and frac_u/frac_v.
	success = success && Jit_GetTexelCoordsQuad(id);

	// Early exit on !srcPtr (either one.)
	FixupBranch zeroSrc;
	if (id.hasInvalidPtr) {
		Describe("NullCheck");
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);

		if (id.hasAnyMips) {
			X64Reg tempReg = regCache_.Alloc(RegCache::GEN_TEMP0);
			MOV(64, R(tempReg), MDisp(srcReg, 0));
			AND(64, R(tempReg), MDisp(srcReg, 8));

			CMP(PTRBITS, R(tempReg), Imm8(0));
			regCache_.Release(tempReg, RegCache::GEN_TEMP0);
		} else {
			CMP(PTRBITS, MatR(srcReg), Imm8(0));
		}
		FixupBranch nonZeroSrc = J_CC(CC_NZ);
		PXOR(XMM0, R(XMM0));
		zeroSrc = J(true);
		SetJumpTarget(nonZeroSrc);

		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR_PTR);
	}

	auto prepareDataOffsets = [&](RegCache::Purpose uPurpose, RegCache::Purpose vPurpose, bool level1) {
		X64Reg uReg = regCache_.Find(uPurpose);
		X64Reg vReg = regCache_.Find(vPurpose);
		success = success && Jit_PrepareDataOffsets(id, uReg, vReg, level1);
		regCache_.Unlock(uReg, uPurpose);
		regCache_.Unlock(vReg, vPurpose);
	};

	Describe("DataOffsets");
	prepareDataOffsets(RegCache::VEC_ARG_U, RegCache::VEC_ARG_V, false);
	if (id.hasAnyMips)
		prepareDataOffsets(RegCache::VEC_U1, RegCache::VEC_V1, true);

	regCache_.ChangeReg(XMM5, RegCache::VEC_RESULT);
	regCache_.ForceRetain(RegCache::VEC_RESULT);
	if (id.hasAnyMips) {
		regCache_.ChangeReg(XMM8, RegCache::VEC_RESULT1);
		regCache_.ForceRetain(RegCache::VEC_RESULT1);
	}

	// This stores the result in an XMM for later processing.
	// We map lookups to nearest CALLs, with arg order: u, v, src, bufw, level
	auto doNearestCall = [&](int off, bool level1) {
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

		X64Reg uReg = regCache_.Find(level1 ? RegCache::VEC_U1 : RegCache::VEC_ARG_U);
		X64Reg vReg = regCache_.Find(level1 ? RegCache::VEC_V1 : RegCache::VEC_ARG_V);
		// Otherwise, we'll overwrite them...
		_assert_(level1 || (uReg == XMM0 && vReg == XMM1));

		if (cpu_info.bSSE4_1) {
			PEXTRD(R(uArgReg), uReg, off / 4);
			PEXTRD(R(vArgReg), vReg, off / 4);
		} else {
			MOVD_xmm(R(uArgReg), uReg);
			MOVD_xmm(R(vArgReg), vReg);
			PSRLDQ(uReg, 4);
			PSRLDQ(vReg, 4);
		}
		regCache_.Unlock(uReg, level1 ? RegCache::VEC_U1 : RegCache::VEC_ARG_U);
		regCache_.Unlock(vReg, level1 ? RegCache::VEC_V1 : RegCache::VEC_ARG_V);

		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		MOV(64, R(srcArgReg), MDisp(srcReg, level1 ? 8 : 0));
		MOV(32, R(bufwArgReg), MDisp(bufwReg, level1 ? 4 : 0));
		// Leave level/levelFrac, we just always load from RAM on Windows and lock on POSIX.
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR_PTR);
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);

		CALL(nearest);

		X64Reg vecResultReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
		if (cpu_info.bSSE4_1) {
			PINSRD(vecResultReg, R(resultReg), off / 4);
		} else if (off == 0) {
			MOVD_xmm(vecResultReg, R(resultReg));
		} else {
			X64Reg tempReg = regCache_.Alloc(RegCache::VEC_TEMP0);
			MOVD_xmm(tempReg, R(resultReg));
			PSLLDQ(tempReg, off);
			POR(vecResultReg, R(tempReg));
			regCache_.Release(tempReg, RegCache::VEC_TEMP0);
		}
		regCache_.Unlock(vecResultReg, level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
	};

	Describe("Calls");
	doNearestCall(0, false);
	doNearestCall(4, false);
	doNearestCall(8, false);
	doNearestCall(12, false);

	if (id.hasAnyMips) {
		Describe("MipsCalls");
		if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
			X64Reg levelFracReg = regCache_.Find(RegCache::GEN_ARG_LEVELFRAC);
			CMP(8, R(levelFracReg), Imm8(0));
			regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
		} else {
			CMP(8, MDisp(RSP, stackArgPos_ + 24), Imm8(0));
		}
		FixupBranch skip = J_CC(CC_Z, true);

		// Modify the level, so the new level value is used.  We don't need the old.
		if (regCache_.Has(RegCache::GEN_ARG_LEVEL)) {
			X64Reg levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
			ADD(32, R(levelReg), Imm8(1));
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
		} else {
			// It's fine to just modify this in place.
			ADD(32, MDisp(RSP, stackArgPos_ + 16), Imm8(1));
		}

		doNearestCall(0, true);
		doNearestCall(4, true);
		doNearestCall(8, true);
		doNearestCall(12, true);

		SetJumpTarget(skip);
	}

	// We're done with these now.
	regCache_.ForceRelease(RegCache::VEC_ARG_U);
	regCache_.ForceRelease(RegCache::VEC_ARG_V);
	if (regCache_.Has(RegCache::VEC_U1))
		regCache_.ForceRelease(RegCache::VEC_U1);
	if (regCache_.Has(RegCache::VEC_V1))
		regCache_.ForceRelease(RegCache::VEC_V1);
	regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);
	regCache_.ForceRelease(RegCache::GEN_ARG_BUFW_PTR);
	if (regCache_.Has(RegCache::GEN_ARG_LEVEL))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);

	success = success && Jit_BlendQuad(id, false);
	if (id.hasAnyMips) {
		Describe("BlendMips");
		if (!regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
			X64Reg levelFracReg = regCache_.Alloc(RegCache::GEN_ARG_LEVELFRAC);
			MOVZX(32, 8, levelFracReg, MDisp(RSP, stackArgPos_ + 24));
			regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
			regCache_.ForceRetain(RegCache::GEN_ARG_LEVELFRAC);
		}

		X64Reg levelFracReg = regCache_.Find(RegCache::GEN_ARG_LEVELFRAC);
		CMP(8, R(levelFracReg), Imm8(0));
		FixupBranch skip = J_CC(CC_Z, true);

		success = success && Jit_BlendQuad(id, true);

		Describe("BlendMips");
		// First, broadcast the levelFrac value into an XMM.
		X64Reg fracReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		MOVD_xmm(fracReg, R(levelFracReg));
		PSHUFLW(fracReg, R(fracReg), _MM_SHUFFLE(0, 0, 0, 0));
		regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVELFRAC);

		// Multiply level1 color by the fraction.
		X64Reg color1Reg = regCache_.Find(RegCache::VEC_RESULT1);
		PMULLW(color1Reg, R(fracReg));

		// Okay, next we need an inverse for color 0.
		X64Reg invFracReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		MOVDQA(invFracReg, M(const10All16_));
		PSUBW(invFracReg, R(fracReg));

		// And multiply.
		PMULLW(XMM0, R(invFracReg));
		regCache_.Release(fracReg, RegCache::VEC_TEMP0);
		regCache_.Release(invFracReg, RegCache::VEC_TEMP1);

		// Okay, now sum and divide by 16 (which is what the fraction maxed at.)
		PADDW(XMM0, R(color1Reg));
		PSRLW(XMM0, 4);

		// And now we're done with color1Reg/VEC_RESULT1.
		regCache_.Unlock(color1Reg, RegCache::VEC_RESULT1);
		regCache_.ForceRelease(RegCache::VEC_RESULT1);

		SetJumpTarget(skip);
	}

	// Finally, it's time to apply the texture function.
	success = success && Jit_ApplyTextureFunc(id);

	// Last of all, convert to 32-bit channels.
	Describe("Init");
	if (cpu_info.bSSE4_1) {
		PMOVZXWD(XMM0, R(XMM0));
	} else {
		X64Reg zeroReg = GetZeroVec();
		PUNPCKLWD(XMM0, R(zeroReg));
		regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
	}

	regCache_.ForceRelease(RegCache::VEC_RESULT);

	if (!success) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(nearest));
		return nullptr;
	}

	if (id.hasInvalidPtr) {
		SetJumpTarget(zeroSrc);
	}

#if PPSSPP_PLATFORM(WINDOWS)
	MOVDQA(XMM6, MDisp(RSP, 0));
	MOVDQA(XMM7, MDisp(RSP, 16));
	MOVDQA(XMM8, MDisp(RSP, 32));
	MOVDQA(XMM9, MDisp(RSP, 48));
	ADD(64, R(RSP), Imm8(16 * 4 + 8));
#endif
	POP(R12);
	POP(R13);
	POP(R14);
	POP(R15);

	RET();

	regCache_.Reset(true);

	EndWrite();
	return (LinearFunc)start;
}

void SamplerJitCache::WriteConstantPool(const SamplerID &id) {
	// We reuse constants in any pool, because our code space is small.
	if (const10All16_ == nullptr) {
		const10All16_ = AlignCode16();
		for (int i = 0; i < 8; ++i)
			Write16(0x10);
	}

	if (const10Low_ == nullptr) {
		const10Low_ = AlignCode16();
		for (int i = 0; i < 4; ++i)
			Write16(0x10);
		for (int i = 0; i < 4; ++i)
			Write16(0);
	}

	if (const10All8_ == nullptr) {
		const10All8_ = AlignCode16();
		for (int i = 0; i < 16; ++i)
			Write8(0x10);
	}

	if (constOnes32_ == nullptr) {
		constOnes32_ = AlignCode16();
		for (int i = 0; i < 4; ++i)
			Write32(1);
	}

	if (constOnes16_ == nullptr) {
		constOnes16_ = AlignCode16();
		for (int i = 0; i < 8; ++i)
			Write16(1);
	}

	if (constUNext_ == nullptr) {
		constUNext_ = AlignCode16();
		Write32(0); Write32(1); Write32(0); Write32(1);
	}

	if (constVNext_ == nullptr) {
		constVNext_ = AlignCode16();
		Write32(0); Write32(0); Write32(1); Write32(1);
	}

	// These are unique to the sampler ID.
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
}

RegCache::Reg SamplerJitCache::GetZeroVec() {
	if (!regCache_.Has(RegCache::VEC_ZERO)) {
		X64Reg r = regCache_.Alloc(RegCache::VEC_ZERO);
		PXOR(r, R(r));
		return r;
	}
	return regCache_.Find(RegCache::VEC_ZERO);
}

RegCache::Reg SamplerJitCache::GetGState() {
	if (!regCache_.Has(RegCache::GEN_GSTATE)) {
		X64Reg r = regCache_.Alloc(RegCache::GEN_GSTATE);
		MOV(PTRBITS, R(r), ImmPtr(&gstate.nop));
		return r;
	}
	return regCache_.Find(RegCache::GEN_GSTATE);
}

bool SamplerJitCache::Jit_BlendQuad(const SamplerID &id, bool level1) {
	Describe(level1 ? "BlendQuadMips" : "BlendQuad");

	if (cpu_info.bSSE4_1 && cpu_info.bSSSE3) {
		// Let's start by rearranging from TL TR BL BR like this:
		// ABCD EFGH IJKL MNOP -> AI BJ CK DL EM FN GO HP -> AIEM BJFN CKGO DLHP
		// This way, all the RGBAs are next to each other, and in order TL BL TR BR.
		X64Reg quadReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
		X64Reg tempArrangeReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		PSHUFD(tempArrangeReg, R(quadReg), _MM_SHUFFLE(3, 2, 3, 2));
		PUNPCKLBW(quadReg, R(tempArrangeReg));
		// Okay, that's top and bottom interleaved, now for left and right.
		PSHUFD(tempArrangeReg, R(quadReg), _MM_SHUFFLE(3, 2, 3, 2));
		PUNPCKLWD(quadReg, R(tempArrangeReg));
		regCache_.Release(tempArrangeReg, RegCache::VEC_TEMP0);

		// Next up, we want to multiply and add using a repeated TB frac pair.
		// That's (0x10 - frac_v) in byte 1, frac_v in byte 2, repeating.
		X64Reg fracReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		X64Reg zeroReg = GetZeroVec();
		if (level1) {
			MOVD_xmm(fracReg, MDisp(RSP, stackArgPos_ + stackFracUV1Offset_ + 4));
		} else {
			X64Reg fracVReg = regCache_.Find(RegCache::GEN_ARG_FRAC_V);
			MOVD_xmm(fracReg, R(fracVReg));
			regCache_.Unlock(fracVReg, RegCache::GEN_ARG_FRAC_V);
			regCache_.ForceRelease(RegCache::GEN_ARG_FRAC_V);
		}
		PSHUFB(fracReg, R(zeroReg));
		regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);

		// Now, inverse fracReg, then interleave into the actual multiplier.
		// This gives us the repeated TB pairs we wanted.
		X64Reg multTBReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		MOVDQA(multTBReg, M(const10All8_));
		PSUBB(multTBReg, R(fracReg));
		PUNPCKLBW(multTBReg, R(fracReg));
		regCache_.Release(fracReg, RegCache::VEC_TEMP0);

		// Now we can multiply and add paired lanes in one go.
		// Note that since T+B=0x10, this gives us exactly 12 bits.
		PMADDUBSW(quadReg, R(multTBReg));
		regCache_.Release(multTBReg, RegCache::VEC_TEMP1);

		// With that done, we need to multiply by LR, or rather 0L0R, and sum again.
		// Since RRRR was all next to each other, this gives us a clean total R.
		fracReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		if (level1) {
			MOVD_xmm(fracReg, MDisp(RSP, stackArgPos_ + stackFracUV1Offset_));
		} else {
			X64Reg fracUReg = regCache_.Find(RegCache::GEN_ARG_FRAC_U);
			MOVD_xmm(fracReg, R(fracUReg));
			regCache_.Unlock(fracUReg, RegCache::GEN_ARG_FRAC_U);
			regCache_.ForceRelease(RegCache::GEN_ARG_FRAC_U);
		}
		// We can ignore the high bits, since we'll interleave those away anyway.
		PSHUFLW(fracReg, R(fracReg), _MM_SHUFFLE(0, 0, 0, 0));

		// Again, we're inversing into an interleaved multiplier.  L is the inversed one.
		// 0L0R is (0x10 - frac_u), frac_u - 2x16 repeated four times.
		X64Reg multLRReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		MOVDQA(multLRReg, M(const10All16_));
		PSUBW(multLRReg, R(fracReg));
		PUNPCKLWD(multLRReg, R(fracReg));
		regCache_.Release(fracReg, RegCache::VEC_TEMP0);

		// This gives us RGBA as dwords, but they're all shifted left by 8 from the multiplies.
		PMADDWD(quadReg, R(multLRReg));
		PSRLD(quadReg, 8);
		regCache_.Release(multLRReg, RegCache::VEC_TEMP1);

		// Shrink to 16-bit, it's more convenient for later.
		PACKSSDW(quadReg, R(quadReg));
		if (level1) {
			regCache_.Unlock(quadReg, RegCache::VEC_RESULT1);
		} else {
			MOVDQA(XMM0, R(quadReg));
			regCache_.Unlock(quadReg, RegCache::VEC_RESULT);

			regCache_.ForceRelease(RegCache::VEC_RESULT);
			bool changeSuccess = regCache_.ChangeReg(XMM0, RegCache::VEC_RESULT);
			_assert_msg_(changeSuccess, "Unexpected reg locked as destReg");
		}
	} else {
		X64Reg topReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		X64Reg bottomReg = regCache_.Alloc(RegCache::VEC_TEMP1);

		X64Reg quadReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
		if (!cpu_info.bSSE4_1) {
			X64Reg zeroReg = GetZeroVec();
			PSHUFD(topReg, R(quadReg), _MM_SHUFFLE(0, 0, 1, 0));
			PSHUFD(bottomReg, R(quadReg), _MM_SHUFFLE(0, 0, 3, 2));
			PUNPCKLBW(topReg, R(zeroReg));
			PUNPCKLBW(bottomReg, R(zeroReg));
			regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		} else {
			PSHUFD(bottomReg, R(quadReg), _MM_SHUFFLE(0, 0, 3, 2));
			PMOVZXBW(topReg, R(quadReg));
			PMOVZXBW(bottomReg, R(bottomReg));
		}
		if (!level1) {
			regCache_.Unlock(quadReg, RegCache::VEC_RESULT);
			regCache_.ForceRelease(RegCache::VEC_RESULT);
		}

		// Grab frac_u and spread to lower (L) lanes.
		X64Reg fracReg = regCache_.Alloc(RegCache::VEC_TEMP2);
		X64Reg fracMulReg = regCache_.Alloc(RegCache::VEC_TEMP3);
		if (level1) {
			MOVD_xmm(fracReg, MDisp(RSP, stackArgPos_ + stackFracUV1Offset_));
		} else {
			X64Reg fracUReg = regCache_.Find(RegCache::GEN_ARG_FRAC_U);
			MOVD_xmm(fracReg, R(fracUReg));
			regCache_.Unlock(fracUReg, RegCache::GEN_ARG_FRAC_U);
			regCache_.ForceRelease(RegCache::GEN_ARG_FRAC_U);
		}
		PSHUFLW(fracReg, R(fracReg), _MM_SHUFFLE(0, 0, 0, 0));
		// Now subtract 0x10 - frac_u in the L lanes only: 00000000 LLLLLLLL.
		MOVDQA(fracMulReg, M(const10Low_));
		PSUBW(fracMulReg, R(fracReg));
		// Then we just shift and OR in the original frac_u.
		PSLLDQ(fracReg, 8);
		POR(fracMulReg, R(fracReg));
		regCache_.Release(fracReg, RegCache::VEC_TEMP2);

		// Okay, we have 8-bits in the top and bottom rows for the color.
		// Multiply by frac to get 12, which we keep for the next stage.
		PMULLW(topReg, R(fracMulReg));
		PMULLW(bottomReg, R(fracMulReg));
		regCache_.Release(fracMulReg, RegCache::VEC_TEMP3);

		// Time for frac_v.  This time, we want it in all 8 lanes.
		fracReg = regCache_.Alloc(RegCache::VEC_TEMP2);
		X64Reg fracTopReg = regCache_.Alloc(RegCache::VEC_TEMP3);
		if (level1) {
			MOVD_xmm(fracReg, MDisp(RSP, stackArgPos_ + stackFracUV1Offset_ + 4));
		} else {
			X64Reg fracVReg = regCache_.Find(RegCache::GEN_ARG_FRAC_V);
			MOVD_xmm(fracReg, R(fracVReg));
			regCache_.Unlock(fracVReg, RegCache::GEN_ARG_FRAC_V);
			regCache_.ForceRelease(RegCache::GEN_ARG_FRAC_V);
		}
		PSHUFLW(fracReg, R(fracReg), _MM_SHUFFLE(0, 0, 0, 0));
		PSHUFD(fracReg, R(fracReg), _MM_SHUFFLE(0, 0, 0, 0));

		// Now, inverse fracReg into fracTopReg for the top row.
		MOVDQA(fracTopReg, M(const10All16_));
		PSUBW(fracTopReg, R(fracReg));

		// We had 12, plus 4 frac, that gives us 16.
		PMULLW(bottomReg, R(fracReg));
		PMULLW(topReg, R(fracTopReg));
		regCache_.Release(fracReg, RegCache::VEC_TEMP2);
		regCache_.Release(fracTopReg, RegCache::VEC_TEMP3);

		// Finally, time to sum them all up and divide by 256 to get back to 8 bits.
		PADDUSW(bottomReg, R(topReg));
		regCache_.Release(topReg, RegCache::VEC_TEMP0);

		if (level1) {
			PSHUFD(quadReg, R(bottomReg), _MM_SHUFFLE(3, 2, 3, 2));
			PADDUSW(quadReg, R(bottomReg));
			PSRLW(quadReg, 8);
			regCache_.Release(bottomReg, RegCache::VEC_TEMP1);
			regCache_.Unlock(quadReg, RegCache::VEC_RESULT1);
		} else {
			bool changeSuccess = regCache_.ChangeReg(XMM0, RegCache::VEC_RESULT);
			if (!changeSuccess) {
				_assert_msg_(XMM0 == bottomReg, "Unexpected other reg locked as destReg");
				X64Reg otherReg = regCache_.Alloc(RegCache::VEC_TEMP0);
				PSHUFD(otherReg, R(bottomReg), _MM_SHUFFLE(3, 2, 3, 2));
				PADDUSW(bottomReg, R(otherReg));
				regCache_.Release(otherReg, RegCache::VEC_TEMP0);
				regCache_.Release(bottomReg, RegCache::VEC_TEMP1);

				// Okay, now it can be changed.
				regCache_.ChangeReg(XMM0, RegCache::VEC_RESULT);
			} else {
				PSHUFD(XMM0, R(bottomReg), _MM_SHUFFLE(3, 2, 3, 2));
				PADDUSW(XMM0, R(bottomReg));
				regCache_.Release(bottomReg, RegCache::VEC_TEMP1);
			}

			PSRLW(XMM0, 8);
		}
	}

	return true;
}

bool SamplerJitCache::Jit_ApplyTextureFunc(const SamplerID &id) {
	X64Reg resultReg = regCache_.Find(RegCache::VEC_RESULT);
	X64Reg primColorReg = regCache_.Find(RegCache::VEC_ARG_COLOR);
	X64Reg tempReg = regCache_.Alloc(RegCache::VEC_TEMP0);

	auto useAlphaFrom = [&](X64Reg alphaColorReg) {
		if (cpu_info.bSSE4_1) {
			// Copy only alpha.
			PBLENDW(resultReg, R(alphaColorReg), 0x08);
		} else {
			PSRLDQ(alphaColorReg, 6);
			PSLLDQ(alphaColorReg, 6);
			// Zero out the result alpha and OR them together.
			PSLLDQ(resultReg, 10);
			PSRLDQ(resultReg, 10);
			POR(resultReg, R(alphaColorReg));
		}
	};

	// Note: color is in DWORDs, but result is in WORDs.
	switch (id.TexFunc()) {
	case GE_TEXFUNC_MODULATE:
		Describe("Modulate");
		PACKSSDW(primColorReg, R(primColorReg));
		MOVDQA(tempReg, M(constOnes16_));
		PADDW(tempReg, R(primColorReg));

		// Okay, time to multiply.  This produces 16 bits, neatly.
		PMULLW(resultReg, R(tempReg));
		if (id.useColorDoubling)
			PSRLW(resultReg, 7);
		else
			PSRLW(resultReg, 8);

		if (!id.useTextureAlpha) {
			useAlphaFrom(primColorReg);
		} else if (id.useColorDoubling) {
			// We still need to finish dividing alpha, it's currently doubled (from the 7 above.)
			MOVDQA(primColorReg, R(resultReg));
			PSRLW(primColorReg, 1);
			useAlphaFrom(primColorReg);
		}
		break;

	case GE_TEXFUNC_DECAL:
		Describe("Decal");
		PACKSSDW(primColorReg, R(primColorReg));
		if (id.useTextureAlpha) {
			// Get alpha into the tempReg.
			PSHUFLW(tempReg, R(resultReg), _MM_SHUFFLE(3, 3, 3, 3));
			PADDW(resultReg, M(constOnes16_));
			PMULLW(resultReg, R(tempReg));

			X64Reg invAlphaReg = regCache_.Alloc(RegCache::VEC_TEMP1);
			// Materialize some 255s, and subtract out alpha.
			PCMPEQD(invAlphaReg, R(invAlphaReg));
			PSRLW(invAlphaReg, 8);
			PSUBW(invAlphaReg, R(tempReg));

			MOVDQA(tempReg, R(primColorReg));
			PADDW(tempReg, M(constOnes16_));
			PMULLW(tempReg, R(invAlphaReg));
			regCache_.Release(invAlphaReg, RegCache::VEC_TEMP1);

			// Now sum, and divide.
			PADDW(resultReg, R(tempReg));
			if (id.useColorDoubling)
				PSRLW(resultReg, 7);
			else
				PSRLW(resultReg, 8);
		}
		useAlphaFrom(primColorReg);
		break;

	case GE_TEXFUNC_BLEND:
	{
		Describe("EnvBlend");
		PACKSSDW(primColorReg, R(primColorReg));

		// Start out with the prim color side.  Materialize a 255 to inverse resultReg and round.
		PCMPEQD(tempReg, R(tempReg));
		PSRLW(tempReg, 8);

		// We're going to lose tempReg, so save the 255s.
		X64Reg roundValueReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		MOVDQA(roundValueReg, R(tempReg));

		PSUBW(tempReg, R(resultReg));
		PMULLW(tempReg, R(primColorReg));
		// Okay, now add the rounding value.
		PADDW(tempReg, R(roundValueReg));
		regCache_.Release(roundValueReg, RegCache::VEC_TEMP1);

		if (id.useTextureAlpha) {
			// Before we modify the texture color, let's calculate alpha.
			PADDW(primColorReg, M(constOnes16_));
			PMULLW(primColorReg, R(resultReg));
			// We divide later.
		}

		X64Reg gstateReg = GetGState();
		X64Reg texEnvReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		if (cpu_info.bSSE4_1) {
			PMOVZXBW(texEnvReg, MDisp(gstateReg, offsetof(GPUgstate, texenvcolor)));
		} else {
			MOVD_xmm(texEnvReg, MDisp(gstateReg, offsetof(GPUgstate, texenvcolor)));
			X64Reg zeroReg = GetZeroVec();
			PUNPCKLBW(texEnvReg, R(zeroReg));
			regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		}
		PMULLW(resultReg, R(texEnvReg));
		regCache_.Release(texEnvReg, RegCache::VEC_TEMP1);
		regCache_.Unlock(gstateReg, RegCache::GEN_GSTATE);

		// Add in the prim color side and divide.
		PADDW(resultReg, R(tempReg));
		if (id.useColorDoubling)
			PSRLW(resultReg, 7);
		else
			PSRLW(resultReg, 8);

		if (id.useTextureAlpha) {
			// We put the alpha in here, just need to divide it after that multiply.
			PSRLW(primColorReg, 8);
		}
		useAlphaFrom(primColorReg);
		break;
	}

	case GE_TEXFUNC_REPLACE:
		Describe("Replace");
		if (id.useColorDoubling && id.useTextureAlpha) {
			// We can abuse primColorReg as a temp.
			MOVDQA(primColorReg, R(resultReg));
			// Shift to zero out alpha in resultReg.
			PSLLDQ(resultReg, 10);
			PSRLDQ(resultReg, 10);
			// Now simply add them together, restoring alpha and doubling the colors.
			PADDW(resultReg, R(primColorReg));
		} else if (!id.useTextureAlpha) {
			if (id.useColorDoubling) {
				// Let's just double using shifting.  Ignore alpha.
				PSLLW(resultReg, 1);
			}
			// Now we want prim_color in W, so convert, then shift-mask away the color.
			PACKSSDW(primColorReg, R(primColorReg));
			useAlphaFrom(primColorReg);
		}
		break;

	case GE_TEXFUNC_ADD:
	case GE_TEXFUNC_UNKNOWN1:
	case GE_TEXFUNC_UNKNOWN2:
	case GE_TEXFUNC_UNKNOWN3:
		Describe("Add");
		PACKSSDW(primColorReg, R(primColorReg));
		if (id.useTextureAlpha) {
			MOVDQA(tempReg, M(constOnes16_));
			// Add and multiply the alpha (and others, but we'll mask them.)
			PADDW(tempReg, R(primColorReg));
			PMULLW(tempReg, R(resultReg));

			// Now that we've extracted alpha, sum and double as needed.
			PADDW(resultReg, R(primColorReg));
			if (id.useColorDoubling)
				PSLLW(resultReg, 1);

			// Divide by 256 to normalize alpha.
			PSRLW(tempReg, 8);
			useAlphaFrom(tempReg);
		} else {
			PADDW(resultReg, R(primColorReg));
			if (id.useColorDoubling)
				PSLLW(resultReg, 1);
			useAlphaFrom(primColorReg);
		}
		break;
	}

	regCache_.Release(tempReg, RegCache::VEC_TEMP0);
	regCache_.Unlock(resultReg, RegCache::VEC_RESULT);
	regCache_.Unlock(primColorReg, RegCache::VEC_ARG_COLOR);
	regCache_.ForceRelease(RegCache::VEC_ARG_COLOR);
	return true;
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
	Describe("DXT1");
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
		Describe("DXT3A");
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
		Describe("DXT5A");
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
		Describe("TexDataL");
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

	Describe("TexData");
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
		Describe("TexDataS4L");
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

	Describe("TexDataS4");
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
		Describe("TexDataSL");
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

	Describe("TexDataS");
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
	Describe("TexelQuad");
	// RCX ought to be free, it was either bufw or never used.
	bool success = regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
	_assert_msg_(success, "Should have RCX free");

	X64Reg sReg = regCache_.Find(RegCache::VEC_ARG_S);
	X64Reg tReg = regCache_.Find(RegCache::VEC_ARG_T);

	// Start by multiplying with the width/height... which might be complex with mips.
	X64Reg width0VecReg = INVALID_REG;
	X64Reg height0VecReg = INVALID_REG;
	X64Reg width1VecReg = INVALID_REG;
	X64Reg height1VecReg = INVALID_REG;

	if (constWidth256f_ == nullptr) {
		// We have to figure out levels and the proper width, ugh.
		X64Reg shiftReg = regCache_.Find(RegCache::GEN_SHIFTVAL);
		X64Reg gstateReg = GetGState();
		X64Reg tempReg = regCache_.Alloc(RegCache::GEN_TEMP0);

		X64Reg levelReg = INVALID_REG;
		// To avoid ABI problems, we don't hold onto level.
		bool releaseLevelReg = !regCache_.Has(RegCache::GEN_ARG_LEVEL);
		if (!releaseLevelReg) {
			levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
		} else {
			releaseLevelReg = true;
			levelReg = regCache_.Alloc(RegCache::GEN_ARG_LEVEL);
			MOV(32, R(levelReg), MDisp(RSP, stackArgPos_ + 16));
		}

		MOV(PTRBITS, R(gstateReg), ImmPtr(&gstate.nop));

		X64Reg tempVecReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		auto loadSizeAndMul = [&](X64Reg dest, X64Reg size, bool isY, bool isLevel1) {
			int offset = offsetof(GPUgstate, texsize) + (isY ? 1 : 0) + (isLevel1 ? 4 : 0);
			// Grab the size, and shift.
			MOVZX(32, 8, shiftReg, MComplex(gstateReg, levelReg, SCALE_4, offset));
			AND(32, R(shiftReg), Imm8(0x0F));
			MOV(32, R(tempReg), Imm32(1));
			SHL(32, R(tempReg), R(shiftReg));

			// Okay, now move into a vec (two, in fact, one for the multiply.)
			MOVD_xmm(tempVecReg, R(tempReg));
			PSHUFD(size, R(tempVecReg), _MM_SHUFFLE(0, 0, 0, 0));

			// Multiply by 256 and convert to a float.
			PSLLD(tempVecReg, 8);
			CVTDQ2PS(tempVecReg, R(tempVecReg));
			// And then multiply.
			MULPS(dest, R(tempVecReg));
		};

		// Copy out S and T so we can multiply.
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		MOVDQA(u1Reg, R(sReg));
		MOVDQA(v1Reg, R(tReg));

		// Load width and height for the given level, and multiply sReg/tReg meanwhile.
		width0VecReg = regCache_.Alloc(RegCache::VEC_TEMP2);
		loadSizeAndMul(sReg, width0VecReg, false, false);
		height0VecReg = regCache_.Alloc(RegCache::VEC_TEMP3);
		loadSizeAndMul(tReg, height0VecReg, true, false);

		// And same for the next level, but with u1Reg/v1Reg.
		width1VecReg = regCache_.Alloc(RegCache::VEC_TEMP4);
		loadSizeAndMul(u1Reg, width1VecReg, false, true);
		height1VecReg = regCache_.Alloc(RegCache::VEC_TEMP5);
		loadSizeAndMul(v1Reg, height1VecReg, true, true);

		regCache_.Unlock(shiftReg, RegCache::GEN_SHIFTVAL);
		regCache_.Unlock(gstateReg, RegCache::GEN_GSTATE);
		regCache_.Release(tempReg, RegCache::GEN_TEMP0);
		if (releaseLevelReg)
			regCache_.Release(levelReg, RegCache::GEN_ARG_LEVEL);
		else
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);

		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);

		// Now just subtract one.  We use this later for clamp/wrap.
		MOVDQA(tempVecReg, M(constOnes32_));
		PSUBD(width0VecReg, R(tempVecReg));
		PSUBD(height0VecReg, R(tempVecReg));
		PSUBD(width1VecReg, R(tempVecReg));
		PSUBD(height1VecReg, R(tempVecReg));
		regCache_.Release(tempVecReg, RegCache::VEC_TEMP0);
	} else {
		// Easy mode.
		MULSS(sReg, M(constWidth256f_));
		MULSS(tReg, M(constHeight256f_));
	}

	// And now, convert to integers for all later processing.
	CVTPS2DQ(sReg, R(sReg));
	CVTPS2DQ(tReg, R(tReg));
	if (regCache_.Has(RegCache::VEC_U1)) {
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		CVTPS2DQ(u1Reg, R(u1Reg));
		CVTPS2DQ(v1Reg, R(v1Reg));
		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);
	}

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
	if (regCache_.Has(RegCache::VEC_U1)) {
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		PADDD(u1Reg, R(tempXYReg));
		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
	}
	MOVD_xmm(tempXYReg, R(yReg));
	PADDD(tReg, R(tempXYReg));
	if (regCache_.Has(RegCache::VEC_V1)) {
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		PADDD(v1Reg, R(tempXYReg));
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);
	}
	regCache_.Release(tempXYReg, RegCache::VEC_TEMP0);

	regCache_.Unlock(xReg, RegCache::GEN_ARG_X);
	regCache_.Unlock(yReg, RegCache::GEN_ARG_Y);
	regCache_.ForceRelease(RegCache::GEN_ARG_X);
	regCache_.ForceRelease(RegCache::GEN_ARG_Y);

	// We do want the fraction, though, so extract that.
	X64Reg fracUReg = regCache_.Find(RegCache::GEN_ARG_FRAC_U);
	X64Reg fracVReg = regCache_.Find(RegCache::GEN_ARG_FRAC_V);
	if (regCache_.Has(RegCache::VEC_U1)) {
		// Start with the next level so we end with current in the regs.
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		MOVD_xmm(R(fracUReg), u1Reg);
		MOVD_xmm(R(fracVReg), v1Reg);
		SHR(32, R(fracUReg), Imm8(4));
		AND(32, R(fracUReg), Imm8(0x0F));
		SHR(32, R(fracVReg), Imm8(4));
		AND(32, R(fracVReg), Imm8(0x0F));
		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);

		// Store them on the stack for now.
		MOV(32, MDisp(RSP, stackArgPos_ + stackFracUV1Offset_), R(fracUReg));
		MOV(32, MDisp(RSP, stackArgPos_ + stackFracUV1Offset_ + 4), R(fracVReg));
	}
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

	if (regCache_.Has(RegCache::VEC_U1)) {
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		PSRAD(u1Reg, 8);
		PSRAD(v1Reg, 8);
		PSHUFD(u1Reg, R(u1Reg), _MM_SHUFFLE(0, 0, 0, 0));
		PSHUFD(v1Reg, R(v1Reg), _MM_SHUFFLE(0, 0, 0, 0));
		PADDD(u1Reg, M(constUNext_));
		PADDD(v1Reg, M(constVNext_));
		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);
	}

	X64Reg temp0ClampReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	bool temp0ClampZero = false;

	auto doClamp = [&](bool clamp, X64Reg stReg, const OpArg &bound) {
		if (!clamp) {
			// Wrapping is easy.
			PAND(stReg, bound);
			return;
		}

		if (!temp0ClampZero)
			PXOR(temp0ClampReg, R(temp0ClampReg));
		temp0ClampZero = true;

		if (cpu_info.bSSE4_1) {
			PMINSD(stReg, bound);
			PMAXSD(stReg, R(temp0ClampReg));
		} else {
			temp0ClampZero = false;
			// Set temp to max(0, stReg) = AND(NOT(0 > stReg), stReg).
			PCMPGTD(temp0ClampReg, R(stReg));
			PANDN(temp0ClampReg, R(stReg));

			// Now make a mask where bound is greater than the ST value in temp0ClampReg.
			MOVDQA(stReg, bound);
			PCMPGTD(stReg, R(temp0ClampReg));
			// Throw away the values that are greater in our temp0ClampReg in progress result.
			PAND(temp0ClampReg, R(stReg));

			// Now, set bound only where ST was too high.
			PANDN(stReg, bound);
			// And put in the values that were fine.
			POR(stReg, R(temp0ClampReg));
		}
	};

	doClamp(id.clampS, sReg, width0VecReg == INVALID_REG ? M(constWidthMinus1i_) : R(width0VecReg));
	doClamp(id.clampT, tReg, height0VecReg == INVALID_REG ? M(constHeightMinus1i_) : R(height0VecReg));
	if (width1VecReg != INVALID_REG) {
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		doClamp(id.clampS, u1Reg, R(width1VecReg));
		doClamp(id.clampT, v1Reg, R(height1VecReg));
		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);
	}

	if (width0VecReg != INVALID_REG)
		regCache_.Release(width0VecReg, RegCache::VEC_TEMP2);
	if (height0VecReg != INVALID_REG)
		regCache_.Release(height0VecReg, RegCache::VEC_TEMP3);
	if (width1VecReg != INVALID_REG)
		regCache_.Release(width1VecReg, RegCache::VEC_TEMP4);
	if (height1VecReg != INVALID_REG)
		regCache_.Release(height1VecReg, RegCache::VEC_TEMP5);

	regCache_.Release(temp0ClampReg, RegCache::VEC_TEMP0);

	regCache_.Unlock(sReg, RegCache::VEC_ARG_S);
	regCache_.Unlock(tReg, RegCache::VEC_ARG_T);
	regCache_.Change(RegCache::VEC_ARG_S, RegCache::VEC_ARG_U);
	regCache_.Change(RegCache::VEC_ARG_T, RegCache::VEC_ARG_V);
	return true;
}

bool SamplerJitCache::Jit_PrepareDataOffsets(const SamplerID &id, RegCache::Reg uReg, RegCache::Reg vReg, bool level1) {
	_assert_(id.linear);

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
			success = Jit_PrepareDataSwizzledOffsets(id, uReg, vReg, level1, bits);
		} else {
			success = Jit_PrepareDataDirectOffsets(id, uReg, vReg, level1, bits);
		}
	}

	return success;
}

bool SamplerJitCache::Jit_PrepareDataDirectOffsets(const SamplerID &id, RegCache::Reg uReg, RegCache::Reg vReg, bool level1, int bitsPerTexel) {
	Describe("DataOff");
	X64Reg bufwVecReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	if (!id.useStandardBufw || id.hasAnyMips) {
		// Spread bufw into each lane.
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		MOVD_xmm(bufwVecReg, MDisp(bufwReg, level1 ? 4 : 0));
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);
		PSHUFD(bufwVecReg, R(bufwVecReg), _MM_SHUFFLE(0, 0, 0, 0));

		if (bitsPerTexel == 4)
			PSRLD(bufwVecReg, 1);
		else if (bitsPerTexel == 16)
			PSLLD(bufwVecReg, 1);
		else if (bitsPerTexel == 32)
			PSLLD(bufwVecReg, 2);
	}

	if (id.useStandardBufw && !id.hasAnyMips) {
		int amt = id.width0Shift;
		if (bitsPerTexel == 4)
			amt -= 1;
		else if (bitsPerTexel == 16)
			amt += 1;
		else if (bitsPerTexel == 32)
			amt += 2;
		// It's aligned to 16 bytes, so must at least be 16.
		PSLLD(vReg, std::max(4, amt));
	} else if (cpu_info.bSSE4_1) {
		// And now multiply.  This is slow, but not worse than the SSE2 version...
		PMULLD(vReg, R(bufwVecReg));
	} else {
		// Copy that into another temp for multiply.
		X64Reg vOddLaneReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		MOVDQA(vOddLaneReg, R(vReg));

		// Okay, first, multiply to get XXXX CCCC XXXX AAAA.
		PMULUDQ(vReg, R(bufwVecReg));
		PSRLDQ(vOddLaneReg, 4);
		PSRLDQ(bufwVecReg, 4);
		// And now get XXXX DDDD XXXX BBBB.
		PMULUDQ(vOddLaneReg, R(bufwVecReg));

		// We know everything is positive, so XXXX must be zero.  Let's combine.
		PSLLDQ(vOddLaneReg, 4);
		POR(vReg, R(vOddLaneReg));
		regCache_.Release(vOddLaneReg, RegCache::VEC_TEMP1);
	}
	regCache_.Release(bufwVecReg, RegCache::VEC_TEMP0);

	if (bitsPerTexel == 4) {
		// Need to keep uvec for the odd bit.
		X64Reg uCopyReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		MOVDQA(uCopyReg, R(uReg));
		PSRLD(uCopyReg, 1);
		PADDD(vReg, R(uCopyReg));
		regCache_.Release(uCopyReg, RegCache::VEC_TEMP0);
	} else {
		// Destroy uvec, we won't use it again.
		if (bitsPerTexel == 16)
			PSLLD(uReg, 1);
		else if (bitsPerTexel == 32)
			PSLLD(uReg, 2);
		PADDD(vReg, R(uReg));
	}

	return true;
}

bool SamplerJitCache::Jit_PrepareDataSwizzledOffsets(const SamplerID &id, RegCache::Reg uReg, RegCache::Reg vReg, bool level1, int bitsPerTexel) {
	Describe("DataOffS");
	// See Jit_GetTexDataSwizzled() for usage of this offset.

	X64Reg bufwVecReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	if (!id.useStandardBufw || id.hasAnyMips) {
		// Spread bufw into each lane.
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		MOVD_xmm(bufwVecReg, MDisp(bufwReg, level1 ? 4 : 0));
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);
		PSHUFD(bufwVecReg, R(bufwVecReg), _MM_SHUFFLE(0, 0, 0, 0));
	}

	// Divide vvec by 8 in a temp.
	X64Reg vMultReg = regCache_.Alloc(RegCache::VEC_TEMP1);
	MOVDQA(vMultReg, R(vReg));
	PSRLD(vMultReg, 3);

	// And now multiply by bufw.  May be able to use a shift in a common case.
	int shiftAmount = 32 - clz32_nonzero(bitsPerTexel - 1);
	if (id.useStandardBufw && !id.hasAnyMips) {
		int amt = id.width0Shift;
		// Account for 16 byte minimum.
		amt = std::max(7 - shiftAmount, amt);
		shiftAmount += amt;
	} else if (cpu_info.bSSE4_1) {
		// And now multiply.  This is slow, but not worse than the SSE2 version...
		PMULLD(vMultReg, R(bufwVecReg));
	} else {
		// Copy that into another temp for multiply.
		X64Reg vOddLaneReg = regCache_.Alloc(RegCache::VEC_TEMP2);
		MOVDQA(vOddLaneReg, R(vMultReg));

		// Okay, first, multiply to get XXXX CCCC XXXX AAAA.
		PMULUDQ(vMultReg, R(bufwVecReg));
		PSRLDQ(vOddLaneReg, 4);
		PSRLDQ(bufwVecReg, 4);
		// And now get XXXX DDDD XXXX BBBB.
		PMULUDQ(vOddLaneReg, R(bufwVecReg));

		// We know everything is positive, so XXXX must be zero.  Let's combine.
		PSLLDQ(vOddLaneReg, 4);
		POR(vMultReg, R(vOddLaneReg));
		regCache_.Release(vOddLaneReg, RegCache::VEC_TEMP2);
	}
	regCache_.Release(bufwVecReg, RegCache::VEC_TEMP0);

	// Multiply the result by bitsPerTexel using a shift.
	PSLLD(vMultReg, shiftAmount);

	// Now we're adding (v & 7) * 16.  Use a 16-bit wall.
	PSLLW(vReg, 13);
	PSRLD(vReg, 9);
	PADDD(vReg, R(vMultReg));
	regCache_.Release(vMultReg, RegCache::VEC_TEMP1);

	// Now get ((uvec / texels_per_tile) / 4) * 32 * 4 aka (uvec / (128 / bitsPerTexel)) << 7.
	X64Reg uCopyReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	MOVDQA(uCopyReg, R(uReg));
	PSRLD(uCopyReg, 7 + clz32_nonzero(bitsPerTexel - 1) - 32);
	PSLLD(uCopyReg, 7);
	// Add it in to our running total.
	PADDD(vReg, R(uCopyReg));

	if (bitsPerTexel == 4) {
		// Finally, we want (uvec & 31) / 2.  Use a 16-bit wall.
		MOVDQA(uCopyReg, R(uReg));
		PSLLW(uCopyReg, 11);
		PSRLD(uCopyReg, 12);
		// With that, this is our byte offset.  uvec & 1 has which half.
		PADDD(vReg, R(uCopyReg));
	} else {
		// We can destroy uvec in this path.  Clear all but 2 bits for 32, 3 for 16, or 4 for 8.
		PSLLW(uReg, 32 - clz32_nonzero(bitsPerTexel - 1) + 9);
		// Now that it's at the top of the 16 bits, we always shift that to the top of 4 bits.
		PSRLD(uReg, 12);
		PADDD(vReg, R(uReg));
	}
	regCache_.Release(uCopyReg, RegCache::VEC_TEMP0);

	return true;
}

bool SamplerJitCache::Jit_Decode5650() {
	Describe("5650");
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
	Describe("5551");
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
	Describe("4444");
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
	Describe("TrCLUT");
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
	Describe("ReadCLUT");
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
				// Extra 8 to account for call.
				MOV(32, R(temp2Reg), MDisp(RSP, stackArgPos_ + 8 + 16));
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
