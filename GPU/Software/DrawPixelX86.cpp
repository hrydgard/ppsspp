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
#include "GPU/Software/SoftGpu.h"
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
static bool Accessible(const T *t1, const T *t2) {
	ptrdiff_t diff = (const uint8_t *)t1 - (const uint8_t *)t2;
	return diff > -0x7FFFFFE0 && diff < 0x7FFFFFE0;
}

template <typename T>
static bool ConstAccessible(const T *t) {
	return Accessible((const uint8_t *)&const255_16s[0], (const uint8_t *)t);
}

template <typename T>
static OpArg MConstDisp(X64Reg r, const T *t) {
	_assert_(ConstAccessible(t));
	ptrdiff_t diff = (const uint8_t *)t - (const uint8_t *)&const255_16s[0];
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
	success = success && Jit_ColorTest(id);

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

PixelRegCache::Reg PixelJitCache::GetColorOff(const PixelFuncID &id) {
	if (!regCache_.Has(PixelRegCache::COLOR_OFF, PixelRegCache::T_GEN)) {
		if (id.useStandardStride) {
			// In this mode, we force argXReg to the off, and throw away argYReg.
			SHL(32, R(argYReg), Imm8(9));
			ADD(32, R(argXReg), R(argYReg));

			// Now add the pointer for the color buffer.
			MOV(PTRBITS, R(argYReg), ImmPtr(&fb.data));
			MOV(PTRBITS, R(argYReg), MatR(argYReg));
			LEA(PTRBITS, argYReg, MComplex(argYReg, argXReg, id.FBFormat() == GE_FORMAT_8888 ? 4 : 2, 0));
			// With that, argYOff is now COLOR_OFF.
			regCache_.Release(argYReg, PixelRegCache::T_GEN, PixelRegCache::COLOR_OFF);
			// Lock it, because we can't recalculate this.
			regCache_.ForceLock(PixelRegCache::COLOR_OFF, PixelRegCache::T_GEN);

			// Next, also calculate the depth offset, unless we won't need it at all.
			if (id.depthWrite || id.DepthTestFunc() != GE_COMP_ALWAYS) {
				X64Reg temp = regCache_.Alloc(PixelRegCache::DEPTH_OFF, PixelRegCache::T_GEN);
				MOV(PTRBITS, R(temp), ImmPtr(&depthbuf.data));
				MOV(PTRBITS, R(temp), MatR(temp));
				LEA(PTRBITS, argXReg, MComplex(temp, argXReg, 2, 0));
				regCache_.Release(temp, PixelRegCache::T_GEN);

				// Okay, same deal - release as DEPTH_OFF and force lock.
				regCache_.Release(argXReg, PixelRegCache::T_GEN, PixelRegCache::DEPTH_OFF);
				regCache_.ForceLock(PixelRegCache::DEPTH_OFF, PixelRegCache::T_GEN);
			} else {
				regCache_.Release(argXReg, PixelRegCache::T_GEN);
			}

			return regCache_.Find(PixelRegCache::COLOR_OFF, PixelRegCache::T_GEN);
		}

		X64Reg gstateReg = GetGState();
		X64Reg r = regCache_.Alloc(PixelRegCache::COLOR_OFF, PixelRegCache::T_GEN);
		MOVZX(32, 16, r, MDisp(gstateReg, offsetof(GPUgstate, fbwidth)));
		regCache_.Unlock(gstateReg, PixelRegCache::T_GEN);

		AND(32, R(r), Imm32(0x000007FC));
		IMUL(32, r, R(argYReg));
		ADD(32, R(r), R(argXReg));

		X64Reg temp = regCache_.Alloc(PixelRegCache::TEMP_HELPER, PixelRegCache::T_GEN);
		MOV(PTRBITS, R(temp), ImmPtr(&fb.data));
		MOV(PTRBITS, R(temp), MatR(temp));
		LEA(PTRBITS, r, MComplex(temp, r, id.FBFormat() == GE_FORMAT_8888 ? 4 : 2, 0));
		regCache_.Release(temp, PixelRegCache::T_GEN);

		return r;
	}
	return regCache_.Find(PixelRegCache::COLOR_OFF, PixelRegCache::T_GEN);
}

PixelRegCache::Reg PixelJitCache::GetDepthOff(const PixelFuncID &id) {
	if (!regCache_.Has(PixelRegCache::DEPTH_OFF, PixelRegCache::T_GEN)) {
		// If both color and depth use 512, the offsets are the same.
		if (id.useStandardStride) {
			// Calculate once inside GetColorOff().
			regCache_.Unlock(GetColorOff(id), PixelRegCache::T_GEN);
			return regCache_.Find(PixelRegCache::DEPTH_OFF, PixelRegCache::T_GEN);
		}

		X64Reg gstateReg = GetGState();
		X64Reg r = regCache_.Alloc(PixelRegCache::DEPTH_OFF, PixelRegCache::T_GEN);
		MOVZX(32, 16, r, MDisp(gstateReg, offsetof(GPUgstate, zbwidth)));
		regCache_.Unlock(gstateReg, PixelRegCache::T_GEN);

		AND(32, R(r), Imm32(0x000007FC));
		IMUL(32, r, R(argYReg));
		ADD(32, R(r), R(argXReg));

		X64Reg temp = regCache_.Alloc(PixelRegCache::TEMP_HELPER, PixelRegCache::T_GEN);
		MOV(PTRBITS, R(temp), ImmPtr(&depthbuf.data));
		MOV(PTRBITS, R(temp), MatR(temp));
		LEA(PTRBITS, r, MComplex(temp, r, 2, 0));
		regCache_.Release(temp, PixelRegCache::T_GEN);

		return r;
	}
	return regCache_.Find(PixelRegCache::DEPTH_OFF, PixelRegCache::T_GEN);
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

bool PixelJitCache::Jit_ColorTest(const PixelFuncID &id) {
	if (!id.colorTest || id.clearMode)
		return true;

	// We'll have 4 with fog released, so we're using them all...
	X64Reg gstateReg = GetGState();
	X64Reg funcReg = regCache_.Alloc(PixelRegCache::TEMP0, PixelRegCache::T_GEN);
	X64Reg maskReg = regCache_.Alloc(PixelRegCache::TEMP1, PixelRegCache::T_GEN);
	X64Reg refReg = regCache_.Alloc(PixelRegCache::TEMP2, PixelRegCache::T_GEN);

	// First, load the registers: mask and ref.
	MOV(32, R(maskReg), MDisp(gstateReg, offsetof(GPUgstate, colortestmask)));
	AND(32, R(maskReg), Imm32(0x00FFFFFF));
	MOV(32, R(refReg), MDisp(gstateReg, offsetof(GPUgstate, colorref)));
	AND(32, R(refReg), R(maskReg));

	// Temporarily abuse funcReg to grab the color into maskReg.
	MOVD_xmm(R(funcReg), argColorReg);
	AND(32, R(maskReg), R(funcReg));

	// Now that we're setup, get the func and follow it.
	MOVZX(32, 8, funcReg, MDisp(gstateReg, offsetof(GPUgstate, colortest)));
	AND(32, R(funcReg), Imm32(3));
	regCache_.Unlock(gstateReg, PixelRegCache::T_GEN);

	CMP(32, R(funcReg), Imm32(GE_COMP_ALWAYS));
	// Discard for GE_COMP_NEVER...
	Discard(CC_B);
	FixupBranch skip = J_CC(CC_E);

	CMP(32, R(funcReg), Imm32(GE_COMP_EQUAL));
	FixupBranch doEqual = J_CC(CC_E);
	regCache_.Release(funcReg, PixelRegCache::T_GEN);

	// The not equal path here... if they are equal, we discard.
	CMP(32, R(refReg), R(maskReg));
	Discard(CC_E);
	FixupBranch skip2 = J();

	SetJumpTarget(doEqual);
	CMP(32, R(refReg), R(maskReg));
	Discard(CC_NE);

	regCache_.Release(maskReg, PixelRegCache::T_GEN);
	regCache_.Release(refReg, PixelRegCache::T_GEN);

	SetJumpTarget(skip);
	SetJumpTarget(skip2);

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
