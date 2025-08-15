// Copyright (c) 2023- PPSSPP Project.

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
#if PPSSPP_ARCH(RISCV64)

#include <utility>
#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Common/RiscVEmitter.h"
#include "Core/HDRemaster.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/VertexDecoderCommon.h"

static const float by128 = 1.0f / 128.0f;
static const float by32768 = 1.0f / 32768.0f;
static const float const65535 = 65535.0f;

using namespace RiscVGen;

static const RiscVReg srcReg = X10;  // a0
static const RiscVReg dstReg = X11;  // a1
static const RiscVReg counterReg = X12;  // a2

static const RiscVReg tempReg1 = X13;  // a3
static const RiscVReg tempReg2 = X14;
static const RiscVReg tempReg3 = X15;
static const RiscVReg scratchReg = X16;

static const RiscVReg morphBaseReg = X5;

static const RiscVReg fullAlphaReg = X17;
static const RiscVReg boundsMinUReg = X28;
static const RiscVReg boundsMinVReg = X29;
static const RiscVReg boundsMaxUReg = X30;
static const RiscVReg boundsMaxVReg = X31;

static const RiscVReg fpScratchReg1 = F10;
static const RiscVReg fpScratchReg2 = F11;
static const RiscVReg fpScratchReg3 = F12;
// We want most of these within 8-15, to be compressible.
static const RiscVReg fpSrc[4] = { F13, F14, F15, F16 };
static const RiscVReg fpScratchReg4 = F17;
static const RiscVReg fpExtra[4] = { F28, F29, F30, F31 };

struct UVScaleRegs {
	struct {
		RiscVReg u;
		RiscVReg v;
	} scale;
	struct {
		RiscVReg u;
		RiscVReg v;
	} offset;
};

static const UVScaleRegs prescaleRegs = { { F0, F1 }, { F2, F3 } };
static const RiscVReg by128Reg = F4;
static const RiscVReg by32768Reg = F5;
// Warning: usually not valid.
static const RiscVReg const65535Reg = F6;

struct MorphValues {
	float by128[8];
	float by32768[8];
	float asFloat[8];
	float color4[8];
	float color5[8];
	float color6[8];
};
enum class MorphValuesIndex {
	BY_128 = 0,
	BY_32768 = 1,
	AS_FLOAT = 2,
	COLOR_4 = 3,
	COLOR_5 = 4,
	COLOR_6 = 5,
};
static MorphValues morphValues;
static float skinMatrix[12];

static uint32_t GetMorphValueUsage(uint32_t vtype) {
	uint32_t morphFlags = 0;
	switch (vtype & GE_VTYPE_TC_MASK) {
	case GE_VTYPE_TC_8BIT: morphFlags |= 1 << (int)MorphValuesIndex::BY_128; break;
	case GE_VTYPE_TC_16BIT: morphFlags |= 1 << (int)MorphValuesIndex::BY_32768; break;
	case GE_VTYPE_TC_FLOAT: morphFlags |= 1 << (int)MorphValuesIndex::AS_FLOAT; break;
	}
	switch (vtype & GE_VTYPE_COL_MASK) {
	case GE_VTYPE_COL_565: morphFlags |= (1 << (int)MorphValuesIndex::COLOR_5) | (1 << (int)MorphValuesIndex::COLOR_6); break;
	case GE_VTYPE_COL_5551: morphFlags |= 1 << (int)MorphValuesIndex::COLOR_5; break;
	case GE_VTYPE_COL_4444: morphFlags |= 1 << (int)MorphValuesIndex::COLOR_4; break;
	case GE_VTYPE_COL_8888: morphFlags |= 1 << (int)MorphValuesIndex::AS_FLOAT; break;
	}
	switch (vtype & GE_VTYPE_NRM_MASK) {
	case GE_VTYPE_NRM_8BIT: morphFlags |= 1 << (int)MorphValuesIndex::BY_128; break;
	case GE_VTYPE_NRM_16BIT: morphFlags |= 1 << (int)MorphValuesIndex::BY_32768; break;
	case GE_VTYPE_NRM_FLOAT: morphFlags |= 1 << (int)MorphValuesIndex::AS_FLOAT; break;
	}
	switch (vtype & GE_VTYPE_POS_MASK) {
	case GE_VTYPE_POS_8BIT: morphFlags |= 1 << (int)MorphValuesIndex::BY_128; break;
	case GE_VTYPE_POS_16BIT: morphFlags |= 1 << (int)MorphValuesIndex::BY_32768; break;
	case GE_VTYPE_POS_FLOAT: morphFlags |= 1 << (int)MorphValuesIndex::AS_FLOAT; break;
	}
	return morphFlags;
}

// TODO: Use vector, where supported.

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoder::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoder::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},
	{&VertexDecoder::Step_WeightsU8Skin, &VertexDecoderJitCache::Jit_WeightsU8Skin},
	{&VertexDecoder::Step_WeightsU16Skin, &VertexDecoderJitCache::Jit_WeightsU16Skin},
	{&VertexDecoder::Step_WeightsFloatSkin, &VertexDecoderJitCache::Jit_WeightsFloatSkin},

	{&VertexDecoder::Step_TcU8ToFloat, &VertexDecoderJitCache::Jit_TcU8ToFloat},
	{&VertexDecoder::Step_TcU16ToFloat, &VertexDecoderJitCache::Jit_TcU16ToFloat},
	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},

	{&VertexDecoder::Step_TcU16ThroughToFloat, &VertexDecoderJitCache::Jit_TcU16ThroughToFloat},
	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	// We use the same jit code whether doubling or not.
	{&VertexDecoder::Step_TcU16DoublePrescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},

	{&VertexDecoder::Step_TcU8MorphToFloat, &VertexDecoderJitCache::Jit_TcU8MorphToFloat},
	{&VertexDecoder::Step_TcU16MorphToFloat, &VertexDecoderJitCache::Jit_TcU16MorphToFloat},
	{&VertexDecoder::Step_TcFloatMorph, &VertexDecoderJitCache::Jit_TcFloatMorph},
	{&VertexDecoder::Step_TcU8PrescaleMorph, &VertexDecoderJitCache::Jit_TcU8PrescaleMorph},
	{&VertexDecoder::Step_TcU16PrescaleMorph, &VertexDecoderJitCache::Jit_TcU16PrescaleMorph},
	{&VertexDecoder::Step_TcU16DoublePrescaleMorph, &VertexDecoderJitCache::Jit_TcU16PrescaleMorph},
	{&VertexDecoder::Step_TcFloatPrescaleMorph, &VertexDecoderJitCache::Jit_TcFloatPrescaleMorph},

	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoder::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoder::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},
	{&VertexDecoder::Step_NormalS8Skin, &VertexDecoderJitCache::Jit_NormalS8Skin},
	{&VertexDecoder::Step_NormalS16Skin, &VertexDecoderJitCache::Jit_NormalS16Skin},
	{&VertexDecoder::Step_NormalFloatSkin, &VertexDecoderJitCache::Jit_NormalFloatSkin},

	{&VertexDecoder::Step_NormalS8Morph, &VertexDecoderJitCache::Jit_NormalS8Morph},
	{&VertexDecoder::Step_NormalS16Morph, &VertexDecoderJitCache::Jit_NormalS16Morph},
	{&VertexDecoder::Step_NormalFloatMorph, &VertexDecoderJitCache::Jit_NormalFloatMorph},
	{&VertexDecoder::Step_NormalS8MorphSkin, &VertexDecoderJitCache::Jit_NormalS8MorphSkin},
	{&VertexDecoder::Step_NormalS16MorphSkin, &VertexDecoderJitCache::Jit_NormalS16MorphSkin},
	{&VertexDecoder::Step_NormalFloatMorphSkin, &VertexDecoderJitCache::Jit_NormalFloatMorphSkin},

	{&VertexDecoder::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoder::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoder::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},
	{&VertexDecoder::Step_PosS8Skin, &VertexDecoderJitCache::Jit_PosS8Skin},
	{&VertexDecoder::Step_PosS16Skin, &VertexDecoderJitCache::Jit_PosS16Skin},
	{&VertexDecoder::Step_PosFloatSkin, &VertexDecoderJitCache::Jit_PosFloatSkin},

	{&VertexDecoder::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	{&VertexDecoder::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloatThrough},

	{&VertexDecoder::Step_PosS8Morph, &VertexDecoderJitCache::Jit_PosS8Morph},
	{&VertexDecoder::Step_PosS16Morph, &VertexDecoderJitCache::Jit_PosS16Morph},
	{&VertexDecoder::Step_PosFloatMorph, &VertexDecoderJitCache::Jit_PosFloatMorph},
	{&VertexDecoder::Step_PosS8MorphSkin, &VertexDecoderJitCache::Jit_PosS8MorphSkin},
	{&VertexDecoder::Step_PosS16MorphSkin, &VertexDecoderJitCache::Jit_PosS16MorphSkin},
	{&VertexDecoder::Step_PosFloatMorphSkin, &VertexDecoderJitCache::Jit_PosFloatMorphSkin},

	{&VertexDecoder::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	{&VertexDecoder::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoder::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoder::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},

	{&VertexDecoder::Step_Color8888Morph, &VertexDecoderJitCache::Jit_Color8888Morph},
	{&VertexDecoder::Step_Color4444Morph, &VertexDecoderJitCache::Jit_Color4444Morph},
	{&VertexDecoder::Step_Color565Morph, &VertexDecoderJitCache::Jit_Color565Morph},
	{&VertexDecoder::Step_Color5551Morph, &VertexDecoderJitCache::Jit_Color5551Morph},
};

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec, int32_t *jittedSize) {
	dec_ = &dec;

	BeginWrite(4096);
	const u8 *start = AlignCode16();
	SetAutoCompress(true);

	bool log = false;
	bool prescaleStep = false;
	bool posThroughStep = false;

	// Look for prescaled texcoord steps
	for (int i = 0; i < dec.numSteps_; i++) {
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16Prescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16DoublePrescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescale ||
			dec.steps_[i] == &VertexDecoder::Step_TcU8PrescaleMorph ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16PrescaleMorph ||
			dec.steps_[i] == &VertexDecoder::Step_TcU16DoublePrescaleMorph ||
			dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescaleMorph) {
			prescaleStep = true;
		}
		if (dec.steps_[i] == &VertexDecoder::Step_PosFloatThrough) {
			posThroughStep = true;
		}
	}

	// TODO: Only load these when needed?
	QuickFLI(32, by128Reg, by128, scratchReg);
	QuickFLI(32, by32768Reg, by32768, scratchReg);
	if (posThroughStep) {
		LI(scratchReg, const65535);
		FMV(FMv::W, FMv::X, const65535Reg, scratchReg);
	}

	// Keep the scale/offset in a few fp registers if we need it.
	if (prescaleStep) {
		// tempReg1 happens to be the fourth argument register.
		FL(32, prescaleRegs.scale.u, tempReg1, 0);
		FL(32, prescaleRegs.scale.v, tempReg1, 4);
		FL(32, prescaleRegs.offset.u, tempReg1, 8);
		FL(32, prescaleRegs.offset.v, tempReg1, 12);
		if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
			FMUL(32, prescaleRegs.scale.u, prescaleRegs.scale.u, by128Reg);
			FMUL(32, prescaleRegs.scale.v, prescaleRegs.scale.v, by128Reg);
		} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
			RiscVReg multipler = g_DoubleTextureCoordinates ? fpScratchReg1 : by32768Reg;
			if (g_DoubleTextureCoordinates) {
				FADD(32, fpScratchReg1, by32768Reg, by32768Reg);
			}
			FMUL(32, prescaleRegs.scale.u, prescaleRegs.scale.u, multipler);
			FMUL(32, prescaleRegs.scale.v, prescaleRegs.scale.v, multipler);
		}
	}

	if (dec_->morphcount > 1) {
		uint32_t morphFlags = GetMorphValueUsage(dec.VertexType());

		auto storePremultiply = [&](RiscVReg factorReg, MorphValuesIndex index, int n) {
			FMUL(32, fpScratchReg2, fpScratchReg1, factorReg);
			FS(32, fpScratchReg2, morphBaseReg, ((int)index * 8 + n) * 4);
		};

		LI(morphBaseReg, &morphValues);
		LI(tempReg1, &gstate_c.morphWeights[0]);

		if ((morphFlags & (1 << (int)MorphValuesIndex::COLOR_4)) != 0) {
			LI(scratchReg, 255.0f / 15.0f);
			FMV(FMv::W, FMv::X, fpExtra[0], scratchReg);
		}
		if ((morphFlags & (1 << (int)MorphValuesIndex::COLOR_5)) != 0) {
			LI(scratchReg, 255.0f / 31.0f);
			FMV(FMv::W, FMv::X, fpExtra[1], scratchReg);
		}
		if ((morphFlags & (1 << (int)MorphValuesIndex::COLOR_6)) != 0) {
			LI(scratchReg, 255.0f / 63.0f);
			FMV(FMv::W, FMv::X, fpExtra[2], scratchReg);
		}

		// Premultiply the values we need and store them so we can reuse.
		for (int n = 0; n < dec_->morphcount; n++) {
			FL(32, fpScratchReg1, tempReg1, n * 4);

			if ((morphFlags & (1 << (int)MorphValuesIndex::BY_128)) != 0)
				storePremultiply(by128Reg, MorphValuesIndex::BY_128, n);
			if ((morphFlags & (1 << (int)MorphValuesIndex::BY_32768)) != 0)
				storePremultiply(by32768Reg, MorphValuesIndex::BY_32768, n);
			if ((morphFlags & (1 << (int)MorphValuesIndex::AS_FLOAT)) != 0)
				FS(32, fpScratchReg1, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
			if ((morphFlags & (1 << (int)MorphValuesIndex::COLOR_4)) != 0)
				storePremultiply(fpExtra[0], MorphValuesIndex::COLOR_4, n);
			if ((morphFlags & (1 << (int)MorphValuesIndex::COLOR_5)) != 0)
				storePremultiply(fpExtra[1], MorphValuesIndex::COLOR_5, n);
			if ((morphFlags & (1 << (int)MorphValuesIndex::COLOR_6)) != 0)
				storePremultiply(fpExtra[2], MorphValuesIndex::COLOR_6, n);
		}
	} else if (dec_->skinInDecode) {
		LI(morphBaseReg, &skinMatrix[0]);
	}

	if (dec.col) {
		// Or LB and skip the conditional?  This is probably cheaper.
		LI(fullAlphaReg, 0xFF);
	}

	if (dec.tc && dec.throughmode) {
		// TODO: Smarter, only when doing bounds.
		LI(tempReg1, &gstate_c.vertBounds.minU);
		LH(boundsMinUReg, tempReg1, offsetof(KnownVertexBounds, minU));
		LH(boundsMaxUReg, tempReg1, offsetof(KnownVertexBounds, maxU));
		LH(boundsMinVReg, tempReg1, offsetof(KnownVertexBounds, minV));
		LH(boundsMaxVReg, tempReg1, offsetof(KnownVertexBounds, maxV));
	}

	const u8 *loopStart = GetCodePtr();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			EndWrite();
			// Reset the code ptr (effectively undoing what we generated) and return zero to indicate that we failed.
			ResetCodePtr(GetOffset(start));
			char temp[1024]{};
			dec.ToString(temp, true);
			ERROR_LOG(Log::G3D, "Could not compile vertex decoder, failed at step %d: %s", i, temp);
			return nullptr;
		}
	}

	ADDI(srcReg, srcReg, dec.VertexSize());
	ADDI(dstReg, dstReg, dec.decFmt.stride);
	ADDI(counterReg, counterReg, -1);
	BLT(R_ZERO, counterReg, loopStart);

	if (dec.col) {
		LI(tempReg1, &gstate_c.vertexFullAlpha);
		FixupBranch skip = BNE(R_ZERO, fullAlphaReg);
		SB(fullAlphaReg, tempReg1, 0);
		SetJumpTarget(skip);
	}

	if (dec.tc && dec.throughmode) {
		// TODO: Smarter, only when doing bounds.
		LI(tempReg1, &gstate_c.vertBounds.minU);
		SH(boundsMinUReg, tempReg1, offsetof(KnownVertexBounds, minU));
		SH(boundsMaxUReg, tempReg1, offsetof(KnownVertexBounds, maxU));
		SH(boundsMinVReg, tempReg1, offsetof(KnownVertexBounds, minV));
		SH(boundsMaxVReg, tempReg1, offsetof(KnownVertexBounds, maxV));
	}

	RET();

	FlushIcache();

	if (log) {
		char temp[1024]{};
		dec.ToString(temp, true);
		INFO_LOG(Log::JIT, "=== %s (%d bytes) ===", temp, (int)(GetCodePtr() - start));
		std::vector<std::string> lines = DisassembleRV64(start, (int)(GetCodePtr() - start));
		for (auto line : lines) {
			INFO_LOG(Log::JIT, "%s", line.c_str());
		}
		INFO_LOG(Log::JIT, "==========");
	}

	*jittedSize = (int)(GetCodePtr() - start);
	EndWrite();
	return (JittedVertexDecoder)start;
}

bool VertexDecoderJitCache::CompileStep(const VertexDecoder &dec, int step) {
	// See if we find a matching JIT function.
	for (size_t i = 0; i < ARRAY_SIZE(jitLookup); i++) {
		if (dec.steps_[step] == jitLookup[i].func) {
			((*this).*jitLookup[i].jitFunc)();
			return true;
		}
	}
	return false;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	// Just copy a byte at a time.  Would be nice if we knew if misaligned access was fast.
	// If it's not fast, it can crash or hit a software trap (100x slower.)
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LB(tempReg1, srcReg, dec_->weightoff + j);
		SB(tempReg1, dstReg, dec_->decFmt.w0off + j);
	}
	// We zero out any weights up to a multiple of 4.
	while (j & 3) {
		SB(R_ZERO, dstReg, dec_->decFmt.w0off + j);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LH(tempReg1, srcReg, dec_->weightoff + j * 2);
		SH(tempReg1, dstReg, dec_->decFmt.w0off + j * 2);
	}
	while (j & 3) {
		SH(R_ZERO, dstReg, dec_->decFmt.w0off + j * 2);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LW(tempReg1, srcReg, dec_->weightoff + j * 4);
		SW(tempReg1, dstReg, dec_->decFmt.w0off + j * 4);
	}
	while (j & 3) {
		SW(R_ZERO, dstReg, dec_->decFmt.w0off + j * 4);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_ApplyWeights() {
	int weightSize = 4;
	switch (dec_->weighttype) {
	case 1: weightSize = 1; break;
	case 2: weightSize = 2; break;
	case 3: weightSize = 4; break;
	default:
		_assert_(false);
		break;
	}

	const RiscVReg boneMatrixReg = tempReg1;
	// If we are doing morph + skin, we abuse morphBaseReg.
	const RiscVReg skinMatrixReg = morphBaseReg;
	const RiscVReg loopEndReg = tempReg3;

	LI(boneMatrixReg, &gstate.boneMatrix[0]);
	if (dec_->morphcount > 1)
		LI(skinMatrixReg, &skinMatrix[0]);
	if (weightSize == 4)
		FMV(FMv::W, FMv::X, fpScratchReg3, R_ZERO);

	for (int j = 0; j < 12; ++j) {
		if (cpu_info.Mode64bit) {
			SD(R_ZERO, skinMatrixReg, j * 4);
			++j;
		} else {
			SW(R_ZERO, skinMatrixReg, j * 4);
		}
	}

	// Now let's loop through each weight.  This is the end point.
	ADDI(loopEndReg, srcReg, dec_->nweights * weightSize);
	const u8 *weightLoop = GetCodePointer();

	FixupBranch skipZero;

	switch (weightSize) {
	case 1:
		LBU(scratchReg, srcReg, dec_->weightoff);
		skipZero = std::move(BEQ(R_ZERO, scratchReg));
		FCVT(FConv::S, FConv::WU, fpScratchReg4, scratchReg, Round::TOZERO);
		FMUL(32, fpScratchReg4, fpScratchReg4, by128Reg);
		break;
	case 2:
		LHU(scratchReg, srcReg, dec_->weightoff);
		skipZero = std::move(BEQ(R_ZERO, scratchReg));
		FCVT(FConv::S, FConv::WU, fpScratchReg4, scratchReg, Round::TOZERO);
		FMUL(32, fpScratchReg4, fpScratchReg4, by32768Reg);
		break;
	case 4:
		FL(32, fpScratchReg4, srcReg, dec_->weightoff);
		FEQ(32, scratchReg, fpScratchReg3, fpScratchReg4);
		skipZero = std::move(BNE(R_ZERO, scratchReg));
		break;
	default:
		_assert_(false);
		break;
	}

	// This is the loop where we add up the skinMatrix itself by the weight.
	for (int j = 0; j < 12; j += 4) {
		for (int i = 0; i < 4; ++i)
			FL(32, fpSrc[i], boneMatrixReg, (j + i) * 4);
		for (int i = 0; i < 4; ++i)
			FL(32, fpExtra[i], skinMatrixReg, (j + i) * 4);
		for (int i = 0; i < 4; ++i)
			FMADD(32, fpExtra[i], fpSrc[i], fpScratchReg4, fpExtra[i]);
		for (int i = 0; i < 4; ++i)
			FS(32, fpExtra[i], skinMatrixReg, (j + i) * 4);
	}

	SetJumpTarget(skipZero);

	// Okay, now return back for the next weight.
	ADDI(boneMatrixReg, boneMatrixReg, 12 * 4);
	ADDI(srcReg, srcReg, weightSize);
	BLT(srcReg, loopEndReg, weightLoop);

	// Undo the changes to srcReg.
	ADDI(srcReg, srcReg, dec_->nweights * -weightSize);

	// Restore if we abused this.
	if (dec_->morphcount > 1)
		LI(morphBaseReg, &morphValues);
}

void VertexDecoderJitCache::Jit_TcU8ToFloat() {
	Jit_AnyU8ToFloat(dec_->tcoff, 16);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16ToFloat() {
	Jit_AnyU16ToFloat(dec_->tcoff, 32);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcFloat() {
	// Just copy 64 bits.  Might be nice if we could detect misaligned load perf.
	LW(tempReg1, srcReg, dec_->tcoff);
	LW(tempReg2, srcReg, dec_->tcoff + 4);
	SW(tempReg1, dstReg, dec_->decFmt.uvoff);
	SW(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16ThroughToFloat() {
	LHU(tempReg1, srcReg, dec_->tcoff + 0);
	LHU(tempReg2, srcReg, dec_->tcoff + 2);

	if (cpu_info.RiscV_Zbb) {
		MINU(boundsMinUReg, boundsMinUReg, tempReg1);
		MAXU(boundsMaxUReg, boundsMaxUReg, tempReg1);
		MINU(boundsMinVReg, boundsMinVReg, tempReg2);
		MAXU(boundsMaxVReg, boundsMaxVReg, tempReg2);
	} else {
		auto updateSide = [&](RiscVReg src, bool greater, RiscVReg dst) {
			FixupBranch skip = BLT(greater ? dst : src, greater ? src : dst);
			MV(dst, src);
			SetJumpTarget(skip);
		};

		updateSide(tempReg1, false, boundsMinUReg);
		updateSide(tempReg1, true, boundsMaxUReg);
		updateSide(tempReg2, false, boundsMinVReg);
		updateSide(tempReg2, true, boundsMaxVReg);
	}

	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
	// Just copy 64 bits.  Might be nice if we could detect misaligned load perf.
	LW(tempReg1, srcReg, dec_->tcoff);
	LW(tempReg2, srcReg, dec_->tcoff + 4);
	SW(tempReg1, dstReg, dec_->decFmt.uvoff);
	SW(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
	LBU(tempReg1, srcReg, dec_->tcoff + 0);
	LBU(tempReg2, srcReg, dec_->tcoff + 1);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FMADD(32, fpSrc[0], fpSrc[0], prescaleRegs.scale.u, prescaleRegs.offset.u);
	FMADD(32, fpSrc[1], fpSrc[1], prescaleRegs.scale.v, prescaleRegs.offset.v);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
	LHU(tempReg1, srcReg, dec_->tcoff + 0);
	LHU(tempReg2, srcReg, dec_->tcoff + 2);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FMADD(32, fpSrc[0], fpSrc[0], prescaleRegs.scale.u, prescaleRegs.offset.u);
	FMADD(32, fpSrc[1], fpSrc[1], prescaleRegs.scale.v, prescaleRegs.offset.v);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
	FL(32, fpSrc[0], srcReg, dec_->tcoff + 0);
	FL(32, fpSrc[1], srcReg, dec_->tcoff + 4);
	FMADD(32, fpSrc[0], fpSrc[0], prescaleRegs.scale.u, prescaleRegs.offset.u);
	FMADD(32, fpSrc[1], fpSrc[1], prescaleRegs.scale.v, prescaleRegs.offset.v);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU8MorphToFloat() {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_128 * 8 + 0) * 4);
	LBU(tempReg1, srcReg, dec_->tcoff + 0);
	LBU(tempReg2, srcReg, dec_->tcoff + 1);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_128 * 8 + n) * 4);
		LBU(tempReg1, srcReg, dec_->onesize_ * n + dec_->tcoff + 0);
		LBU(tempReg2, srcReg, dec_->onesize_ * n + dec_->tcoff + 1);
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FCVT(FConv::S, FConv::WU, fpScratchReg2, tempReg2, Round::TOZERO);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
	}

	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16MorphToFloat() {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_32768 * 8 + 0) * 4);
	LHU(tempReg1, srcReg, dec_->tcoff + 0);
	LHU(tempReg2, srcReg, dec_->tcoff + 2);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_32768 * 8 + n) * 4);
		LHU(tempReg1, srcReg, dec_->onesize_ * n + dec_->tcoff + 0);
		LHU(tempReg2, srcReg, dec_->onesize_ * n + dec_->tcoff + 2);
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FCVT(FConv::S, FConv::WU, fpScratchReg2, tempReg2, Round::TOZERO);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
	}

	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcFloatMorph() {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + 0) * 4);
	FL(32, fpSrc[0], srcReg, dec_->tcoff + 0);
	FL(32, fpSrc[1], srcReg, dec_->tcoff + 4);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
		FL(32, fpScratchReg1, srcReg, dec_->onesize_ * n + dec_->tcoff + 0);
		FL(32, fpScratchReg2, srcReg, dec_->onesize_ * n + dec_->tcoff + 4);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
	}

	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU8PrescaleMorph() {
	// We use AS_FLOAT since by128 is already baked into precale.
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + 0) * 4);
	LBU(tempReg1, srcReg, dec_->tcoff + 0);
	LBU(tempReg2, srcReg, dec_->tcoff + 1);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
		LBU(tempReg1, srcReg, dec_->onesize_ * n + dec_->tcoff + 0);
		LBU(tempReg2, srcReg, dec_->onesize_ * n + dec_->tcoff + 1);
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FCVT(FConv::S, FConv::WU, fpScratchReg2, tempReg2, Round::TOZERO);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
	}

	FMADD(32, fpSrc[0], fpSrc[0], prescaleRegs.scale.u, prescaleRegs.offset.u);
	FMADD(32, fpSrc[1], fpSrc[1], prescaleRegs.scale.v, prescaleRegs.offset.v);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16PrescaleMorph() {
	// We use AS_FLOAT since by32768 is already baked into precale.
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + 0) * 4);
	LHU(tempReg1, srcReg, dec_->tcoff + 0);
	LHU(tempReg2, srcReg, dec_->tcoff + 2);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
		LHU(tempReg1, srcReg, dec_->onesize_ * n + dec_->tcoff + 0);
		LHU(tempReg2, srcReg, dec_->onesize_ * n + dec_->tcoff + 2);
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FCVT(FConv::S, FConv::WU, fpScratchReg2, tempReg2, Round::TOZERO);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
	}

	FMADD(32, fpSrc[0], fpSrc[0], prescaleRegs.scale.u, prescaleRegs.offset.u);
	FMADD(32, fpSrc[1], fpSrc[1], prescaleRegs.scale.v, prescaleRegs.offset.v);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcFloatPrescaleMorph() {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + 0) * 4);
	FL(32, fpSrc[0], srcReg, dec_->tcoff + 0);
	FL(32, fpSrc[1], srcReg, dec_->tcoff + 4);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
		FL(32, fpScratchReg1, srcReg, dec_->onesize_ * n + dec_->tcoff + 0);
		FL(32, fpScratchReg2, srcReg, dec_->onesize_ * n + dec_->tcoff + 4);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
	}

	FMADD(32, fpSrc[0], fpSrc[0], prescaleRegs.scale.u, prescaleRegs.offset.u);
	FMADD(32, fpSrc[1], fpSrc[1], prescaleRegs.scale.v, prescaleRegs.offset.v);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.uvoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_NormalS8() {
	LB(tempReg1, srcReg, dec_->nrmoff + 0);
	LB(tempReg2, srcReg, dec_->nrmoff + 1);
	LB(tempReg3, srcReg, dec_->nrmoff + 2);
	SB(tempReg1, dstReg, dec_->decFmt.nrmoff + 0);
	SB(tempReg2, dstReg, dec_->decFmt.nrmoff + 1);
	SB(tempReg3, dstReg, dec_->decFmt.nrmoff + 2);
	SB(R_ZERO, dstReg, dec_->decFmt.nrmoff + 3);
}

void VertexDecoderJitCache::Jit_NormalS16() {
	LH(tempReg1, srcReg, dec_->nrmoff + 0);
	LH(tempReg2, srcReg, dec_->nrmoff + 2);
	LH(tempReg3, srcReg, dec_->nrmoff + 4);
	SH(tempReg1, dstReg, dec_->decFmt.nrmoff + 0);
	SH(tempReg2, dstReg, dec_->decFmt.nrmoff + 2);
	SH(tempReg3, dstReg, dec_->decFmt.nrmoff + 4);
	SH(R_ZERO, dstReg, dec_->decFmt.nrmoff + 6);
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	// Just copy 12 bytes, play with over read/write later.
	LW(tempReg1, srcReg, dec_->nrmoff + 0);
	LW(tempReg2, srcReg, dec_->nrmoff + 4);
	LW(tempReg3, srcReg, dec_->nrmoff + 8);
	SW(tempReg1, dstReg, dec_->decFmt.nrmoff + 0);
	SW(tempReg2, dstReg, dec_->decFmt.nrmoff + 4);
	SW(tempReg3, dstReg, dec_->decFmt.nrmoff + 8);
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
	FL(32, fpSrc[0], srcReg, dec_->nrmoff + 0);
	FL(32, fpSrc[1], srcReg, dec_->nrmoff + 4);
	FL(32, fpSrc[2], srcReg, dec_->nrmoff + 8);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
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

void VertexDecoderJitCache::Jit_NormalS8MorphSkin() {
	Jit_AnyS8Morph(dec_->nrmoff, -1);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalS16MorphSkin() {
	Jit_AnyS16Morph(dec_->nrmoff, -1);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalFloatMorphSkin() {
	Jit_AnyFloatMorph(dec_->nrmoff, -1);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_PosS8() {
	Jit_AnyS8ToFloat(dec_->posoff);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.posoff + 0);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.posoff + 4);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16() {
	Jit_AnyS16ToFloat(dec_->posoff);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.posoff + 0);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.posoff + 4);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosFloat() {
	// Just copy 12 bytes, play with over read/write later.
	LW(tempReg1, srcReg, dec_->posoff + 0);
	LW(tempReg2, srcReg, dec_->posoff + 4);
	LW(tempReg3, srcReg, dec_->posoff + 8);
	SW(tempReg1, dstReg, dec_->decFmt.posoff + 0);
	SW(tempReg2, dstReg, dec_->decFmt.posoff + 4);
	SW(tempReg3, dstReg, dec_->decFmt.posoff + 8);
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
	FL(32, fpSrc[0], srcReg, dec_->posoff + 0);
	FL(32, fpSrc[1], srcReg, dec_->posoff + 4);
	FL(32, fpSrc[2], srcReg, dec_->posoff + 8);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS8Through() {
	// 8-bit positions in throughmode always decode to 0, depth included.
	SW(R_ZERO, dstReg, dec_->decFmt.posoff + 0);
	SW(R_ZERO, dstReg, dec_->decFmt.posoff + 4);
	SW(R_ZERO, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16Through() {
	// Start with X and Y (which are signed.)
	LH(tempReg1, srcReg, dec_->posoff + 0);
	LH(tempReg2, srcReg, dec_->posoff + 2);
	// This one, Z, has to be unsigned.
	LHU(tempReg3, srcReg, dec_->posoff + 4);
	FCVT(FConv::S, FConv::W, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[2], tempReg3, Round::TOZERO);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.posoff + 0);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.posoff + 4);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosFloatThrough() {
	// Start by copying 8 bytes, then handle Z separately to clamp it.
	LW(tempReg1, srcReg, dec_->posoff + 0);
	LW(tempReg2, srcReg, dec_->posoff + 4);
	FL(32, fpSrc[2], srcReg, dec_->posoff + 8);
	SW(tempReg1, dstReg, dec_->decFmt.posoff + 0);
	SW(tempReg2, dstReg, dec_->decFmt.posoff + 4);

	// Load the constant zero and clamp.  Maybe could static alloc zero, but fairly cheap...
	FMV(FMv::W, FMv::X, fpScratchReg1, R_ZERO);
	FMAX(32, fpSrc[2], fpSrc[2], fpScratchReg1);
	FMIN(32, fpSrc[2], fpSrc[2], const65535Reg);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
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

void VertexDecoderJitCache::Jit_PosS8MorphSkin() {
	Jit_AnyS8Morph(dec_->posoff, -1);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS16MorphSkin() {
	Jit_AnyS16Morph(dec_->posoff, -1);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosFloatMorphSkin() {
	Jit_AnyFloatMorph(dec_->posoff, -1);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_Color8888() {
	LWU(tempReg1, srcReg, dec_->coloff);

	// Set tempReg2=-1 if full alpha, 0 otherwise.
	SRLI(tempReg2, tempReg1, 24);
	SLTIU(tempReg2, tempReg2, 0xFF);
	ADDI(tempReg2, tempReg2, -1);

	// Now use that as a mask to clear fullAlpha.
	AND(fullAlphaReg, fullAlphaReg, tempReg2);

	SW(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color4444() {
	LHU(tempReg1, srcReg, dec_->coloff);

	// Red...
	ANDI(tempReg2, tempReg1, 0x0F);
	// Move green left to position 8.
	ANDI(tempReg3, tempReg1, 0xF0);
	SLLI(tempReg3, tempReg3, 4);
	OR(tempReg2, tempReg2, tempReg3);
	// For blue, we modify tempReg1 since immediates are sign extended after 11 bits.
	SRLI(tempReg1, tempReg1, 8);
	ANDI(tempReg3, tempReg1, 0x0F);
	SLLI(tempReg3, tempReg3, 16);
	OR(tempReg2, tempReg2, tempReg3);
	// And now alpha, moves 20 to get to 24.
	ANDI(tempReg3, tempReg1, 0xF0);
	SLLI(tempReg3, tempReg3, 20);
	OR(tempReg2, tempReg2, tempReg3);

	// Now we swizzle.
	SLLI(tempReg3, tempReg2, 4);
	OR(tempReg2, tempReg2, tempReg3);

	// Color is down, now let's say the fullAlphaReg flag from tempReg1 (still has alpha.)
	// Set tempReg1=-1 if full alpha, 0 otherwise.
	SLTIU(tempReg1, tempReg1, 0xF0);
	ADDI(tempReg1, tempReg1, -1);

	// Now use that as a mask to clear fullAlpha.
	AND(fullAlphaReg, fullAlphaReg, tempReg1);

	SW(tempReg2, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color565() {
	LHU(tempReg1, srcReg, dec_->coloff);

	// Start by extracting green.
	SRLI(tempReg2, tempReg1, 5);
	ANDI(tempReg2, tempReg2, 0x3F);
	// And now swizzle 6 -> 8, using a wall to clear bits.
	SRLI(tempReg3, tempReg2, 4);
	SLLI(tempReg3, tempReg3, 8);
	SLLI(tempReg2, tempReg2, 2 + 8);
	OR(tempReg2, tempReg2, tempReg3);

	// Now pull blue out using a wall to isolate it.
	SRLI(tempReg3, tempReg1, 11);
	// And now isolate red and combine them.
	ANDI(tempReg1, tempReg1, 0x1F);
	SLLI(tempReg3, tempReg3, 16);
	OR(tempReg1, tempReg1, tempReg3);
	// Now we swizzle them together.
	SRLI(tempReg3, tempReg1, 2);
	SLLI(tempReg1, tempReg1, 3);
	OR(tempReg1, tempReg1, tempReg3);
	// But we have to clear the bits now which is annoying.
	LI(tempReg3, 0x00FF00FF);
	AND(tempReg1, tempReg1, tempReg3);

	// Now add green back in, and then make an alpha FF and add it too.
	OR(tempReg1, tempReg1, tempReg2);
	LI(tempReg3, (s32)0xFF000000);
	OR(tempReg1, tempReg1, tempReg3);

	SW(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color5551() {
	LHU(tempReg1, srcReg, dec_->coloff);

	// Separate each color.
	SRLI(tempReg2, tempReg1, 5);
	SRLI(tempReg3, tempReg1, 10);

	// Set scratchReg to -1 if the alpha bit is set.
	SLLIW(scratchReg, tempReg1, 16);
	SRAIW(scratchReg, scratchReg, 31);
	// Now we can mask the flag.
	AND(fullAlphaReg, fullAlphaReg, scratchReg);

	// Let's move alpha into position.
	SLLI(scratchReg, scratchReg, 24);

	// Mask each.
	ANDI(tempReg1, tempReg1, 0x1F);
	ANDI(tempReg2, tempReg2, 0x1F);
	ANDI(tempReg3, tempReg3, 0x1F);
	// And shift into position.
	SLLI(tempReg2, tempReg2, 8);
	SLLI(tempReg3, tempReg3, 16);
	// Combine RGB together.
	OR(tempReg1, tempReg1, tempReg2);
	OR(tempReg1, tempReg1, tempReg3);
	// Swizzle our 5 -> 8
	SRLI(tempReg2, tempReg1, 2);
	SLLI(tempReg1, tempReg1, 3);
	// Mask out the overflow in tempReg2 and combine.
	LI(tempReg3, 0x00070707);
	AND(tempReg2, tempReg2, tempReg3);
	OR(tempReg1, tempReg1, tempReg2);

	// Add in alpha and we're done.
	OR(tempReg1, tempReg1, scratchReg);

	SW(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color8888Morph() {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + 0) * 4);
	LWU(tempReg1, srcReg, dec_->coloff);
	for (int i = 0; i < 3; ++i) {
		ANDI(tempReg2, tempReg1, 0xFF);
		FCVT(FConv::S, FConv::WU, fpSrc[i], tempReg2, Round::TOZERO);
		SRLI(tempReg1, tempReg1, 8);
		FMUL(32, fpSrc[i], fpSrc[i], fpScratchReg4);
	}
	FCVT(FConv::S, FConv::WU, fpSrc[3], tempReg1, Round::TOZERO);
	FMUL(32, fpSrc[3], fpSrc[3], fpScratchReg4);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
		LWU(tempReg1, srcReg, dec_->onesize_ * n + dec_->coloff);
		for (int i = 0; i < 3; ++i) {
			ANDI(tempReg2, tempReg1, 0xFF);
			FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg2, Round::TOZERO);
			SRLI(tempReg1, tempReg1, 8);
			FMADD(32, fpSrc[i], fpScratchReg1, fpScratchReg4, fpSrc[i]);
		}
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FMADD(32, fpSrc[3], fpScratchReg1, fpScratchReg4, fpSrc[3]);
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off, true);
}

void VertexDecoderJitCache::Jit_Color4444Morph() {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::COLOR_4 * 8 + 0) * 4);
	LHU(tempReg1, srcReg, dec_->coloff);
	for (int i = 0; i < 3; ++i) {
		ANDI(tempReg2, tempReg1, 0xF);
		FCVT(FConv::S, FConv::WU, fpSrc[i], tempReg2, Round::TOZERO);
		SRLI(tempReg1, tempReg1, 4);
		FMUL(32, fpSrc[i], fpSrc[i], fpScratchReg4, Round::TOZERO);
	}
	FCVT(FConv::S, FConv::WU, fpSrc[3], tempReg1, Round::TOZERO);
	FMUL(32, fpSrc[3], fpSrc[3], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::COLOR_4 * 8 + n) * 4);
		LHU(tempReg1, srcReg, dec_->onesize_ * n + dec_->coloff);
		for (int i = 0; i < 3; ++i) {
			ANDI(tempReg2, tempReg1, 0xF);
			FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg2, Round::TOZERO);
			SRLI(tempReg1, tempReg1, 4);
			FMADD(32, fpSrc[i], fpScratchReg1, fpScratchReg4, fpSrc[i]);
		}
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FMADD(32, fpSrc[3], fpScratchReg1, fpScratchReg4, fpSrc[3]);
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off, true);
}

void VertexDecoderJitCache::Jit_Color565Morph() {
	FL(32, fpScratchReg3, morphBaseReg, ((int)MorphValuesIndex::COLOR_5 * 8 + 0) * 4);
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::COLOR_6 * 8 + 0) * 4);
	LHU(tempReg1, srcReg, dec_->coloff);

	ANDI(tempReg2, tempReg1, 0x1F);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg2, Round::TOZERO);
	SRLI(tempReg1, tempReg1, 5);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg3, Round::TOZERO);

	ANDI(tempReg2, tempReg1, 0x3F);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	SRLI(tempReg1, tempReg1, 6);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);

	FCVT(FConv::S, FConv::WU, fpSrc[2], tempReg1, Round::TOZERO);
	FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg3, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg3, morphBaseReg, ((int)MorphValuesIndex::COLOR_5 * 8 + n) * 4);
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::COLOR_6 * 8 + n) * 4);
		LHU(tempReg1, srcReg, dec_->onesize_ * n + dec_->coloff);

		ANDI(tempReg2, tempReg1, 0x1F);
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg2, Round::TOZERO);
		SRLI(tempReg1, tempReg1, 5);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg3, fpSrc[0]);

		ANDI(tempReg2, tempReg1, 0x3F);
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg2, Round::TOZERO);
		SRLI(tempReg1, tempReg1, 6);
		FMADD(32, fpSrc[1], fpScratchReg1, fpScratchReg4, fpSrc[1]);

		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FMADD(32, fpSrc[2], fpScratchReg1, fpScratchReg3, fpSrc[2]);
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off, false);
}

void VertexDecoderJitCache::Jit_Color5551Morph() {
	FL(32, fpScratchReg3, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + 0) * 4);
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::COLOR_5 * 8 + 0) * 4);
	LHU(tempReg1, srcReg, dec_->coloff);
	for (int i = 0; i < 3; ++i) {
		ANDI(tempReg2, tempReg1, 0x1F);
		FCVT(FConv::S, FConv::WU, fpSrc[i], tempReg2, Round::TOZERO);
		SRLI(tempReg1, tempReg1, 5);
		FMUL(32, fpSrc[i], fpSrc[i], fpScratchReg4, Round::TOZERO);
	}

	// We accumulate alpha to [0, 1] and then scale up to 255 later.
	FCVT(FConv::S, FConv::WU, fpSrc[3], tempReg1, Round::TOZERO);
	FMUL(32, fpSrc[3], fpSrc[3], fpScratchReg3, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg3, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::COLOR_5 * 8 + n) * 4);
		LHU(tempReg1, srcReg, dec_->onesize_ * n + dec_->coloff);
		for (int i = 0; i < 3; ++i) {
			ANDI(tempReg2, tempReg1, 0x1F);
			FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg2, Round::TOZERO);
			SRLI(tempReg1, tempReg1, 5);
			FMADD(32, fpSrc[i], fpScratchReg1, fpScratchReg4, fpSrc[i]);
		}
		FCVT(FConv::S, FConv::WU, fpScratchReg1, tempReg1, Round::TOZERO);
		FMADD(32, fpSrc[3], fpScratchReg1, fpScratchReg3, fpSrc[3]);
	}

	LI(scratchReg, 255.0f);
	FMV(FMv::W, FMv::X, fpScratchReg2, scratchReg);
	FMUL(32, fpSrc[3], fpSrc[3], fpScratchReg2, Round::TOZERO);

	Jit_WriteMorphColor(dec_->decFmt.c0off, true);
}

void VertexDecoderJitCache::Jit_WriteMorphColor(int outOff, bool checkAlpha) {
	if (cpu_info.RiscV_Zbb) {
		LI(scratchReg, 0xFF);
		FCVT(FConv::WU, FConv::S, tempReg1, fpSrc[0], Round::TOZERO);
		MAX(tempReg1, tempReg1, R_ZERO);
		MIN(tempReg1, tempReg1, scratchReg);
		for (int i = 1; i < (checkAlpha ? 4 : 3); ++i) {
			FCVT(FConv::WU, FConv::S, tempReg2, fpSrc[i], Round::TOZERO);
			MAX(tempReg2, tempReg2, R_ZERO);
			MIN(tempReg2, tempReg2, scratchReg);
			// If it's alpha, set tempReg3 as a flag.
			if (i == 3)
				SLTIU(tempReg3, tempReg2, 0xFF);
			SLLI(tempReg2, tempReg2, i * 8);
			OR(tempReg1, tempReg1, tempReg2);
		}

		if (!checkAlpha) {
			// For 565 only, take our 0xFF constant above and slot it into alpha.
			SLLI(scratchReg, scratchReg, 24);
			OR(tempReg1, tempReg1, scratchReg);
		}
	} else {
		// Clamp to [0, 255] as floats, since we have FMIN/FMAX.  Better than branching, probably...
		LI(scratchReg, 255.0f);
		FMV(FMv::W, FMv::X, fpScratchReg1, R_ZERO);
		FMV(FMv::W, FMv::X, fpScratchReg2, scratchReg);
		for (int i = 0; i < (checkAlpha ? 4 : 3); ++i) {
			FMAX(32, fpSrc[i], fpSrc[i], fpScratchReg1);
			FMIN(32, fpSrc[i], fpSrc[i], fpScratchReg2);
		}

		FCVT(FConv::WU, FConv::S, tempReg1, fpSrc[0], Round::TOZERO);
		for (int i = 1; i < (checkAlpha ? 4 : 3); ++i) {
			FCVT(FConv::WU, FConv::S, tempReg2, fpSrc[i], Round::TOZERO);
			// If it's alpha, set tempReg3 as a flag.
			if (i == 3)
				SLTIU(tempReg3, tempReg2, 0xFF);
			SLLI(tempReg2, tempReg2, i * 8);
			OR(tempReg1, tempReg1, tempReg2);
		}

		if (!checkAlpha) {
			// For 565 only, we need to force alpha to 0xFF.
			LI(scratchReg, (s32)0xFF000000);
			OR(tempReg1, tempReg1, scratchReg);
		}
	}

	if (checkAlpha) {
		// Now use the flag we set earlier to update fullAlphaReg.
		// We translate it to a mask, tempReg3=-1 if full alpha, 0 otherwise.
		ADDI(tempReg3, tempReg3, -1);
		AND(fullAlphaReg, fullAlphaReg, tempReg3);
	}

	SW(tempReg1, dstReg, outOff);
}

void VertexDecoderJitCache::Jit_AnyS8ToFloat(int srcoff) {
	LB(tempReg1, srcReg, srcoff + 0);
	LB(tempReg2, srcReg, srcoff + 1);
	LB(tempReg3, srcReg, srcoff + 2);
	FCVT(FConv::S, FConv::W, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], by128Reg);
	FMUL(32, fpSrc[1], fpSrc[1], by128Reg);
	FMUL(32, fpSrc[2], fpSrc[2], by128Reg);
}

void VertexDecoderJitCache::Jit_AnyS16ToFloat(int srcoff) {
	LH(tempReg1, srcReg, srcoff + 0);
	LH(tempReg2, srcReg, srcoff + 2);
	LH(tempReg3, srcReg, srcoff + 4);
	FCVT(FConv::S, FConv::W, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], by32768Reg);
	FMUL(32, fpSrc[1], fpSrc[1], by32768Reg);
	FMUL(32, fpSrc[2], fpSrc[2], by32768Reg);
}

void VertexDecoderJitCache::Jit_AnyU8ToFloat(int srcoff, u32 bits) {
	_dbg_assert_msg_((bits & ~(16 | 8)) == 0, "Bits must be a multiple of 8.");
	_dbg_assert_msg_(bits >= 8 && bits <= 24, "Bits must be a between 8 and 24.");

	LBU(tempReg1, srcReg, srcoff + 0);
	if (bits >= 16)
		LBU(tempReg2, srcReg, srcoff + 1);
	if (bits >= 24)
		LBU(tempReg3, srcReg, srcoff + 2);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	if (bits >= 16)
		FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	if (bits >= 24)
		FCVT(FConv::S, FConv::WU, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], by128Reg);
	if (bits >= 16)
		FMUL(32, fpSrc[1], fpSrc[1], by128Reg);
	if (bits >= 24)
		FMUL(32, fpSrc[2], fpSrc[2], by128Reg);
}

void VertexDecoderJitCache::Jit_AnyU16ToFloat(int srcoff, u32 bits) {
	_dbg_assert_msg_((bits & ~(32 | 16)) == 0, "Bits must be a multiple of 16.");
	_dbg_assert_msg_(bits >= 16 && bits <= 48, "Bits must be a between 16 and 48.");

	LHU(tempReg1, srcReg, srcoff + 0);
	if (bits >= 32)
		LHU(tempReg2, srcReg, srcoff + 2);
	if (bits >= 48)
		LHU(tempReg3, srcReg, srcoff + 4);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	if (bits >= 32)
		FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	if (bits >= 48)
		FCVT(FConv::S, FConv::WU, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], by32768Reg);
	if (bits >= 32)
		FMUL(32, fpSrc[1], fpSrc[1], by32768Reg);
	if (bits >= 48)
		FMUL(32, fpSrc[2], fpSrc[2], by32768Reg);
}

void VertexDecoderJitCache::Jit_AnyS8Morph(int srcoff, int dstoff) {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_128 * 8 + 0) * 4);
	LB(tempReg1, srcReg, srcoff + 0);
	LB(tempReg2, srcReg, srcoff + 1);
	LB(tempReg3, srcReg, srcoff + 2);
	FCVT(FConv::S, FConv::W, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_128 * 8 + n) * 4);
		LB(tempReg1, srcReg, dec_->onesize_ * n + srcoff + 0);
		LB(tempReg2, srcReg, dec_->onesize_ * n + srcoff + 1);
		LB(tempReg3, srcReg, dec_->onesize_ * n + srcoff + 2);
		FCVT(FConv::S, FConv::W, fpScratchReg1, tempReg1, Round::TOZERO);
		FCVT(FConv::S, FConv::W, fpScratchReg2, tempReg2, Round::TOZERO);
		FCVT(FConv::S, FConv::W, fpScratchReg3, tempReg3, Round::TOZERO);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
		FMADD(32, fpSrc[2], fpScratchReg3, fpScratchReg4, fpSrc[2]);
	}

	if (dstoff >= 0) {
		FS(32, fpSrc[0], dstReg, dstoff + 0);
		FS(32, fpSrc[1], dstReg, dstoff + 4);
		FS(32, fpSrc[2], dstReg, dstoff + 8);
	}
}

void VertexDecoderJitCache::Jit_AnyS16Morph(int srcoff, int dstoff) {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_32768 * 8 + 0) * 4);
	LH(tempReg1, srcReg, srcoff + 0);
	LH(tempReg2, srcReg, srcoff + 2);
	LH(tempReg3, srcReg, srcoff + 4);
	FCVT(FConv::S, FConv::W, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::BY_32768 * 8 + n) * 4);
		LH(tempReg1, srcReg, dec_->onesize_ * n + srcoff + 0);
		LH(tempReg2, srcReg, dec_->onesize_ * n + srcoff + 2);
		LH(tempReg3, srcReg, dec_->onesize_ * n + srcoff + 4);
		FCVT(FConv::S, FConv::W, fpScratchReg1, tempReg1, Round::TOZERO);
		FCVT(FConv::S, FConv::W, fpScratchReg2, tempReg2, Round::TOZERO);
		FCVT(FConv::S, FConv::W, fpScratchReg3, tempReg3, Round::TOZERO);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
		FMADD(32, fpSrc[2], fpScratchReg3, fpScratchReg4, fpSrc[2]);
	}

	if (dstoff >= 0) {
		FS(32, fpSrc[0], dstReg, dstoff + 0);
		FS(32, fpSrc[1], dstReg, dstoff + 4);
		FS(32, fpSrc[2], dstReg, dstoff + 8);
	}
}

void VertexDecoderJitCache::Jit_AnyFloatMorph(int srcoff, int dstoff) {
	FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + 0) * 4);
	FL(32, fpSrc[0], srcReg, srcoff + 0);
	FL(32, fpSrc[1], srcReg, srcoff + 4);
	FL(32, fpSrc[2], srcReg, srcoff + 8);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg4, Round::TOZERO);
	FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg4, Round::TOZERO);

	for (int n = 1; n < dec_->morphcount; n++) {
		FL(32, fpScratchReg4, morphBaseReg, ((int)MorphValuesIndex::AS_FLOAT * 8 + n) * 4);
		FL(32, fpScratchReg1, srcReg, dec_->onesize_ * n + srcoff + 0);
		FL(32, fpScratchReg2, srcReg, dec_->onesize_ * n + srcoff + 4);
		FL(32, fpScratchReg3, srcReg, dec_->onesize_ * n + srcoff + 8);
		FMADD(32, fpSrc[0], fpScratchReg1, fpScratchReg4, fpSrc[0]);
		FMADD(32, fpSrc[1], fpScratchReg2, fpScratchReg4, fpSrc[1]);
		FMADD(32, fpSrc[2], fpScratchReg3, fpScratchReg4, fpSrc[2]);
	}

	if (dstoff >= 0) {
		FS(32, fpSrc[0], dstReg, dstoff + 0);
		FS(32, fpSrc[1], dstReg, dstoff + 4);
		FS(32, fpSrc[2], dstReg, dstoff + 8);
	}
}

void VertexDecoderJitCache::Jit_WriteMatrixMul(int dstoff, bool pos) {
	const RiscVReg fpDst[3] = { fpScratchReg1, fpScratchReg2, fpScratchReg3 };

	// When using morph + skin, we don't keep skinMatrix in a reg.
	RiscVReg skinMatrixReg = morphBaseReg;
	if (dec_->morphcount > 1) {
		LI(scratchReg, &skinMatrix[0]);
		skinMatrixReg = scratchReg;
	}

	// First, take care of the 3x3 portion of the matrix.
	for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) {
			FL(32, fpScratchReg4, skinMatrixReg, (y * 3 + x) * 4);
			if (y == 0)
				FMUL(32, fpDst[x], fpSrc[y], fpScratchReg4);
			else
				FMADD(32, fpDst[x], fpSrc[y], fpScratchReg4, fpDst[x]);
		}
	}

	// For normal, z is 0 so we skip.
	if (pos) {
		for (int x = 0; x < 3; ++x)
			FL(32, fpSrc[x], skinMatrixReg, (9 + x) * 4);
		for (int x = 0; x < 3; ++x)
			FADD(32, fpDst[x], fpDst[x], fpSrc[x]);
	}

	for (int x = 0; x < 3; ++x)
		FS(32, fpDst[x], dstReg, dstoff + x * 4);
}

#endif // PPSSPP_ARCH(RISCV64)
