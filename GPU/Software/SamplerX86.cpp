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

namespace Sampler {

FetchFunc SamplerJitCache::CompileFetch(const SamplerID &id) {
	_assert_msg_(id.fetch && !id.linear, "Only fetch should be set on sampler id");
	regCache_.SetupABI({
		RegCache::GEN_ARG_U,
		RegCache::GEN_ARG_V,
		RegCache::GEN_ARG_TEXPTR,
		RegCache::GEN_ARG_BUFW,
		RegCache::GEN_ARG_LEVEL,
		RegCache::GEN_ARG_ID,
	});
	regCache_.ChangeReg(RAX, RegCache::GEN_RESULT);
	regCache_.ForceRetain(RegCache::GEN_RESULT);
	regCache_.ChangeReg(XMM0, RegCache::VEC_RESULT);

	BeginWrite(2048);
	Describe("Init");
	const u8 *start = AlignCode16();

#if PPSSPP_PLATFORM(WINDOWS)
	// RET and shadow space.
	stackArgPos_ = 8 + 32;
	stackIDOffset_ = 8;
	stackLevelOffset_ = 0;
#else
	stackArgPos_ = 0;
	stackIDOffset_ = -1;
	stackLevelOffset_ = -1;
#endif

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
		ERROR_LOG(Log::G3D, "Failed to compile fetch %s", DescribeSamplerID(id).c_str());
		return nullptr;
	}

	if (regCache_.Has(RegCache::GEN_ARG_LEVEL))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);
	if (regCache_.Has(RegCache::GEN_ARG_ID))
		regCache_.ForceRelease(RegCache::GEN_ARG_ID);

	X64Reg vecResultReg = regCache_.Find(RegCache::VEC_RESULT);

	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	MOVD_xmm(vecResultReg, R(resultReg));
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	regCache_.ForceRelease(RegCache::GEN_RESULT);

	if (cpu_info.bSSE4_1) {
		PMOVZXBD(vecResultReg, R(vecResultReg));
	} else {
		X64Reg vecTempReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		PXOR(vecTempReg, R(vecTempReg));
		PUNPCKLBW(vecResultReg, R(vecTempReg));
		PUNPCKLWD(vecResultReg, R(vecTempReg));
		regCache_.Release(vecTempReg, RegCache::VEC_TEMP0);
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
	BeginWrite(2048);
	Describe("Init");

	// Let's drop some helpful constants here.
	WriteConstantPool(id);

	const u8 *start = AlignCode16();

	regCache_.SetupABI({
		RegCache::VEC_ARG_S,
		RegCache::VEC_ARG_T,
		RegCache::VEC_ARG_COLOR,
		RegCache::GEN_ARG_TEXPTR_PTR,
		RegCache::GEN_ARG_BUFW_PTR,
		RegCache::GEN_ARG_LEVEL,
		RegCache::GEN_ARG_LEVELFRAC,
		RegCache::GEN_ARG_ID,
	});

#if PPSSPP_PLATFORM(WINDOWS)
	// RET + shadow space.
	stackArgPos_ = 8 + 32;

	// Positions: stackArgPos_+0=bufwptr, stackArgPos_+8=level, stackArgPos_+16=levelFrac
	stackIDOffset_ = 24;
	stackLevelOffset_ = 8;
#else
	stackArgPos_ = 0;
	// No args on the stack.
	stackIDOffset_ = -1;
	stackLevelOffset_ = -1;
#endif

	// Start out by saving some registers, since we'll need more.
	PUSH(R15);
	PUSH(R14);
	PUSH(R13);
	PUSH(R12);
	regCache_.Add(R15, RegCache::GEN_INVALID);
	regCache_.Add(R14, RegCache::GEN_INVALID);
	regCache_.Add(R13, RegCache::GEN_INVALID);
	regCache_.Add(R12, RegCache::GEN_INVALID);
	stackArgPos_ += 32;

#if PPSSPP_PLATFORM(WINDOWS)
	// Use the shadow space to save U1/V1.
	stackUV1Offset_ = -8;
#else
	// Use the red zone, but account for the R15-R12 we push just below.
	stackUV1Offset_ = -stackArgPos_ - 8;
#endif

	// We can throw these away right off if there are no mips.
	if (!id.hasAnyMips && regCache_.Has(RegCache::GEN_ARG_LEVEL) && id.useSharedClut)
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);
	if (!id.hasAnyMips && regCache_.Has(RegCache::GEN_ARG_LEVELFRAC))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVELFRAC);

	if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
		// On Linux, RCX is currently levelFrac, but we'll need it for other things.
		if (!cpu_info.bBMI2) {
			X64Reg levelFracReg = regCache_.Find(RegCache::GEN_ARG_LEVELFRAC);
			MOV(64, R(R15), R(levelFracReg));
			regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
			regCache_.ForceRelease(RegCache::GEN_ARG_LEVELFRAC);
			regCache_.ChangeReg(R15, RegCache::GEN_ARG_LEVELFRAC);
			regCache_.ForceRetain(RegCache::GEN_ARG_LEVELFRAC);
		}
	} else if (!regCache_.Has(RegCache::GEN_ARG_BUFW_PTR)) {
		// Let's load bufwptr into regs.  RDX is free.
		MOV(64, R(RDX), MDisp(RSP, stackArgPos_ + 0));
		regCache_.ChangeReg(RDX, RegCache::GEN_ARG_BUFW_PTR);
		regCache_.ForceRetain(RegCache::GEN_ARG_BUFW_PTR);
	}
	// Okay, now lock RCX as a shifting reg.
	if (!cpu_info.bBMI2) {
		regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
		regCache_.ForceRetain(RegCache::GEN_SHIFTVAL);
	}

	bool success = true;

	// Convert S/T + X/Y to U/V (and U1/V1 if appropriate.)
	success = success && Jit_GetTexelCoords(id);

	// At this point, XMM0 should be free.  Swap it to the result.
	success = success && regCache_.ChangeReg(XMM0, RegCache::VEC_RESULT);
	// Let's also pick a reg for GEN_RESULT - doesn't matter which.
	X64Reg resultReg = regCache_.Alloc(RegCache::GEN_RESULT);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	regCache_.ForceRetain(RegCache::GEN_RESULT);

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

	auto loadPtrs = [&](bool level1) {
		X64Reg bufwReg = regCache_.Alloc(RegCache::GEN_ARG_BUFW);
		X64Reg bufwPtrReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		MOVZX(32, 16, bufwReg, MDisp(bufwPtrReg, level1 ? 2 : 0));
		regCache_.Unlock(bufwPtrReg, RegCache::GEN_ARG_BUFW_PTR);
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW);
		regCache_.ForceRetain(RegCache::GEN_ARG_BUFW);

		X64Reg srcReg = regCache_.Alloc(RegCache::GEN_ARG_TEXPTR);
		X64Reg srcPtrReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);
		MOV(64, R(srcReg), MDisp(srcPtrReg, level1 ? 8 : 0));
		regCache_.Unlock(srcPtrReg, RegCache::GEN_ARG_TEXPTR_PTR);
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRetain(RegCache::GEN_ARG_TEXPTR);
	};

	loadPtrs(false);
	success = success && Jit_ReadTextureFormat(id);

	// Convert that to 16-bit from 8-bit channels.
	X64Reg vecResultReg = regCache_.Find(RegCache::VEC_RESULT);
	resultReg = regCache_.Find(RegCache::GEN_RESULT);
	MOVD_xmm(vecResultReg, R(resultReg));
	if (cpu_info.bSSE4_1) {
		PMOVZXBW(vecResultReg, R(vecResultReg));
	} else {
		X64Reg zeroReg = GetZeroVec();
		PUNPCKLBW(vecResultReg, R(zeroReg));
		regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
	}
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	regCache_.Unlock(vecResultReg, RegCache::VEC_RESULT);

	if (id.hasAnyMips) {
		X64Reg vecResultReg = regCache_.Alloc(RegCache::VEC_RESULT1);

		if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
			X64Reg levelFracReg = regCache_.Find(RegCache::GEN_ARG_LEVELFRAC);
			CMP(8, R(levelFracReg), Imm8(0));
			regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
		} else {
			CMP(8, MDisp(RSP, stackArgPos_ + 16), Imm8(0));
		}
		FixupBranch skip = J_CC(CC_Z, true);

		// Modify the level, so the new level value is used.  We don't need the old.
		if (regCache_.Has(RegCache::GEN_ARG_LEVEL)) {
			X64Reg levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
			ADD(32, R(levelReg), Imm8(1));
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
		} else {
			// It's fine to just modify this in place.
			ADD(32, MDisp(RSP, stackArgPos_ + stackLevelOffset_), Imm8(1));
		}

		// This is inside the conditional, but it's okay because we throw it away after.
		loadPtrs(true);
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW_PTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);

		X64Reg uReg = regCache_.Alloc(RegCache::GEN_ARG_U);
		MOV(32, R(uReg), MDisp(RSP, stackArgPos_ + stackUV1Offset_ + 0));
		regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
		regCache_.ForceRetain(RegCache::GEN_ARG_U);

		X64Reg vReg = regCache_.Alloc(RegCache::GEN_ARG_V);
		MOV(32, R(vReg), MDisp(RSP, stackArgPos_ + stackUV1Offset_ + 4));
		regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
		regCache_.ForceRetain(RegCache::GEN_ARG_V);

		bool hadId = regCache_.Has(RegCache::GEN_ID);
		bool hadZero = regCache_.Has(RegCache::VEC_ZERO);
		success = success && Jit_ReadTextureFormat(id);

		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
		MOVD_xmm(vecResultReg, R(resultReg));
		if (cpu_info.bSSE4_1) {
			PMOVZXBW(vecResultReg, R(vecResultReg));
		} else {
			X64Reg zeroReg = GetZeroVec();
			PUNPCKLBW(vecResultReg, R(zeroReg));
			regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		}
		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);

		// Since we're inside a conditional, make sure these go away if we allocated them.
		if (!hadId && regCache_.Has(RegCache::GEN_ID))
			regCache_.ForceRelease(RegCache::GEN_ID);
		if (!hadZero && regCache_.Has(RegCache::VEC_ZERO))
			regCache_.ForceRelease(RegCache::VEC_ZERO);

		SetJumpTarget(skip);

		regCache_.Unlock(vecResultReg, RegCache::VEC_RESULT1);
	} else {
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW_PTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);
	}

	// We're done with these now.
	if (regCache_.Has(RegCache::GEN_ARG_TEXPTR_PTR))
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);
	if (regCache_.Has(RegCache::GEN_ARG_BUFW_PTR))
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW_PTR);
	if (regCache_.Has(RegCache::GEN_ARG_LEVEL))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);
	if (regCache_.Has(RegCache::GEN_SHIFTVAL))
		regCache_.ForceRelease(RegCache::GEN_SHIFTVAL);
	regCache_.ForceRelease(RegCache::GEN_RESULT);

	if (id.hasAnyMips) {
		Describe("BlendMips");
		if (!regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
			X64Reg levelFracReg = regCache_.Alloc(RegCache::GEN_ARG_LEVELFRAC);
			MOVZX(32, 8, levelFracReg, MDisp(RSP, stackArgPos_ + 16));
			regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
			regCache_.ForceRetain(RegCache::GEN_ARG_LEVELFRAC);
		}

		X64Reg levelFracReg = regCache_.Find(RegCache::GEN_ARG_LEVELFRAC);
		CMP(8, R(levelFracReg), Imm8(0));
		FixupBranch skip = J_CC(CC_Z, true);

		// TODO: PMADDWD?  Refactor shared?
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
	if (regCache_.Has(RegCache::GEN_ARG_ID))
		regCache_.ForceRelease(RegCache::GEN_ARG_ID);

	if (!success) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(start));
		ERROR_LOG(Log::G3D, "Failed to compile nearest %s", DescribeSamplerID(id).c_str());
		return nullptr;
	}

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
	return (NearestFunc)start;
}

LinearFunc SamplerJitCache::CompileLinear(const SamplerID &id) {
	_assert_msg_(id.linear && !id.fetch, "Only linear should be set on sampler id");
	BeginWrite(2048);
	Describe("Init");

	// We don't use stackArgPos_ here, this is just for DXT.
	stackArgPos_ = -1;

	// Let's drop some helpful constants here.
	WriteConstantPool(id);

	const u8 *nearest = nullptr;
	if (id.TexFmt() >= GE_TFMT_DXT1) {
		regCache_.SetupABI({
			RegCache::GEN_ARG_U,
			RegCache::GEN_ARG_V,
			RegCache::GEN_ARG_TEXPTR,
			RegCache::GEN_ARG_BUFW,
			RegCache::GEN_ARG_LEVEL,
			// Avoid clobber.
			RegCache::GEN_ARG_LEVELFRAC,
		});
		auto lockReg = [&](X64Reg r, RegCache::Purpose p) {
			regCache_.ChangeReg(r, p);
			regCache_.ForceRetain(p);
		};
		lockReg(RAX, RegCache::GEN_RESULT);
		lockReg(XMM0, RegCache::VEC_ARG_U);
		lockReg(XMM1, RegCache::VEC_ARG_V);
		lockReg(XMM5, RegCache::VEC_RESULT);
#if !PPSSPP_PLATFORM(WINDOWS)
		if (id.hasAnyMips) {
			lockReg(XMM6, RegCache::VEC_U1);
			lockReg(XMM7, RegCache::VEC_V1);
			lockReg(XMM8, RegCache::VEC_RESULT1);
			lockReg(XMM12, RegCache::VEC_INDEX1);
		}
		lockReg(XMM9, RegCache::VEC_ARG_COLOR);
		lockReg(XMM10, RegCache::VEC_FRAC);
		lockReg(XMM11, RegCache::VEC_INDEX);
#endif

		// We'll first write the nearest sampler, which we will CALL.
		// This may differ slightly based on the "linear" flag.
		nearest = AlignCode16();

		if (!Jit_ReadTextureFormat(id)) {
			regCache_.Reset(false);
			EndWrite();
			ResetCodePtr(GetOffset(nearest));
			ERROR_LOG(Log::G3D, "Failed to compile linear nearest %s", DescribeSamplerID(id).c_str());
			return nullptr;
		}

		Describe("Init");
		RET();

		regCache_.ForceRelease(RegCache::GEN_RESULT);
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
		unlockOptReg(RegCache::VEC_FRAC);
		unlockOptReg(RegCache::VEC_INDEX);
		unlockOptReg(RegCache::VEC_INDEX1);
		regCache_.Reset(true);
	}
	EndWrite();

	// Now the actual linear func, which is exposed externally.
	const u8 *linearResetPos = GetCodePointer();
	Describe("Init");

	regCache_.SetupABI({
		RegCache::VEC_ARG_S,
		RegCache::VEC_ARG_T,
		RegCache::VEC_ARG_COLOR,
		RegCache::GEN_ARG_TEXPTR_PTR,
		RegCache::GEN_ARG_BUFW_PTR,
		RegCache::GEN_ARG_LEVEL,
		RegCache::GEN_ARG_LEVELFRAC,
		RegCache::GEN_ARG_ID,
	});

#if PPSSPP_PLATFORM(WINDOWS)
	// RET + shadow space.
	stackArgPos_ = 8 + 32;
	// Free up some more vector regs on Windows too, where we're a bit tight.
	stackArgPos_ += WriteProlog(0, { XMM6, XMM7, XMM8, XMM9, XMM10, XMM11, XMM12 }, { R15, R14, R13, R12 });

	// Positions: stackArgPos_+0=bufwptr, stackArgPos_+8=level, stackArgPos_+16=levelFrac
	stackIDOffset_ = 24;
	stackLevelOffset_ = 8;

	// If needed, we could store UV1 data in shadow space, but we no longer do.
	stackUV1Offset_ = -8;
#else
	stackArgPos_ = 0;
	stackArgPos_ += WriteProlog(0, {}, { R15, R14, R13, R12 });
	stackIDOffset_ = -1;
	stackLevelOffset_ = -1;

	// Use the red zone.
	stackUV1Offset_ = -stackArgPos_ - 8;
#endif

	// This is what we'll put in them, anyway...
	if (nearest != nullptr) {
		regCache_.ChangeReg(XMM10, RegCache::VEC_FRAC);
		regCache_.ForceRetain(RegCache::VEC_FRAC);
		regCache_.ChangeReg(XMM11, RegCache::VEC_INDEX);
		regCache_.ForceRetain(RegCache::VEC_INDEX);
		if (id.hasAnyMips) {
			regCache_.ChangeReg(XMM12, RegCache::VEC_INDEX1);
			regCache_.ForceRetain(RegCache::VEC_INDEX1);
		}
	}

	// Reserve a couple regs that the nearest CALL won't use.
	if (id.hasAnyMips) {
		regCache_.ChangeReg(XMM6, RegCache::VEC_U1);
		regCache_.ChangeReg(XMM7, RegCache::VEC_V1);
		regCache_.ForceRetain(RegCache::VEC_U1);
		regCache_.ForceRetain(RegCache::VEC_V1);
	} else if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVELFRAC);
	}

	// Save prim color for later in a different XMM too if we're using the nearest helper.
	if (nearest != nullptr) {
		X64Reg primColorReg = regCache_.Find(RegCache::VEC_ARG_COLOR);
		MOVDQA(XMM9, R(primColorReg));
		regCache_.Unlock(primColorReg, RegCache::VEC_ARG_COLOR);
		regCache_.ForceRelease(RegCache::VEC_ARG_COLOR);
		regCache_.ChangeReg(XMM9, RegCache::VEC_ARG_COLOR);
		regCache_.ForceRetain(RegCache::VEC_ARG_COLOR);
	}

	// We also want to save src and bufw for later.  Might be in a reg already.
	if (regCache_.Has(RegCache::GEN_ARG_TEXPTR_PTR) && regCache_.Has(RegCache::GEN_ARG_BUFW_PTR)) {
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		MOV(64, R(R14), R(srcReg));
		MOV(64, R(R15), R(bufwReg));
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR_PTR);
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW_PTR);
	} else if (regCache_.Has(RegCache::GEN_ARG_TEXPTR_PTR)) {
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);
		MOV(64, R(R14), R(srcReg));
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR_PTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);
		MOV(64, R(R15), MDisp(RSP, stackArgPos_ + 0));
	} else {
		MOV(64, R(R14), MDisp(RSP, stackArgPos_ + 0));
		MOV(64, R(R15), MDisp(RSP, stackArgPos_ + 8));
	}

	// Okay, and now remember we moved to R14/R15.
	regCache_.ChangeReg(R14, RegCache::GEN_ARG_TEXPTR_PTR);
	regCache_.ForceRetain(RegCache::GEN_ARG_TEXPTR_PTR);
	if (!regCache_.Has(RegCache::GEN_ARG_BUFW_PTR)) {
		regCache_.ChangeReg(R15, RegCache::GEN_ARG_BUFW_PTR);
		regCache_.ForceRetain(RegCache::GEN_ARG_BUFW_PTR);
	}

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

	// The data offset goes into V, except in the CLUT4 case and DXT (nearest func) cases.
	if (nearest == nullptr && id.TexFmt() != GE_TFMT_CLUT4)
		regCache_.ForceRelease(RegCache::VEC_ARG_U);

	// Hard allocate results if we're using the func method.
	if (nearest != nullptr) {
		regCache_.ChangeReg(XMM5, RegCache::VEC_RESULT);
		regCache_.ForceRetain(RegCache::VEC_RESULT);
		if (id.hasAnyMips) {
			regCache_.ChangeReg(XMM8, RegCache::VEC_RESULT1);
			regCache_.ForceRetain(RegCache::VEC_RESULT1);
		}
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

		X64Reg indexReg = regCache_.Find(level1 ? RegCache::VEC_INDEX1 : RegCache::VEC_INDEX);
		if (cpu_info.bSSE4_1) {
			PEXTRD(R(srcArgReg), indexReg, off / 4);
		} else {
			MOVD_xmm(R(srcArgReg), indexReg);
			PSRLDQ(indexReg, 4);
		}
		regCache_.Unlock(indexReg, level1 ? RegCache::VEC_INDEX1 : RegCache::VEC_INDEX);

		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		ADD(64, R(srcArgReg), MDisp(srcReg, level1 ? 8 : 0));
		MOVZX(32, 16, bufwArgReg, MDisp(bufwReg, level1 ? 2 : 0));
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

	if (nearest != nullptr) {
		Describe("Calls");
		doNearestCall(0, false);
		doNearestCall(4, false);
		doNearestCall(8, false);
		doNearestCall(12, false);

		// After doing the calls, certain cached things aren't safe.
		if (regCache_.Has(RegCache::GEN_ID))
			regCache_.ForceRelease(RegCache::GEN_ID);
		if (regCache_.Has(RegCache::VEC_ZERO))
			regCache_.ForceRelease(RegCache::VEC_ZERO);
	} else {
		success = success && Jit_FetchQuad(id, false);
	}

	if (id.hasAnyMips) {
		Describe("MipsCalls");
		if (regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
			X64Reg levelFracReg = regCache_.Find(RegCache::GEN_ARG_LEVELFRAC);
			CMP(8, R(levelFracReg), Imm8(0));
			regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
		} else {
			CMP(8, MDisp(RSP, stackArgPos_ + 16), Imm8(0));
		}
		FixupBranch skip = J_CC(CC_Z, true);

		// Modify the level, so the new level value is used.  We don't need the old.
		if (regCache_.Has(RegCache::GEN_ARG_LEVEL)) {
			X64Reg levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
			ADD(32, R(levelReg), Imm8(1));
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
		} else {
			// It's fine to just modify this in place.
			ADD(32, MDisp(RSP, stackArgPos_ + stackLevelOffset_), Imm8(1));
		}

		if (nearest != nullptr) {
			Describe("MipsCalls");
			doNearestCall(0, true);
			doNearestCall(4, true);
			doNearestCall(8, true);
			doNearestCall(12, true);
		} else {
			success = success && Jit_FetchQuad(id, true);
		}

		SetJumpTarget(skip);
	}

	// We're done with these now.
	if (nearest != nullptr) {
		regCache_.ForceRelease(RegCache::VEC_ARG_U);
		regCache_.ForceRelease(RegCache::VEC_ARG_V);
		regCache_.ForceRelease(RegCache::VEC_INDEX);
	}
	if (regCache_.Has(RegCache::VEC_INDEX1))
		regCache_.ForceRelease(RegCache::VEC_INDEX1);
	if (regCache_.Has(RegCache::VEC_U1))
		regCache_.ForceRelease(RegCache::VEC_U1);
	if (regCache_.Has(RegCache::VEC_V1))
		regCache_.ForceRelease(RegCache::VEC_V1);
	regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR_PTR);
	regCache_.ForceRelease(RegCache::GEN_ARG_BUFW_PTR);
	if (regCache_.Has(RegCache::GEN_ARG_LEVEL))
		regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);

	success = success && Jit_DecodeQuad(id, false);
	success = success && Jit_BlendQuad(id, false);
	if (id.hasAnyMips) {
		Describe("BlendMips");
		if (!regCache_.Has(RegCache::GEN_ARG_LEVELFRAC)) {
			X64Reg levelFracReg = regCache_.Alloc(RegCache::GEN_ARG_LEVELFRAC);
			MOVZX(32, 8, levelFracReg, MDisp(RSP, stackArgPos_ + 16));
			regCache_.Unlock(levelFracReg, RegCache::GEN_ARG_LEVELFRAC);
			regCache_.ForceRetain(RegCache::GEN_ARG_LEVELFRAC);
		}

		X64Reg levelFracReg = regCache_.Find(RegCache::GEN_ARG_LEVELFRAC);
		CMP(8, R(levelFracReg), Imm8(0));
		FixupBranch skip = J_CC(CC_Z, true);

		success = success && Jit_DecodeQuad(id, true);
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

	if (regCache_.Has(RegCache::VEC_FRAC))
		regCache_.ForceRelease(RegCache::VEC_FRAC);

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
	if (regCache_.Has(RegCache::GEN_ARG_ID))
		regCache_.ForceRelease(RegCache::GEN_ARG_ID);

	if (!success) {
		regCache_.Reset(false);
		EndWrite();
		ResetCodePtr(GetOffset(nearest ? nearest : linearResetPos));
		ERROR_LOG(Log::G3D, "Failed to compile linear %s", DescribeSamplerID(id).c_str());
		return nullptr;
	}

	if (id.hasInvalidPtr) {
		SetJumpTarget(zeroSrc);
	}

	const u8 *start = WriteFinalizedEpilog();
	regCache_.Reset(true);
	return (LinearFunc)start;
}

void SamplerJitCache::WriteConstantPool(const SamplerID &id) {
	// We reuse constants in any pool, because our code space is small.
	WriteSimpleConst8x16(const10All16_, 0x10);
	WriteSimpleConst16x8(const10All8_, 0x10);

	if (const10Low_ == nullptr) {
		const10Low_ = AlignCode16();
		for (int i = 0; i < 4; ++i)
			Write16(0x10);
		for (int i = 0; i < 4; ++i)
			Write16(0);
	}

	WriteSimpleConst4x32(constOnes32_, 1);
	WriteSimpleConst8x16(constOnes16_, 1);
	// This is the mask for clamp or wrap, the max texel in the S or T direction.
	WriteSimpleConst4x32(constMaxTexel32_, 511);

	if (constUNext_ == nullptr) {
		constUNext_ = AlignCode16();
		Write32(0); Write32(1); Write32(0); Write32(1);
	}

	if (constVNext_ == nullptr) {
		constVNext_ = AlignCode16();
		Write32(0); Write32(0); Write32(1); Write32(1);
	}

	WriteSimpleConst4x32(const5551Swizzle_, 0x00070707);
	WriteSimpleConst4x32(const5650Swizzle_, 0x00070307);

	// These are unique to the sampler ID.
	if (!id.hasAnyMips) {
		float w256f = (1 << id.width0Shift) * 256;
		float h256f = (1 << id.height0Shift) * 256;
		constWidthHeight256f_ = AlignCode16();
		Write32(*(uint32_t *)&w256f);
		Write32(*(uint32_t *)&h256f);
		Write32(*(uint32_t *)&w256f);
		Write32(*(uint32_t *)&h256f);

		WriteDynamicConst4x32(constWidthMinus1i_, id.width0Shift > 9 ? 511 : (1 << id.width0Shift) - 1);
		WriteDynamicConst4x32(constHeightMinus1i_, id.height0Shift > 9 ? 511 : (1 << id.height0Shift) - 1);
	} else {
		constWidthHeight256f_ = nullptr;
		constWidthMinus1i_ = nullptr;
		constHeightMinus1i_ = nullptr;
	}
}

RegCache::Reg SamplerJitCache::GetSamplerID() {
	if (regCache_.Has(RegCache::GEN_ARG_ID))
		return regCache_.Find(RegCache::GEN_ARG_ID);
	if (!regCache_.Has(RegCache::GEN_ID)) {
		X64Reg r = regCache_.Alloc(RegCache::GEN_ID);
		_assert_(stackIDOffset_ != -1);
		MOV(PTRBITS, R(r), MDisp(RSP, stackArgPos_ + stackIDOffset_));
		return r;
	}
	return regCache_.Find(RegCache::GEN_ID);
}

void SamplerJitCache::UnlockSamplerID(RegCache::Reg &r) {
	if (regCache_.Has(RegCache::GEN_ARG_ID))
		regCache_.Unlock(r, RegCache::GEN_ARG_ID);
	else
		regCache_.Unlock(r, RegCache::GEN_ID);
}

bool SamplerJitCache::Jit_FetchQuad(const SamplerID &id, bool level1) {
	bool success = true;
	switch (id.TexFmt()) {
	case GE_TFMT_5650:
	case GE_TFMT_5551:
	case GE_TFMT_4444:
		success = Jit_GetDataQuad(id, level1, 16);
		// Mask away the high bits, if loaded via AVX2.
		if (cpu_info.bAVX2) {
			X64Reg destReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
			PSLLD(destReg, 16);
			PSRLD(destReg, 16);
			regCache_.Unlock(destReg, level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
		}
		break;

	case GE_TFMT_8888:
		success = Jit_GetDataQuad(id, level1, 32);
		break;

	case GE_TFMT_CLUT32:
		success = Jit_GetDataQuad(id, level1, 32);
		if (success)
			success = Jit_TransformClutIndexQuad(id, 32);
		if (success)
			success = Jit_ReadClutQuad(id, level1);
		break;

	case GE_TFMT_CLUT16:
		success = Jit_GetDataQuad(id, level1, 16);
		if (success)
			success = Jit_TransformClutIndexQuad(id, 16);
		if (success)
			success = Jit_ReadClutQuad(id, level1);
		break;

	case GE_TFMT_CLUT8:
		success = Jit_GetDataQuad(id, level1, 8);
		if (success)
			success = Jit_TransformClutIndexQuad(id, 8);
		if (success)
			success = Jit_ReadClutQuad(id, level1);
		break;

	case GE_TFMT_CLUT4:
		success = Jit_GetDataQuad(id, level1, 4);
		if (success)
			success = Jit_TransformClutIndexQuad(id, 4);
		if (success)
			success = Jit_ReadClutQuad(id, level1);
		break;

	case GE_TFMT_DXT1:
	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
		// No SIMD version currently, should use nearest helper path.
		success = false;
		break;

	default:
		success = false;
	}

	return success;
}

bool SamplerJitCache::Jit_GetDataQuad(const SamplerID &id, bool level1, int bitsPerTexel) {
	Describe("DataQuad");
	bool success = true;

	X64Reg baseReg = regCache_.Alloc(RegCache::GEN_ARG_TEXPTR);
	X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR_PTR);
	MOV(64, R(baseReg), MDisp(srcReg, level1 ? 8 : 0));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR_PTR);

	X64Reg destReg = INVALID_REG;
	if (id.TexFmt() >= GE_TFMT_CLUT4 && id.TexFmt() <= GE_TFMT_CLUT32)
		destReg = regCache_.Alloc(RegCache::VEC_INDEX);
	else if (regCache_.Has(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT))
		destReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
	else
		destReg = regCache_.Alloc(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);

	X64Reg byteOffsetReg = regCache_.Find(level1 ? RegCache::VEC_V1 : RegCache::VEC_ARG_V);
	if (cpu_info.bAVX2 && id.overReadSafe) {
		// We have to set a mask for which values to load.  Load all 4.
		// Note this is overwritten with zeroes by the gather instruction.
		X64Reg maskReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		PCMPEQD(maskReg, R(maskReg));
		VPGATHERDD(128, destReg, MComplex(baseReg, byteOffsetReg, SCALE_1, 0), maskReg);
		regCache_.Release(maskReg, RegCache::VEC_TEMP0);
	} else {
		if (bitsPerTexel != 32)
			PXOR(destReg, R(destReg));

		// Grab each value separately... try to use the right memory access size.
		X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
		if (cpu_info.bSSE4_1) {
			for (int i = 0; i < 4; ++i) {
				PEXTRD(R(temp2Reg), byteOffsetReg, i);
				if (bitsPerTexel <= 8)
					PINSRB(destReg, MComplex(baseReg, temp2Reg, SCALE_1, 0), i * 4);
				else if (bitsPerTexel == 16)
					PINSRW(destReg, MComplex(baseReg, temp2Reg, SCALE_1, 0), i * 2);
				else if (bitsPerTexel == 32)
					PINSRD(destReg, MComplex(baseReg, temp2Reg, SCALE_1, 0), i);
			}
		} else {
			for (int i = 0; i < 4; ++i) {
				MOVD_xmm(R(temp2Reg), byteOffsetReg);
				if (i != 3)
					PSRLDQ(byteOffsetReg, 4);
				if (bitsPerTexel <= 8) {
					MOVZX(32, 8, temp2Reg, MComplex(baseReg, temp2Reg, SCALE_1, 0));
					PINSRW(destReg, R(temp2Reg), i * 2);
				} else if (bitsPerTexel == 16) {
					PINSRW(destReg, MComplex(baseReg, temp2Reg, SCALE_1, 0), i * 2);
				} else if (bitsPerTexel == 32) {
					if (i == 0) {
						MOVD_xmm(destReg, MComplex(baseReg, temp2Reg, SCALE_1, 0));
					} else {
						// Maybe a temporary would be better, but this path should be rare.
						PINSRW(destReg, MComplex(baseReg, temp2Reg, SCALE_1, 0), i * 2);
						PINSRW(destReg, MComplex(baseReg, temp2Reg, SCALE_1, 2), i * 2 + 1);
					}
				}
			}
		}
		regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	}
	regCache_.Unlock(byteOffsetReg, level1 ? RegCache::VEC_V1 : RegCache::VEC_ARG_V);
	regCache_.ForceRelease(level1 ? RegCache::VEC_V1 : RegCache::VEC_ARG_V);
	regCache_.Release(baseReg, RegCache::GEN_ARG_TEXPTR);

	if (bitsPerTexel == 4) {
		// Take only lowest bit, multiply by 4 with shifting.
		X64Reg uReg = regCache_.Find(level1 ? RegCache::VEC_U1 : RegCache::VEC_ARG_U);
		// Next, shift away based on the odd U bits.
		if (cpu_info.bAVX2) {
			// This is really convenient with AVX.  Just make the bit into a shift amount.
			PSLLD(uReg, 31);
			PSRLD(uReg, 29);
			VPSRLVD(128, destReg, destReg, R(uReg));
		} else {
			// This creates a mask - FFFFFFFF to shift, zero otherwise.
			PSLLD(uReg, 31);
			PSRAD(uReg, 31);

			X64Reg unshiftedReg = regCache_.Alloc(RegCache::VEC_TEMP0);
			MOVDQA(unshiftedReg, R(destReg));
			PSRLD(destReg, 4);
			// Mask destReg (shifted) and reverse uReg to unshifted masked.
			PAND(destReg, R(uReg));
			PANDN(uReg, R(unshiftedReg));
			// Now combine.
			POR(destReg, R(uReg));
			regCache_.Release(unshiftedReg, RegCache::VEC_TEMP0);
		}
		regCache_.Unlock(uReg, level1 ? RegCache::VEC_U1 : RegCache::VEC_ARG_U);
		regCache_.ForceRelease(level1 ? RegCache::VEC_U1 : RegCache::VEC_ARG_U);
	}

	if (id.TexFmt() >= GE_TFMT_CLUT4 && id.TexFmt() <= GE_TFMT_CLUT32) {
		regCache_.Unlock(destReg, RegCache::VEC_INDEX);
	} else {
		regCache_.Unlock(destReg, level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
		regCache_.ForceRetain(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
	}

	return success;
}

bool SamplerJitCache::Jit_TransformClutIndexQuad(const SamplerID &id, int bitsPerIndex) {
	Describe("TrCLUTQuad");
	GEPaletteFormat fmt = id.ClutFmt();
	if (!id.hasClutShift && !id.hasClutMask && !id.hasClutOffset) {
		// This is simple - just mask.
		X64Reg indexReg = regCache_.Find(RegCache::VEC_INDEX);
		// Mask to 8 bits for CLUT8/16/32, 4 bits for CLUT4.
		PSLLD(indexReg, bitsPerIndex >= 8 ? 24 : 28);
		PSRLD(indexReg, bitsPerIndex >= 8 ? 24 : 28);
		regCache_.Unlock(indexReg, RegCache::VEC_INDEX);

		return true;
	}

	X64Reg indexReg = regCache_.Find(RegCache::VEC_INDEX);
	bool maskedIndex = false;

	// Okay, first load the actual samplerID clutformat bits we'll use.
	X64Reg formatReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	X64Reg idReg = GetSamplerID();
	if (cpu_info.bAVX2 && !id.hasClutShift)
		VPBROADCASTD(128, formatReg, MDisp(idReg, offsetof(SamplerID, cached.clutFormat)));
	else
		MOVD_xmm(formatReg, MDisp(idReg, offsetof(SamplerID, cached.clutFormat)));
	UnlockSamplerID(idReg);

	// Shift = (clutformat >> 2) & 0x1F
	if (id.hasClutShift) {
		// Before shifting, let's mask if needed (we always read 32 bits.)
		// We have to do this here, because the bits should be zero even if F is used as a mask.
		if (bitsPerIndex < 32) {
			PSLLD(indexReg, 32 - bitsPerIndex);
			PSRLD(indexReg, 32 - bitsPerIndex);
			maskedIndex = true;
		}

		X64Reg shiftReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		// Shift against walls to get 5 bits after the rightmost 2.
		PSLLD(shiftReg, formatReg, 32 - 7);
		PSRLD(shiftReg, 32 - 5);
		// The other lanes are zero, so we can use PSRLD.
		PSRLD(indexReg, R(shiftReg));
		regCache_.Release(shiftReg, RegCache::VEC_TEMP1);
	}

	// With shifting done, we need the format in each lane.
	if (!cpu_info.bAVX2 || id.hasClutShift)
		PSHUFD(formatReg, R(formatReg), _MM_SHUFFLE(0, 0, 0, 0));

	// Mask = (clutformat >> 8) & 0xFF
	if (id.hasClutMask) {
		X64Reg maskReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		// If it was CLUT4, grab only 4 bits of the mask.
		PSLLD(maskReg, formatReg, bitsPerIndex == 4 ? 20 : 16);
		PSRLD(maskReg, bitsPerIndex == 4 ? 28 : 24);

		PAND(indexReg, R(maskReg));
		regCache_.Release(maskReg, RegCache::VEC_TEMP1);
	} else if (!maskedIndex || bitsPerIndex > 8) {
		// Apply the fixed 8 bit mask (or the CLUT4 mask if we didn't shift.)
		PSLLD(indexReg, maskedIndex || bitsPerIndex >= 8 ? 24 : 28);
		PSRLD(indexReg, maskedIndex || bitsPerIndex >= 8 ? 24 : 28);
	}

	// Offset = (clutformat >> 12) & 0x01F0
	if (id.hasClutOffset) {
		// Use walls to extract the 5 bits at 16, and then put them shifted left by 4.
		int offsetBits = fmt == GE_CMODE_32BIT_ABGR8888 ? 4 : 5;
		PSRLD(formatReg, 16);
		PSLLD(formatReg, 32 - offsetBits);
		PSRLD(formatReg, 32 - offsetBits - 4);

		POR(indexReg, R(formatReg));
	}

	regCache_.Release(formatReg, RegCache::VEC_TEMP0);
	regCache_.Unlock(indexReg, RegCache::VEC_INDEX);
	return true;
}

bool SamplerJitCache::Jit_ReadClutQuad(const SamplerID &id, bool level1) {
	Describe("ReadCLUTQuad");
	X64Reg indexReg = regCache_.Find(RegCache::VEC_INDEX);

	if (!id.useSharedClut) {
		X64Reg vecLevelReg = regCache_.Alloc(RegCache::VEC_TEMP0);

		if (regCache_.Has(RegCache::GEN_ARG_LEVEL)) {
			X64Reg levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
			MOVD_xmm(vecLevelReg, R(levelReg));
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
		} else {
#if PPSSPP_PLATFORM(WINDOWS)
			if (cpu_info.bAVX2) {
				VPBROADCASTD(128, vecLevelReg, MDisp(RSP, stackArgPos_ + stackLevelOffset_));
			} else {
				MOVD_xmm(vecLevelReg, MDisp(RSP, stackArgPos_ + stackLevelOffset_));
				PSHUFD(vecLevelReg, R(vecLevelReg), _MM_SHUFFLE(0, 0, 0, 0));
			}
#else
			_assert_(false);
#endif
		}

		// Now we multiply by 16, and add.
		PSLLD(vecLevelReg, 4);
		PADDD(indexReg, R(vecLevelReg));
		regCache_.Release(vecLevelReg, RegCache::VEC_TEMP0);
	}

	X64Reg idReg = GetSamplerID();
	X64Reg clutBaseReg = regCache_.Alloc(RegCache::GEN_TEMP1);
	MOV(PTRBITS, R(clutBaseReg), MDisp(idReg, offsetof(SamplerID, cached.clut)));
	UnlockSamplerID(idReg);

	X64Reg resultReg = INVALID_REG;
	if (regCache_.Has(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT))
		resultReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
	else
		resultReg = regCache_.Alloc(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
	X64Reg maskReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	if (cpu_info.bAVX2 && id.overReadSafe)
		PCMPEQD(maskReg, R(maskReg));

	switch (id.ClutFmt()) {
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		if (cpu_info.bAVX2 && id.overReadSafe) {
			VPGATHERDD(128, resultReg, MComplex(clutBaseReg, indexReg, SCALE_2, 0), maskReg);
			// Clear out the top 16 bits.
			PCMPEQD(maskReg, R(maskReg));
			PSRLD(maskReg, 16);
			PAND(resultReg, R(maskReg));
		} else {
			PXOR(resultReg, R(resultReg));

			X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
			if (cpu_info.bSSE4_1) {
				for (int i = 0; i < 4; ++i) {
					PEXTRD(R(temp2Reg), indexReg, i);
					PINSRW(resultReg, MComplex(clutBaseReg, temp2Reg, SCALE_2, 0), i * 2);
				}
			} else {
				for (int i = 0; i < 4; ++i) {
					MOVD_xmm(R(temp2Reg), indexReg);
					if (i != 3)
						PSRLDQ(indexReg, 4);
					PINSRW(resultReg, MComplex(clutBaseReg, temp2Reg, SCALE_2, 0), i * 2);
				}
			}
			regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
		}
		break;

	case GE_CMODE_32BIT_ABGR8888:
		if (cpu_info.bAVX2 && id.overReadSafe) {
			VPGATHERDD(128, resultReg, MComplex(clutBaseReg, indexReg, SCALE_4, 0), maskReg);
		} else {
			X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
			if (cpu_info.bSSE4_1) {
				for (int i = 0; i < 4; ++i) {
					PEXTRD(R(temp2Reg), indexReg, i);
					PINSRD(resultReg, MComplex(clutBaseReg, temp2Reg, SCALE_4, 0), i);
				}
			} else {
				for (int i = 0; i < 4; ++i) {
					MOVD_xmm(R(temp2Reg), indexReg);
					if (i != 3)
						PSRLDQ(indexReg, 4);

					if (i == 0) {
						MOVD_xmm(resultReg , MComplex(clutBaseReg, temp2Reg, SCALE_4, 0));
					} else {
						MOVD_xmm(maskReg, MComplex(clutBaseReg, temp2Reg, SCALE_4, 0));
						PSLLDQ(maskReg, 4 * i);
						POR(resultReg, R(maskReg));
					}
				}
			}
			regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
		}
		break;
	}
	regCache_.Release(maskReg, RegCache::VEC_TEMP0);
	regCache_.Unlock(resultReg, level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
	regCache_.ForceRetain(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);

	regCache_.Release(clutBaseReg, RegCache::GEN_TEMP1);
	regCache_.Release(indexReg, RegCache::VEC_INDEX);
	return true;
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
		X64Reg allFracReg = regCache_.Find(RegCache::VEC_FRAC);
		X64Reg zeroReg = GetZeroVec();
		if (level1) {
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(3, 3, 3, 3));
		} else {
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(1, 1, 1, 1));
		}
		PSHUFB(fracReg, R(zeroReg));
		regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		regCache_.Unlock(allFracReg, RegCache::VEC_FRAC);

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
		allFracReg = regCache_.Find(RegCache::VEC_FRAC);
		if (level1) {
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(2, 2, 2, 2));
		} else {
			// We can ignore the high bits, since we'll interleave those away anyway.
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(0, 0, 0, 0));
		}
		regCache_.Unlock(allFracReg, RegCache::VEC_FRAC);

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
		if (level1) {
			PACKSSDW(quadReg, R(quadReg));
			regCache_.Unlock(quadReg, RegCache::VEC_RESULT1);
		} else {
			if (cpu_info.bAVX) {
				VPACKSSDW(128, XMM0, quadReg, R(quadReg));
			} else {
				PACKSSDW(quadReg, R(quadReg));
				MOVDQA(XMM0, R(quadReg));
			}
			regCache_.Unlock(quadReg, RegCache::VEC_RESULT);

			regCache_.ForceRelease(RegCache::VEC_RESULT);
			bool changeSuccess = regCache_.ChangeReg(XMM0, RegCache::VEC_RESULT);
			_assert_msg_(changeSuccess, "Unexpected reg locked as destReg");
		}
	} else {
		X64Reg topReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		X64Reg bottomReg = regCache_.Alloc(RegCache::VEC_TEMP1);

		X64Reg quadReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
		X64Reg zeroReg = GetZeroVec();
		PSHUFD(topReg, R(quadReg), _MM_SHUFFLE(0, 0, 1, 0));
		PSHUFD(bottomReg, R(quadReg), _MM_SHUFFLE(0, 0, 3, 2));
		PUNPCKLBW(topReg, R(zeroReg));
		PUNPCKLBW(bottomReg, R(zeroReg));
		regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		if (!level1) {
			regCache_.Unlock(quadReg, RegCache::VEC_RESULT);
			regCache_.ForceRelease(RegCache::VEC_RESULT);
		}

		// Grab frac_u and spread to lower (L) lanes.
		X64Reg fracReg = regCache_.Alloc(RegCache::VEC_TEMP2);
		X64Reg allFracReg = regCache_.Find(RegCache::VEC_FRAC);
		X64Reg fracMulReg = regCache_.Alloc(RegCache::VEC_TEMP3);
		if (level1) {
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(2, 2, 2, 2));
		} else {
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(0, 0, 0, 0));
		}
		regCache_.Unlock(allFracReg, RegCache::VEC_FRAC);
		// Now subtract 0x10 - frac_u in the L lanes only: 00000000 LLLLLLLL.
		MOVDQA(fracMulReg, M(const10Low_));
		PSUBW(fracMulReg, R(fracReg));
		// Then we just put the original frac_u in the upper bits.
		PUNPCKLQDQ(fracMulReg, R(fracReg));
		regCache_.Release(fracReg, RegCache::VEC_TEMP2);

		// Okay, we have 8-bits in the top and bottom rows for the color.
		// Multiply by frac to get 12, which we keep for the next stage.
		PMULLW(topReg, R(fracMulReg));
		PMULLW(bottomReg, R(fracMulReg));
		regCache_.Release(fracMulReg, RegCache::VEC_TEMP3);

		// Time for frac_v.  This time, we want it in all 8 lanes.
		fracReg = regCache_.Alloc(RegCache::VEC_TEMP2);
		allFracReg = regCache_.Find(RegCache::VEC_FRAC);
		X64Reg fracTopReg = regCache_.Alloc(RegCache::VEC_TEMP3);
		if (level1) {
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(3, 3, 3, 3));
		} else {
			PSHUFLW(fracReg, R(allFracReg), _MM_SHUFFLE(1, 1, 1, 1));
		}
		PSHUFD(fracReg, R(fracReg), _MM_SHUFFLE(0, 0, 0, 0));
		regCache_.Unlock(allFracReg, RegCache::VEC_FRAC);

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
		if (cpu_info.bAVX) {
			VPADDW(128, tempReg, primColorReg, M(constOnes16_));

			// Okay, time to multiply.  This produces 16 bits, neatly.
			VPMULLW(128, resultReg, tempReg, R(resultReg));
		} else {
			MOVDQA(tempReg, M(constOnes16_));
			PADDW(tempReg, R(primColorReg));

			PMULLW(resultReg, R(tempReg));
		}

		if (id.useColorDoubling)
			PSRLW(resultReg, 7);
		else
			PSRLW(resultReg, 8);

		if (!id.useTextureAlpha) {
			useAlphaFrom(primColorReg);
		} else if (id.useColorDoubling) {
			// We still need to finish dividing alpha, it's currently doubled (from the 7 above.)
			PSRLW(primColorReg, resultReg, 1);
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
		} else if (id.useColorDoubling) {
			PSLLW(resultReg, 1);
		}
		useAlphaFrom(primColorReg);
		break;

	case GE_TEXFUNC_BLEND:
	{
		Describe("EnvBlend");
		PACKSSDW(primColorReg, R(primColorReg));

		// First off, let's grab the color value.
		X64Reg idReg = GetSamplerID();
		X64Reg texEnvReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		if (cpu_info.bSSE4_1) {
			PMOVZXBW(texEnvReg, MDisp(idReg, offsetof(SamplerID, cached.texBlendColor)));
		} else {
			MOVD_xmm(texEnvReg, MDisp(idReg, offsetof(SamplerID, cached.texBlendColor)));
			X64Reg zeroReg = GetZeroVec();
			PUNPCKLBW(texEnvReg, R(zeroReg));
			regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		}
		UnlockSamplerID(idReg);

		// Now merge in the prim color so we have them interleaved, texenv low.
		PUNPCKLWD(texEnvReg, R(primColorReg));

		// Okay, now materialize 255 for inversing resultReg and rounding.
		PCMPEQD(tempReg, R(tempReg));
		PSRLW(tempReg, 8);

		// If alpha is used, we want the roundup and factor to be zero.
		if (id.useTextureAlpha)
			PSRLDQ(tempReg, 10);

		// We're going to lose tempReg, so save the 255s.
		X64Reg roundValueReg = regCache_.Alloc(RegCache::VEC_TEMP2);
		MOVDQA(roundValueReg, R(tempReg));

		// Okay, now inverse, then merge with resultReg low to match texenv low.
		PSUBUSW(tempReg, R(resultReg));
		PUNPCKLWD(resultReg, R(tempReg));

		if (id.useTextureAlpha) {
			// Before we multiply, let's include alpha in that multiply.
			PADDW(primColorReg, M(constOnes16_));
			// Mask off everything but alpha, and move to the second highest short.
			PSRLDQ(primColorReg, 6);
			PSLLDQ(primColorReg, 12);
			// Now simply merge in with texenv.
			POR(texEnvReg, R(primColorReg));
		}

		// Alright, now to multiply and add all in one go.  Note this gives us DWORDs.
		PMADDWD(resultReg, R(texEnvReg));
		regCache_.Release(texEnvReg, RegCache::VEC_TEMP1);

		// Now convert back to 16 bit and add the 255s for rounding.
		if (cpu_info.bSSE4_1) {
			PACKUSDW(resultReg, R(resultReg));
		} else {
			PSLLD(resultReg, 16);
			PSRAD(resultReg, 16);
			PACKSSDW(resultReg, R(resultReg));
		}
		PADDW(resultReg, R(roundValueReg));
		regCache_.Release(roundValueReg, RegCache::VEC_TEMP2);

		// Okay, divide by 256 or 128 depending on doubling (we want to preserve the precision.)
		if (id.useColorDoubling && id.useTextureAlpha) {
			// If doubling, we want to still divide alpha by 256.
			PSRLW(resultReg, 7);
			PSRLW(primColorReg, resultReg, 1);
			useAlphaFrom(primColorReg);
		} else if (id.useColorDoubling) {
			PSRLW(resultReg, 7);
		} else {
			PSRLW(resultReg, 8);
		}

		if (!id.useTextureAlpha)
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
			success = Jit_Decode5650(id);
		break;

	case GE_TFMT_5551:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode5551(id);
		break;

	case GE_TFMT_4444:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode4444(id);
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

	X64Reg colorIndexReg = INVALID_REG;
	if (!id.linear) {
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

		// Make sure we don't grab this as colorIndexReg.
		if (uReg != ECX && !cpu_info.bBMI2)
			regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);

		// The colorIndex is simply the 2 bits at blockPos + (v & 3), shifted right by (u & 3) twice.
		colorIndexReg = regCache_.Alloc(RegCache::GEN_TEMP0);
		MOVZX(32, 8, colorIndexReg, MRegSum(srcReg, vReg));
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
		// Only DXT3/5 need this reg later.
		if (id.TexFmt() == GE_TFMT_DXT1)
			regCache_.ForceRelease(RegCache::GEN_ARG_V);

		if (uReg == ECX) {
			SHR(32, R(colorIndexReg), R(CL));
			SHR(32, R(colorIndexReg), R(CL));
		} else if (cpu_info.bBMI2) {
			SHRX(32, colorIndexReg, R(colorIndexReg), uReg);
			SHRX(32, colorIndexReg, R(colorIndexReg), uReg);
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
	} else {
		// For linear, we already precalculated the block pos into srcReg.
		// uReg is the shift for the color index fomr the 32 bits of color index data.
		regCache_.ForceRelease(RegCache::GEN_ARG_BUFW);
		// If we don't have alpha, we don't need vReg.
		if (id.TexFmt() == GE_TFMT_DXT1)
			regCache_.ForceRelease(RegCache::GEN_ARG_V);

		// Make sure we don't grab this as colorIndexReg.
		X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
		if (uReg != ECX && !cpu_info.bBMI2)
			regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);

		// Shift and mask out the 2 bits we need into colorIndexReg.
		colorIndexReg = regCache_.Alloc(RegCache::GEN_TEMP0);
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		if (cpu_info.bBMI2) {
			SHRX(32, colorIndexReg, MatR(srcReg), uReg);
		} else {
			MOV(32, R(colorIndexReg), MatR(srcReg));
			if (uReg != RCX) {
				bool hasRCX = regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
				_assert_(hasRCX);
				MOV(32, R(RCX), R(uReg));
			}
			SHR(32, R(colorIndexReg), R(CL));
		}
		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		// We're done with U now.
		regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
		regCache_.ForceRelease(RegCache::GEN_ARG_U);
	}

	// Mask out the value.
	AND(32, R(colorIndexReg), Imm32(3));

	X64Reg color1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg color2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);

	// For colorIndex 0 or 1, we'll simply take the 565 color and convert.
	CMP(32, R(colorIndexReg), Imm32(1));
	FixupBranch handleSimple565 = J_CC(CC_BE);

	// Otherwise, it depends if color1 or color2 is higher, so fetch them.
	X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	MOVZX(32, 16, color1Reg, MDisp(srcReg, 4));
	MOVZX(32, 16, color2Reg, MDisp(srcReg, 6));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);

	CMP(32, R(color1Reg), R(color2Reg));
	FixupBranch handleMix23 = J_CC(CC_A, true);

	// If we're still here, then colorIndex is either 3 for 0 (easy) or 2 for 50% mix.
	XOR(32, R(resultReg), R(resultReg));
	CMP(32, R(colorIndexReg), Imm32(3));
	FixupBranch finishZero = J_CC(CC_E, true);

	// At this point, resultReg, colorIndexReg, and maybe R12/R13 can be used as temps.
	// We'll add, then shift from 565 a bit less to "divide" by 2 for a 50/50 mix.

	if (cpu_info.bBMI2_fast) {
		// Expand everything out to 0BGR at 8888, but halved.
		MOV(32, R(colorIndexReg), Imm32(0x007C7E7C));
		PDEP(32, color1Reg, color1Reg, R(colorIndexReg));
		PDEP(32, color2Reg, color2Reg, R(colorIndexReg));

		// Now let's sum them together (this undoes our halving.)
		LEA(32, resultReg, MRegSum(color1Reg, color2Reg));

		// Time to swap into order.  Luckily we can ignore alpha.
		BSWAP(32, resultReg);
		SHR(32, R(resultReg), Imm8(8));
	} else {
		// We'll need more regs.  Grab two more.
		PUSH(R12);
		PUSH(R13);

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
	}

	FixupBranch finishMix50 = J(true);

	// Simply load the 565 color, and convert to 0888.
	SetJumpTarget(handleSimple565);
	srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
	MOVZX(32, 16, colorIndexReg, MComplex(srcReg, colorIndexReg, SCALE_2, 4));
	regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
	// DXT1 is done with this reg.
	if (id.TexFmt() == GE_TFMT_DXT1)
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);

	if (cpu_info.bBMI2_fast) {
		// We're only grabbing the high bits, no swizzle here.
		MOV(32, R(resultReg), Imm32(0x00F8FCF8));
		PDEP(32, resultReg, colorIndexReg, R(resultReg));
		BSWAP(32, resultReg);
		SHR(32, R(resultReg), Imm8(8));
	} else {
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
	}
	FixupBranch finish565 = J(true);

	// Here we'll mix color1 and color2 by 2/3 (which gets the 2 depends on colorIndexReg.)
	SetJumpTarget(handleMix23);

	// If colorIndexReg is 2, it's color1Reg * 2 + color2Reg, but if colorIndexReg is 3, it's reversed.
	// Let's swap the regs in that case.
	CMP(32, R(colorIndexReg), Imm32(2));
	FixupBranch skipSwap23 = J_CC(CC_E);
	XCHG(32, R(color2Reg), R(color1Reg));
	SetJumpTarget(skipSwap23);

	if (cpu_info.bBMI2_fast) {
		// Gather B, G, and R and space them apart by 14 or 15 bits.
		MOV(64, R(colorIndexReg), Imm64(0x00001F0003F0001FULL));
		PDEP(64, color1Reg, color1Reg, R(colorIndexReg));
		PDEP(64, color2Reg, color2Reg, R(colorIndexReg));
		LEA(64, resultReg, MComplex(color2Reg, color1Reg, SCALE_2, 0));

		// Now multiply all of them by a special constant to divide by 3.
		// This constant is (1 << 13) / 3, which is importantly less than 14 or 15.
		IMUL(64, resultReg, R(resultReg), Imm32(0x00000AAB));

		// Now extract the BGR values to 8 bits each.
		// We subtract 3 from 13 to get 8 from 5 bits, then 2 from 20 + 13, and 3 from 40 + 13.
		MOV(64, R(colorIndexReg), Imm64((0xFFULL << 10) | (0xFFULL << 31) | (0xFFULL << 50)));
		PEXT(64, resultReg, resultReg, R(colorIndexReg));

		// Finally swap B and R.
		BSWAP(32, resultReg);
		SHR(32, R(resultReg), Imm8(8));
	} else {
		// We'll need more regs.  Grab two more to keep the stack aligned.
		PUSH(R12);
		PUSH(R13);

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
	}

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
		X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);

		if (id.linear) {
			// We precalculated the shift for the 64 bits of alpha data in vReg.
			if (!cpu_info.bBMI2) {
				regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
				_assert_(regCache_.Has(RegCache::GEN_SHIFTVAL));
			}

			if (cpu_info.bBMI2) {
				SHRX(64, srcReg, MDisp(srcReg, 8), vReg);
			} else {
				MOV(64, R(srcReg), MDisp(srcReg, 8));
				MOV(32, R(RCX), R(vReg));
				SHR(64, R(srcReg), R(CL));
			}
			// This will mask the 4 bits we want using a wall also.
			SHL(32, R(srcReg), Imm8(28));
			X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
			OR(32, R(resultReg), R(srcReg));
			regCache_.Unlock(resultReg, RegCache::GEN_RESULT);

			success = true;
		} else {
			X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);

			if (uReg != RCX && !cpu_info.bBMI2) {
				regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
				_assert_(regCache_.Has(RegCache::GEN_SHIFTVAL));
			}

			X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
			MOVZX(32, 16, temp1Reg, MComplex(srcReg, vReg, SCALE_2, 8));
			if (cpu_info.bBMI2) {
				LEA(32, uReg, MScaled(uReg, SCALE_4, 0));
				SHRX(32, temp1Reg, R(temp1Reg), uReg);
			} else {
				// Still depending on it being GEN_SHIFTVAL or GEN_ARG_U above.
				LEA(32, RCX, MScaled(uReg, SCALE_4, 0));
				SHR(32, R(temp1Reg), R(CL));
			}
			SHL(32, R(temp1Reg), Imm8(28));
			X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
			OR(32, R(resultReg), R(temp1Reg));
			regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
			regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);

			success = true;

			regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
			regCache_.ForceRelease(RegCache::GEN_ARG_U);
		}

		regCache_.Unlock(srcReg, RegCache::GEN_ARG_TEXPTR);
		regCache_.ForceRelease(RegCache::GEN_ARG_TEXPTR);
		regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
		regCache_.ForceRelease(RegCache::GEN_ARG_V);
	} else if (fmt == GE_TFMT_DXT5) {
		Describe("DXT5A");

		X64Reg vReg = regCache_.Find(RegCache::GEN_ARG_V);
		X64Reg srcReg = regCache_.Find(RegCache::GEN_ARG_TEXPTR);
		X64Reg alphaIndexReg = INVALID_REG;
		if (id.linear) {
			// We precalculated the shift for the 64 bits of alpha data in vReg.
			if (cpu_info.bBMI2) {
				alphaIndexReg = regCache_.Alloc(RegCache::GEN_TEMP0);
				SHRX(64, alphaIndexReg, MDisp(srcReg, 8), vReg);
			} else {
				regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
				alphaIndexReg = regCache_.Alloc(RegCache::GEN_TEMP0);

				MOV(64, R(alphaIndexReg), MDisp(srcReg, 8));
				MOV(32, R(RCX), R(vReg));
				SHR(64, R(alphaIndexReg), R(CL));
			}
			regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
			regCache_.ForceRelease(RegCache::GEN_ARG_V);
		} else {
			X64Reg uReg = regCache_.Find(RegCache::GEN_ARG_U);
			if (uReg != RCX && !cpu_info.bBMI2)
				regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
			alphaIndexReg = regCache_.Alloc(RegCache::GEN_TEMP0);

			// Let's figure out the alphaIndex bit offset so we can read the right byte.
			// bitOffset = (u + v * 4) * 3;
			LEA(32, uReg, MComplex(uReg, vReg, SCALE_4, 0));
			LEA(32, uReg, MComplex(uReg, uReg, SCALE_2, 0));
			regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
			regCache_.ForceRelease(RegCache::GEN_ARG_V);

			if (cpu_info.bBMI2) {
				SHRX(64, alphaIndexReg, MDisp(srcReg, 8), uReg);
			} else {
				// And now the byte offset and bit from there, from those.
				MOV(32, R(alphaIndexReg), R(uReg));
				SHR(32, R(alphaIndexReg), Imm8(3));
				AND(32, R(uReg), Imm32(7));

				// Load 16 bits and mask, in case it straddles bytes.
				MOVZX(32, 16, alphaIndexReg, MComplex(srcReg, alphaIndexReg, SCALE_1, 8));
				// If not, it's in what was bufwReg.
				if (uReg != RCX) {
					_assert_(regCache_.Has(RegCache::GEN_SHIFTVAL));
					MOV(32, R(RCX), R(uReg));
				}
				SHR(32, R(alphaIndexReg), R(CL));
			}
			regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
			regCache_.ForceRelease(RegCache::GEN_ARG_U);
		}

		X64Reg alpha1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
		X64Reg alpha2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

		AND(32, R(alphaIndexReg), Imm32(7));

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
		SetJumpTarget(finishLerp6);
		SetJumpTarget(finishLerp8);

		SHL(32, R(srcReg), Imm8(24));
		X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
		OR(32, R(resultReg), R(srcReg));
		regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
		success = true;

		SetJumpTarget(finishZero);

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

	_assert_msg_(!id.linear, "Should not use this path for linear")
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
		if (cpu_info.bBMI2_fast)
			MOV(32, R(temp2Reg), Imm32(0x0F));
		else
			XOR(32, R(temp2Reg), R(temp2Reg));
		SHR(32, R(uReg), Imm8(1));
		FixupBranch skip = J_CC(CC_NC);
		// Track whether we shifted a 1 off or not.
		if (cpu_info.bBMI2_fast)
			SHL(32, R(temp2Reg), Imm8(4));
		else
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

	if (bitsPerTexel == 4 && !cpu_info.bBMI2) {
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
		if (cpu_info.bBMI2_fast) {
			MOV(8, R(resultReg), MRegSum(temp1Reg, resultReg));
			PEXT(32, resultReg, resultReg, R(temp2Reg));
		} else if (cpu_info.bBMI2) {
			SHRX(32, resultReg, MRegSum(temp1Reg, resultReg), temp2Reg);
			AND(32, R(resultReg), Imm8(0x0F));
		} else {
			MOV(8, R(resultReg), MRegSum(temp1Reg, resultReg));
			// RCX is now free.
			MOV(8, R(RCX), R(temp2Reg));
			SHR(8, R(resultReg), R(RCX));
			// Zero out any bits not shifted off.
			AND(32, R(resultReg), Imm8(0x0F));
		}
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
	Describe("TexDataS4");
	_assert_msg_(!id.linear, "Should not use this path for linear")
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
	_assert_msg_(!id.linear, "Should not use this path for linear")

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

bool SamplerJitCache::Jit_GetTexelCoords(const SamplerID &id) {
	Describe("Texel");

	X64Reg uReg = regCache_.Alloc(RegCache::GEN_ARG_U);
	X64Reg vReg = regCache_.Alloc(RegCache::GEN_ARG_V);
	X64Reg sReg = regCache_.Find(RegCache::VEC_ARG_S);
	X64Reg tReg = regCache_.Find(RegCache::VEC_ARG_T);
	if (id.hasAnyMips) {
		// We have to figure out levels and the proper width, ugh.
		X64Reg idReg = GetSamplerID();
		X64Reg tempReg = regCache_.Alloc(RegCache::GEN_TEMP0);

		X64Reg levelReg = INVALID_REG;
		if (regCache_.Has(RegCache::GEN_ARG_LEVEL)) {
			levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
		} else {
			levelReg = regCache_.Alloc(RegCache::GEN_ARG_LEVEL);
			MOV(32, R(levelReg), MDisp(RSP, stackArgPos_ + stackLevelOffset_));
		}

		// We'll multiply these at the same time, so it's nice to put together.
		UNPCKLPS(sReg, R(tReg));
		SHUFPS(sReg, R(sReg), _MM_SHUFFLE(1, 0, 1, 0));

		X64Reg sizesReg = regCache_.Alloc(RegCache::VEC_TEMP0);
		if (cpu_info.bSSE4_1) {
			PMOVZXWD(sizesReg, MComplex(idReg, levelReg, SCALE_4, offsetof(SamplerID, cached.sizes[0].w)));
		} else {
			MOVQ_xmm(sizesReg, MComplex(idReg, levelReg, SCALE_4, offsetof(SamplerID, cached.sizes[0].w)));
			X64Reg zeroReg = GetZeroVec();
			PUNPCKLWD(sizesReg, R(zeroReg));
			regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		}

		// We just want this value as a float, times 256.
		PSLLD(sizesReg, 8);
		CVTDQ2PS(sizesReg, R(sizesReg));

		// Okay, we can multiply now, and convert back to integer.
		MULPS(sReg, R(sizesReg));
		CVTTPS2DQ(sReg, R(sReg));
		regCache_.Release(sizesReg, RegCache::VEC_TEMP0);

		PSRAD(sReg, 8);

		// Reuse tempXYReg for the level1 values.
		if (!cpu_info.bSSE4_1)
			PSHUFD(tReg, R(sReg), _MM_SHUFFLE(3, 2, 3, 2));

		auto applyClampWrap = [&](X64Reg dest, bool clamp, bool isY, bool isLevel1) {
			int offset = offsetof(SamplerID, cached.sizes[0].w) + (isY ? 2 : 0) + (isLevel1 ? 4 : 0);
			// Grab the size, already pre-shifted for us.
			MOVZX(32, 16, tempReg, MComplex(idReg, levelReg, SCALE_4, offset));

			// Grab the size from the multiply.
			if (cpu_info.bSSE4_1) {
				if (isY || isLevel1)
					PEXTRD(R(dest), sReg, (isY ? 1 : 0) + (isLevel1 ? 2 : 0));
				else
					MOVD_xmm(R(dest), sReg);
			} else {
				X64Reg srcReg = isLevel1 ? tReg : sReg;
				MOVD_xmm(R(dest), srcReg);
				if (!isY)
					PSRLDQ(srcReg, 4);
			}

			SUB(32, R(tempReg), Imm8(1));
			AND(32, R(tempReg), Imm32(0x000001FF));
			if (clamp) {
				CMP(32, R(dest), R(tempReg));
				CMOVcc(32, dest, R(tempReg), CC_G);
				XOR(32, R(tempReg), R(tempReg));
				CMP(32, R(dest), R(tempReg));
				CMOVcc(32, dest, R(tempReg), CC_L);
			} else {
				AND(32, R(dest), R(tempReg));
			}
		};

		// Do the next level first, so we can save them and reuse the regs.
		// Note: for non-SSE4, this must be in S/T order.
		applyClampWrap(uReg, id.clampS, false, true);
		applyClampWrap(vReg, id.clampT, true, true);

		// Okay, now stuff them on the stack - we'll load them again later.
		MOV(32, MDisp(RSP, stackArgPos_ + stackUV1Offset_ + 0), R(uReg));
		MOV(32, MDisp(RSP, stackArgPos_ + stackUV1Offset_ + 4), R(vReg));

		// And then the given level.
		// Note: for non-SSE4, this must be in S/T order.
		applyClampWrap(uReg, id.clampS, false, false);
		applyClampWrap(vReg, id.clampT, true, false);

		UnlockSamplerID(idReg);
		regCache_.Release(tempReg, RegCache::GEN_TEMP0);
		regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
	} else {
		// Multiply, then convert to integer...
		UNPCKLPS(sReg, R(tReg));
		MULPS(sReg, M(constWidthHeight256f_));
		CVTTPS2DQ(sReg, R(sReg));
		// Great, shift out the fraction.
		PSRAD(sReg, 8);

		// Square textures are kinda common.
		bool clampApplied = false;
		if (id.width0Shift == id.height0Shift) {
			if (!id.clampS && !id.clampT) {
				PAND(sReg, M(constWidthMinus1i_));
				clampApplied = true;
			} else if (id.clampS && id.clampT && cpu_info.bSSE4_1) {
				X64Reg zeroReg = GetZeroVec();
				PMINSD(sReg, M(constWidthMinus1i_));
				PMAXSD(sReg, R(zeroReg));
				regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
				clampApplied = true;
			}
		}

		// Now extract to do the clamping (unless we already did it.)
		MOVQ_xmm(R(uReg), sReg);
		MOV(64, R(vReg), R(uReg));
		SHR(64, R(vReg), Imm8(32));
		// Strip off the top bits.
		AND(32, R(uReg), R(uReg));

		auto applyClampWrap = [this](X64Reg dest, bool clamp, uint8_t shift) {
			// Clamp and wrap both max out at 512.
			if (shift > 9)
				shift = 9;

			if (clamp) {
				X64Reg tempReg = regCache_.Alloc(RegCache::GEN_TEMP0);
				MOV(32, R(tempReg), Imm32((1 << shift) - 1));
				CMP(32, R(dest), R(tempReg));
				CMOVcc(32, dest, R(tempReg), CC_G);
				XOR(32, R(tempReg), R(tempReg));
				CMP(32, R(dest), R(tempReg));
				CMOVcc(32, dest, R(tempReg), CC_L);
				regCache_.Release(tempReg, RegCache::GEN_TEMP0);
			} else {
				AND(32, R(dest), Imm32((1 << shift) - 1));
			}
		};

		// Now apply clamp/wrap.
		if (!clampApplied) {
			applyClampWrap(uReg, id.clampS, id.width0Shift);
			applyClampWrap(vReg, id.clampT, id.height0Shift);
		}
	}

	regCache_.Unlock(uReg, RegCache::GEN_ARG_U);
	regCache_.Unlock(vReg, RegCache::GEN_ARG_V);
	regCache_.ForceRetain(RegCache::GEN_ARG_U);
	regCache_.ForceRetain(RegCache::GEN_ARG_V);

	// And get rid of S and T, we're done with them now.
	regCache_.Unlock(sReg, RegCache::VEC_ARG_S);
	regCache_.Unlock(tReg, RegCache::VEC_ARG_T);
	regCache_.ForceRelease(RegCache::VEC_ARG_S);
	regCache_.ForceRelease(RegCache::VEC_ARG_T);

	return true;
}

bool SamplerJitCache::Jit_GetTexelCoordsQuad(const SamplerID &id) {
	Describe("TexelQuad");

	X64Reg sReg = regCache_.Find(RegCache::VEC_ARG_S);
	X64Reg tReg = regCache_.Find(RegCache::VEC_ARG_T);

	// We use this if there are mips later, to apply wrap/clamp.
	X64Reg sizesReg = INVALID_REG;

	// Start by multiplying with the width/height... which might be complex with mips.
	if (id.hasAnyMips) {
		// We have to figure out levels and the proper width, ugh.
		X64Reg idReg = GetSamplerID();

		X64Reg levelReg = INVALID_REG;
		// To avoid ABI problems, we don't hold onto level.
		bool releaseLevelReg = !regCache_.Has(RegCache::GEN_ARG_LEVEL);
		if (!releaseLevelReg) {
			levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
		} else {
			releaseLevelReg = true;
			levelReg = regCache_.Alloc(RegCache::GEN_ARG_LEVEL);
			MOV(32, R(levelReg), MDisp(RSP, stackArgPos_ + stackLevelOffset_));
		}

		// This will load the current and next level's sizes, 16x4.
		sizesReg = regCache_.Alloc(RegCache::VEC_TEMP5);
		// We actually want this in 32-bit, though, so extend.
		if (cpu_info.bSSE4_1) {
			PMOVZXWD(sizesReg, MComplex(idReg, levelReg, SCALE_4, offsetof(SamplerID, cached.sizes[0].w)));
		} else {
			MOVQ_xmm(sizesReg, MComplex(idReg, levelReg, SCALE_4, offsetof(SamplerID, cached.sizes[0].w)));
			X64Reg zeroReg = GetZeroVec();
			PUNPCKLWD(sizesReg, R(zeroReg));
			regCache_.Unlock(zeroReg, RegCache::VEC_ZERO);
		}

		if (releaseLevelReg)
			regCache_.Release(levelReg, RegCache::GEN_ARG_LEVEL);
		else
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
		UnlockSamplerID(idReg);

		// Now make a float version of sizesReg, times 256.
		X64Reg sizes256Reg = regCache_.Alloc(RegCache::VEC_TEMP0);
		PSLLD(sizes256Reg, sizesReg, 8);
		CVTDQ2PS(sizes256Reg, R(sizes256Reg));

		// Next off, move S and T into a single reg, which will become U0 V0 U1 V1.
		UNPCKLPS(sReg, R(tReg));
		SHUFPS(sReg, R(sReg), _MM_SHUFFLE(1, 0, 1, 0));
		// And multiply by the sizes, all lined up already.
		MULPS(sReg, R(sizes256Reg));
		regCache_.Release(sizes256Reg, RegCache::VEC_TEMP0);

		// For wrap/clamp purposes, we want width or height minus one.  Do that now.
		PSUBD(sizesReg, M(constOnes32_));
		PAND(sizesReg, M(constMaxTexel32_));
	} else {
		// Easy mode.
		UNPCKLPS(sReg, R(tReg));
		MULPS(sReg, M(constWidthHeight256f_));
	}

	// And now, convert to integers for all later processing.
	CVTPS2DQ(sReg, R(sReg));

	// Now adjust X and Y...
	X64Reg tempXYReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	// Product a -128 constant.
	PCMPEQD(tempXYReg, R(tempXYReg));
	PSLLD(tempXYReg, 7);
	PADDD(sReg, R(tempXYReg));
	regCache_.Release(tempXYReg, RegCache::VEC_TEMP0);

	// We do want the fraction, though, so extract that to an XMM for later.
	X64Reg allFracReg = INVALID_REG;
	if (regCache_.Has(RegCache::VEC_FRAC))
		allFracReg = regCache_.Find(RegCache::VEC_FRAC);
	else
		allFracReg = regCache_.Alloc(RegCache::VEC_FRAC);
	// We only want the four bits after the first four, though.
	PSLLD(allFracReg, sReg, 24);
	PSRLD(allFracReg, 28);
	// It's convenient later if this is in the low words only.
	PACKSSDW(allFracReg, R(allFracReg));
	regCache_.Unlock(allFracReg, RegCache::VEC_FRAC);
	regCache_.ForceRetain(RegCache::VEC_FRAC);

	// With those extracted, we can now get rid of the fractional bits.
	PSRAD(sReg, 8);

	// Now it's time to separate the lanes into separate registers and add next UV offsets.
	if (id.hasAnyMips) {
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		PSHUFD(u1Reg, R(sReg), _MM_SHUFFLE(2, 2, 2, 2));
		PSHUFD(v1Reg, R(sReg), _MM_SHUFFLE(3, 3, 3, 3));
		PADDD(u1Reg, M(constUNext_));
		PADDD(v1Reg, M(constVNext_));
		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);
	}

	PSHUFD(tReg, R(sReg), _MM_SHUFFLE(1, 1, 1, 1));
	PSHUFD(sReg, R(sReg), _MM_SHUFFLE(0, 0, 0, 0));
	PADDD(tReg, M(constVNext_));
	PADDD(sReg, M(constUNext_));

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
			if (cpu_info.bAVX && bound.IsSimpleReg()) {
				VPCMPGTD(128, stReg, bound.GetSimpleReg(), R(temp0ClampReg));
			} else {
				MOVDQA(stReg, bound);
				PCMPGTD(stReg, R(temp0ClampReg));
			}
			// Throw away the values that are greater in our temp0ClampReg in progress result.
			PAND(temp0ClampReg, R(stReg));

			// Now, set bound only where ST was too high.
			PANDN(stReg, bound);
			// And put in the values that were fine.
			POR(stReg, R(temp0ClampReg));
		}
	};

	if (id.hasAnyMips) {
		// We'll spread sizes out into a temp.
		X64Reg spreadSizeReg = regCache_.Alloc(RegCache::VEC_TEMP1);

		PSHUFD(spreadSizeReg, R(sizesReg), _MM_SHUFFLE(0, 0, 0, 0));
		doClamp(id.clampS, sReg, R(spreadSizeReg));
		PSHUFD(spreadSizeReg, R(sizesReg), _MM_SHUFFLE(1, 1, 1, 1));
		doClamp(id.clampT, tReg, R(spreadSizeReg));
		X64Reg u1Reg = regCache_.Find(RegCache::VEC_U1);
		X64Reg v1Reg = regCache_.Find(RegCache::VEC_V1);
		PSHUFD(spreadSizeReg, R(sizesReg), _MM_SHUFFLE(2, 2, 2, 2));
		doClamp(id.clampS, u1Reg, R(spreadSizeReg));
		PSHUFD(spreadSizeReg, R(sizesReg), _MM_SHUFFLE(3, 3, 3, 3));
		doClamp(id.clampT, v1Reg, R(spreadSizeReg));
		regCache_.Unlock(u1Reg, RegCache::VEC_U1);
		regCache_.Unlock(v1Reg, RegCache::VEC_V1);

		regCache_.Release(spreadSizeReg, RegCache::VEC_TEMP1);
	} else {
		doClamp(id.clampS, sReg, M(constWidthMinus1i_));
		doClamp(id.clampT, tReg, M(constHeightMinus1i_));
	}

	if (sizesReg != INVALID_REG)
		regCache_.Release(sizesReg, RegCache::VEC_TEMP5);
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
	int bits = 0;
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
		bits = -8;
		break;

	case GE_TFMT_DXT3:
	case GE_TFMT_DXT5:
		bits = -16;
		break;

	default:
		success = false;
	}

	if (success && bits != 0) {
		if (bits < 0) {
			success = Jit_PrepareDataDXTOffsets(id, uReg, vReg, level1, -bits);
		} else if (id.swizzle) {
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
		if (cpu_info.bSSE4_1) {
			PMOVZXWD(bufwVecReg, MDisp(bufwReg, level1 ? 2 : 0));
		} else {
			PXOR(bufwVecReg, R(bufwVecReg));
			PINSRW(bufwVecReg, MDisp(bufwReg, level1 ? 2 : 0), 0);
		}
		PSHUFD(bufwVecReg, R(bufwVecReg), _MM_SHUFFLE(0, 0, 0, 0));
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);

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
		if (cpu_info.bSSE4_1) {
			PMOVZXWD(bufwVecReg, MDisp(bufwReg, level1 ? 2 : 0));
		} else {
			PXOR(bufwVecReg, R(bufwVecReg));
			PINSRW(bufwVecReg, MDisp(bufwReg, level1 ? 2 : 0), 0);
		}
		PSHUFD(bufwVecReg, R(bufwVecReg), _MM_SHUFFLE(0, 0, 0, 0));
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);
	}

	// Divide vvec by 8 in a temp.
	X64Reg vMultReg = regCache_.Alloc(RegCache::VEC_TEMP1);
	PSRLD(vMultReg, vReg, 3);

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
	PSRLD(uCopyReg, uReg, 7 + clz32_nonzero(bitsPerTexel - 1) - 32);
	PSLLD(uCopyReg, 7);
	// Add it in to our running total.
	PADDD(vReg, R(uCopyReg));

	if (bitsPerTexel == 4) {
		// Finally, we want (uvec & 31) / 2.  Use a 16-bit wall.
		PSLLW(uCopyReg, uReg, 11);
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

bool SamplerJitCache::Jit_PrepareDataDXTOffsets(const SamplerID &id, Rasterizer::RegCache::Reg uReg, Rasterizer::RegCache::Reg vReg, bool level1, int blockSize) {
	Describe("DataOffDXT");
	// Wwe need to get the block's offset, which is:
	// blockPos = src + (v/4 * bufw/4 + u/4) * blockSize
	// We distribute the blockSize constant for convenience:
	// blockPos = src + (blockSize*v/4 * bufw/4 + blockSize*u/4)

	X64Reg baseVReg = regCache_.Find(level1 ? RegCache::VEC_INDEX1 : RegCache::VEC_INDEX);
	// This gives us the V factor for the block, which we multiply by bufw.
	PSRLD(baseVReg, vReg, 2);
	PSLLD(baseVReg, blockSize == 16 ? 4 : 3);

	X64Reg bufwVecReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	if (!id.useStandardBufw || id.hasAnyMips) {
		// Spread bufw into each lane.
		X64Reg bufwReg = regCache_.Find(RegCache::GEN_ARG_BUFW_PTR);
		if (cpu_info.bSSE4_1) {
			PMOVZXWD(bufwVecReg, MDisp(bufwReg, level1 ? 2 : 0));
		} else {
			PXOR(bufwVecReg, R(bufwVecReg));
			PINSRW(bufwVecReg, MDisp(bufwReg, level1 ? 2 : 0), 0);
		}
		PSHUFD(bufwVecReg, R(bufwVecReg), _MM_SHUFFLE(0, 0, 0, 0));
		regCache_.Unlock(bufwReg, RegCache::GEN_ARG_BUFW_PTR);

		// Divide by 4 before the multiply.
		PSRLD(bufwVecReg, 2);
	}

	if (id.useStandardBufw && !id.hasAnyMips) {
		int amt = id.width0Shift - 2;
		if (amt < 0)
			PSRLD(baseVReg, -amt);
		else if (amt > 0)
			PSLLD(baseVReg, amt);
	} else if (cpu_info.bSSE4_1) {
		// And now multiply.  This is slow, but not worse than the SSE2 version...
		PMULLD(baseVReg, R(bufwVecReg));
	} else {
		// Copy that into another temp for multiply.
		X64Reg vOddLaneReg = regCache_.Alloc(RegCache::VEC_TEMP1);
		MOVDQA(vOddLaneReg, R(baseVReg));

		// Okay, first, multiply to get XXXX CCCC XXXX AAAA.
		PMULUDQ(baseVReg, R(bufwVecReg));
		PSRLDQ(vOddLaneReg, 4);
		PSRLDQ(bufwVecReg, 4);
		// And now get XXXX DDDD XXXX BBBB.
		PMULUDQ(vOddLaneReg, R(bufwVecReg));

		// We know everything is positive, so XXXX must be zero.  Let's combine.
		PSLLDQ(vOddLaneReg, 4);
		POR(baseVReg, R(vOddLaneReg));
		regCache_.Release(vOddLaneReg, RegCache::VEC_TEMP1);
	}
	regCache_.Release(bufwVecReg, RegCache::VEC_TEMP0);

	// Now add in the U factor for the block.
	X64Reg baseUReg = regCache_.Alloc(RegCache::VEC_TEMP0);
	PSRLD(baseUReg, uReg, 2);
	PSLLD(baseUReg, blockSize == 16 ? 4 : 3);
	PADDD(baseVReg, R(baseUReg));
	regCache_.Release(baseUReg, RegCache::VEC_TEMP0);

	// Okay, the base index (block byte offset from src) is ready.
	regCache_.Unlock(baseVReg, level1 ? RegCache::VEC_INDEX1 : RegCache::VEC_INDEX);
	regCache_.ForceRetain(level1 ? RegCache::VEC_INDEX1 : RegCache::VEC_INDEX);

	// For everything else, we only want the low two bits of U and V.
	PSLLD(uReg, 30);
	PSLLD(vReg, 30);

	X64Reg alphaTempRegU = regCache_.Alloc(RegCache::VEC_TEMP0);
	if (id.TexFmt() == GE_TFMT_DXT3 || id.TexFmt() == GE_TFMT_DXT5)
		PSRLD(alphaTempRegU, uReg, 30);

	PSRLD(uReg, 30 - 1);
	PSRLD(vReg, 30 - 3);
	// At this point, uReg is now the bit offset of the color index.
	PADDD(uReg, R(vReg));

	// Grab the alpha index into vReg next.
	if (id.TexFmt() == GE_TFMT_DXT3 || id.TexFmt() == GE_TFMT_DXT5) {
		PSRLD(vReg, 1);
		PADDD(vReg, R(alphaTempRegU));

		if (id.TexFmt() == GE_TFMT_DXT3) {
			PSLLD(vReg, 2);
		} else if (id.TexFmt() == GE_TFMT_DXT5) {
			// Multiply by 3.
			PSLLD(alphaTempRegU, vReg, 1);
			PADDD(vReg, R(alphaTempRegU));
		}
	}
	regCache_.Release(alphaTempRegU, RegCache::VEC_TEMP0);

	return true;
}

bool SamplerJitCache::Jit_DecodeQuad(const SamplerID &id, bool level1) {
	GETextureFormat decodeFmt = id.TexFmt();
	switch (id.TexFmt()) {
	case GE_TFMT_CLUT32:
	case GE_TFMT_CLUT16:
	case GE_TFMT_CLUT8:
	case GE_TFMT_CLUT4:
		// The values match, so just use the clut fmt.
		decodeFmt = (GETextureFormat)id.ClutFmt();
		break;

	default:
		// We'll decode below.
		break;
	}

	bool success = true;
	X64Reg quadReg = regCache_.Find(level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);

	switch (decodeFmt) {
	case GE_TFMT_5650:
		success = Jit_Decode5650Quad(id, quadReg);
		break;

	case GE_TFMT_5551:
		success = Jit_Decode5551Quad(id, quadReg);
		break;

	case GE_TFMT_4444:
		success = Jit_Decode4444Quad(id, quadReg);
		break;

	default:
		// Doesn't need decoding.
		break;
	}

	regCache_.Unlock(quadReg, level1 ? RegCache::VEC_RESULT1 : RegCache::VEC_RESULT);
	return success;
}

bool SamplerJitCache::Jit_Decode5650Quad(const SamplerID &id, Rasterizer::RegCache::Reg quadReg) {
	Describe("5650Quad");
	X64Reg temp1Reg = regCache_.Alloc(RegCache::VEC_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::VEC_TEMP2);

	// Filter out red only into temp1.  We do this by shifting into a wall.
	PSLLD(temp1Reg, quadReg, 32 - 5);
	// Move it right to the top of the 8 bits.
	PSRLD(temp1Reg, 24);

	// Now we bring in blue, since it's also 5 like red.
	// Luckily, we know the top 16 bits are zero.  Shift right into a wall.
	PSRLD(temp2Reg, quadReg, 11);
	// Shift blue into place at 19, and merge back to temp1.
	PSLLD(temp2Reg, 19);
	POR(temp1Reg, R(temp2Reg));

	// Make a copy back in temp2, and shift left 1 so we can swizzle together with G.
	PSLLD(temp2Reg, temp1Reg, 1);

	// We go to green last because it's the different one.  Shift off red and blue.
	PSRLD(quadReg, 5);
	// Use a word shift to put a wall just at the right place, top 6 bits of second byte.
	PSLLW(quadReg, 10);
	// Combine with temp2 (for swizzling), then merge in temp1 (R+B pre-swizzle.)
	POR(temp2Reg, R(quadReg));
	POR(quadReg, R(temp1Reg));

	// Now shift and mask temp2 for swizzle.
	PSRLD(temp2Reg, 6);
	PAND(temp2Reg, M(const5650Swizzle_));
	// And then OR that in too.  Only alpha left now.
	POR(quadReg, R(temp2Reg));

	if (id.useTextureAlpha) {
		// Just put a fixed FF in.  Maybe we could even avoid this and act like it's FF later...
		PCMPEQD(temp2Reg, R(temp2Reg));
		PSLLD(temp2Reg, 24);
		POR(quadReg, R(temp2Reg));
	}

	regCache_.Release(temp1Reg, RegCache::VEC_TEMP1);
	regCache_.Release(temp2Reg, RegCache::VEC_TEMP2);
	return true;
}

bool SamplerJitCache::Jit_Decode5650(const SamplerID &id) {
	Describe("5650");
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

	if (cpu_info.bBMI2_fast) {
		// Start off with the high bits.
		MOV(32, R(temp1Reg), Imm32(0x00F8FCF8));
		PDEP(32, temp1Reg, resultReg, R(temp1Reg));
		if (id.useTextureAlpha || id.fetch)
			OR(32, R(temp1Reg), Imm32(0xFF000000));

		// Now grab the low bits (they end up packed.)
		MOV(32, R(temp2Reg), Imm32(0x0000E61C));
		PEXT(32, resultReg, resultReg, R(temp2Reg));
		// And spread them back out.
		MOV(32, R(temp2Reg), Imm32(0x00070307));
		PDEP(32, resultReg, resultReg, R(temp2Reg));

		// Finally put the high bits in, we're done.
		OR(32, R(resultReg), R(temp1Reg));
	} else {
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
		if (id.useTextureAlpha || id.fetch)
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
	}

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return true;
}

bool SamplerJitCache::Jit_Decode5551Quad(const SamplerID &id, Rasterizer::RegCache::Reg quadReg) {
	Describe("5551Quad");
	X64Reg temp1Reg = regCache_.Alloc(RegCache::VEC_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::VEC_TEMP2);

	// Filter out red only into temp1.  We do this by shifting into a wall.
	PSLLD(temp1Reg, quadReg, 32 - 5);
	// Move it right to the top of the 8 bits.
	PSRLD(temp1Reg, 24);

	// Add in green and shift into place (top 5 bits of byte 2.)
	PSRLD(temp2Reg, quadReg, 5);
	PSLLW(temp2Reg, 11);
	POR(temp1Reg, R(temp2Reg));

	// First, extend alpha using an arithmetic shift.
	// We use 10 to meanwhile get rid of green too.  The extra alpha bits are fine.
	PSRAW(quadReg, 10);
	// This gets rid of those extra alpha bits and puts blue in place too.
	PSLLD(quadReg, 19);

	// Combine both together, we still need to swizzle.
	POR(quadReg, R(temp1Reg));
	PSRLD(temp1Reg, quadReg, 5);

	// Now for swizzle, we'll mask carefully to avoid overflow.
	PAND(temp1Reg, M(const5551Swizzle_));
	// Then finally merge in the swizzle bits.
	POR(quadReg, R(temp1Reg));

	regCache_.Release(temp1Reg, RegCache::VEC_TEMP1);
	regCache_.Release(temp2Reg, RegCache::VEC_TEMP2);
	return true;
}

bool SamplerJitCache::Jit_Decode5551(const SamplerID &id) {
	Describe("5551");
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

	if (cpu_info.bBMI2_fast) {
		// First, grab the top bits.
		bool keepAlpha = id.useTextureAlpha || id.fetch;
		MOV(32, R(temp1Reg), Imm32(keepAlpha ? 0x01F8F8F8 : 0x00F8F8F8));
		PDEP(32, resultReg, resultReg, R(temp1Reg));

		// Now make the swizzle bits.
		MOV(32, R(temp2Reg), R(resultReg));
		SHR(32, R(temp2Reg), Imm8(5));
		AND(32, R(temp2Reg), Imm32(0x00070707));

		if (keepAlpha) {
			// Sign extend the alpha bit to 8 bits.
			SHL(32, R(resultReg), Imm8(7));
			SAR(32, R(resultReg), Imm8(7));
		}

		OR(32, R(resultReg), R(temp2Reg));
	} else {
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

		if (id.useTextureAlpha || id.fetch) {
			// For A, we sign extend to get either 16 1s or 0s of alpha.
			SAR(16, R(resultReg), Imm8(15));
			// Now, shift left by 24 to get the lowest 8 of those at the top.
			SHL(32, R(resultReg), Imm8(24));
			OR(32, R(resultReg), R(temp2Reg));
		} else {
			MOV(32, R(resultReg), R(temp2Reg));
		}
	}

	regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	regCache_.Unlock(resultReg, RegCache::GEN_RESULT);
	return true;
}

bool SamplerJitCache::Jit_Decode4444Quad(const SamplerID &id, Rasterizer::RegCache::Reg quadReg) {
	Describe("4444Quad");
	X64Reg temp1Reg = regCache_.Alloc(RegCache::VEC_TEMP1);
	X64Reg temp2Reg = regCache_.Alloc(RegCache::VEC_TEMP2);

	// Mask and move red into position within temp1.
	PSLLD(temp1Reg, quadReg, 28);
	PSRLD(temp1Reg, 24);

	// Green is easy too, we use a word shift to get a free wall.
	PSRLD(temp2Reg, quadReg, 4);
	PSLLW(temp2Reg, 12);
	POR(temp1Reg, R(temp2Reg));

	// Blue isn't last this time, but it's next.
	PSRLD(temp2Reg, quadReg, 8);
	PSLLD(temp2Reg, 28);
	PSRLD(temp2Reg, 8);
	POR(temp1Reg, R(temp2Reg));

	if (id.useTextureAlpha) {
		// Last but not least, alpha.
		PSRLW(quadReg, 12);
		PSLLD(quadReg, 28);
		POR(quadReg, R(temp1Reg));

		// Masking isn't necessary here since everything is 4 wide.
		PSRLD(temp1Reg, quadReg, 4);
		POR(quadReg, R(temp1Reg));
	} else {
		// Overwrite quadReg (we need temp1 as a copy anyway.)
		PSRLD(quadReg, temp1Reg, 4);
		POR(quadReg, R(temp1Reg));
	}

	regCache_.Release(temp1Reg, RegCache::VEC_TEMP1);
	regCache_.Release(temp2Reg, RegCache::VEC_TEMP2);
	return true;
}

alignas(16) static const u32 color4444mask[4] = { 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, };

bool SamplerJitCache::Jit_Decode4444(const SamplerID &id) {
	Describe("4444");
	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);

	if (cpu_info.bBMI2_fast) {
		X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
		// First, spread the bits out with spaces.
		MOV(32, R(temp1Reg), Imm32(0xF0F0F0F0));
		PDEP(32, resultReg, resultReg, R(temp1Reg));

		// Now swizzle the low bits in.
		MOV(32, R(temp1Reg), R(resultReg));
		SHR(32, R(temp1Reg), Imm8(4));
		OR(32, R(resultReg), R(temp1Reg));

		regCache_.Release(temp1Reg, RegCache::GEN_TEMP1);
	} else {
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
	}
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

	if (!cpu_info.bBMI2) {
		bool hasRCX = regCache_.ChangeReg(RCX, RegCache::GEN_SHIFTVAL);
		_assert_msg_(hasRCX, "Could not obtain RCX, locked?");
	}

	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	X64Reg idReg = GetSamplerID();
	MOV(32, R(temp1Reg), MDisp(idReg, offsetof(SamplerID, cached.clutFormat)));
	UnlockSamplerID(idReg);

	X64Reg resultReg = regCache_.Find(RegCache::GEN_RESULT);
	int shiftedToSoFar = 0;

	// Shift = (clutformat >> 2) & 0x1F
	if (id.hasClutShift) {
		SHR(32, R(temp1Reg), Imm8(2 - shiftedToSoFar));
		shiftedToSoFar = 2;

		if (cpu_info.bBMI2) {
			SHRX(32, resultReg, R(resultReg), temp1Reg);
		} else {
			_assert_(regCache_.Has(RegCache::GEN_SHIFTVAL));
			MOV(32, R(RCX), R(temp1Reg));
			SHR(32, R(resultReg), R(RCX));
		}
	}

	// Mask = (clutformat >> 8) & 0xFF
	if (id.hasClutMask) {
		SHR(32, R(temp1Reg), Imm8(8 - shiftedToSoFar));
		shiftedToSoFar = 8;

		AND(32, R(resultReg), R(temp1Reg));
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
		SHR(32, R(temp1Reg), Imm8(16 - shiftedToSoFar));
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
	_assert_msg_(!id.linear, "Should not use this path for linear");

	if (!id.useSharedClut) {
		X64Reg temp2Reg = regCache_.Alloc(RegCache::GEN_TEMP2);

		if (regCache_.Has(RegCache::GEN_ARG_LEVEL)) {
			X64Reg levelReg = regCache_.Find(RegCache::GEN_ARG_LEVEL);
			// We need to multiply by 16 and add, LEA allows us to copy too.
			LEA(32, temp2Reg, MScaled(levelReg, SCALE_4, 0));
			regCache_.Unlock(levelReg, RegCache::GEN_ARG_LEVEL);
			if (id.fetch)
				regCache_.ForceRelease(RegCache::GEN_ARG_LEVEL);
		} else {
			_assert_(stackLevelOffset_ != -1);
			// The argument was saved on the stack.
			MOV(32, R(temp2Reg), MDisp(RSP, stackArgPos_ + stackLevelOffset_));
			LEA(32, temp2Reg, MScaled(temp2Reg, SCALE_4, 0));
		}

		// Second step of the multiply by 16 (since we only multiplied by 4 before.)
		LEA(64, resultReg, MComplex(resultReg, temp2Reg, SCALE_4, 0));
		regCache_.Release(temp2Reg, RegCache::GEN_TEMP2);
	}

	X64Reg idReg = GetSamplerID();
	X64Reg temp1Reg = regCache_.Alloc(RegCache::GEN_TEMP1);
	MOV(PTRBITS, R(temp1Reg), MDisp(idReg, offsetof(SamplerID, cached.clut)));
	UnlockSamplerID(idReg);

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
		return Jit_Decode5650(id);

	case GE_CMODE_16BIT_ABGR5551:
		return Jit_Decode5551(id);

	case GE_CMODE_16BIT_ABGR4444:
		return Jit_Decode4444(id);

	case GE_CMODE_32BIT_ABGR8888:
		return true;

	default:
		return false;
	}
}

};

#endif
