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

#include <emmintrin.h>

#include "Common/CPUDetect.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/VertexDecoder.h"

// We start out by converting the active matrices into 4x4 which are easier to multiply with
// using SSE / NEON and store them here.
static float MEMORY_ALIGNED16(bones[16 * 8]);

using namespace Gen;

static const float MEMORY_ALIGNED16( by127[4] ) = {
	1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f
};
static const float MEMORY_ALIGNED16( by128[4] ) = {
	1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 128.0f
};
static const float MEMORY_ALIGNED16( by256[4] ) = {
	1.0f / 256, 1.0f / 256, 1.0f / 256, 1.0f / 256
};
static const float MEMORY_ALIGNED16( by32767[4] ) = {
	1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f,
};
static const float MEMORY_ALIGNED16( by32768[4] ) = {
	1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f,
};

static const u32 MEMORY_ALIGNED16( threeMasks[4] ) = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0};
static const u32 MEMORY_ALIGNED16( aOne[4] ) = {0, 0, 0, 0x3F800000};

#ifdef _M_X64
#ifdef _WIN32
static const X64Reg tempReg1 = RAX;
static const X64Reg tempReg2 = R9;
static const X64Reg tempReg3 = R10;
static const X64Reg srcReg = RCX;
static const X64Reg dstReg = RDX;
static const X64Reg counterReg = R8;
#else
static const X64Reg tempReg1 = RAX;
static const X64Reg tempReg2 = R9;
static const X64Reg tempReg3 = R10;
static const X64Reg srcReg = RDI;
static const X64Reg dstReg = RSI;
static const X64Reg counterReg = RDX;
#endif
#else
static const X64Reg tempReg1 = EAX;
static const X64Reg tempReg2 = EBX;
static const X64Reg tempReg3 = EDX;
static const X64Reg srcReg = ESI;
static const X64Reg dstReg = EDI;
static const X64Reg counterReg = ECX;
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

	{&VertexDecoder::Step_TcU8, &VertexDecoderJitCache::Jit_TcU8},
	{&VertexDecoder::Step_TcU16, &VertexDecoderJitCache::Jit_TcU16},
	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
	{&VertexDecoder::Step_TcU16Double, &VertexDecoderJitCache::Jit_TcU16Double},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},

	{&VertexDecoder::Step_TcU16Through, &VertexDecoderJitCache::Jit_TcU16Through},
	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},
	{&VertexDecoder::Step_TcU16ThroughDouble, &VertexDecoderJitCache::Jit_TcU16ThroughDouble},

	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
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
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloat},

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

// TODO: This should probably be global...
#ifdef _M_X64
#define PTRBITS 64
#else
#define PTRBITS 32
#endif

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec) {
	dec_ = &dec;
	const u8 *start = this->GetCodePtr();

#ifdef _M_IX86
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
#endif

	// Save XMM4/XMM5 which apparently can be problematic?
	// Actually, if they are, it must be a compiler bug because they SHOULD be ok.
	// So I won't bother.
	SUB(PTRBITS, R(ESP), Imm8(64));
	MOVUPS(MDisp(ESP, 0), XMM4);
	MOVUPS(MDisp(ESP, 16), XMM5);
	MOVUPS(MDisp(ESP, 32), XMM6);
	MOVUPS(MDisp(ESP, 48), XMM7);

	bool prescaleStep = false;
	// Look for prescaled texcoord steps
	for (int i = 0; i < dec.numSteps_; i++) {
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescale) {
			prescaleStep = true;
		}
	}

	// Add code to convert matrices to 4x4.
	// Later we might want to do this when the matrices are loaded instead.
	// This is mostly proof of concept.
	int boneCount = 0;
	if (dec.weighttype && g_Config.bSoftwareSkinning) {
		for (int i = 0; i < 8; i++) {
			MOVUPS(XMM0, M((gstate.boneMatrix + 12 * i)));
			MOVUPS(XMM1, M((gstate.boneMatrix + 12 * i + 3)));
			MOVUPS(XMM2, M((gstate.boneMatrix + 12 * i + 3 * 2)));
			MOVUPS(XMM3, M((gstate.boneMatrix + 12 * i + 3 * 3)));
			ANDPS(XMM0, M(&threeMasks));
			ANDPS(XMM1, M(&threeMasks));
			ANDPS(XMM2, M(&threeMasks));
			ANDPS(XMM3, M(&threeMasks));
			ORPS(XMM3, M(&aOne));
			MOVAPS(M((bones + 16 * i)), XMM0);
			MOVAPS(M((bones + 16 * i + 4)), XMM1);
			MOVAPS(M((bones + 16 * i + 8)), XMM2);
			MOVAPS(M((bones + 16 * i + 12)), XMM3);
		}
	}

	// Keep the scale/offset in a few fp registers if we need it.
	if (prescaleStep) {
#ifdef _M_X64
		MOV(64, R(tempReg1), Imm64((u64)(&gstate_c.uv)));
#else
		MOV(32, R(tempReg1), Imm32((u32)(&gstate_c.uv)));
#endif
		MOVSS(fpScaleOffsetReg, MDisp(tempReg1, 0));
		MOVSS(fpScratchReg, MDisp(tempReg1, 4));
		UNPCKLPS(fpScaleOffsetReg, R(fpScratchReg));
		if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
			MULPS(fpScaleOffsetReg, M(&by128));
		} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
			MULPS(fpScaleOffsetReg, M(&by32768));
		}
		MOVSS(fpScratchReg, MDisp(tempReg1, 8));
		MOVSS(fpScratchReg2, MDisp(tempReg1, 12));
		UNPCKLPS(fpScratchReg, R(fpScratchReg2));
		UNPCKLPD(fpScaleOffsetReg, R(fpScratchReg));
	}

	// Let's not bother with a proper stack frame. We just grab the arguments and go.
	JumpTarget loopStart = GetCodePtr();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			// Reset the code ptr and return zero to indicate that we failed.
			SetCodePtr(const_cast<u8 *>(start));
			return 0;
		}
	}

	ADD(PTRBITS, R(srcReg), Imm32(dec.VertexSize()));
	ADD(PTRBITS, R(dstReg), Imm32(dec.decFmt.stride));
	SUB(32, R(counterReg), Imm8(1));
	J_CC(CC_NZ, loopStart, true);

	MOVUPS(XMM4, MDisp(ESP, 0));
	MOVUPS(XMM5, MDisp(ESP, 16));
	MOVUPS(XMM6, MDisp(ESP, 32));
	MOVUPS(XMM7, MDisp(ESP, 48));
	ADD(PTRBITS, R(ESP), Imm8(64));

#ifdef _M_IX86
	// Restore register values
	POP(EBP);
	POP(EBX);
	POP(EDI);
	POP(ESI);
#endif

	RET();

	return (JittedVertexDecoder)start;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	switch (dec_->nweights) {
	case 1:
		MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 2:
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 3:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		AND(32, R(tempReg1), Imm32(0x00FFFFFF));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 4:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 8:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->weightoff + 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w1off), R(tempReg2));
		return;
	}

	// Basic implementation - a byte at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		MOV(8, R(tempReg1), MDisp(srcReg, dec_->weightoff + j));
		MOV(8, MDisp(dstReg, dec_->decFmt.w0off + j), R(tempReg1));
	}
	while (j & 3) {
		MOV(8, MDisp(dstReg, dec_->decFmt.w0off + j), Imm8(0));
		j++;
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
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->weightoff + 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), R(tempReg2));
		return;
	}

	// Basic implementation - a short at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		MOV(16, R(tempReg1), MDisp(srcReg, dec_->weightoff + j * 2));
		MOV(16, MDisp(dstReg, dec_->decFmt.w0off + j * 2), R(tempReg1));
	}
	while (j & 3) {
		MOV(16, MDisp(dstReg, dec_->decFmt.w0off + j * 2), Imm16(0));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff + j * 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + j * 4), R(tempReg1));
	}
	while (j & 3) {  // Zero additional weights rounding up to 4.
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + j * 4), Imm32(0));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
	MOV(PTRBITS, R(tempReg2), ImmPtr(&bones));
	for (int j = 0; j < dec_->nweights; j++) {
		MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->weightoff + j));
		CVTSI2SS(XMM1, R(tempReg1));
		MULSS(XMM1, M(&by128));
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

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
	MOV(PTRBITS, R(tempReg2), ImmPtr(&bones));
	for (int j = 0; j < dec_->nweights; j++) {
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff + j * 2));
		CVTSI2SS(XMM1, R(tempReg1));
		MULSS(XMM1, M(&by32768));
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

// Fill last two bytes with zeroes to align to 4 bytes. MOVZX does it for us, handy.
void VertexDecoderJitCache::Jit_TcU8() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcU16() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcU16Double() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->tcoff + 2));
	SHL(16, R(tempReg1), Imm8(1));  // 16 to get a wall to shift into
	SHL(32, R(tempReg2), Imm8(17));
	OR(32, R(tempReg1), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcFloat() {
#ifdef _M_X64
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

void VertexDecoderJitCache::Jit_TcU16Through() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcU16ThroughDouble() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->tcoff + 2));
	SHL(16, R(tempReg1), Imm8(1));  // 16 to get a wall to shift into
	SHL(32, R(tempReg2), Imm8(17));
	OR(32, R(tempReg1), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
#ifdef _M_X64
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
	MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));
	SetJumpTarget(skip);
}

static const u32 MEMORY_ALIGNED16(nibbles[4]) = { 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, };
static const u32 MEMORY_ALIGNED16(color4444mask[4]) = { 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, 0xf00ff00f, };

void VertexDecoderJitCache::Jit_Color4444() {
	// Needs benchmarking. A bit wasteful by only using 1 SSE lane.
#if 0
	MOVD_xmm(fpScratchReg, MDisp(srcReg, dec_->coloff));
	PUNPCKLBW(fpScratchReg, R(fpScratchReg));
	PAND(fpScratchReg, M(color4444mask));
	MOVSS(fpScratchReg2, R(fpScratchReg));
	MOVSS(fpScratchReg3, R(fpScratchReg));
	PSRLW(fpScratchReg2, 4);
	PSLLW(fpScratchReg3, 4);
	POR(fpScratchReg, R(fpScratchReg2));
	POR(fpScratchReg, R(fpScratchReg3));
	MOVD_xmm(MDisp(dstReg, dec_->decFmt.c0off), fpScratchReg);
	return;
#elif 0
	// Alternate approach
	MOVD_xmm(XMM3, MDisp(srcReg, dec_->coloff));
	MOVAPS(XMM2, R(XMM3));
	MOVAPS(XMM1, M(nibbles));
	PSLLD(XMM2, 4);
	PAND(XMM3, R(XMM1));
	PAND(XMM2, R(XMM1));
	PSRLD(XMM2, 4);
	PXOR(XMM1, R(XMM1));
	PUNPCKLBW(XMM2, R(XMM1));
	PUNPCKLBW(XMM3, R(XMM1));
	PSLLD(XMM2, 4);
	POR(XMM3, R(XMM2));
	MOVAPS(XMM2, R(XMM3));
	PSLLD(XMM2, 4);
	POR(XMM3, R(XMM2));
	MOVD_xmm(MDisp(dstReg, dec_->decFmt.c0off), XMM3);
	return;
#endif

	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->coloff));

	// Pick out A and B, and space them out by a nibble.
	MOV(32, R(tempReg2), R(tempReg1));
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg2), Imm32(0x0000F000));
	AND(32, R(tempReg3), Imm32(0x00000F00));
	SHL(32, R(tempReg2), Imm8(4));
	OR(32, R(tempReg2), R(tempReg3));

	// Now grab R and G.
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg1), Imm32(0x0000000F));
	AND(32, R(tempReg3), Imm32(0x000000F0));

	// Currently: 000A0B00, so let's shift once so G is spaced out.
	SHL(32, R(tempReg2), Imm8(4));
	OR(32, R(tempReg2), R(tempReg3));

	// Now: 00A0B0G0, so shift it once more to add R at the bottom.
	SHL(32, R(tempReg2), Imm8(4));
	OR(32, R(tempReg2), R(tempReg1));

	// Now we just need to duplicate the nibbles.
	MOV(32, R(tempReg3), R(tempReg2));
	SHL(32, R(tempReg3), Imm8(4));
	OR(32, R(tempReg2), R(tempReg3));

	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg2));

	CMP(32, R(tempReg2), Imm32(0xFF000000));
	FixupBranch skip = J_CC(CC_AE, false);
	MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));
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

	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x00007C00));
	SHL(32, R(tempReg3), Imm8(6));
	OR(32, R(tempReg2), R(tempReg3));

	// Expand 5 -> 8.  After this is just A.
	MOV(32, R(tempReg3), R(tempReg2));
	SHL(32, R(tempReg2), Imm8(3));
	SHR(32, R(tempReg3), Imm8(2));
	// Chop off the bits that were shifted out.
	AND(32, R(tempReg3), Imm32(0x00070707));
	OR(32, R(tempReg2), R(tempReg3));

	// For A, we shift it to a single bit, and then subtract and XOR.
	// That's probably the simplest way to expand it...
	SHR(32, R(tempReg1), Imm8(15));
	// If it was 0, it's now -1, otherwise it's 0.  Easy.
	SUB(32, R(tempReg1), Imm8(1));
	XOR(32, R(tempReg1), Imm32(0xFF000000));
	AND(32, R(tempReg1), Imm32(0xFF000000));
	OR(32, R(tempReg2), R(tempReg1));

	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg2));

	CMP(32, R(tempReg2), Imm32(0xFF000000));
	FixupBranch skip = J_CC(CC_AE, false);
	MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));
	SetJumpTarget(skip);
}

void VertexDecoderJitCache::Jit_Color8888Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	PXOR(fpScratchReg4, R(fpScratchReg4));

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

static const float MEMORY_ALIGNED16(byColor4444[4]) = { 255.0f / 15.0f, 255.0f / 15.0f, 255.0f / 15.0f, 255.0f / 15.0f, };

void VertexDecoderJitCache::Jit_Color4444Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	PXOR(fpScratchReg4, R(fpScratchReg4));
	MOVDQA(XMM5, M(color4444mask));
	MOVAPS(XMM6, M(byColor4444));

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
static const u32 MEMORY_ALIGNED16(color565Mask[4]) = { 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000, };
static const float MEMORY_ALIGNED16(byColor565[4]) = { 255.0f / 31.0f, 255.0f / 63.0f, 255.0f / 31.0f, 255.0f / 1.0f, };

void VertexDecoderJitCache::Jit_Color565Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	MOV(32, R(tempReg2), Imm32(1));
	MOVDQA(XMM5, M(color565Mask));
	MOVAPS(XMM6, M(byColor565));

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
static const u32 MEMORY_ALIGNED16(color5551Mask[4]) = { 0x00008000, 0x00007c00, 0x000003e0, 0x0000001f, };
static const float MEMORY_ALIGNED16(byColor5551[4]) = { 255.0f / 31.0f, 255.0f / 31.0f, 255.0f / 31.0f, 255.0f / 1.0f, };

void VertexDecoderJitCache::Jit_Color5551Morph() {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	MOVDQA(XMM5, M(color5551Mask));
	MOVAPS(XMM6, M(byColor5551));

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
		MOV(8, M(&gstate_c.vertexFullAlpha), Imm8(0));
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

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->nrmoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 4), R(tempReg2));
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->nrmoff + 4));
	MOV(32, R(tempReg3), MDisp(srcReg, dec_->nrmoff + 8));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 4), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 8), R(tempReg3));
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
	XORPS(XMM3, R(XMM3));
	MOVD_xmm(XMM1, MDisp(srcReg, dec_->nrmoff));
	if (cpu_info.bSSE4_1) {
		PMOVSXBD(XMM1, R(XMM1));
	} else {
		PUNPCKLBW(XMM1, R(XMM3));
		PUNPCKLWD(XMM1, R(XMM3));
		PSLLD(XMM1, 24);
		PSRAD(XMM1, 24);
	}
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M(&by128));
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16Skin() {
	XORPS(XMM3, R(XMM3));
	MOVQ_xmm(XMM1, MDisp(srcReg, dec_->nrmoff));
	if (cpu_info.bSSE4_1) {
		PMOVSXWD(XMM1, R(XMM1));
	} else {
		PSLLD(XMM1, 16);
		PSRAD(XMM1, 16);
	}
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M(&by32768));
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalFloatSkin() {
	MOVUPS(XMM3, MDisp(srcReg, dec_->nrmoff));
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS8Through() {
	DEBUG_LOG_REPORT_ONCE(vertexS8Through, G3D, "Using S8 positions in throughmode");
	// TODO: SIMD
	for (int i = 0; i < 3; i++) {
		MOVSX(32, 8, tempReg1, MDisp(srcReg, dec_->posoff + i));
		CVTSI2SS(fpScratchReg, R(tempReg1));
		MOVSS(MDisp(dstReg, dec_->decFmt.posoff + i * 4), fpScratchReg);
	}
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS16Through() {
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

// Copy 3 bytes and then a zero. Might as well copy four.
void VertexDecoderJitCache::Jit_PosS8() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
	AND(32, R(tempReg1), Imm32(0x00FFFFFF));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_PosS16() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->posoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 4), R(tempReg2));
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->posoff + 4));
	MOV(32, R(tempReg3), MDisp(srcReg, dec_->posoff + 8));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 4), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 8), R(tempReg3));
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
	XORPS(XMM3, R(XMM3));
	MOVD_xmm(XMM1, MDisp(srcReg, dec_->posoff));
	if (cpu_info.bSSE4_1) {
		PMOVSXBD(XMM1, R(XMM1));
	} else {
		PUNPCKLBW(XMM1, R(XMM3));
		PUNPCKLWD(XMM1, R(XMM3));
		PSLLD(XMM1, 24);
		PSRAD(XMM1, 24);
	}
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M(&by128));
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS16Skin() {
	XORPS(XMM3, R(XMM3));
	MOVQ_xmm(XMM1, MDisp(srcReg, dec_->posoff));
	if (cpu_info.bSSE4_1) {
		PMOVSXWD(XMM1, R(XMM1));
	} else {
		PUNPCKLWD(XMM1, R(XMM3));
		PSLLD(XMM1, 16);
		PSRAD(XMM1, 16);
	}
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M(&by32768));
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloatSkin() {
	MOVUPS(XMM3, MDisp(srcReg, dec_->posoff));
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_AnyS8Morph(int srcoff, int dstoff) {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	PXOR(fpScratchReg4, R(fpScratchReg4));
	MOVAPS(XMM5, M(by127));

	// Sum into fpScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		// Okay, first convert to floats.
		MOVD_xmm(reg, MDisp(srcReg, dec_->onesize_ * n + srcoff));
		if (cpu_info.bSSE4_1) {
			PMOVSXBD(reg, R(reg));
		} else {
			PUNPCKLBW(reg, R(fpScratchReg4));
			PUNPCKLWD(reg, R(fpScratchReg4));
			PSLLD(reg, 24);
			PSRAD(reg, 24);
		}
		CVTDQ2PS(reg, R(reg));

		// Now, It's time to multiply by the weight and 1.0f/127.0f.
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

	// TODO: Is it okay that we're over-writing by 4 bytes?  Probably...
	MOVUPS(MDisp(dstReg, dstoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_AnyS16Morph(int srcoff, int dstoff) {
	MOV(PTRBITS, R(tempReg1), ImmPtr(&gstate_c.morphWeights[0]));
	PXOR(fpScratchReg4, R(fpScratchReg4));
	MOVAPS(XMM5, M(by32767));

	// Sum into fpScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const X64Reg reg = first ? fpScratchReg : fpScratchReg2;
		// Okay, first convert to floats.
		MOVQ_xmm(reg, MDisp(srcReg, dec_->onesize_ * n + srcoff));
		if (cpu_info.bSSE4_1) {
			PMOVSXWD(reg, R(reg));
		} else {
			PUNPCKLWD(reg, R(fpScratchReg4));
			PSLLD(reg, 16);
			PSRAD(reg, 16);
		}
		CVTDQ2PS(reg, R(reg));

		// Now, It's time to multiply by the weight and 1.0f/32767.0f.
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

	// TODO: Is it okay that we're over-writing by 4 bytes?  Probably...
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

	// TODO: Is it okay that we're over-writing by 4 bytes?  Probably...
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
