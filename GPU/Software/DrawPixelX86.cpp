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
#if PPSSPP_ARCH(AMD64)

#include <emmintrin.h>
#include "Common/x64Emitter.h"
#include "Common/CPUDetect.h"
#include "GPU/GPUState.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/ge_constants.h"

using namespace Gen;

namespace Rasterizer {

#if PPSSPP_PLATFORM(WINDOWS)
static const X64Reg argXReg = RCX;
static const X64Reg argYReg = RDX;
static const X64Reg argZReg = R8;
static const X64Reg argFogReg = R9;
static const X64Reg argColorReg = XMM4;

// Must save: RBX, RSP, RBP, RDI, RSI, R12-R15, XMM6-15
#else
static const X64Reg argXReg = RDI;
static const X64Reg argYReg = RSI;
static const X64Reg argZReg = RDX;
static const X64Reg argFogReg = RCX;
static const X64Reg argColorReg = XMM0;

// Must save: RBX, RSP, RBP, R12-R15
#endif

// This one is the const base.  Also a set of 255s.
alignas(16) static const uint16_t const255_16s[8] = { 255, 255, 255, 255, 255, 255, 255, 255 };
// This is used for a multiply that divides by 255 with shifting.
alignas(16) static const uint16_t by255i[8] = { 0x8081, 0x8081, 0x8081, 0x8081, 0x8081, 0x8081, 0x8081, 0x8081 };

template <typename T>
static bool ConstAccessible(const T *t) {
	ptrdiff_t diff = (const uint8_t *)&const255_16s[0] - (const uint8_t *)t;
	return diff > -0x7FFFFFE0 && diff < 0x7FFFFFE0;
}

template <typename T>
static OpArg MConstDisp(X64Reg r, const T *t) {
	_assert_(ConstAccessible(t));
	ptrdiff_t diff = (const uint8_t *)&const255_16s[0] - (const uint8_t *)t;
	return MDisp(r, (int)diff);
}

SingleFunc PixelJitCache::CompileSingle(const PixelFuncID &id) {
	// Setup the reg cache.
	regCache_.Reset();
	regCache_.Release(RAX, PixelRegCache::T_GEN);
	regCache_.Release(R10, PixelRegCache::T_GEN);
	regCache_.Release(R11, PixelRegCache::T_GEN);
	regCache_.Release(XMM1, PixelRegCache::T_VEC);
	regCache_.Release(XMM2, PixelRegCache::T_VEC);
	regCache_.Release(XMM3, PixelRegCache::T_VEC);
	regCache_.Release(XMM5, PixelRegCache::T_VEC);

#if !PPSSPP_PLATFORM(WINDOWS)
	regCache_.Release(R8, PixelRegCache::T_GEN);
	regCache_.Release(R9, PixelRegCache::T_GEN);
	regCache_.Release(XMM4, PixelRegCache::T_VEC);
#else
	regCache_.Release(XMM0, PixelRegCache::T_VEC);
#endif

	BeginWrite();
	const u8 *start = AlignCode16();
	bool success = true;

	// Start with the depth range.
	success = success && Jit_ApplyDepthRange(id);

	// Next, let's clamp the color (might affect alpha test, and everything expects it clamped.)
	// We simply convert to 4x8-bit to clamp.  Everything else expects color in this format.
	PACKSSDW(argColorReg, R(argColorReg));
	PACKUSWB(argColorReg, R(argColorReg));

	success = success && Jit_AlphaTest(id);
	// Fog is applied prior to color test.  Maybe before alpha test too, but it doesn't affect it...
	success = success && Jit_ApplyFog(id);

	// TODO: There's more...
	success = false;

	for (auto &fixup : discards_) {
		SetJumpTarget(fixup);
	}
	discards_.clear();

	if (!success) {
		EndWrite();
		ResetCodePtr(GetOffset(start));
		return nullptr;
	}

	EndWrite();
	return (SingleFunc)start;
}

PixelRegCache::Reg PixelJitCache::GetGState() {
	if (!regCache_.Has(PixelRegCache::GSTATE, PixelRegCache::T_GEN)) {
		X64Reg r = regCache_.Alloc(PixelRegCache::GSTATE, PixelRegCache::T_GEN);
		MOV(PTRBITS, R(r), ImmPtr(&gstate.nop));
		return r;
	}
	return regCache_.Find(PixelRegCache::GSTATE, PixelRegCache::T_GEN);
}

PixelRegCache::Reg PixelJitCache::GetConstBase() {
	if (!regCache_.Has(PixelRegCache::CONST_BASE, PixelRegCache::T_GEN)) {
		X64Reg r = regCache_.Alloc(PixelRegCache::CONST_BASE, PixelRegCache::T_GEN);
		MOV(PTRBITS, R(r), ImmPtr(&const255_16s[0]));
		return r;
	}
	return regCache_.Find(PixelRegCache::CONST_BASE, PixelRegCache::T_GEN);
}

void PixelJitCache::Discard(Gen::CCFlags cc) {
	discards_.push_back(J_CC(cc, true));
}

bool PixelJitCache::Jit_ApplyDepthRange(const PixelFuncID &id) {
	if (id.applyDepthRange) {
		X64Reg gstateReg = GetGState();
		X64Reg minReg = regCache_.Alloc(PixelRegCache::TEMP0, PixelRegCache::T_GEN);
		X64Reg maxReg = regCache_.Alloc(PixelRegCache::TEMP1, PixelRegCache::T_GEN);

		// Only load the lowest 16 bits of each.
		MOVZX(32, 16, minReg, MDisp(gstateReg, offsetof(GPUgstate, minz)));
		MOVZX(32, 16, maxReg, MDisp(gstateReg, offsetof(GPUgstate, maxz)));

		CMP(32, R(argZReg), R(minReg));
		Discard(CC_L);
		CMP(32, R(argZReg), R(maxReg));
		Discard(CC_G);

		regCache_.Unlock(gstateReg, PixelRegCache::T_GEN);
		regCache_.Release(minReg, PixelRegCache::T_GEN);
		regCache_.Release(maxReg, PixelRegCache::T_GEN);
	}

	// Since this is early on, try to free up the z reg if we don't need it anymore.
	if (id.clearMode && !id.DepthClear())
		regCache_.Release(argZReg, PixelRegCache::T_GEN);
	else if (!id.clearMode && !id.depthWrite && id.DepthTestFunc() == GE_COMP_ALWAYS)
		regCache_.Release(argZReg, PixelRegCache::T_GEN);

	return true;
}

bool PixelJitCache::Jit_AlphaTest(const PixelFuncID &id) {
	// Take care of ALWAYS/NEVER first.  ALWAYS is common, means disabled.
	switch (id.AlphaTestFunc()) {
	case GE_COMP_NEVER:
		CMP(32, R(RAX), R(RAX));
		Discard(CC_E);
		return true;

	case GE_COMP_ALWAYS:
		return true;

	default:
		break;
	}

	// Load alpha into its own general reg.
	X64Reg alphaReg;
	if (regCache_.Has(PixelRegCache::ALPHA, PixelRegCache::T_GEN)) {
		alphaReg = regCache_.Find(PixelRegCache::ALPHA, PixelRegCache::T_GEN);
	} else {
		alphaReg = regCache_.Alloc(PixelRegCache::ALPHA, PixelRegCache::T_GEN);

		// TODO: Could do this a bit more cheaply on SSE4.1?
		X64Reg zeroReg = regCache_.Alloc(PixelRegCache::TEMP0, PixelRegCache::T_VEC);
		X64Reg colorCopyReg = regCache_.Alloc(PixelRegCache::TEMP1, PixelRegCache::T_VEC);
		MOVDQA(colorCopyReg, R(argColorReg));
		PXOR(zeroReg, R(zeroReg));
		PUNPCKLBW(colorCopyReg, R(zeroReg));
		PEXTRW(alphaReg, R(colorCopyReg), 3);
		regCache_.Release(zeroReg, PixelRegCache::T_VEC);
		regCache_.Release(colorCopyReg, PixelRegCache::T_VEC);
	}

	if (id.hasAlphaTestMask) {
		// Unfortunate, we'll need gstate to load the mask.
		// Note: we leave the ALPHA purpose untouched and free it, because later code may reuse.
		X64Reg gstateReg = GetGState();
		X64Reg maskedReg = regCache_.Alloc(PixelRegCache::TEMP0, PixelRegCache::T_GEN);

		// The mask is >> 16, so we load + 2.
		MOVZX(32, 8, maskedReg, MDisp(gstateReg, offsetof(GPUgstate, alphatest) + 2));
		regCache_.Unlock(gstateReg, PixelRegCache::T_GEN);
		AND(32, R(maskedReg), R(alphaReg));
		regCache_.Unlock(alphaReg, PixelRegCache::T_GEN);

		// Okay now do the rest using the masked reg, which we modified.
		alphaReg = maskedReg;
		// Pre-emptively release, we don't need any other regs.
		regCache_.Release(maskedReg, PixelRegCache::T_GEN);
	} else {
		regCache_.Unlock(alphaReg, PixelRegCache::T_GEN);
	}

	// We hardcode the ref into this jit func.
	CMP(8, R(alphaReg), Imm8(id.alphaTestRef));

	switch (id.AlphaTestFunc()) {
	case GE_COMP_EQUAL:
		Discard(CC_NE);
		break;

	case GE_COMP_NOTEQUAL:
		Discard(CC_E);
		break;

	case GE_COMP_LESS:
		Discard(CC_AE);
		break;

	case GE_COMP_LEQUAL:
		Discard(CC_A);
		break;

	case GE_COMP_GREATER:
		Discard(CC_BE);
		break;

	case GE_COMP_GEQUAL:
		Discard(CC_B);
		break;
	}

	return true;
}

bool PixelJitCache::Jit_ApplyFog(const PixelFuncID &id) {
	if (!id.applyFog) {
		// Okay, anyone can use the fog register then.
		regCache_.Release(argFogReg, PixelRegCache::T_GEN);
		return true;
	}

	// Load fog and expand to 16 bit.  Ignore the high 8 bits, which'll match up with A.
	X64Reg zeroReg = regCache_.Alloc(PixelRegCache::TEMP0, PixelRegCache::T_VEC);
	X64Reg fogColorReg = regCache_.Alloc(PixelRegCache::TEMP1, PixelRegCache::T_VEC);
	PXOR(zeroReg, R(zeroReg));
	X64Reg gstateReg = GetGState();
	MOVD_xmm(fogColorReg, MDisp(gstateReg, offsetof(GPUgstate, fogcolor)));
	regCache_.Unlock(gstateReg, PixelRegCache::T_GEN);
	PUNPCKLBW(fogColorReg, R(zeroReg));

	// Load a set of 255s at 16 bit into a reg for later...
	X64Reg invertReg = regCache_.Alloc(PixelRegCache::TEMP2, PixelRegCache::T_VEC);
	X64Reg constReg = GetConstBase();
	MOVDQA(invertReg, MConstDisp(constReg, &const255_16s[0]));
	regCache_.Unlock(constReg, PixelRegCache::T_GEN);

	// Expand (we clamped) color to 16 bit as well, so we can multiply with fog.
	PUNPCKLBW(argColorReg, R(zeroReg));
	regCache_.Release(zeroReg, PixelRegCache::T_VEC);

	// Save A so we can put it back, we don't "fog" A.
	X64Reg alphaReg;
	if (regCache_.Has(PixelRegCache::ALPHA, PixelRegCache::T_GEN)) {
		alphaReg = regCache_.Find(PixelRegCache::ALPHA, PixelRegCache::T_GEN);
	} else {
		alphaReg = regCache_.Alloc(PixelRegCache::ALPHA, PixelRegCache::T_GEN);
		PEXTRW(alphaReg, R(argColorReg), 3);
	}

	// Okay, let's broadcast fog to an XMM.
	X64Reg fogMultReg = regCache_.Alloc(PixelRegCache::TEMP3, PixelRegCache::T_VEC);
	MOVD_xmm(fogMultReg, R(argFogReg));
	PSHUFLW(fogMultReg, R(fogMultReg), _MM_SHUFFLE(0, 0, 0, 0));
	// We can free up the actual fog reg now.
	regCache_.Release(argFogReg, PixelRegCache::T_GEN);

	// Now we multiply the existing color by fog...
	PMULLW(argColorReg, R(fogMultReg));
	// And then inverse the fog value using those 255s we loaded, and multiply by fog color.
	PSUBUSW(invertReg, R(fogMultReg));
	PMULLW(fogColorReg, R(invertReg));
	// At this point, argColorReg and fogColorReg are multiplied at 16-bit, so we need to sum.
	PADDUSW(argColorReg, R(fogColorReg));
	regCache_.Release(fogColorReg, PixelRegCache::T_VEC);
	regCache_.Release(fogMultReg, PixelRegCache::T_VEC);
	regCache_.Release(invertReg, PixelRegCache::T_VEC);

	// Now to divide by 255, we use bit tricks: multiply by 0x8081, and shift right by 16+7.
	constReg = GetConstBase();
	PMULHUW(argColorReg, MConstDisp(constReg, &by255i));
	regCache_.Unlock(constReg, PixelRegCache::T_GEN);
	// Now shift right by 7 (PMULHUW already did 16 of the shift.)
	PSRLW(argColorReg, 7);

	// Okay, put A back in and shrink to 8888 again.
	PINSRW(argColorReg, R(alphaReg), 3);
	PACKUSWB(argColorReg, R(argColorReg));
	regCache_.Unlock(alphaReg, PixelRegCache::T_GEN);

	return true;
}

};

#endif
