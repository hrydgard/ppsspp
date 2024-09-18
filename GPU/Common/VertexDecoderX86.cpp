// Copyright (c) 2013- PPSSPP Project.

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

#include "Common/CPUDetect.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/VertexDecoderHandwritten.h"

// We start out by converting the active matrices into 4x4 which are easier to multiply with
// using SSE / NEON and store them here.
alignas(16) static float bones[16 * 8];

using namespace Gen;

alignas(16) static const float by128[4] = {
	1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 128.0f
};
alignas(16) static const float by32768[4] = {
	1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f,
};

alignas(16) static const float by128_11[4] = {
	1.0f / 128.0f, 1.0f / 128.0f, 1.0f, 1.0f,
};
alignas(16) static const float by32768_11[4] = {
	1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f, 1.0f,
};

alignas(16) static const u32 threeMasks[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0 };
alignas(16) static const u32 aOne[4] = {0, 0, 0, 0x3F800000};

alignas(16) static const float by16384[4] = {
	1.0f / 16384.0f, 1.0f / 16384.0f, 1.0f / 16384.0f, 1.0f / 16384.0f,
};

#if PPSSPP_ARCH(AMD64)
#ifdef _WIN32
static const X64Reg tempReg1 = RAX;
static const X64Reg tempReg2 = R9;
static const X64Reg tempReg3 = R10;
static const X64Reg srcReg = RCX;
static const X64Reg dstReg = RDX;
static const X64Reg counterReg = R8;
static const X64Reg uvScalePtrReg = R9;  // only used during init
static const X64Reg alphaReg = R11;
#else
static const X64Reg tempReg1 = RAX;
static const X64Reg tempReg2 = R9;
static const X64Reg tempReg3 = R10;
static const X64Reg srcReg = RDI;
static const X64Reg dstReg = RSI;
static const X64Reg counterReg = RDX;
static const X64Reg uvScalePtrReg = RCX;  // only used during init
static const X64Reg alphaReg = R11;
#endif
#else
static const X64Reg tempReg1 = EAX;
static const X64Reg tempReg2 = EBX;
static const X64Reg tempReg3 = EDX;
static const X64Reg srcReg = ESI;
static const X64Reg dstReg = EDI;
static const X64Reg counterReg = ECX;
static const X64Reg uvScalePtrReg = EDX;  // only used during init
#endif

// XMM0-XMM5 are volatile on Windows X64
// XMM0-XMM7 are arguments (and thus volatile) on System V ABI (other x64 platforms)
static const X64Reg fpScaleOffsetReg = XMM0;

static const X64Reg fpScratchReg = XMM1;
static const X64Reg fpScratchReg2 = XMM2;
static const X64Reg fpScratchReg3 = XMM3;
static const X64Reg fpScratchReg4 = XMM4;

// We're gonna keep the current skinning matrix in 4 XMM regs. Fortunately we easily
// have space for that now.

// To debug, just comment them out one at a time until it works. We fall back
// on the interpreter if the compiler fails.

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoder::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoder::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},
	{&VertexDecoder::Step_WeightsU8Skin, &VertexDecoderJitCache::Jit_WeightsU8Skin},
	{&VertexDecoder::Step_WeightsU16Skin, &VertexDecoderJitCache::Jit_WeightsU16Skin},
	{&VertexDecoder::Step_WeightsFloatSkin, &VertexDecoderJitCache::Jit_WeightsFloatSkin},

	{&VertexDecoder::Step_WeightsU8ToFloat, &VertexDecoderJitCache::Jit_WeightsU8ToFloat},
	{&VertexDecoder::Step_WeightsU16ToFloat, &VertexDecoderJitCache::Jit_WeightsU16ToFloat},

	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
	{&VertexDecoder::Step_TcU8ToFloat, &VertexDecoderJitCache::Jit_TcU8ToFloat},
	{&VertexDecoder::Step_TcU16ToFloat, &VertexDecoderJitCache::Jit_TcU16ToFloat},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},

	{&VertexDecoder::Step_TcU16ThroughToFloat, &VertexDecoderJitCache::Jit_TcU16ThroughToFloat},
	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},

	{&VertexDecoder::Step_TcU8MorphToFloat, &VertexDecoderJitCache::Jit_TcU8MorphToFloat},
	{&VertexDecoder::Step_TcU16MorphToFloat, &VertexDecoderJitCache::Jit_TcU16MorphToFloat},
	{&VertexDecoder::Step_TcFloatMorph, &VertexDecoderJitCache::Jit_TcFloatMorph},
	{&VertexDecoder::Step_TcU8PrescaleMorph, &VertexDecoderJitCache::Jit_TcU8PrescaleMorph},
	{&VertexDecoder::Step_TcU16PrescaleMorph, &VertexDecoderJitCache::Jit_TcU16PrescaleMorph},
	{&VertexDecoder::Step_TcFloatPrescaleMorph, &VertexDecoderJitCache::Jit_TcFloatPrescaleMorph},

	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoder::Step_NormalS8ToFloat, &VertexDecoderJitCache::Jit_NormalS8ToFloat},
	{&VertexDecoder::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoder::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},

	{&VertexDecoder::Step_NormalS8Skin, &VertexDecoderJitCache::Jit_NormalS8Skin},
	{&VertexDecoder::Step_NormalS16Skin, &VertexDecoderJitCache::Jit_NormalS16Skin},
	{&VertexDecoder::Step_NormalFloatSkin, &VertexDecoderJitCache::Jit_NormalFloatSkin},

	{&VertexDecoder::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	{&VertexDecoder::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoder::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoder::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},

	{&VertexDecoder::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	{&VertexDecoder::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloatThrough},

	{&VertexDecoder::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoder::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoder::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8Skin, &VertexDecoderJitCache::Jit_PosS8Skin},
	{&VertexDecoder::Step_PosS16Skin, &VertexDecoderJitCache::Jit_PosS16Skin},
	{&VertexDecoder::Step_PosFloatSkin, &VertexDecoderJitCache::Jit_PosFloatSkin},

	{&VertexDecoder::Step_NormalS8Morph, &VertexDecoderJitCache::Jit_NormalS8Morph},
	{&VertexDecoder::Step_NormalS16Morph, &VertexDecoderJitCache::Jit_NormalS16Morph},
	{&VertexDecoder::Step_NormalFloatMorph, &VertexDecoderJitCache::Jit_NormalFloatMorph},

	{&VertexDecoder::Step_PosS8Morph, &VertexDecoderJitCache::Jit_PosS8Morph},
	{&VertexDecoder::Step_PosS16Morph, &VertexDecoderJitCache::Jit_PosS16Morph},
	{&VertexDecoder::Step_PosFloatMorph, &VertexDecoderJitCache::Jit_PosFloatMorph},

	{&VertexDecoder::Step_Color8888Morph, &VertexDecoderJitCache::Jit_Color8888Morph},
	{&VertexDecoder::Step_Color4444Morph, &VertexDecoderJitCache::Jit_Color4444Morph},
	{&VertexDecoder::Step_Color565Morph, &VertexDecoderJitCache::Jit_Color565Morph},
	{&VertexDecoder::Step_Color5551Morph, &VertexDecoderJitCache::Jit_Color5551Morph},
};

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec, int32_t *jittedSize) {
	dec_ = &dec;
	BeginWrite(4096);
	const u8 *start = this->AlignCode16();

	bool prescaleStep = false;
	// Look for prescaled texcoord steps
	for (int i = 0; i < dec.numSteps_; i++) {
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescale) {
			prescaleStep = true;
		}
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8PrescaleMorph ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16PrescaleMorph ||
			dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescaleMorph) {
			prescaleStep = true;
		}
	}


#if PPSSPP_ARCH(X86)
	// Store register values
	PUSH(ESI);
	PUSH(EDI);
	PUSH(EBX);
	PUSH(EBP);

	// Read parameters
	int offset = 4;
	MOV(32, R(srcReg), MDisp(ESP, 16 + offset + 0));
	MOV(32, R(dstReg), MDisp(ESP, 16 + offset + 4));
	MOV(32, R(counterReg), MDisp(ESP, 16 + offset + 8));
	MOV(32, R(uvScalePtrReg), MDisp(ESP, 16 + offset + 12));

	const uint8_t STACK_FIXED_ALLOC = 64;
#else
	// Parameters automatically fall into place.

	// This will align the stack properly to 16 bytes (the call of this function pushed RIP, which is 8 bytes).
	const uint8_t STACK_FIXED_ALLOC = 96 + 8;
#endif

	// Allocate temporary storage on the stack.
	SUB(PTRBITS, R(ESP), Imm8(STACK_FIXED_ALLOC));
	// Save XMM4/XMM5 which apparently can be problematic?
	// Actually, if they are, it must be a compiler bug because they SHOULD be ok.
	// So I won't bother.
	MOVUPS(MDisp(ESP, 0), XMM4);
	MOVUPS(MDisp(ESP, 16), XMM5);
	MOVUPS(MDisp(ESP, 32), XMM6);
	MOVUPS(MDisp(ESP, 48), XMM7);
#if PPSSPP_ARCH(AMD64)
	MOVUPS(MDisp(ESP, 64), XMM8);
	MOVUPS(MDisp(ESP, 80), XMM9);
#endif

	// Initialize alpha reg.
#if PPSSPP_ARCH(AMD64)
	if (dec.col) {
		MOV(32, R(alphaReg), Imm32(1));
	}
#endif

	// Keep the scale/offset in a few fp registers if we need it.
	// TODO: Read it from an argument pointer instead of gstate_c.uv.
	if (prescaleStep) {
		// uvScalePtrReg should point to gstate_c.uv, or wherever the UV scale we want to use is located.
		MOVUPS(fpScaleOffsetReg, MatR(uvScalePtrReg));
		if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
			MOV(PTRBITS, R(tempReg2), ImmPtr(&by128_11));
			MULPS(fpScaleOffsetReg, MatR(tempReg2));
		} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
			MOV(PTRBITS, R(tempReg2), ImmPtr(&by32768_11));
			MULPS(fpScaleOffsetReg, MatR(tempReg2));
		}
	}

	// Add code to convert matrices to 4x4.
	// Later we might want to do this when the matrices are loaded instead.
	// Can't touch fpScaleOffsetReg (XMM0) in here!
	if (dec.skinInDecode) {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&threeMasks));
		MOVAPS(XMM5, MatR(tempReg1));
		MOV(PTRBITS, R(tempReg1), ImmPtr(&aOne));
		MOVUPS(XMM6, MatR(tempReg1));
		MOV(PTRBITS, R(tempReg1), ImmPtr(gstate.boneMatrix));
		MOV(PTRBITS, R(tempReg2), ImmPtr(bones));
		for (int i = 0; i < dec.nweights; i++) {
			MOVUPS(XMM1, MDisp(tempReg1, (12 * i) * 4));
			MOVUPS(XMM2, MDisp(tempReg1, (12 * i + 3) * 4));
			MOVUPS(XMM3, MDisp(tempReg1, (12 * i + 3 * 2) * 4));
			MOVUPS(XMM4, MDisp(tempReg1, (12 * i + 3 * 3) * 4));
			ANDPS(XMM1, R(XMM5));
			ANDPS(XMM2, R(XMM5));
			ANDPS(XMM3, R(XMM5));
			ANDPS(XMM4, R(XMM5));
			ORPS(XMM4, R(XMM6));
			MOVAPS(MDisp(tempReg2, (16 * i) * 4), XMM1);
			MOVAPS(MDisp(tempReg2, (16 * i + 4) * 4), XMM2);
			MOVAPS(MDisp(tempReg2, (16 * i + 8) * 4), XMM3);
			MOVAPS(MDisp(tempReg2, (16 * i + 12) * 4), XMM4);
		}
	}

	// Let's not bother with a proper stack frame. We just grab the arguments and go.
	JumpTarget loopStart = NopAlignCode16();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			EndWrite();
			// Reset the code ptr and return zero to indicate that we failed.
			ResetCodePtr(GetOffset(start));
			return 0;
		}
	}

	ADD(PTRBITS, R(srcReg), Imm32(dec.VertexSize()));
	ADD(PTRBITS, R(dstReg), Imm32(dec.decFmt.stride));
	SUB(32, R(counterReg), Imm8(1));
	J_CC(CC_NZ, loopStart, true);

	// Writeback alpha reg
#if PPSSPP_ARCH(AMD64)
	if (dec.col) {
		CMP(32, R(alphaReg), Imm32(1));
		FixupBranch alphaJump = J_CC(CC_E, false);
		if (RipAccessible(&gstate_c.vertexFullAlpha)) {
			MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.vertexFullAlpha));
			MOV(8, MatR(tempReg1), Imm8(0));  // rip accessible
		}
		SetJumpTarget(alphaJump);
	}
#endif

	MOVUPS(XMM4, MDisp(ESP, 0));
	MOVUPS(XMM5, MDisp(ESP, 16));
	MOVUPS(XMM6, MDisp(ESP, 32));
	MOVUPS(XMM7, MDisp(ESP, 48));
#if PPSSPP_ARCH(AMD64)
	MOVUPS(XMM8, MDisp(ESP, 64));
	MOVUPS(XMM9, MDisp(ESP, 80));
#endif
	ADD(PTRBITS, R(ESP), Imm8(STACK_FIXED_ALLOC));

#if PPSSPP_ARCH(X86)
	// Restore register values
	POP(EBP);
	POP(EBX);
	POP(EDI);
	POP(ESI);
#endif

	RET();

	*jittedSize = GetCodePtr() - start;
	EndWrite();
	return (JittedVertexDecoder)start;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	switch (dec_->nweights) {
	case 1:
		MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->weightoff));
		break;
	case 2:
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff));
		break;
	case 3:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		AND(32, R(tempReg1), Imm32(0x00FFFFFF));
		break;
	case 4:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		break;
	case 5:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOVZX(32, 8, tempReg2, MDisp(srcReg, dec_->weightoff + 4));
		break;
	case 6:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->weightoff + 4));
		break;
	case 7:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->weightoff + 4));
		AND(32, R(tempReg2), Imm32(0x00FFFFFF));
		break;
	case 8:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->weightoff + 4));
		break;
	}

	if (dec_->nweights <= 4) {
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
	} else {
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w1off), R(tempReg2));
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	switch (dec_->nweights) {
	case 1:
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), Imm32(0));
		return;

	case 2:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), Imm32(0));
		return;

	case 3:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->weightoff + 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), R(tempReg2));
		return;

	case 4:
		// Anything above 4 will do 4 here, and then the rest after.
	case 5:
	case 6:
	case 7:
	case 8:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->weightoff + 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), R(tempReg2));
		break;
	}

	// Basic implementation - a short at a time. TODO: Optimize
	int j;
	for (j = 4; j < dec_->nweights; j++) {
		MOV(16, R(tempReg1), MDisp(srcReg, dec_->weightoff + j * 2));
		MOV(16, MDisp(dstReg, dec_->decFmt.w0off + j * 2), R(tempReg1));
	}
	while (j & 3) {
		MOV(16, MDisp(dstReg, dec_->decFmt.w0off + j * 2), Imm16(0));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU8ToFloat() {
	if (dec_->nweights >= 4) {
		Jit_AnyU8ToFloat(dec_->weightoff, 32);
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		if (dec_->nweights > 4) {
			Jit_AnyU8ToFloat(dec_->weightoff + 4, (dec_->nweights - 4) * 8);
			MOVUPS(MDisp(dstReg, dec_->decFmt.w1off), XMM3);
		}
	} else {
		Jit_AnyU8ToFloat(dec_->weightoff, dec_->nweights * 8);
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
	}
}

void VertexDecoderJitCache::Jit_WeightsU16ToFloat() {
	if (dec_->nweights >= 4) {
		Jit_AnyU16ToFloat(dec_->weightoff, 64);
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		if (dec_->nweights > 4) {
			Jit_AnyU16ToFloat(dec_->weightoff + 4 * 2, (dec_->nweights - 4) * 16);
			MOVUPS(MDisp(dstReg, dec_->decFmt.w1off), XMM3);
		}
	} else {
		Jit_AnyU16ToFloat(dec_->weightoff, dec_->nweights * 16);
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	switch (dec_->nweights) {
	case 1:
		// MOVSS: When the source operand is a memory location and destination operand is an XMM register, the three high-order doublewords of the destination operand are cleared to all 0s.
		MOVSS(XMM3, MDisp(srcReg, dec_->weightoff));
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		break;

	case 2:
		MOVQ_xmm(XMM3, MDisp(srcReg, dec_->weightoff));
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		break;

	case 4:
		MOVUPS(XMM3, MDisp(srcReg, dec_->weightoff));
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		break;

	case 5:
		MOVUPS(XMM3, MDisp(srcReg, dec_->weightoff));
		MOVSS(XMM4, MDisp(srcReg, dec_->weightoff + 16));
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off + 16), XMM4);
		break;

	case 6:
		MOVUPS(XMM3, MDisp(srcReg, dec_->weightoff));
		MOVQ_xmm(XMM4, MDisp(srcReg, dec_->weightoff + 16));
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off + 16), XMM4);
		break;

	case 8:
		MOVUPS(XMM3, MDisp(srcReg, dec_->weightoff));
		MOVUPS(XMM4, MDisp(srcReg, dec_->weightoff + 16));
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off), XMM3);
		MOVUPS(MDisp(dstReg, dec_->decFmt.w0off + 16), XMM4);
		break;

	default:
		for (j = 0; j < dec_->nweights; j++) {
			MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff + j * 4));
			MOV(32, MDisp(dstReg, dec_->decFmt.w0off + j * 4), R(tempReg1));
		}
		while (j & 3) {  // Zero additional weights rounding up to 4.
			MOV(32, MDisp(dstReg, dec_->decFmt.w0off + j * 4), Imm32(0));
			j++;
		}
		break;
	}
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
	MOV(PTRBITS, R(tempReg2), ImmPtr(&bones));

#if PPSSPP_ARCH(AMD64)
	if (dec_->nweights > 4) {
		// This reads 8 bytes, we split the top 4 so we can expand each set of 4.
		MOVQ_xmm(XMM8, MDisp(srcReg, dec_->weightoff));
		PSHUFD(XMM9, R(XMM8), _MM_SHUFFLE(1, 1, 1, 1));
	} else {
		MOVD_xmm(XMM8, MDisp(srcReg, dec_->weightoff));
	}
	if (cpu_info.bSSE4_1) {
		PMOVZXBD(XMM8, R(XMM8));
	} else {
		PXOR(fpScratchReg, R(fpScratchReg));
		PUNPCKLBW(XMM8, R(fpScratchReg));
		PUNPCKLWD(XMM8, R(fpScratchReg));
	}
	if (dec_->nweights > 4) {
		if (cpu_info.bSSE4_1) {
			PMOVZXBD(XMM9, R(XMM9));
		} else {
			PUNPCKLBW(XMM9, R(fpScratchReg));
			PUNPCKLWD(XMM9, R(fpScratchReg));
		}
	}
	CVTDQ2PS(XMM8, R(XMM8));
	if (dec_->nweights > 4)
		CVTDQ2PS(XMM9, R(XMM9));

	if (RipAccessible(&by128)) {
		MULPS(XMM8, M(&by128));  // rip accessible
		if (dec_->nweights > 4)
			MULPS(XMM9, M(&by128));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by128));
		MULPS(XMM8, MatR(tempReg1));
		if (dec_->nweights > 4)
			MULPS(XMM9, MatR(tempReg1));
	}

	auto weightToAllLanes = [this](X64Reg dst, int lane) {
		X64Reg src = lane < 4 ? XMM8 : XMM9;
		if (dst != INVALID_REG && dst != src) {
			MOVAPS(dst, R(src));
		} else {
			// INVALID_REG means ruin the existing src (it's not needed any more.)
			dst = src;
		}
		SHUFPS(dst, R(dst), _MM_SHUFFLE(lane % 4, lane % 4, lane % 4, lane % 4));
	};
#endif

	for (int j = 0; j < dec_->nweights; j++) {
		X64Reg weight = XMM1;
#if PPSSPP_ARCH(AMD64)
		X64Reg weightSrc = j < 4 ? XMM8 : XMM9;
		if (j == 3 || j == dec_->nweights - 1) {
			// In the previous iteration, we already spread this value to all lanes.
			weight = weightSrc;
			if (j == 0) {
				// If there's only the one weight, no one shuffled it for us yet.
				weightToAllLanes(weight, j);
			}
			// If we're on #3, prepare #4 if it's the last (and only for that reg, in fact.)
			if (j == dec_->nweights - 2) {
				weightToAllLanes(INVALID_REG, j + 1);
			}
		} else {
			weightToAllLanes(weight, j);
			// To improve latency, we shuffle in the last weight of the reg.
			// If we're on slot #2, slot #3 will be the last.  Otherwise, nweights - 1 is last.
			if ((j == 2 && dec_->nweights > 3) || (j == dec_->nweights - 2)) {
				// Prepare the last one now for better latency.
				weightToAllLanes(INVALID_REG, j + 1);
			}
		}
#else
		MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->weightoff + j));
		CVTSI2SS(weight, R(tempReg1));
		MULSS(weight, M(&by128));  // rip accessible (x86)
		SHUFPS(weight, R(weight), _MM_SHUFFLE(0, 0, 0, 0));
#endif
		if (j == 0) {
			MOVAPS(XMM4, MDisp(tempReg2, 0));
			MOVAPS(XMM5, MDisp(tempReg2, 16));
			MOVAPS(XMM6, MDisp(tempReg2, 32));
			MOVAPS(XMM7, MDisp(tempReg2, 48));
			MULPS(XMM4, R(weight));
			MULPS(XMM5, R(weight));
			MULPS(XMM6, R(weight));
			MULPS(XMM7, R(weight));
		} else {
			MOVAPS(XMM2, MDisp(tempReg2, 0));
			MOVAPS(XMM3, MDisp(tempReg2, 16));
			MULPS(XMM2, R(weight));
			MULPS(XMM3, R(weight));
			ADDPS(XMM4, R(XMM2));
			ADDPS(XMM5, R(XMM3));
			MOVAPS(XMM2, MDisp(tempReg2, 32));
			MOVAPS(XMM3, MDisp(tempReg2, 48));
			MULPS(XMM2, R(weight));
			MULPS(XMM3, R(weight));
			ADDPS(XMM6, R(XMM2));
			ADDPS(XMM7, R(XMM3));
		}
		ADD(PTRBITS, R(tempReg2), Imm8(4 * 16));
	}
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
	MOV(PTRBITS, R(tempReg2), ImmPtr(&bones));

#if PPSSPP_ARCH(AMD64)
	if (dec_->nweights > 6) {
		// Since this is probably not aligned, two MOVQs are better than one MOVDQU.
		MOVQ_xmm(XMM8, MDisp(srcReg, dec_->weightoff));
		MOVQ_xmm(XMM9, MDisp(srcReg, dec_->weightoff + 8));
	} else if (dec_->nweights > 4) {
		// Since this is probably not aligned, two MOVQs are better than one MOVDQU.
		MOVQ_xmm(XMM8, MDisp(srcReg, dec_->weightoff));
		MOVD_xmm(XMM9, MDisp(srcReg, dec_->weightoff + 8));
	} else if (dec_->nweights > 2) {
		MOVQ_xmm(XMM8, MDisp(srcReg, dec_->weightoff));
	} else {
		MOVD_xmm(XMM8, MDisp(srcReg, dec_->weightoff));
	}
	if (cpu_info.bSSE4_1) {
		PMOVZXWD(XMM8, R(XMM8));
	} else {
		PXOR(fpScratchReg, R(fpScratchReg));
		PUNPCKLWD(XMM8, R(fpScratchReg));
	}
	if (dec_->nweights > 4) {
		if (cpu_info.bSSE4_1) {
			PMOVZXWD(XMM9, R(XMM9));
		} else {
			PUNPCKLWD(XMM9, R(fpScratchReg));
		}
	}
	CVTDQ2PS(XMM8, R(XMM8));
	if (dec_->nweights > 4)
		CVTDQ2PS(XMM9, R(XMM9));

	if (RipAccessible(&by32768)) {
		MULPS(XMM8, M(&by32768));  // rip accessible
		if (dec_->nweights > 4)
			MULPS(XMM9, M(&by32768));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by32768));
		MULPS(XMM8, MatR(tempReg1));
		if (dec_->nweights > 4)
			MULPS(XMM9, MatR(tempReg1));
	}

	auto weightToAllLanes = [this](X64Reg dst, int lane) {
		X64Reg src = lane < 4 ? XMM8 : XMM9;
		if (dst != INVALID_REG && dst != src) {
			MOVAPS(dst, R(src));
		} else {
			// INVALID_REG means ruin the existing src (it's not needed any more.)
			dst = src;
		}
		SHUFPS(dst, R(dst), _MM_SHUFFLE(lane % 4, lane % 4, lane % 4, lane % 4));
	};
#endif

	for (int j = 0; j < dec_->nweights; j++) {
		X64Reg weight = XMM1;
#if PPSSPP_ARCH(AMD64)
		X64Reg weightSrc = j < 4 ? XMM8 : XMM9;
		if (j == 3 || j == dec_->nweights - 1) {
			// In the previous iteration, we already spread this value to all lanes.
			weight = weightSrc;
			if (j == 0) {
				// If there's only the one weight, no one shuffled it for us yet.
				weightToAllLanes(weight, j);
			}
			// If we're on #3, prepare #4 if it's the last (and only for that reg, in fact.)
			if (j == dec_->nweights - 2) {
				weightToAllLanes(INVALID_REG, j + 1);
			}
		} else {
			weightToAllLanes(weight, j);
			// To improve latency, we shuffle in the last weight of the reg.
			// If we're on slot #2, slot #3 will be the last.  Otherwise, nweights - 1 is last.
			if ((j == 2 && dec_->nweights > 3) || (j == dec_->nweights - 2)) {
				// Prepare the last one now for better latency.
				weightToAllLanes(INVALID_REG, j + 1);
			}
		}
#else
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff + j * 2));
		CVTSI2SS(weight, R(tempReg1));
		MULSS(weight, M(&by32768));  // rip accessible (x86)
		SHUFPS(weight, R(weight), _MM_SHUFFLE(0, 0, 0, 0));
#endif
		if (j == 0) {
			MOVAPS(XMM4, MDisp(tempReg2, 0));
			MOVAPS(XMM5, MDisp(tempReg2, 16));
			MOVAPS(XMM6, MDisp(tempReg2, 32));
			MOVAPS(XMM7, MDisp(tempReg2, 48));
			MULPS(XMM4, R(weight));
			MULPS(XMM5, R(weight));
			MULPS(XMM6, R(weight));
			MULPS(XMM7, R(weight));
		} else {
			MOVAPS(XMM2, MDisp(tempReg2, 0));
			MOVAPS(XMM3, MDisp(tempReg2, 16));
			MULPS(XMM2, R(weight));
			MULPS(XMM3, R(weight));
			ADDPS(XMM4, R(XMM2));
			ADDPS(XMM5, R(XMM3));
			MOVAPS(XMM2, MDisp(tempReg2, 32));
			MOVAPS(XMM3, MDisp(tempReg2, 48));
			MULPS(XMM2, R(weight));
			MULPS(XMM3, R(weight));
			ADDPS(XMM6, R(XMM2));
			ADDPS(XMM7, R(XMM3));
		}
		ADD(PTRBITS, R(tempReg2), Imm8(4 * 16));
	}
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
	MOV(PTRBITS, R(tempReg2), ImmPtr(&bones));
	for (int j = 0; j < dec_->nweights; j++) {
		MOVSS(XMM1, MDisp(srcReg, dec_->weightoff + j * 4));
		SHUFPS(XMM1, R(XMM1), _MM_SHUFFLE(0, 0, 0, 0));
		if (j == 0) {
			MOVAPS(XMM4, MDisp(tempReg2, 0));
			MOVAPS(XMM5, MDisp(tempReg2, 16));
			MOVAPS(XMM6, MDisp(tempReg2, 32));
			MOVAPS(XMM7, MDisp(tempReg2, 48));
			MULPS(XMM4, R(XMM1));
			MULPS(XMM5, R(XMM1));
			MULPS(XMM6, R(XMM1));
			MULPS(XMM7, R(XMM1));
		} else {
			MOVAPS(XMM2, MDisp(tempReg2, 0));
			MOVAPS(XMM3, MDisp(tempReg2, 16));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM4, R(XMM2));
			ADDPS(XMM5, R(XMM3));
			MOVAPS(XMM2, MDisp(tempReg2, 32));
			MOVAPS(XMM3, MDisp(tempReg2, 48));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM6, R(XMM2));
			ADDPS(XMM7, R(XMM3));
		}
		ADD(PTRBITS, R(tempReg2), Imm8(4 * 16));
	}
}

void VertexDecoderJitCache::Jit_TcU8ToFloat() {
	Jit_AnyU8ToFloat(dec_->tcoff, 16);
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), XMM3);
}

void VertexDecoderJitCache::Jit_TcU16ToFloat() {
	Jit_AnyU16ToFloat(dec_->tcoff, 32);
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), XMM3);
}

void VertexDecoderJitCache::Jit_TcFloat() {
#if PPSSPP_ARCH(AMD64)
	MOV(64, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(64, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
#else
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->tcoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff + 4), R(tempReg2));
#endif
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
	// TODO: The first five instructions could be done in 1 or 2 in SSE4
	MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOVZX(32, 8, tempReg2, MDisp(srcReg, dec_->tcoff + 1));
	CVTSI2SS(fpScratchReg, R(tempReg1));
	CVTSI2SS(fpScratchReg2, R(tempReg2));
	UNPCKLPS(fpScratchReg, R(fpScratchReg2));
	// TODO: These are a lot of nasty consecutive dependencies. Can probably be made faster
	// if we can spare another register to avoid the shuffle, like on ARM.
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
	PXOR(fpScratchReg2, R(fpScratchReg2));
	MOVD_xmm(fpScratchReg, MDisp(srcReg, dec_->tcoff));
	PUNPCKLWD(fpScratchReg, R(fpScratchReg2));
	CVTDQ2PS(fpScratchReg, R(fpScratchReg));
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
	MOVQ_xmm(fpScratchReg, MDisp(srcReg, dec_->tcoff));
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcAnyMorph(int bits) {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	if (!cpu_info.bSSE4_1) {
		PXOR(fpScratchReg4, R(fpScratchReg4));
	}

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		const OpArg src = MDisp(srcReg, dec_->onesize_ * n + dec_->tcoff);

		// Load the actual values and convert to float.
		if (bits == 32) {
			// Two floats: just load as a MOVQ.
			MOVQ_xmm(reg, src);
		} else {
			if (bits == 8) {
				MOVZX(32, 16, tempReg2, src);
				MOVD_xmm(reg, R(tempReg2));
			} else {
				MOVD_xmm(reg, src);
			}
			if (cpu_info.bSSE4_1) {
				if (bits == 8) {
					PMOVZXBD(reg, R(reg));
				} else {
					PMOVZXWD(reg, R(reg));
				}
			} else {
				if (bits == 8) {
					PUNPCKLBW(reg, R(fpScratchReg4));
				}
				PUNPCKLWD(reg, R(fpScratchReg4));
			}

			CVTDQ2PS(reg, R(reg));
		}

		// And now scale by the weight.
		MOVSS(fpScratchReg3, MDisp(tempReg1, n * sizeof(float)));
		SHUFPS(fpScratchReg3, R(fpScratchReg3), _MM_SHUFFLE(0, 0, 0, 0));
		MULPS(reg, R(fpScratchReg3));

		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg2));
		} else {
			first = false;
		}
	}
}

void VertexDecoderJitCache::Jit_TcU8MorphToFloat() {
	Jit_TcAnyMorph(8);
	// They were all added (weighted) pre-normalize, we normalize once here.
	if (RipAccessible(&by128)) {
		MULPS(fpScratchReg, M(&by128));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by128));
		MULPS(fpScratchReg, MatR(tempReg1));
	}
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcU16MorphToFloat() {
	Jit_TcAnyMorph(16);
	// They were all added (weighted) pre-normalize, we normalize once here.
	if (RipAccessible(&by32768)) {
		MULPS(fpScratchReg, M(&by32768));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by32768));
		MULPS(fpScratchReg, MatR(tempReg1));
	}
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcFloatMorph() {
	Jit_TcAnyMorph(32);
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcU8PrescaleMorph() {
	Jit_TcAnyMorph(8);
	// The scale takes into account the u8 normalization.
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcU16PrescaleMorph() {
	Jit_TcAnyMorph(16);
	// The scale takes into account the u16 normalization.
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcFloatPrescaleMorph() {
	Jit_TcAnyMorph(32);
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcU16ThroughToFloat() {
	PXOR(fpScratchReg2, R(fpScratchReg2));
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOVD_xmm(fpScratchReg, R(tempReg1));
	PUNPCKLWD(fpScratchReg, R(fpScratchReg2));
	CVTDQ2PS(fpScratchReg, R(fpScratchReg));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);

	MOV(32, R(tempReg2), R(tempReg1));
	SHR(32, R(tempReg2), Imm8(16));

	MOV(PTRBITS, R(tempReg3), ImmPtr(&gstate_c.vertBounds));
	auto updateSide = [&](X64Reg r, CCFlags skipCC, int offset) {
		CMP(16, R(r), MDisp(tempReg3, offset));
		FixupBranch skip = J_CC(skipCC);
		MOV(16, MDisp(tempReg3, offset), R(r));
		SetJumpTarget(skip);
	};
	// TODO: Can this actually be fast?  Hmm, floats aren't better.
	updateSide(tempReg1, CC_GE, offsetof(KnownVertexBounds, minU));
	updateSide(tempReg1, CC_LE, offsetof(KnownVertexBounds, maxU));
	updateSide(tempReg2, CC_GE, offsetof(KnownVertexBounds, minV));
	updateSide(tempReg2, CC_LE, offsetof(KnownVertexBounds, maxV));
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
#if PPSSPP_ARCH(AMD64)
	MOV(64, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(64, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
#else
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->tcoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff + 4), R(tempReg2));
#endif
}

void VertexDecoderJitCache::Jit_Color8888() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->coloff));
	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg1));

	CMP(32, R(tempReg1), Imm32(0xFF000000));
	FixupBranch skip = J_CC(CC_AE, false);
#if PPSSPP_ARCH(AMD64)
	// Would like to use CMOV or SetCC but CMOV doesn't take immediates and SetCC isn't right. So...
	XOR(32, R(alphaReg), R(alphaReg));
#else
	if (RipAccessible(&gstate_c.vertexFullAlpha)) {
		MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.vertexFullAlpha));
		MOV(8, MatR(tempReg1), Imm8(0));
	}
#endif
	SetJumpTarget(skip);
}

alignas(16) static const u32 color4444mask[4] = { 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, };

void VertexDecoderJitCache::Jit_Color4444() {
	// This over-reads slightly, but we assume pos or another component follows anyway.
	MOVD_xmm(fpScratchReg, MDisp(srcReg, dec_->coloff));
	// Spread to RGBA -> R00GB00A.
	PUNPCKLBW(fpScratchReg, R(fpScratchReg));
	if (RipAccessible(&color4444mask[0])) {
		PAND(fpScratchReg, M(&color4444mask[0]));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&color4444mask));
		PAND(fpScratchReg, MatR(tempReg1));
	}
	MOVSS(fpScratchReg2, R(fpScratchReg));
	MOVSS(fpScratchReg3, R(fpScratchReg));
	// Create 0R000B00 and 00G000A0.
	PSRLW(fpScratchReg2, 4);
	PSLLW(fpScratchReg3, 4);
	// Combine for the complete set: RRGGBBAA.
	POR(fpScratchReg, R(fpScratchReg2));
	POR(fpScratchReg, R(fpScratchReg3));
	MOVD_xmm(R(tempReg1), fpScratchReg);
	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg1));

	CMP(32, R(tempReg1), Imm32(0xFF000000));
	FixupBranch skip = J_CC(CC_AE, false);
#if PPSSPP_ARCH(AMD64)
	XOR(32, R(alphaReg), R(alphaReg));
#else
	if (RipAccessible(&gstate_c.vertexFullAlpha)) {
		MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.vertexFullAlpha));
		MOV(8, MatR(tempReg1), Imm8(0));
	}
#endif
	SetJumpTarget(skip);
}

void VertexDecoderJitCache::Jit_Color565() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->coloff));

	MOV(32, R(tempReg2), R(tempReg1));
	AND(32, R(tempReg2), Imm32(0x0000001F));

	// B (we do R and B at the same time, they're both 5.)
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x0000F800));
	SHL(32, R(tempReg3), Imm8(5));
	OR(32, R(tempReg2), R(tempReg3));

	// Expand 5 -> 8.  At this point we have 00BB00RR.
	MOV(32, R(tempReg3), R(tempReg2));
	SHL(32, R(tempReg2), Imm8(3));
	SHR(32, R(tempReg3), Imm8(2));
	OR(32, R(tempReg2), R(tempReg3));
	AND(32, R(tempReg2), Imm32(0x00FF00FF));

	// Now's as good a time to put in A as any.
	OR(32, R(tempReg2), Imm32(0xFF000000));

	// Last, we need to align, extract, and expand G.
	// 3 to align to G, and then 2 to expand to 8.
	SHL(32, R(tempReg1), Imm8(3 + 2));
	AND(32, R(tempReg1), Imm32(0x0000FC00));
	MOV(32, R(tempReg3), R(tempReg1));
	// 2 to account for tempReg1 being preshifted, 4 for expansion.
	SHR(32, R(tempReg3), Imm8(2 + 4));
	OR(32, R(tempReg1), R(tempReg3));
	AND(32, R(tempReg1), Imm32(0x0000FF00));
	OR(32, R(tempReg2), R(tempReg1));

	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg2));
	// Never has alpha, no need to update fullAlphaArg.
}

void VertexDecoderJitCache::Jit_Color5551() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->coloff));

	MOV(32, R(tempReg2), R(tempReg1));
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg2), Imm32(0x0000001F));
	AND(32, R(tempReg3), Imm32(0x000003E0));
	SHL(32, R(tempReg3), Imm8(3));
	OR(32, R(tempReg2), R(tempReg3));

	// This mask intentionally keeps alpha, but it's all the same bit so we can keep it.
	AND(32, R(tempReg1), Imm32(0x0000FC00));
	SHL(32, R(tempReg1), Imm8(6));
	// At this point, 0x003F1F1F are potentially set in tempReg2.
	OR(32, R(tempReg2), R(tempReg1));

	// Expand 5 -> 8.  After this is just expanding A.
	MOV(32, R(tempReg3), R(tempReg2));
	SHL(32, R(tempReg2), Imm8(3));
	SHR(32, R(tempReg3), Imm8(2));
	// Chop off the bits that were shifted out (and also alpha.)
	AND(32, R(tempReg3), Imm32(0x00070707));
	OR(32, R(tempReg2), R(tempReg3));

	// For A, use sign extend to repeat the bit.  B is still here, but it's the same bits.
	SHL(32, R(tempReg1), Imm8(10));
	SAR(32, R(tempReg1), Imm8(7));
	OR(32, R(tempReg2), R(tempReg1));

	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg2));

	// Let's AND to avoid a branch, tempReg1 has alpha only in the top 8 bits, and they're all equal.
	SHR(32, R(tempReg1), Imm8(24));
#if PPSSPP_ARCH(AMD64)
	AND(8, R(alphaReg), R(tempReg1));
#else
	if (RipAccessible(&gstate_c.vertexFullAlpha)) {
		AND(8, M(&gstate_c.vertexFullAlpha), R(tempReg1));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg3), ImmPtr(&gstate_c.vertexFullAlpha));
		AND(8, MatR(tempReg3), R(tempReg1));
	}
#endif
}

void VertexDecoderJitCache::Jit_Color8888Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	if (!cpu_info.bSSE4_1) {
		PXOR(fpScratchReg4, R(fpScratchReg4));
	}

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		MOVD_xmm(reg, MDisp(srcReg, dec_->onesize_ * n + dec_->coloff));
		if (cpu_info.bSSE4_1) {
			PMOVZXBD(reg, R(reg));
		} else {
			PUNPCKLBW(reg, R(fpScratchReg4));
			PUNPCKLWD(reg, R(fpScratchReg4));
		}

		CVTDQ2PS(reg, R(reg));

		// And now the weight.
		MOVSS(fpScratchReg3, MDisp(tempReg1, n * sizeof(float)));
		SHUFPS(fpScratchReg3, R(fpScratchReg3), _MM_SHUFFLE(0, 0, 0, 0));
		MULPS(reg, R(fpScratchReg3));

		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg2));
		} else {
			first = false;
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

alignas(16) static const float byColor4444[4] = { 255.0f / 15.0f, 255.0f / 15.0f, 255.0f / 15.0f, 255.0f / 15.0f, };

void VertexDecoderJitCache::Jit_Color4444Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	if (!cpu_info.bSSE4_1) {
		PXOR(fpScratchReg4, R(fpScratchReg4));
	}
	MOV(PTRBITS, R(tempReg2), ImmPtr(color4444mask));
	MOVDQA(XMM5, MatR(tempReg2));
	MOV(PTRBITS, R(tempReg2), ImmPtr(byColor4444));
	MOVAPS(XMM6, MatR(tempReg2));

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		MOVD_xmm(reg, MDisp(srcReg, dec_->onesize_ * n + dec_->coloff));
		PUNPCKLBW(reg, R(reg));
		PAND(reg, R(XMM5));
		MOVSS(fpScratchReg3, R(reg));
		PSLLW(fpScratchReg3, 4);
		POR(reg, R(fpScratchReg3));
		PSRLW(reg, 4);

		if (cpu_info.bSSE4_1) {
			PMOVZXBD(reg, R(reg));
		} else {
			PUNPCKLBW(reg, R(fpScratchReg4));
			PUNPCKLWD(reg, R(fpScratchReg4));
		}

		CVTDQ2PS(reg, R(reg));
		MULPS(reg, R(XMM6));

		// And now the weight.
		MOVSS(fpScratchReg3, MDisp(tempReg1, n * sizeof(float)));
		SHUFPS(fpScratchReg3, R(fpScratchReg3), _MM_SHUFFLE(0, 0, 0, 0));
		MULPS(reg, R(fpScratchReg3));

		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg2));
		} else {
			first = false;
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

// The mask is intentionally in reverse order (but skips A.)
alignas(16) static const u32 color565Mask[4] = { 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000, };
alignas(16) static const float byColor565[4] = { 255.0f / 31.0f, 255.0f / 63.0f, 255.0f / 31.0f, 255.0f / 1.0f, };

void VertexDecoderJitCache::Jit_Color565Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	MOV(PTRBITS, R(tempReg2), ImmPtr(color565Mask));
	MOVDQA(XMM5, MatR(tempReg2));
	MOV(PTRBITS, R(tempReg2), ImmPtr(byColor565));
	MOVAPS(XMM6, MatR(tempReg2));

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg3;
		MOVD_xmm(fpScratchReg2, MDisp(srcReg, dec_->onesize_ * n + dec_->coloff));
		// Spread it out into each lane.  We end up with it reversed (R high, A low.)
		// Below, we shift out each lane from low to high and reverse them.
		PSHUFD(fpScratchReg2, R(fpScratchReg2), _MM_SHUFFLE(0, 0, 0, 0));
		PAND(fpScratchReg2, R(XMM5));

		// Alpha handled in Jit_WriteMorphColor.

		// Blue first.
		MOVSS(reg, R(fpScratchReg2));
		PSRLD(reg, 6);
		PSHUFD(reg, R(reg), _MM_SHUFFLE(3, 0, 0, 0));

		// Green, let's shift it into the right lane first.
		PSRLDQ(fpScratchReg2, 4);
		MOVSS(reg, R(fpScratchReg2));
		PSRLD(reg, 5);
		PSHUFD(reg, R(reg), _MM_SHUFFLE(3, 2, 0, 0));

		// Last one, red.
		PSRLDQ(fpScratchReg2, 4);
		MOVSS(reg, R(fpScratchReg2));

		CVTDQ2PS(reg, R(reg));
		MULPS(reg, R(XMM6));

		// And now the weight.
		MOVSS(fpScratchReg2, MDisp(tempReg1, n * sizeof(float)));
		SHUFPS(fpScratchReg2, R(fpScratchReg2), _MM_SHUFFLE(0, 0, 0, 0));
		MULPS(reg, R(fpScratchReg2));

		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg3));
		} else {
			first = false;
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off, false);
}

// The mask is intentionally in reverse order.
alignas(16) static const u32 color5551Mask[4] = { 0x00008000, 0x00007c00, 0x000003e0, 0x0000001f, };
alignas(16) static const float byColor5551[4] = { 255.0f / 31.0f, 255.0f / 31.0f, 255.0f / 31.0f, 255.0f / 1.0f, };

void VertexDecoderJitCache::Jit_Color5551Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	MOV(PTRBITS, R(tempReg2), ImmPtr(color5551Mask));
	MOVDQA(XMM5, MatR(tempReg2));
	MOV(PTRBITS, R(tempReg2), ImmPtr(byColor5551));
	MOVAPS(XMM6, MatR(tempReg2));

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg3;
		MOVD_xmm(fpScratchReg2, MDisp(srcReg, dec_->onesize_ * n + dec_->coloff));
		// Spread it out into each lane.
		PSHUFD(fpScratchReg2, R(fpScratchReg2), _MM_SHUFFLE(0, 0, 0, 0));
		PAND(fpScratchReg2, R(XMM5));

		// Alpha first.
		MOVSS(reg, R(fpScratchReg2));
		PSRLD(reg, 5);
		PSHUFD(reg, R(reg), _MM_SHUFFLE(0, 0, 0, 0));

		// Blue, let's shift it into the right lane first.
		PSRLDQ(fpScratchReg2, 4);
		MOVSS(reg, R(fpScratchReg2));
		PSRLD(reg, 5);
		PSHUFD(reg, R(reg), _MM_SHUFFLE(3, 0, 0, 0));

		// Green.
		PSRLDQ(fpScratchReg2, 4);
		MOVSS(reg, R(fpScratchReg2));
		PSRLD(reg, 5);
		PSHUFD(reg, R(reg), _MM_SHUFFLE(3, 2, 0, 0));

		// Last one, red.
		PSRLDQ(fpScratchReg2, 4);
		MOVSS(reg, R(fpScratchReg2));

		CVTDQ2PS(reg, R(reg));
		MULPS(reg, R(XMM6));

		// And now the weight.
		MOVSS(fpScratchReg2, MDisp(tempReg1, n * sizeof(float)));
		SHUFPS(fpScratchReg2, R(fpScratchReg2), _MM_SHUFFLE(0, 0, 0, 0));
		MULPS(reg, R(fpScratchReg2));

		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg3));
		} else {
			first = false;
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_WriteMorphColor(int outOff, bool checkAlpha) {
	// Pack back into a u32, with saturation.
	CVTPS2DQ(fpScratchReg, R(fpScratchReg));
	PACKSSDW(fpScratchReg, R(fpScratchReg));
	PACKUSWB(fpScratchReg, R(fpScratchReg));
	MOVD_xmm(R(tempReg1), fpScratchReg);

	// TODO: May be a faster way to do this without the MOVD.
	if (checkAlpha) {
		CMP(32, R(tempReg1), Imm32(0xFF000000));
		FixupBranch skip = J_CC(CC_AE, false);
#if PPSSPP_ARCH(AMD64)
		XOR(32, R(alphaReg), R(alphaReg));
#else
		if (RipAccessible(&gstate_c.vertexFullAlpha)) {
			MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));  // rip accessible
		} else {
			MOV(PTRBITS, R(tempReg2), ImmPtr(&gstate_c.vertexFullAlpha));
			MOV(8, MatR(tempReg2), Imm8(0));
		}
#endif
		SetJumpTarget(skip);
	} else {
		// Force alpha to full if we're not checking it.
		OR(32, R(tempReg1), Imm32(0xFF000000));
	}

	MOV(32, MDisp(dstReg, outOff), R(tempReg1));
}

// Copy 3 bytes and then a zero. Might as well copy four.
void VertexDecoderJitCache::Jit_NormalS8() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
	AND(32, R(tempReg1), Imm32(0x00FFFFFF));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_NormalS8ToFloat() {
	Jit_AnyS8ToFloat(dec_->nrmoff);
	MOVUPS(MDisp(dstReg, dec_->decFmt.nrmoff), XMM3);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->nrmoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 4), R(tempReg2));
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	if (cpu_info.Mode64bit) {
		MOV(64, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
		MOV(32, R(tempReg3), MDisp(srcReg, dec_->nrmoff + 8));
		MOV(64, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 8), R(tempReg3));
	} else {
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->nrmoff + 4));
		MOV(32, R(tempReg3), MDisp(srcReg, dec_->nrmoff + 8));
		MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 4), R(tempReg2));
		MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 8), R(tempReg3));
	}
}

// This could be a bit shorter with AVX 3-operand instructions and FMA.
void VertexDecoderJitCache::Jit_WriteMatrixMul(int outOff, bool pos) {
	MOVAPS(XMM1, R(XMM3));
	MOVAPS(XMM2, R(XMM3));
	SHUFPS(XMM1, R(XMM1), _MM_SHUFFLE(0, 0, 0, 0));
	SHUFPS(XMM2, R(XMM2), _MM_SHUFFLE(1, 1, 1, 1));
	SHUFPS(XMM3, R(XMM3), _MM_SHUFFLE(2, 2, 2, 2));
	MULPS(XMM1, R(XMM4));
	MULPS(XMM2, R(XMM5));
	MULPS(XMM3, R(XMM6));
	ADDPS(XMM1, R(XMM2));
	ADDPS(XMM1, R(XMM3));
	if (pos) {
		ADDPS(XMM1, R(XMM7));
	}
	MOVUPS(MDisp(dstReg, outOff), XMM1);
}

void VertexDecoderJitCache::Jit_NormalS8Skin() {
	Jit_AnyS8ToFloat(dec_->nrmoff);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalS16Skin() {
	Jit_AnyS16ToFloat(dec_->nrmoff);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalFloatSkin() {
	MOVUPS(XMM3, MDisp(srcReg, dec_->nrmoff));
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS8Through() {
	// SIMD doesn't really matter since this isn't useful on hardware.
	XORPS(fpScratchReg, R(fpScratchReg));
	for (int i = 0; i < 3; i++) {
		MOVSS(MDisp(dstReg, dec_->decFmt.posoff + i * 4), fpScratchReg);
	}
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS16Through() {
	if (cpu_info.bSSE4_1) {
		MOVD_xmm(fpScratchReg, MDisp(srcReg, dec_->posoff));
		MOVZX(32, 16, tempReg3, MDisp(srcReg, dec_->posoff + 4));
		MOVD_xmm(fpScratchReg2, R(tempReg3));
		PMOVSXWD(fpScratchReg, R(fpScratchReg));
		PUNPCKLQDQ(fpScratchReg, R(fpScratchReg2));
		CVTDQ2PS(fpScratchReg, R(fpScratchReg));
		MOVUPS(MDisp(dstReg, dec_->decFmt.posoff), fpScratchReg);
	} else {
		MOVSX(32, 16, tempReg1, MDisp(srcReg, dec_->posoff));
		MOVSX(32, 16, tempReg2, MDisp(srcReg, dec_->posoff + 2));
		MOVZX(32, 16, tempReg3, MDisp(srcReg, dec_->posoff + 4));  // NOTE: MOVZX
		CVTSI2SS(fpScratchReg, R(tempReg1));
		MOVSS(MDisp(dstReg, dec_->decFmt.posoff), fpScratchReg);
		CVTSI2SS(fpScratchReg, R(tempReg2));
		MOVSS(MDisp(dstReg, dec_->decFmt.posoff + 4), fpScratchReg);
		CVTSI2SS(fpScratchReg, R(tempReg3));
		MOVSS(MDisp(dstReg, dec_->decFmt.posoff + 8), fpScratchReg);
	}
}

void VertexDecoderJitCache::Jit_PosFloatThrough() {
	PXOR(fpScratchReg2, R(fpScratchReg2));
	if (cpu_info.Mode64bit) {
		MOV(64, R(tempReg1), MDisp(srcReg, dec_->posoff));
		MOVSS(fpScratchReg, MDisp(srcReg, dec_->posoff + 8));
		MOV(64, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
	} else {
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->posoff + 4));
		MOVSS(fpScratchReg, MDisp(srcReg, dec_->posoff + 8));
		MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 4), R(tempReg2));
	}

	CVTTPS2DQ(fpScratchReg, R(fpScratchReg));
	// Use pack to saturate to 0,65535.
	if (cpu_info.bSSE4_1) {
		PACKUSDW(fpScratchReg, R(fpScratchReg));
	} else {
		PSLLD(fpScratchReg, 16);
		PSRAD(fpScratchReg, 16);
		PACKSSDW(fpScratchReg, R(fpScratchReg));
	}
	PUNPCKLWD(fpScratchReg, R(fpScratchReg2));
	CVTDQ2PS(fpScratchReg, R(fpScratchReg));

	MOVSS(MDisp(dstReg, dec_->decFmt.posoff + 8), fpScratchReg);
}

void VertexDecoderJitCache::Jit_PosS8() {
	Jit_AnyS8ToFloat(dec_->posoff);
	MOVUPS(MDisp(dstReg, dec_->decFmt.posoff), XMM3);
}

void VertexDecoderJitCache::Jit_PosS16() {
	Jit_AnyS16ToFloat(dec_->posoff);
	MOVUPS(MDisp(dstReg, dec_->decFmt.posoff), XMM3);
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
	if (cpu_info.Mode64bit) {
		MOV(64, R(tempReg1), MDisp(srcReg, dec_->posoff));
		MOV(32, R(tempReg3), MDisp(srcReg, dec_->posoff + 8));
		MOV(64, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 8), R(tempReg3));
	} else {
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->posoff + 4));
		MOV(32, R(tempReg3), MDisp(srcReg, dec_->posoff + 8));
		MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 4), R(tempReg2));
		MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 8), R(tempReg3));
	}
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
	Jit_AnyS8ToFloat(dec_->posoff);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS16Skin() {
	Jit_AnyS16ToFloat(dec_->posoff);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosFloatSkin() {
	MOVUPS(XMM3, MDisp(srcReg, dec_->posoff));
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_AnyS8ToFloat(int srcoff) {
	MOVD_xmm(XMM1, MDisp(srcReg, srcoff));
	if (cpu_info.bSSE4_1) {
		PMOVSXBD(XMM1, R(XMM1));
	} else {
		PUNPCKLBW(XMM1, R(XMM1));
		PUNPCKLWD(XMM1, R(XMM1));
		PSRAD(XMM1, 24);
	}
	CVTDQ2PS(XMM3, R(XMM1));
	if (RipAccessible(&by128)) {
		MULPS(XMM3, M(&by128));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by128));
		MULPS(XMM3, MatR(tempReg1));
	}
}

void VertexDecoderJitCache::Jit_AnyS16ToFloat(int srcoff) {
	MOVQ_xmm(XMM1, MDisp(srcReg, srcoff));
	if (cpu_info.bSSE4_1) {
		PMOVSXWD(XMM1, R(XMM1));
	} else {
		PUNPCKLWD(XMM1, R(XMM1));
		PSRAD(XMM1, 16);
	}
	CVTDQ2PS(XMM3, R(XMM1));
	if (RipAccessible(&by32768)) {
		MULPS(XMM3, M(&by32768));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by32768));
		MULPS(XMM3, MatR(tempReg1));
	}
}

void VertexDecoderJitCache::Jit_AnyU8ToFloat(int srcoff, u32 bits) {
	_dbg_assert_msg_((bits & ~(32 | 16 | 8)) == 0, "Bits must be a multiple of 8.");
	_dbg_assert_msg_(bits >= 8 && bits <= 32, "Bits must be a between 8 and 32.");

	if (!cpu_info.bSSE4_1) {
		PXOR(XMM3, R(XMM3));
	}
	if (bits == 32) {
		MOVD_xmm(XMM1, MDisp(srcReg, srcoff));
	} else if (bits == 24) {
		MOV(32, R(tempReg1), MDisp(srcReg, srcoff));
		AND(32, R(tempReg1), Imm32(0x00FFFFFF));
		MOVD_xmm(XMM1, R(tempReg1));
	} else {
		MOVZX(32, bits, tempReg1, MDisp(srcReg, srcoff));
		MOVD_xmm(XMM1, R(tempReg1));
	}
	if (cpu_info.bSSE4_1) {
		PMOVZXBD(XMM1, R(XMM1));
	} else {
		PUNPCKLBW(XMM1, R(XMM3));
		PUNPCKLWD(XMM1, R(XMM3));
	}
	CVTDQ2PS(XMM3, R(XMM1));
	if (RipAccessible(&by128)) {
		MULPS(XMM3, M(&by128));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by128));
		MULPS(XMM3, MatR(tempReg1));
	}
}

void VertexDecoderJitCache::Jit_AnyU16ToFloat(int srcoff, u32 bits) {
	_dbg_assert_msg_((bits & ~(64 | 32 | 16)) == 0, "Bits must be a multiple of 16.");
	_dbg_assert_msg_(bits >= 16 && bits <= 64, "Bits must be a between 16 and 64.");

	if (!cpu_info.bSSE4_1) {
		PXOR(XMM3, R(XMM3));
	}
	if (bits == 64) {
		MOVQ_xmm(XMM1, MDisp(srcReg, srcoff));
	} else if (bits == 48) {
		MOVD_xmm(XMM1, MDisp(srcReg, srcoff));
		PINSRW(XMM1, MDisp(srcReg, srcoff + 4), 2);
	} else if (bits == 32) {
		MOVD_xmm(XMM1, MDisp(srcReg, srcoff));
	} else if (bits == 16) {
		MOVZX(32, bits, tempReg1, MDisp(srcReg, srcoff));
		MOVD_xmm(XMM1, R(tempReg1));
	}
	if (cpu_info.bSSE4_1) {
		PMOVZXWD(XMM1, R(XMM1));
	} else {
		PUNPCKLWD(XMM1, R(XMM3));
	}
	CVTDQ2PS(XMM3, R(XMM1));
	if (RipAccessible(&by32768)) {
		MULPS(XMM3, M(&by32768));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by32768));
		MULPS(XMM3, MatR(tempReg1));
	}
}

void VertexDecoderJitCache::Jit_AnyS8Morph(int srcoff, int dstoff) {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	if (RipAccessible(&by128)) {
		MOVAPS(XMM5, M(&by128));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by128));
		MOVAPS(XMM5, MatR(tempReg1));
	}

	// Sum into fpScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		// Okay, first convert to floats.
		MOVD_xmm(reg, MDisp(srcReg, dec_->onesize_ * n + srcoff));
		if (cpu_info.bSSE4_1) {
			PMOVSXBD(reg, R(reg));
		} else {
			PUNPCKLBW(reg, R(reg));
			PUNPCKLWD(reg, R(reg));
			PSRAD(reg, 24);
		}
		CVTDQ2PS(reg, R(reg));

		// Now, It's time to multiply by the weight and 1.0f/128.0f.
		MOVSS(fpScratchReg3, MDisp(tempReg1, sizeof(float) * n));
		MULSS(fpScratchReg3, R(XMM5));
		SHUFPS(fpScratchReg3, R(fpScratchReg3), _MM_SHUFFLE(0, 0, 0, 0));

		MULPS(reg, R(fpScratchReg3));
		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg2));
		} else {
			first = false;
		}
	}

	MOVUPS(MDisp(dstReg, dstoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_AnyS16Morph(int srcoff, int dstoff) {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	if (RipAccessible(&by32768)) {
		MOVAPS(XMM5, M(&by32768));  // rip accessible
	} else {
		MOV(PTRBITS, R(tempReg1), ImmPtr(&by32768));
		MOVAPS(XMM5, MatR(tempReg1));
	}

	// Sum into fpScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		// Okay, first convert to floats.
		MOVQ_xmm(reg, MDisp(srcReg, dec_->onesize_ * n + srcoff));
		if (cpu_info.bSSE4_1) {
			PMOVSXWD(reg, R(reg));
		} else {
			PUNPCKLWD(reg, R(reg));
			PSRAD(reg, 16);
		}
		CVTDQ2PS(reg, R(reg));

		// Now, It's time to multiply by the weight and 1.0f/32768.0f.
		MOVSS(fpScratchReg3, MDisp(tempReg1, sizeof(float) * n));
		MULSS(fpScratchReg3, R(XMM5));
		SHUFPS(fpScratchReg3, R(fpScratchReg3), _MM_SHUFFLE(0, 0, 0, 0));

		MULPS(reg, R(fpScratchReg3));
		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg2));
		} else {
			first = false;
		}
	}

	MOVUPS(MDisp(dstReg, dstoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_AnyFloatMorph(int srcoff, int dstoff) {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));

	// Sum into fpScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		MOVUPS(reg, MDisp(srcReg, dec_->onesize_ * n + srcoff));
		MOVSS(fpScratchReg3, MDisp(tempReg1, sizeof(float) * n));
		SHUFPS(fpScratchReg3, R(fpScratchReg3), _MM_SHUFFLE(0, 0, 0, 0));
		MULPS(reg, R(fpScratchReg3));
		if (!first) {
			ADDPS(fpScratchReg, R(fpScratchReg2));
		} else {
			first = false;
		}
	}

	MOVUPS(MDisp(dstReg, dstoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_PosS8Morph() {
	Jit_AnyS8Morph(dec_->posoff, dec_->decFmt.posoff);
}

void VertexDecoderJitCache::Jit_PosS16Morph() {
	Jit_AnyS16Morph(dec_->posoff, dec_->decFmt.posoff);
}

void VertexDecoderJitCache::Jit_PosFloatMorph() {
	Jit_AnyFloatMorph(dec_->posoff, dec_->decFmt.posoff);
}

void VertexDecoderJitCache::Jit_NormalS8Morph() {
	Jit_AnyS8Morph(dec_->nrmoff, dec_->decFmt.nrmoff);
}

void VertexDecoderJitCache::Jit_NormalS16Morph() {
	Jit_AnyS16Morph(dec_->nrmoff, dec_->decFmt.nrmoff);
}

void VertexDecoderJitCache::Jit_NormalFloatMorph() {
	Jit_AnyFloatMorph(dec_->nrmoff, dec_->decFmt.nrmoff);
}

bool VertexDecoderJitCache::CompileStep(const VertexDecoder &dec, int step) {
	// See if we find a matching JIT function
	for (size_t i = 0; i < ARRAY_SIZE(jitLookup); i++) {
		if (dec.steps_[step] == jitLookup[i].func) {
			((*this).*jitLookup[i].jitFunc)();
			return true;
		}
	}
	return false;
}

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
