// Copyright (c) 2025- PPSSPP Project.

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
#if PPSSPP_ARCH(LOONGARCH64)

#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Common/LoongArch64Emitter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/VertexDecoderCommon.h"

alignas(16) static float bones[16 * 8];

alignas(16) static const float by128_11[4] = {
	1.0f / 128.0f, 1.0f / 128.0f, 1.0f, 1.0f,
};

alignas(16) static const float by32768_11[4] = {
	1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f, 1.0f,
};

static const float by128 = 1.0f / 128.0f;
static const float by32768 = 1.0f / 32768.0f;
static const float const65535 = 65535.0f;

using namespace LoongArch64Gen;

static const LoongArch64Reg srcReg = R4;        // a0
static const LoongArch64Reg dstReg = R5;        // a1
static const LoongArch64Reg counterReg = R6;    // a2

static const LoongArch64Reg tempReg1 = R7;      // a3
static const LoongArch64Reg tempReg2 = R8;      // a4
static const LoongArch64Reg tempReg3 = R9;      // a5
static const LoongArch64Reg scratchReg = R10;   // a6

static const LoongArch64Reg morphBaseReg = R12; // t0

static const LoongArch64Reg fullAlphaReg = R11; // a7
static const LoongArch64Reg boundsMinUReg = R17;
static const LoongArch64Reg boundsMinVReg = R18;
static const LoongArch64Reg boundsMaxUReg = R19;
static const LoongArch64Reg boundsMaxVReg = R20;

static const LoongArch64Reg fpScratchReg = F4;
static const LoongArch64Reg fpScratchReg2 = F5;
static const LoongArch64Reg fpScratchReg3 = F6;
static const LoongArch64Reg fpScratchReg4 = F7;

static const LoongArch64Reg lsxScratchReg = V2;
static const LoongArch64Reg lsxScratchReg2 = V3;
static const LoongArch64Reg lsxScratchReg3 = V10;
static const LoongArch64Reg lsxScratchReg4 = V11;

static const LoongArch64Reg fpSrc[4] = {F2, F3, F10, F11};

static const LoongArch64Reg lsxScaleOffsetReg = V0;
static const LoongArch64Reg lsxOffsetScaleReg = V1;

static const LoongArch64Reg srcLSX = V8;
static const LoongArch64Reg accLSX = V9;

static const LoongArch64Reg by128LSX = V14;
static const LoongArch64Reg by32768LSX = V15;

static const LoongArch64Reg lsxWeightRegs[2] = { V12, V13 };

// We need to save these fregs when using them. (for example, skinning)
static constexpr LoongArch64Reg regs_to_save_fp[]{ F24, F25, F26, F27, F28, F29, F30, F31 };

// V4-V7 is the generated matrix that we multiply things by.
// V8, V9 are accumulators/scratch for matrix mul.
// V10, V11 are more scratch for matrix mul.
// V12, V13 are weight regs.
// V14, V15 are by128 and by32768 regs.
// V16+ are free-for-all for matrices. In 16 registers, we can fit 4 4x4 matrices.

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoder::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoder::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},
	{&VertexDecoder::Step_WeightsU8Skin, &VertexDecoderJitCache::Jit_WeightsU8Skin},
	{&VertexDecoder::Step_WeightsU16Skin, &VertexDecoderJitCache::Jit_WeightsU16Skin},
	{&VertexDecoder::Step_WeightsFloatSkin, &VertexDecoderJitCache::Jit_WeightsFloatSkin},

	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
	{&VertexDecoder::Step_TcU8ToFloat, &VertexDecoderJitCache::Jit_TcU8ToFloat},
	{&VertexDecoder::Step_TcU16ToFloat, &VertexDecoderJitCache::Jit_TcU16ToFloat},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},

	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},
	{&VertexDecoder::Step_TcU16ThroughToFloat, &VertexDecoderJitCache::Jit_TcU16ThroughToFloat},

	{&VertexDecoder::Step_TcU8MorphToFloat, &VertexDecoderJitCache::Jit_TcU8MorphToFloat},
	{&VertexDecoder::Step_TcU16MorphToFloat, &VertexDecoderJitCache::Jit_TcU16MorphToFloat},
	{&VertexDecoder::Step_TcFloatMorph, &VertexDecoderJitCache::Jit_TcFloatMorph},
	{&VertexDecoder::Step_TcU8PrescaleMorph, &VertexDecoderJitCache::Jit_TcU8PrescaleMorph},
	{&VertexDecoder::Step_TcU16PrescaleMorph, &VertexDecoderJitCache::Jit_TcU16PrescaleMorph},
	{&VertexDecoder::Step_TcFloatPrescaleMorph, &VertexDecoderJitCache::Jit_TcFloatPrescaleMorph},

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
	{&VertexDecoder::Step_NormalS8MorphSkin, &VertexDecoderJitCache::Jit_NormalS8MorphSkin},
	{&VertexDecoder::Step_NormalS16MorphSkin, &VertexDecoderJitCache::Jit_NormalS16MorphSkin},
	{&VertexDecoder::Step_NormalFloatMorphSkin, &VertexDecoderJitCache::Jit_NormalFloatMorphSkin},

	{&VertexDecoder::Step_PosS8Morph, &VertexDecoderJitCache::Jit_PosS8Morph},
	{&VertexDecoder::Step_PosS16Morph, &VertexDecoderJitCache::Jit_PosS16Morph},
	{&VertexDecoder::Step_PosFloatMorph, &VertexDecoderJitCache::Jit_PosFloatMorph},
	{&VertexDecoder::Step_PosS8MorphSkin, &VertexDecoderJitCache::Jit_PosS8MorphSkin},
	{&VertexDecoder::Step_PosS16MorphSkin, &VertexDecoderJitCache::Jit_PosS16MorphSkin},
	{&VertexDecoder::Step_PosFloatMorphSkin, &VertexDecoderJitCache::Jit_PosFloatMorphSkin},

	{&VertexDecoder::Step_Color8888Morph, &VertexDecoderJitCache::Jit_Color8888Morph},
	{&VertexDecoder::Step_Color4444Morph, &VertexDecoderJitCache::Jit_Color4444Morph},
	{&VertexDecoder::Step_Color565Morph, &VertexDecoderJitCache::Jit_Color565Morph},
	{&VertexDecoder::Step_Color5551Morph, &VertexDecoderJitCache::Jit_Color5551Morph},
};

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec, int32_t *jittedSize) {
	dec_ = &dec;

	BeginWrite(4096);
	const u8 *start = AlignCode16();

    int saveSize = (64 / 8) * (int)ARRAY_SIZE(regs_to_save_fp);
    int saveOffset = 0;

	bool log = false;
	bool prescaleStep = false;
    bool updateTexBounds = false;
	bool posThroughStep = false;

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
        if (dec.steps_[i] == &VertexDecoder::Step_TcU16ThroughToFloat) {
			updateTexBounds = true;
		}
	}

    // Set rounding mode to RZ (Rounding to Zero)
	SLTUI(scratchReg, R_ZERO, 1);
	MOVGR2FCSR(FCSR3, scratchReg);

    QuickFLI(32, F14, by128, scratchReg);
	QuickFLI(32, F15, by32768, scratchReg);
    VREPLVEI_W(by128LSX, by128LSX, 0);
    VREPLVEI_W(by32768LSX, by32768LSX, 0);

    // We need to save callee saved fregs when skinning
    if (dec.skinInDecode) {
        if (saveSize & 0xF)
		    saveSize += 8;
        _assert_msg_((saveSize & 0xF) == 0, "Stack must be kept aligned");
        ADDI_D(R_SP, R_SP, -saveSize);
        for (LoongArch64Reg r : regs_to_save_fp) {
            FST_D(r, R_SP, saveOffset);
            saveOffset += 64 / 8;
        }
        _assert_(saveOffset <= saveSize);
    }

    // Keep the scale/offset in a few fp registers if we need it.
	if (prescaleStep) {
		VLD(lsxScaleOffsetReg, R7, 0);
        if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
			LI(scratchReg, &by128_11[0]);
			VLD(lsxScratchReg, scratchReg, 0);
			VFMUL_S(lsxScaleOffsetReg, lsxScaleOffsetReg, lsxScratchReg);
		} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
			LI(scratchReg, &by32768_11[0]);
			VLD(lsxScratchReg, scratchReg, 0);
			VFMUL_S(lsxScaleOffsetReg, lsxScaleOffsetReg, lsxScratchReg);
        }
        VSHUF4I_W(lsxOffsetScaleReg, lsxScaleOffsetReg, (1 << 6 | 0 << 4 | 3 << 2 | 2));
	}

    // Add code to convert matrices to 4x4.
    // Later we might want to do this when the matrices are loaded instead.
    if (dec.skinInDecode) {
		// Copying from R7 to R8
		LI(R7, &gstate.boneMatrix[0]);
		// This is only used with more than 4 weights, and points to the first of them.
		if (dec.nweights > 4)
			LI(R8, &bones[16 * 4]);

		// Construct a mask to zero out the top lane with.
		VOR_V(V3, V3, V3);
		VORN_V(V3, V3, V3);
        VINSGR2VR_W(V3, LoongArch64Gen::R_ZERO, 3);

		for (int i = 0; i < dec.nweights; i++) {
			// This loads V4, V5, V6, V7 with 12 floats.
            // And sort those floats into 4 regs: ABCD EFGH IJKL -> ABC0 DEF0 GHI0 JKL0.
            // TODO: Is unaligned load worth it?
            VLD(V4, R7, 0);
            VLD(V5, R7, 12);
            VLD(V6, R7, 24);
            VLD(V7, R7,36);
            ADDI_D(R7, R7, 48);

			LoongArch64Reg matrixRow[4]{ V4, V5, V6, V7 };
			// First four matrices are in registers Q16+.
			if (i < 4) {
				for (int w = 0; w < 4; ++w)
					matrixRow[w] = (LoongArch64Reg)(V16 + i * 4 + w);
			}
			// Zero out the top lane of each one with the mask created above.
			VAND_V(matrixRow[0], V4, V3);
			VAND_V(matrixRow[1], V5, V3);
			VAND_V(matrixRow[2], V6, V3);
			VAND_V(matrixRow[3], V7, V3);

			if (i >= 4) {
                VST(matrixRow[0], R8, 0);
                VST(matrixRow[1], R8, 16);
                VST(matrixRow[2], R8, 32);
                VST(matrixRow[3], R8, 48);
                ADDI_D(R8, R8, 64);
            }
		}
	}

    if (dec.col) {
        // Or LB and skip the conditional?  This is probably cheaper.
        LI(fullAlphaReg, 0xFF);
    }

    if (updateTexBounds) {
        LI(tempReg1, &gstate_c.vertBounds.minU);
        LD_H(boundsMinUReg, tempReg1, offsetof(KnownVertexBounds, minU));
        LD_H(boundsMaxUReg, tempReg1, offsetof(KnownVertexBounds, maxU));
        LD_H(boundsMinVReg, tempReg1, offsetof(KnownVertexBounds, minV));
        LD_H(boundsMaxVReg, tempReg1, offsetof(KnownVertexBounds, maxV));
    }

    const u8 *loopStart = GetCodePtr();
    for (int i = 0; i < dec.numSteps_; i++) {
        if (!CompileStep(dec, i)) {
            EndWrite();
            // Reset the code ptr (effectively undoing what we generated) and return zero to indicate that we failed.
            ResetCodePtr(GetOffset(start));
            char temp[1024]{};
            dec.ToString(temp, true);
            WARN_LOG(Log::G3D, "Could not compile vertex decoder, failed at step %s: %s", GetStepFunctionName(dec.steps_[i]), temp);
            return nullptr;
        }
    }

    ADDI_D(srcReg, srcReg, dec.VertexSize());
    ADDI_D(dstReg, dstReg, dec.decFmt.stride);
    ADDI_D(counterReg, counterReg, -1);
    BLT(R_ZERO, counterReg, loopStart);

    if (dec.col) {
        LI(tempReg1, &gstate_c.vertexFullAlpha);
        FixupBranch skip = BNEZ(fullAlphaReg);
        ST_B(fullAlphaReg, tempReg1, 0);
        SetJumpTarget(skip);
    }

    if (updateTexBounds) {
        LI(tempReg1, &gstate_c.vertBounds.minU);
        ST_H(boundsMinUReg, tempReg1, offsetof(KnownVertexBounds, minU));
        ST_H(boundsMaxUReg, tempReg1, offsetof(KnownVertexBounds, maxU));
        ST_H(boundsMinVReg, tempReg1, offsetof(KnownVertexBounds, minV));
        ST_H(boundsMaxVReg, tempReg1, offsetof(KnownVertexBounds, maxV));
    }

    if (dec.skinInDecode) {
        saveOffset = 0;
        for (LoongArch64Reg r : regs_to_save_fp) {
            FLD_D(r, R_SP, saveOffset);
            saveOffset += 64 / 8;
        }
        ADDI_D(R_SP, R_SP, saveSize);
    }

	RET();

	FlushIcache();

	if (log) {
		char temp[1024]{};
		dec.ToString(temp, true);
		INFO_LOG(Log::JIT, "=== %s (%d bytes) ===", temp, (int)(GetCodePtr() - start));
		std::vector<std::string> lines = DisassembleLA64(start, (int)(GetCodePtr() - start));
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

void VertexDecoderJitCache::Jit_ApplyWeights() {
	// We construct a matrix in V4-V7
	if (dec_->nweights > 4) {
		LI(scratchReg, bones + 16 * 4);
	}
	for (int i = 0; i < dec_->nweights; i++) {
		switch (i) {
		case 0:
            VREPLVEI_W(lsxScratchReg, lsxWeightRegs[0], 0);
			VFMUL_S(V4, V16, lsxScratchReg);
			VFMUL_S(V5, V17, lsxScratchReg);
			VFMUL_S(V6, V18, lsxScratchReg);
			VFMUL_S(V7, V19, lsxScratchReg);
			break;
		case 1:
            VREPLVEI_W(lsxScratchReg, lsxWeightRegs[0], 1);
			VFMADD_S(V4, V20, lsxScratchReg, V4);
			VFMADD_S(V5, V21, lsxScratchReg, V5);
			VFMADD_S(V6, V22, lsxScratchReg, V6);
			VFMADD_S(V7, V23, lsxScratchReg, V7);
			break;
		case 2:
			VREPLVEI_W(lsxScratchReg, lsxWeightRegs[0], 2);
			VFMADD_S(V4, V24, lsxScratchReg, V4);
			VFMADD_S(V5, V25, lsxScratchReg, V5);
			VFMADD_S(V6, V26, lsxScratchReg, V6);
			VFMADD_S(V7, V27, lsxScratchReg, V7);
			break;
		case 3:
			VREPLVEI_W(lsxScratchReg, lsxWeightRegs[0], 3);
			VFMADD_S(V4, V28, lsxScratchReg, V4);
			VFMADD_S(V5, V29, lsxScratchReg, V5);
			VFMADD_S(V6, V30, lsxScratchReg, V6);
			VFMADD_S(V7, V31, lsxScratchReg, V7);
			break;
		default:
			// Matrices 4+ need to be loaded from memory.
			VLD(V8, scratchReg, 0);
            VLD(V9, scratchReg, 16);
            VLD(V10, scratchReg, 32);
            VLD(V11, scratchReg, 48);
            ADDI_D(scratchReg, scratchReg, 64);
            VREPLVEI_W(lsxScratchReg, lsxWeightRegs[i >> 2], i & 3);
			VFMADD_S(V4, V8, lsxScratchReg, V4);
			VFMADD_S(V5, V9, lsxScratchReg, V5);
			VFMADD_S(V6, V10, lsxScratchReg, V6);
			VFMADD_S(V7, V11, lsxScratchReg, V7);
			break;
		}
	}
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	// Basic implementation - a byte at a time.
    // TODO: Could optimize with unaligned load/store
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LD_B(tempReg1, srcReg, dec_->weightoff + j);
		ST_B(tempReg1, dstReg, dec_->decFmt.w0off + j);
	}
	while (j & 3) {
		ST_B(R_ZERO, dstReg, dec_->decFmt.w0off + j);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	// Basic implementation - a short at a time.
    // TODO: Could optimize with unaligned load/store
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LD_H(tempReg1, srcReg, dec_->weightoff + j * 2);
		ST_H(tempReg1, dstReg, dec_->decFmt.w0off + j * 2);
	}
	while (j & 3) {
		ST_H(R_ZERO, dstReg, dec_->decFmt.w0off + j * 2);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LD_W(tempReg1, srcReg, dec_->weightoff + j * 4);
		ST_W(tempReg1, dstReg, dec_->decFmt.w0off + j * 4);
	}
	while (j & 3) {  // Zero additional weights rounding up to 4.
		ST_W(R_ZERO, dstReg, dec_->decFmt.w0off + j * 4);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
	// Weight is first so srcReg is correct.
	switch (dec_->nweights) {
	case 1: LD_BU(scratchReg, srcReg, 0); break;
	case 2: LD_HU(scratchReg, srcReg, 0); break;
	default:
		// For 3, we over read, for over 4, we read more later.
		LD_WU(scratchReg, srcReg, 0);
		break;
	}

    VINSGR2VR_D(lsxScratchReg, scratchReg, 0);
	VSLLWIL_HU_BU(lsxScratchReg, lsxScratchReg, 0);
	VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0);
	VFFINT_S_WU(lsxWeightRegs[0], lsxScratchReg);
    VFMUL_S(lsxWeightRegs[0], lsxWeightRegs[0], by128LSX);

	if (dec_->nweights > 4) {
		switch (dec_->nweights) {
		case 5: LD_BU(scratchReg, srcReg, 4); break;
	    case 6: LD_HU(scratchReg, srcReg, 4); break;
		case 7:
		case 8:
			LD_WU(scratchReg, srcReg, 4);
			break;
		}
        VINSGR2VR_D(lsxScratchReg, scratchReg, 0);
		VSLLWIL_HU_BU(lsxScratchReg, lsxScratchReg, 0);
	    VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0);
		VFFINT_S_WU(lsxWeightRegs[1], lsxScratchReg);
        VFMUL_S(lsxWeightRegs[1], lsxWeightRegs[1], by128LSX);
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
	switch (dec_->nweights) {
	case 1: LD_HU(scratchReg, srcReg, 0); break;
	case 2: LD_WU(scratchReg, srcReg, 0); break;
	default:
		// For 3, we over read, for over 4, we read more later.
		LD_D(scratchReg, srcReg, 0);
		break;
	}
    VINSGR2VR_D(lsxScratchReg, scratchReg, 0);
	VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0);
	VFFINT_S_WU(lsxWeightRegs[0], lsxScratchReg);
    VFMUL_S(lsxWeightRegs[0], lsxWeightRegs[0], by32768LSX);

	if (dec_->nweights > 4) {
		switch (dec_->nweights) {
		case 5: LD_HU(scratchReg, srcReg, 0); break;
	    case 6: LD_WU(scratchReg, srcReg, 0); break;
		case 7:
		case 8:
			LD_D(scratchReg, srcReg, 0);
			break;
		}
		VINSGR2VR_D(lsxScratchReg, scratchReg, 0);
        VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0);
        VFFINT_S_WU(lsxWeightRegs[1], lsxScratchReg);
        VFMUL_S(lsxWeightRegs[1], lsxWeightRegs[1], by32768LSX);
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
	switch (dec_->nweights) {
	case 1:
        FLD_S(F12, srcReg, 0); // Load 32-bits to lsxWeightRegs[0]
		break;
	case 2:
        FLD_D(F12, srcReg, 0); // Load 64-bits to lsxWeightRegs[0]
		break;
	case 3:
	case 4:
		VLD(lsxWeightRegs[0], srcReg, 0);
		break;

	case 5:
		VLD(lsxWeightRegs[0], srcReg, 0);
		FLD_S(F13, srcReg, 16); // Load 32-bits to lsxWeightRegs[1]
		break;
	case 6:
		VLD(lsxWeightRegs[0], srcReg, 0);
		FLD_D(F13, srcReg, 16); // Load 64-bits to lsxWeightRegs[1]
		break;
	case 7:
	case 8:
		VLD(lsxWeightRegs[0], srcReg, 0);
		VLD(lsxWeightRegs[1], srcReg, 16);
		break;
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_Color8888() {
	LD_WU(tempReg1, srcReg, dec_->coloff);

	// Set tempReg2=-1 if full alpha, 0 otherwise.
	SRLI_D(tempReg2, tempReg1, 24);
	SLTUI(tempReg2, tempReg2, 0xFF);
	ADDI_D(tempReg2, tempReg2, -1);

	// Now use that as a mask to clear fullAlpha.
	AND(fullAlphaReg, fullAlphaReg, tempReg2);

	ST_W(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color4444() {
	LD_HU(tempReg1, srcReg, dec_->coloff);

	// Red...
	ANDI(tempReg2, tempReg1, 0x0F);
	// Move green left to position 8.
	ANDI(tempReg3, tempReg1, 0xF0);
	SLLI_D(tempReg3, tempReg3, 4);
	OR(tempReg2, tempReg2, tempReg3);
	// For blue, we modify tempReg1 since immediates are sign extended after 11 bits.
	SRLI_D(tempReg1, tempReg1, 8);
	ANDI(tempReg3, tempReg1, 0x0F);
	SLLI_D(tempReg3, tempReg3, 16);
	OR(tempReg2, tempReg2, tempReg3);
	// And now alpha, moves 20 to get to 24.
	ANDI(tempReg3, tempReg1, 0xF0);
	SLLI_D(tempReg3, tempReg3, 20);
	OR(tempReg2, tempReg2, tempReg3);

	// Now we swizzle.
	SLLI_D(tempReg3, tempReg2, 4);
	OR(tempReg2, tempReg2, tempReg3);

	// Color is down, now let's say the fullAlphaReg flag from tempReg1 (still has alpha.)
	// Set tempReg1=-1 if full alpha, 0 otherwise.
	SLTUI(tempReg1, tempReg1, 0xF0);
	ADDI_D(tempReg1, tempReg1, -1);

	// Now use that as a mask to clear fullAlpha.
	AND(fullAlphaReg, fullAlphaReg, tempReg1);

	ST_W(tempReg2, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color565() {
	LD_HU(tempReg1, srcReg, dec_->coloff);

	// Start by extracting green.
	SRLI_D(tempReg2, tempReg1, 5);
	ANDI(tempReg2, tempReg2, 0x3F);
	// And now swizzle 6 -> 8, using a wall to clear bits.
	SRLI_D(tempReg3, tempReg2, 4);
	SLLI_D(tempReg3, tempReg3, 8);
	SLLI_D(tempReg2, tempReg2, 2 + 8);
	OR(tempReg2, tempReg2, tempReg3);

	// Now pull blue out using a wall to isolate it.
	SRLI_D(tempReg3, tempReg1, 11);
	// And now isolate red and combine them.
	ANDI(tempReg1, tempReg1, 0x1F);
	SLLI_D(tempReg3, tempReg3, 16);
	OR(tempReg1, tempReg1, tempReg3);
	// Now we swizzle them together.
	SRLI_D(tempReg3, tempReg1, 2);
	SLLI_D(tempReg1, tempReg1, 3);
	OR(tempReg1, tempReg1, tempReg3);
	// But we have to clear the bits now which is annoying.
	LI(tempReg3, 0x00FF00FF);
	AND(tempReg1, tempReg1, tempReg3);

	// Now add green back in, and then make an alpha FF and add it too.
	OR(tempReg1, tempReg1, tempReg2);
	LI(tempReg3, (s32)0xFF000000);
	OR(tempReg1, tempReg1, tempReg3);

	ST_W(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color5551() {
	LD_HU(tempReg1, srcReg, dec_->coloff);

	// Separate each color.
	SRLI_D(tempReg2, tempReg1, 5);
	SRLI_D(tempReg3, tempReg1, 10);

	// Set scratchReg to -1 if the alpha bit is set.
	SLLI_W(scratchReg, tempReg1, 16);
	SRAI_W(scratchReg, scratchReg, 31);
	// Now we can mask the flag.
	AND(fullAlphaReg, fullAlphaReg, scratchReg);

	// Let's move alpha into position.
	SLLI_D(scratchReg, scratchReg, 24);

	// Mask each.
	ANDI(tempReg1, tempReg1, 0x1F);
	ANDI(tempReg2, tempReg2, 0x1F);
	ANDI(tempReg3, tempReg3, 0x1F);
	// And shift into position.
	SLLI_D(tempReg2, tempReg2, 8);
	SLLI_D(tempReg3, tempReg3, 16);
	// Combine RGB together.
	OR(tempReg1, tempReg1, tempReg2);
	OR(tempReg1, tempReg1, tempReg3);
	// Swizzle our 5 -> 8
	SRLI_D(tempReg2, tempReg1, 2);
	SLLI_D(tempReg1, tempReg1, 3);
	// Mask out the overflow in tempReg2 and combine.
	LI(tempReg3, 0x00070707);
	AND(tempReg2, tempReg2, tempReg3);
	OR(tempReg1, tempReg1, tempReg2);

	// Add in alpha and we're done.
	OR(tempReg1, tempReg1, scratchReg);

	ST_W(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color8888Morph() {
	LI(tempReg1, &gstate_c.morphWeights[0]);
	VXOR_V(lsxScratchReg4, lsxScratchReg4, lsxScratchReg4);

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg2;
		FLD_S((LoongArch64Reg)(DecodeReg(reg) + F0), srcReg, dec_->onesize_ * n + dec_->coloff);

		VILVL_B(reg, lsxScratchReg4, reg);
		VILVL_H(reg, lsxScratchReg4, reg);
		VFFINT_S_W(reg, reg);

		// And now the weight.
		VLDREPL_W(lsxScratchReg3, tempReg1, n * sizeof(float));
		VFMUL_S(reg, reg, lsxScratchReg3);

		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg,lsxScratchReg2);
		} else {
			first = false;
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color4444Morph() {
	LI(tempReg1, &gstate_c.morphWeights[0]);
	VXOR_V(lsxScratchReg4, lsxScratchReg4, lsxScratchReg4);

	LI(tempReg2, 0xf00ff00f); // color 4444 mask
	VREPLGR2VR_W(V8, tempReg2);
	LI(tempReg3, 255.0f / 15.0f); // by color 4444
	VREPLGR2VR_W(V9, tempReg2);

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg2;
		FLD_S((LoongArch64Reg)(DecodeReg(reg) + F0), srcReg, dec_->onesize_ * n + dec_->coloff);
		VILVL_B(reg, reg, reg);
		VAND_V(reg, reg, V8);
		VEXTRINS_W(lsxScratchReg3, reg, 0);
		VSLLI_H(lsxScratchReg3, lsxScratchReg3, 4);
		VOR_V(reg, reg,lsxScratchReg3);
		VSRLI_W(reg, reg, 4);

		VILVL_B(reg, lsxScratchReg4, reg);
		VILVL_H(reg, lsxScratchReg4, reg);

		VFFINT_S_W(reg, reg);
		VFMUL_S(reg, reg, V9);

		// And now the weight.
		VLDREPL_W(lsxScratchReg3, tempReg1, n * sizeof(float));
		VFMUL_S(reg, reg, lsxScratchReg3);

		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg,lsxScratchReg2);
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
	LI(tempReg1, &gstate_c.morphWeights[0]);
	LI(tempReg2, &color565Mask[0]);
	VLD(V8, tempReg2, 0);
	LI(tempReg2, &byColor565[0]);
	VLD(V9, tempReg2, 0);

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg3;
		// Spread it out into each lane.  We end up with it reversed (R high, A low.)
		// Below, we shift out each lane from low to high and reverse them.
		VLDREPL_W(lsxScratchReg2, srcReg, dec_->onesize_ * n + dec_->coloff);
		VAND_V(lsxScratchReg2, lsxScratchReg2, V8);

		// Alpha handled in Jit_WriteMorphColor.

		// Blue first.
		VEXTRINS_W(reg, lsxScratchReg2, 0);
		VSRLI_W(reg, reg, 6);
		VSHUF4I_W(reg, reg, 3 << 6);

		// Green, let's shift it into the right lane first.
		VEXTRINS_W(reg, lsxScratchReg2, 1);
		VSRLI_W(reg, reg, 5);
		VSHUF4I_W(reg, reg, (3 << 6 | 2 << 4));

		// Last one, red.
		VEXTRINS_W(reg, lsxScratchReg2, 2);
		VFFINT_S_W(reg, reg);
		VFMUL_S(reg, reg, V9);

		// And now the weight.
		VLDREPL_W(lsxScratchReg2, tempReg1, n * sizeof(float));
		VFMUL_S(reg, reg, lsxScratchReg2);

		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg, lsxScratchReg3);
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
	LI(tempReg1, &gstate_c.morphWeights[0]);
	LI(tempReg2, &color5551Mask[0]);
	VLD(V8, tempReg2, 0);
	LI(tempReg2, &byColor5551[0]);
	VLD(V9, tempReg2, 0);

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg3;
		// Spread it out into each lane.
		VLDREPL_W(lsxScratchReg2, srcReg, dec_->onesize_ * n + dec_->coloff);
		VAND_V(lsxScratchReg2, lsxScratchReg2, V8);

		// Alpha first.
		VEXTRINS_W(reg, lsxScratchReg2, 0);
		VSRLI_W(reg, reg, 5);
		VSHUF4I_W(reg, reg, 0);

		// Blue, let's shift it into the right lane first.
		VEXTRINS_W(reg, lsxScratchReg2, 1);
		VSRLI_W(reg, reg, 5);
		VSHUF4I_W(reg, reg, 3 << 6);

		// Green.
		VEXTRINS_W(reg, lsxScratchReg2, 2);
		VSRLI_W(reg, reg, 5);
		VSHUF4I_W(reg, reg, (3 << 6 | 2 << 4));

		// Last one, red.
		VEXTRINS_W(reg, lsxScratchReg2, 3);
		VFFINT_S_W(reg, reg);
		VFMUL_S(reg, reg, V9);

		// And now the weight.
		VLDREPL_W(lsxScratchReg2, tempReg1, n * sizeof(float));
		VFMUL_S(reg, reg, lsxScratchReg2);

		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg, lsxScratchReg3);
		} else {
			first = false;
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_WriteMorphColor(int outOff, bool checkAlpha) {
	// Pack back into a u32, with saturation.
	VFTINT_W_S(lsxScratchReg, lsxScratchReg);
	VSSRLNI_H_W(lsxScratchReg, lsxScratchReg, 0);
	VSSRLNI_BU_H(lsxScratchReg, lsxScratchReg, 0);
	VPICKVE2GR_W(tempReg1, lsxScratchReg, 0);

	// TODO: Could be optimize with a SLLI on fullAlphaReg
	SLLI_D(tempReg2, fullAlphaReg, 24);
	if (checkAlpha) {
		SLTU(tempReg3, tempReg1, tempReg2);
		FixupBranch skip = BEQZ(tempReg3);
		XOR(fullAlphaReg, fullAlphaReg, fullAlphaReg);
		SetJumpTarget(skip);
	} else {
		// Force alpha to full if we're not checking it.
		OR(tempReg1, tempReg1, tempReg2);
	}

	ST_W(tempReg1, dstReg, outOff);
}

void VertexDecoderJitCache::Jit_TcU16ThroughToFloat() {
	LD_HU(tempReg1, srcReg, dec_->tcoff + 0);
	LD_HU(tempReg2, srcReg, dec_->tcoff + 2);

	auto updateSide = [&](LoongArch64Reg src, bool greater, LoongArch64Reg dst) {
		FixupBranch skip = BLT(greater ? src : dst, greater ? dst : src);
		MOVE(dst, src);
		SetJumpTarget(skip);
	};

	updateSide(tempReg1, false, boundsMinUReg);
	updateSide(tempReg1, true, boundsMaxUReg);
	updateSide(tempReg2, false, boundsMinVReg);
	updateSide(tempReg2, true, boundsMaxVReg);

	VINSGR2VR_H(lsxScratchReg, tempReg1, 0);
	VINSGR2VR_H(lsxScratchReg, tempReg2, 1);
	VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0);
	VFFINT_S_WU(lsxScratchReg, lsxScratchReg);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
	// Just copy 64 bits.  Might be nice if we could detect misaligned load perf.
	LD_W(tempReg1, srcReg, dec_->tcoff);
	LD_W(tempReg2, srcReg, dec_->tcoff + 4);
	ST_W(tempReg1, dstReg, dec_->decFmt.uvoff);
	ST_W(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcFloat() {
	// Just copy 64 bits.  Might be nice if we could detect misaligned load perf.
	LD_W(tempReg1, srcReg, dec_->tcoff);
	LD_W(tempReg2, srcReg, dec_->tcoff + 4);
	ST_W(tempReg1, dstReg, dec_->decFmt.uvoff);
	ST_W(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
	LD_HU(scratchReg, srcReg, dec_->tcoff);
	VINSGR2VR_H(lsxScratchReg, scratchReg, 0);
	VSLLWIL_HU_BU(lsxScratchReg, lsxScratchReg, 0); // Widen to 16-bit
	VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0); // Widen to 32-bit
	VFFINT_S_WU(lsxScratchReg, lsxScratchReg);
	VFMADD_S(lsxScratchReg, lsxScratchReg, lsxScaleOffsetReg, lsxOffsetScaleReg);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff); // save the lower 64-bit of lsxScratchReg
}

void VertexDecoderJitCache::Jit_TcU8ToFloat() {
	LD_HU(scratchReg, srcReg, dec_->tcoff);
	VINSGR2VR_H(lsxScratchReg, scratchReg, 0);
	VSLLWIL_HU_BU(lsxScratchReg, lsxScratchReg, 0); // Widen to 16-bit
	VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0); // Widen to 32-bit
	VFFINT_S_WU(lsxScratchReg, lsxScratchReg);
	VFMUL_S(lsxScratchReg, lsxScratchReg, by128LSX);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff); // save the lower 64-bit of lsxScratchReg
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
    FLD_S(fpSrc[0], srcReg, dec_->tcoff);
	VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0); // Widen to 32-bit
	VFFINT_S_WU(lsxScratchReg, lsxScratchReg);
	VFMADD_S(lsxScratchReg, lsxScratchReg, lsxScaleOffsetReg, lsxOffsetScaleReg);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff); // save the lower 64-bit of lsxScratchReg
}

void VertexDecoderJitCache::Jit_TcU16ToFloat() {
    FLD_S(fpSrc[0], srcReg, dec_->tcoff);
	VSLLWIL_WU_HU(lsxScratchReg, lsxScratchReg, 0); // Widen to 32-bit
	VFFINT_S_WU(lsxScratchReg, lsxScratchReg);
	VFMUL_S(lsxScratchReg, lsxScratchReg, by32768LSX);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff); // save the lower 64-bit of lsxScratchReg
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
	FLD_D(fpSrc[0], srcReg, dec_->tcoff); // load to the lower 64-bit of lsxScratchReg
	VFMADD_S(lsxScratchReg, lsxScratchReg, lsxScaleOffsetReg, lsxOffsetScaleReg);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff); // save the lower 64-bit of lsxScratchReg
}

void VertexDecoderJitCache::Jit_TcAnyMorph(int bits) {
	LI(tempReg1, &gstate_c.morphWeights[0]);
	VXOR_V(lsxScratchReg4, lsxScratchReg4, lsxScratchReg4);

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg2;

		// Load the actual values and convert to float.
		if (bits == 32) {
			// Two floats
			FLD_D((LoongArch64Reg)(DecodeReg(reg) + F0), srcReg, dec_->onesize_ * n + dec_->tcoff);
		} else {
			if (bits == 8) {
				LD_HU(tempReg2, srcReg, dec_->onesize_ * n + dec_->tcoff);
				VINSGR2VR_W(reg, tempReg2, 0);
				VILVL_B(reg, lsxScratchReg4, reg);
			} else {
				FLD_S((LoongArch64Reg)(DecodeReg(reg) + F0), srcReg, dec_->onesize_ * n + dec_->tcoff);
			}

			VILVL_H(reg, lsxScratchReg4, reg);
			VFFINT_S_W(reg, reg);
		}

		// And now scale by the weight.
		VLDREPL_W(lsxScratchReg3, tempReg1, n * sizeof(float));
		VFMUL_S(reg, reg, lsxScratchReg3);

		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg,lsxScratchReg2);
		} else {
			first = false;
		}
	}
}

void VertexDecoderJitCache::Jit_TcU8MorphToFloat() {
	Jit_TcAnyMorph(8);
	// They were all added (weighted) pre-normalize, we normalize once here.
	VFMUL_S(lsxScratchReg, lsxScratchReg, by128LSX);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16MorphToFloat() {
	Jit_TcAnyMorph(16);
	// They were all added (weighted) pre-normalize, we normalize once here.
	VFMUL_S(lsxScratchReg, lsxScratchReg, by32768LSX);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloatMorph() {
	Jit_TcAnyMorph(32);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU8PrescaleMorph() {
	Jit_TcAnyMorph(8);
	// The scale takes into account the u8 normalization.
	VFMADD_S(lsxScratchReg, lsxScratchReg, lsxScaleOffsetReg, lsxOffsetScaleReg);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16PrescaleMorph() {
	Jit_TcAnyMorph(16);
	// The scale takes into account the u16 normalization.
	VFMADD_S(lsxScratchReg, lsxScratchReg, lsxScaleOffsetReg, lsxOffsetScaleReg);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloatPrescaleMorph() {
	Jit_TcAnyMorph(32);
	VFMADD_S(lsxScratchReg, lsxScratchReg, lsxScaleOffsetReg, lsxOffsetScaleReg);
	FST_D(fpSrc[0], dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_PosS8() {
	Jit_AnyS8ToFloat(dec_->posoff);
	VST(lsxScratchReg, dstReg, dec_->decFmt.posoff);
}

void VertexDecoderJitCache::Jit_PosS16() {
	Jit_AnyS16ToFloat(dec_->posoff);
	VST(lsxScratchReg, dstReg, dec_->decFmt.posoff);
}

void VertexDecoderJitCache::Jit_PosFloat() {
	// Just copy 12 bytes, play with over read/write later.
	LD_W(tempReg1, srcReg, dec_->posoff + 0);
	LD_W(tempReg2, srcReg, dec_->posoff + 4);
	LD_W(tempReg3, srcReg, dec_->posoff + 8);
	ST_W(tempReg1, dstReg, dec_->decFmt.posoff + 0);
	ST_W(tempReg2, dstReg, dec_->decFmt.posoff + 4);
	ST_W(tempReg3, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS8Through() {
	// 8-bit positions in throughmode always decode to 0, depth included.
	ST_W(R_ZERO, dstReg, dec_->decFmt.posoff + 0);
	ST_W(R_ZERO, dstReg, dec_->decFmt.posoff + 4);
	ST_W(R_ZERO, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16Through() {
	// Start with X and Y (which are signed.)
	LD_H(tempReg1, srcReg, dec_->posoff + 0);
	LD_H(tempReg2, srcReg, dec_->posoff + 2);
	// This one, Z, has to be unsigned.
	LD_HU(tempReg3, srcReg, dec_->posoff + 4);
	MOVGR2FR_W(fpSrc[0], tempReg1);
	MOVGR2FR_W(fpSrc[1], tempReg2);
	MOVGR2FR_W(fpSrc[2], tempReg3);
	FFINT_S_W(fpSrc[0], fpSrc[0]);
	FFINT_S_W(fpSrc[1], fpSrc[1]);
	FFINT_S_W(fpSrc[2], fpSrc[2]);
	FST_S(fpSrc[0], dstReg, dec_->decFmt.posoff + 0);
	FST_S(fpSrc[1], dstReg, dec_->decFmt.posoff + 4);
	FST_S(fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosFloatThrough() {
	// Start by copying 8 bytes, then handle Z separately to clamp it.
	LD_W(tempReg1, srcReg, dec_->posoff + 0);
	LD_W(tempReg2, srcReg, dec_->posoff + 4);
	FLD_S(fpSrc[2], srcReg, dec_->posoff + 8);
	ST_W(tempReg1, dstReg, dec_->decFmt.posoff + 0);
	ST_W(tempReg2, dstReg, dec_->decFmt.posoff + 4);

	// Load the constant zero and clamp.
	MOVGR2FR_W(fpScratchReg, R_ZERO);
	// Is it worth a seperate reg?
	LI(scratchReg, const65535);
	MOVGR2FR_W(fpScratchReg2, scratchReg);
	FMAX_S(fpSrc[2], fpSrc[2], fpScratchReg);
	FMIN_S(fpSrc[2], fpSrc[2], fpScratchReg2);
	FST_S(fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_NormalS8() {
	LD_W(tempReg1, srcReg, dec_->nrmoff + 0);
	BSTRINS_D(tempReg1, R_ZERO, 31, 24);
	ST_W(tempReg1, dstReg, dec_->decFmt.nrmoff + 0);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	LD_D(tempReg1, srcReg, dec_->nrmoff + 0);
	BSTRINS_D(tempReg1, R_ZERO, 63, 48);
	ST_D(tempReg1, dstReg, dec_->decFmt.nrmoff + 0);
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	// Just copy 12 bytes, play with over read/write later.
	LD_D(tempReg1, srcReg, dec_->nrmoff + 0);
	LD_W(tempReg2, srcReg, dec_->nrmoff + 8);
	ST_D(tempReg1, dstReg, dec_->decFmt.nrmoff + 0);
	ST_W(tempReg2, dstReg, dec_->decFmt.nrmoff + 8);
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
	VLD(lsxScratchReg, srcReg, dec_->nrmoff);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
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
	VLD(lsxScratchReg, srcReg, dec_->posoff);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_AnyS8ToFloat(int srcoff) {
	FLD_S(fpSrc[0], srcReg, srcoff); // Directly load to lsxScratchReg.
	VSLLWIL_H_B(lsxScratchReg, lsxScratchReg, 0);
	VSLLWIL_W_H(lsxScratchReg, lsxScratchReg, 0);
	VFFINT_S_W(lsxScratchReg, lsxScratchReg);
	VFMUL_S(lsxScratchReg, lsxScratchReg, by128LSX);
}

void VertexDecoderJitCache::Jit_AnyS16ToFloat(int srcoff) {
	FLD_D(fpSrc[0], srcReg, srcoff); // Directly load to lsxScratchReg.
	VSLLWIL_W_H(lsxScratchReg, lsxScratchReg, 0);
	VFFINT_S_W(lsxScratchReg, lsxScratchReg);
	VFMUL_S(lsxScratchReg, lsxScratchReg, by32768LSX);
}

void VertexDecoderJitCache::Jit_WriteMatrixMul(int outOff, bool pos) {
	// Multiply lsxScratchReg with the matrix sitting in V4-V7.
    VREPLVEI_W(lsxScratchReg2, lsxScratchReg, 0);
	VFMUL_S(accLSX, V4, lsxScratchReg2);
	VREPLVEI_W(lsxScratchReg2, lsxScratchReg, 1);
	VFMADD_S(accLSX, V5, lsxScratchReg2, accLSX);
	VREPLVEI_W(lsxScratchReg2, lsxScratchReg, 2);
	VFMADD_S(accLSX, V6, lsxScratchReg2, accLSX);
	if (pos) {
		VFADD_S(accLSX, accLSX, V7);
	}
	VST(accLSX, dstReg, outOff);
}

void VertexDecoderJitCache::Jit_AnyS8Morph(int srcoff, int dstoff) {
	LI(tempReg1, &gstate_c.morphWeights[0]);

	// Sum into lsxScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg2;
		// Okay, first convert to floats.
		FLD_S((LoongArch64Reg)(DecodeReg(reg) + F0), srcReg, dec_->onesize_ * n + srcoff);
		VSLLWIL_H_B(reg, reg, 0);
		VSLLWIL_W_H(reg, reg, 0);

		VFFINT_S_W(reg, reg);

		// Now, It's time to multiply by the weight and 1.0f/128.0f.
		VLDREPL_W(lsxScratchReg3, tempReg1, sizeof(float) * n);
		VFMUL_S(lsxScratchReg3, lsxScratchReg3, by128LSX);
		VFMUL_S(reg, reg, lsxScratchReg3);

		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg, lsxScratchReg2);
		} else {
			first = false;
		}
	}

	if (dstoff >= 0)
		VST(lsxScratchReg, dstReg, dstoff);
}

void VertexDecoderJitCache::Jit_AnyS16Morph(int srcoff, int dstoff) {
	LI(tempReg1, &gstate_c.morphWeights[0]);

	// Sum into lsxScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg2;
		// Okay, first convert to floats.
		FLD_D((LoongArch64Reg)(DecodeReg(reg) + F0), srcReg, dec_->onesize_ * n + srcoff);
		VSLLWIL_W_H(reg, reg, 0);
		VFFINT_S_W(reg, reg);

		// Now, It's time to multiply by the weight and 1.0f/32768.0f.
		VLDREPL_W(lsxScratchReg3, tempReg1, sizeof(float) * n);
		VFMUL_S(lsxScratchReg3, lsxScratchReg3, by32768LSX);
		VFMUL_S(reg, reg, lsxScratchReg3);

		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg, lsxScratchReg2);
		} else {
			first = false;
		}
	}

	if (dstoff >= 0)
		VST(lsxScratchReg, dstReg, dstoff);
}

void VertexDecoderJitCache::Jit_AnyFloatMorph(int srcoff, int dstoff) {
	LI(tempReg1, &gstate_c.morphWeights[0]);

	// Sum into lsxScratchReg.
	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		const LoongArch64Reg reg = first ? lsxScratchReg : lsxScratchReg2;
		VLD(reg, srcReg, dec_->onesize_ * n + srcoff);
		VLDREPL_W(lsxScratchReg3, tempReg1, sizeof(float) * n);
		VFMUL_S(reg, reg, lsxScratchReg3);
		if (!first) {
			VFADD_S(lsxScratchReg, lsxScratchReg, lsxScratchReg2);
		} else {
			first = false;
		}
	}

	if (dstoff >= 0)
		VST(lsxScratchReg, dstReg, dstoff);
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
#endif // PPSSPP_ARCH(LOONGARCH64)
