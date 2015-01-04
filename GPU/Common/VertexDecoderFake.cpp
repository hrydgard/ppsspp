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
#include "GPU/GPUState.h"
#include "GPU/Common/VertexDecoderCommon.h"

static const float by128 = 1.0f / 128.0f;
static const float by16384 = 1.0f / 16384.0f;
static const float by32768 = 1.0f / 32768.0f;

#ifdef MIPS
using namespace MIPSGen;
#else
using namespace FakeGen;
#endif

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

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec) {
	dec_ = &dec;
	//const u8 *start = AlignCode16();

	bool prescaleStep = false;
	bool skinning = false;

	return nullptr;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
}

void VertexDecoderJitCache::Jit_WeightsU16() {
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
}

void VertexDecoderJitCache::Jit_ApplyWeights() {
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
}

void VertexDecoderJitCache::Jit_TcU8() {
}

void VertexDecoderJitCache::Jit_TcU16() {
}

void VertexDecoderJitCache::Jit_TcFloat() {
}

void VertexDecoderJitCache::Jit_TcU16Through() {
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
}

void VertexDecoderJitCache::Jit_TcU16Double() {
}

void VertexDecoderJitCache::Jit_TcU16ThroughDouble() {
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
}

void VertexDecoderJitCache::Jit_Color8888() {
}

void VertexDecoderJitCache::Jit_Color4444() {
}

void VertexDecoderJitCache::Jit_Color565() {
}

void VertexDecoderJitCache::Jit_Color5551() {
}

void VertexDecoderJitCache::Jit_Color8888Morph() {
}

void VertexDecoderJitCache::Jit_Color4444Morph() {
}

void VertexDecoderJitCache::Jit_Color565Morph() {
}

void VertexDecoderJitCache::Jit_Color5551Morph() {
}

void VertexDecoderJitCache::Jit_WriteMorphColor(int outOff, bool checkAlpha) {
}

void VertexDecoderJitCache::Jit_NormalS8() {
}

void VertexDecoderJitCache::Jit_NormalS16() {
}

void VertexDecoderJitCache::Jit_NormalFloat() {
}

void VertexDecoderJitCache::Jit_PosS8Through() {
}

void VertexDecoderJitCache::Jit_PosS16Through() {
}

void VertexDecoderJitCache::Jit_PosS8() {
}

void VertexDecoderJitCache::Jit_PosS16() {
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
}

void VertexDecoderJitCache::Jit_NormalS8Skin() {
}

void VertexDecoderJitCache::Jit_NormalS16Skin() {
}

void VertexDecoderJitCache::Jit_NormalFloatSkin() {
}

void VertexDecoderJitCache::Jit_WriteMatrixMul(int outOff, bool pos) {
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
}

void VertexDecoderJitCache::Jit_PosS16Skin() {
}

void VertexDecoderJitCache::Jit_PosFloatSkin() {
}

void VertexDecoderJitCache::Jit_AnyS8ToFloat(int srcoff) {
}

void VertexDecoderJitCache::Jit_AnyS16ToFloat(int srcoff) {
}

void VertexDecoderJitCache::Jit_AnyS8Morph(int srcoff, int dstoff) {
}

void VertexDecoderJitCache::Jit_AnyS16Morph(int srcoff, int dstoff) {
}

void VertexDecoderJitCache::Jit_AnyFloatMorph(int srcoff, int dstoff) {
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
	return false;
}

