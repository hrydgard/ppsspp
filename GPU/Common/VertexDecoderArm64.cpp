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

#include "base/logging.h"
#include "Common/CPUDetect.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Common/Arm64Emitter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/VertexDecoderCommon.h"

static const float by128 = 1.0f / 128.0f;
static const float by16384 = 1.0f / 16384.0f;
static const float by32768 = 1.0f / 32768.0f;

using namespace Arm64Gen;

// Pointers, X regs
static const ARM64Reg srcReg = X0;
static const ARM64Reg dstReg = X1;

static const ARM64Reg counterReg = W2;
static const ARM64Reg tempReg1 = W3;
static const ARM64Reg tempReg2 = W4;
static const ARM64Reg tempReg3 = W5;
static const ARM64Reg scratchReg = W6;
static const ARM64Reg scratchReg2 = W7;
static const ARM64Reg scratchReg3 = W8;
static const ARM64Reg fullAlphaReg = W12;

static const ARM64Reg fpScratchReg = S4;
static const ARM64Reg fpScratchReg2 = S5;
static const ARM64Reg fpScratchReg3 = S6;
static const ARM64Reg fpScratchReg4 = S7;
static const ARM64Reg fpUVscaleReg = D0;
static const ARM64Reg fpUVoffsetReg = D1;

static const ARM64Reg neonScratchReg = D2;
static const ARM64Reg neonScratchReg2 = D3;

static const ARM64Reg neonScratchRegQ = Q1;

// Everything above S6 is fair game for skinning

// S8-S15 are used during matrix generation

// These only live through the matrix multiplication
static const ARM64Reg src[3] = { S8, S9, S10 };  // skin source
static const ARM64Reg acc[3] = { S11, S12, S13 };  // skin accumulator

static const ARM64Reg srcNEON = Q2;
static const ARM64Reg accNEON = Q3;

static const JitLookup jitLookup[] = {
	/*
	{&VertexDecoder::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoder::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoder::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},

	{&VertexDecoder::Step_WeightsU8Skin, &VertexDecoderJitCache::Jit_WeightsU8Skin},
	{&VertexDecoder::Step_WeightsU16Skin, &VertexDecoderJitCache::Jit_WeightsU16Skin},
	{&VertexDecoder::Step_WeightsFloatSkin, &VertexDecoderJitCache::Jit_WeightsFloatSkin},
	*/
	{&VertexDecoder::Step_TcU8, &VertexDecoderJitCache::Jit_TcU8},
	{&VertexDecoder::Step_TcU16, &VertexDecoderJitCache::Jit_TcU16},
	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
	/*
	{&VertexDecoder::Step_TcU16Double, &VertexDecoderJitCache::Jit_TcU16Double},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},
	*/
	{&VertexDecoder::Step_TcU16Through, &VertexDecoderJitCache::Jit_TcU16Through},
	/*
	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},
	{&VertexDecoder::Step_TcU16ThroughDouble, &VertexDecoderJitCache::Jit_TcU16ThroughDouble},

	*/
	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoder::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoder::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},

	/*
	{&VertexDecoder::Step_NormalS8Skin, &VertexDecoderJitCache::Jit_NormalS8Skin},
	{&VertexDecoder::Step_NormalS16Skin, &VertexDecoderJitCache::Jit_NormalS16Skin},
	{&VertexDecoder::Step_NormalFloatSkin, &VertexDecoderJitCache::Jit_NormalFloatSkin},
	*/
	{&VertexDecoder::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	/*
	{&VertexDecoder::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoder::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoder::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},

	{&VertexDecoder::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	*/
	{&VertexDecoder::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	/*
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloat},
	*/
	{&VertexDecoder::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoder::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoder::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},
	/*
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
	*/
};


JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec) {
	dec_ = &dec;
	const u8 *start = AlignCode16();

	WARN_LOG(HLE, "VertexDecoderJitCache::Compile");

	bool prescaleStep = false;
	bool skinning = false;

	// Look for prescaled texcoord steps
	for (int i = 0; i < dec.numSteps_; i++) {
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescale) {
			prescaleStep = true;
		}
		if (dec.steps_[i] == &VertexDecoder::Step_WeightsU8Skin ||
			dec.steps_[i] == &VertexDecoder::Step_WeightsU16Skin ||
			dec.steps_[i] == &VertexDecoder::Step_WeightsFloatSkin) {
			skinning = true;
		}
	}

	if (dec.weighttype && g_Config.bSoftwareSkinning && dec.morphcount == 1) {
		WARN_LOG(HLE, "vtxdec-arm64 does not support sw skinning");
		return NULL;
	}

	if (dec.col) {
		// Or LDB and skip the conditional?  This is probably cheaper.
		MOVI2R(fullAlphaReg, 0xFF);
	}

	const u8 *loopStart = GetCodePtr();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			// Reset the code ptr (effectively undoing what we generated) and return zero to indicate that we failed.
			SetCodePtr(const_cast<u8 *>(start));
			char temp[1024] = {0};
			dec.ToString(temp);
			WARN_LOG(HLE, "Could not compile vertex decoder, failed at step %d: %s", i, temp);
			return 0;
		}
	}

	ADDI2R(srcReg, srcReg, dec.VertexSize(), scratchReg);
	ADDI2R(dstReg, dstReg, dec.decFmt.stride, scratchReg);
	SUBS(counterReg, counterReg, 1);
	B(CC_NEQ, loopStart);

	if (dec.col) {
		MOVP2R(tempReg1, &gstate_c.vertexFullAlpha);
		CMP(fullAlphaReg, 0);
		FixupBranch skip = B(CC_NEQ);
		STRB(INDEX_UNSIGNED, fullAlphaReg, tempReg1, 0);
		SetJumpTarget(skip);
	}

	// POP(6, R4, R5, R6, R7, R8, R_PC);
	RET();

	FlushIcache();

	char temp[1024] = { 0 };
	dec.ToString(temp);
	INFO_LOG(HLE, "=== %s (%d bytes) ===", temp, (int)(GetCodePtr() - start));
	std::vector<std::string> lines = DisassembleArm64(start, GetCodePtr() - start);
	for (auto line : lines) {
		INFO_LOG(HLE, "%s", line.c_str());
	}
	INFO_LOG(HLE, "==========", temp);

	return (JittedVertexDecoder)start;
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

void VertexDecoderJitCache::Jit_Color8888() {
	LDR(INDEX_UNSIGNED, tempReg1, srcReg, dec_->coloff);
	// TODO: Set flags to determine if alpha != 0xFF.
	// ANDSI2R(tempReg2, tempReg1, 0xFF000000);
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.c0off);
	// FixupBranch skip = B(CC_NZ);
	MOVI2R(fullAlphaReg, 0);
	// SetJumpTarget(skip);
}

void VertexDecoderJitCache::Jit_TcU8() {
	LDRB(INDEX_UNSIGNED, tempReg1, srcReg, dec_->tcoff);
	LDRB(INDEX_UNSIGNED, tempReg2, srcReg, dec_->tcoff + 1);
	ORR(tempReg1, tempReg1, tempReg2, ArithOption(tempReg2, ST_LSL, 8));
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16() {
	LDRH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->tcoff);
	LDRH(INDEX_UNSIGNED, tempReg2, srcReg, dec_->tcoff + 2);
	ORR(tempReg1, tempReg1, tempReg2, ArithOption(tempReg2, ST_LSL, 16));
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16Through() {
	LDRH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->tcoff);
	LDRH(INDEX_UNSIGNED, tempReg2, srcReg, dec_->tcoff + 2);
	ORR(tempReg1, tempReg1, tempReg2, ArithOption(tempReg2, ST_LSL, 16));
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloat() {
	LDR(INDEX_UNSIGNED, tempReg1, srcReg, dec_->tcoff);
	LDR(INDEX_UNSIGNED, tempReg2, srcReg, dec_->tcoff + 4);
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.uvoff);
	STR(INDEX_UNSIGNED, tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_PosS8() {
	Jit_AnyS8ToFloat(dec_->posoff);
	STR(INDEX_UNSIGNED, src[0], dstReg, dec_->decFmt.posoff);
	STR(INDEX_UNSIGNED, src[1], dstReg, dec_->decFmt.posoff + 4);
	STR(INDEX_UNSIGNED, src[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16() {
	Jit_AnyS16ToFloat(dec_->posoff);
	STR(INDEX_UNSIGNED, src[0], dstReg, dec_->decFmt.posoff);
	STR(INDEX_UNSIGNED, src[1], dstReg, dec_->decFmt.posoff + 4);
	STR(INDEX_UNSIGNED, src[2], dstReg, dec_->decFmt.posoff + 8);
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
	LDR(INDEX_UNSIGNED, tempReg1, srcReg, dec_->posoff);
	LDR(INDEX_UNSIGNED, tempReg2, srcReg, dec_->posoff + 4);
	LDR(INDEX_UNSIGNED, tempReg3, srcReg, dec_->posoff + 8);
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.posoff);
	STR(INDEX_UNSIGNED, tempReg2, dstReg, dec_->decFmt.posoff + 4);
	STR(INDEX_UNSIGNED, tempReg3, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16Through() {
	LDRSH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->posoff);
	LDRSH(INDEX_UNSIGNED, tempReg2, srcReg, dec_->posoff + 2);
	LDRH(INDEX_UNSIGNED, tempReg3, srcReg, dec_->posoff + 4);
	fp.SCVTF(fpScratchReg, tempReg1);
	fp.SCVTF(fpScratchReg2, tempReg2);
	fp.SCVTF(fpScratchReg3, tempReg3);
	STR(INDEX_UNSIGNED, fpScratchReg, dstReg, dec_->decFmt.posoff);
	STR(INDEX_UNSIGNED, fpScratchReg2, dstReg, dec_->decFmt.posoff + 4);
	STR(INDEX_UNSIGNED, fpScratchReg3, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_NormalS8() {
	LDRB(INDEX_UNSIGNED, tempReg1, srcReg, dec_->nrmoff);
	LDRB(INDEX_UNSIGNED, tempReg2, srcReg, dec_->nrmoff + 1);
	LDRB(INDEX_UNSIGNED, tempReg3, srcReg, dec_->nrmoff + 2);
	ORR(tempReg1, tempReg1, tempReg2, ArithOption(tempReg2, ST_LSL, 8));
	ORR(tempReg1, tempReg1, tempReg3, ArithOption(tempReg3, ST_LSL, 16));
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.nrmoff);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	LDRH(INDEX_UNSIGNED, tempReg1, srcReg, dec_->nrmoff);
	LDRH(INDEX_UNSIGNED, tempReg2, srcReg, dec_->nrmoff + 2);
	LDRH(INDEX_UNSIGNED, tempReg3, srcReg, dec_->nrmoff + 4);
	ORR(tempReg1, tempReg1, tempReg2, ArithOption(tempReg2, ST_LSL, 16));
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.nrmoff);
	STR(INDEX_UNSIGNED, tempReg3, dstReg, dec_->decFmt.nrmoff + 4);
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	LDR(INDEX_UNSIGNED, tempReg1, srcReg, dec_->nrmoff);
	LDR(INDEX_UNSIGNED, tempReg2, srcReg, dec_->nrmoff + 4);
	LDR(INDEX_UNSIGNED, tempReg3, srcReg, dec_->nrmoff + 8);
	STR(INDEX_UNSIGNED, tempReg1, dstReg, dec_->decFmt.nrmoff);
	STR(INDEX_UNSIGNED, tempReg2, dstReg, dec_->decFmt.nrmoff + 4);
	STR(INDEX_UNSIGNED, tempReg3, dstReg, dec_->decFmt.nrmoff + 8);
}

void VertexDecoderJitCache::Jit_AnyS8ToFloat(int srcoff) {
	// TODO: NEONize. In that case we'll leave all three floats in one register instead, so callers must change too.
	LDRSB(INDEX_UNSIGNED, tempReg1, srcReg, srcoff);
	LDRSB(INDEX_UNSIGNED, tempReg2, srcReg, srcoff + 1);
	LDRSB(INDEX_UNSIGNED, tempReg3, srcReg, srcoff + 2);
	fp.SCVTF(src[0], tempReg1, 7);
	fp.SCVTF(src[1], tempReg2, 7);
	fp.SCVTF(src[2], tempReg3, 7);
}

void VertexDecoderJitCache::Jit_AnyS16ToFloat(int srcoff) {
	LDRSH(INDEX_UNSIGNED, tempReg1, srcReg, srcoff);
	LDRSH(INDEX_UNSIGNED, tempReg2, srcReg, srcoff + 2);
	LDRSH(INDEX_UNSIGNED, tempReg3, srcReg, srcoff + 4);
	fp.SCVTF(src[0], tempReg1, 15);
	fp.SCVTF(src[1], tempReg2, 15);
	fp.SCVTF(src[2], tempReg3, 15);
}
