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
#include "GPU/GLES/VertexDecoder.h"

extern void DisassembleArm(const u8 *data, int size);

bool NEONSkinning = false;
bool NEONMorphing = false;

// Used only in non-NEON mode.
static float MEMORY_ALIGNED16(skinMatrix[12]);

// Will be used only in NEON mode.
static float MEMORY_ALIGNED16(bones[16 * 8]);  // First two are kept in registers
static float MEMORY_ALIGNED16(boneMask[4]) = {1.0f, 1.0f, 1.0f, 0.0f};

// NEON register allocation:
// Q0: Texture scaling parameters
// Q1: Temp storage
// Q2: Vector-by-matrix accumulator
// Q3: Unused (multiplier temp when morphing)
//
// When skinning, we'll use Q4-Q7 as the "matrix accumulator".
// First two matrices will be preloaded into Q8-Q11 and Q12-Q15 to reduce
// memory bandwidth requirements.
// The rest will be dumped to bones as on x86.
//
// When morphing, we never skin.  So we're free to use Q4+.
// Q4 is for color shift values, and Q5 is a secondary multipler inside the morph.
// TODO: Maybe load all morph weights to Q6+ to avoid memory access?


static const float by127 = 1.0f / 127.0f;
static const float by128 = 1.0f / 128.0f;
static const float by256 = 1.0f / 256.0f;
static const float by32767 = 1.0f / 32767.0f;
static const float by32768 = 1.0f / 32768.0f;

using namespace ArmGen;

static const ARMReg tempReg1 = R3;
static const ARMReg tempReg2 = R4;
static const ARMReg tempReg3 = R5;
static const ARMReg scratchReg = R6;
static const ARMReg scratchReg2 = R7;
static const ARMReg scratchReg3 = R8;
static const ARMReg fullAlphaReg = R12;
static const ARMReg srcReg = R0;
static const ARMReg dstReg = R1;
static const ARMReg counterReg = R2;
static const ARMReg fpScratchReg = S4;
static const ARMReg fpScratchReg2 = S5;
static const ARMReg fpScratchReg3 = S6;
static const ARMReg fpScratchReg4 = S7;
static const ARMReg fpUscaleReg = S0;
static const ARMReg fpVscaleReg = S1;
static const ARMReg fpUoffsetReg = S2;
static const ARMReg fpVoffsetReg = S3;

// Simpler aliases for NEON. Overlaps with corresponding VFP regs.
static const ARMReg neonUVScaleReg = D0;
static const ARMReg neonUVOffsetReg = D1;
static const ARMReg neonScratchReg = D2;
static const ARMReg neonScratchReg2 = D3;
static const ARMReg neonScratchRegQ = Q1;  // Overlaps with all the scratch regs

// Everything above S6 is fair game for skinning

// S8-S15 are used during matrix generation

// These only live through the matrix multiplication
static const ARMReg src[3] = {S8, S9, S10};  // skin source
static const ARMReg acc[3] = {S11, S12, S13};  // skin accumulator

static const ARMReg srcNEON = Q2;
static const ARMReg accNEON = Q3;

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
	const u8 *start = AlignCode16();

	bool prescaleStep = false;
	bool skinning = false;

	NEONSkinning = cpu_info.bNEON;
	NEONMorphing = cpu_info.bNEON;

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

	SetCC(CC_AL);

	PUSH(6, R4, R5, R6, R7, R8, R_LR);
	if (NEONSkinning || NEONMorphing) {
		VPUSH(D8, 8);
	}

	// Keep the scale/offset in a few fp registers if we need it.
	if (prescaleStep) {
		MOVP2R(R3, &gstate_c.uv);
		if (cpu_info.bNEON) {
			VLD1(F_32, neonUVScaleReg, R3, 2, ALIGN_NONE);
			if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
				VMOV_neon(F_32, neonScratchReg, by128);
				VMUL(F_32, neonUVScaleReg, neonUVScaleReg, neonScratchReg);
			} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
				VMOV_neon(F_32, neonScratchReg, by32768);
				VMUL(F_32, neonUVScaleReg, neonUVScaleReg, neonScratchReg);
			}
		} else {
			VLDMIA(R3, false, fpUscaleReg, 4); // fp{Uscale, Yscale, Uoffset, Voffset}Reg = {S0-S4}
			if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
				MOVI2F(fpScratchReg, by128, scratchReg);
				VMUL(fpUscaleReg, fpUscaleReg, fpScratchReg);
				VMUL(fpVscaleReg, fpVscaleReg, fpScratchReg);
			} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
				MOVI2F(fpScratchReg, by32768, scratchReg);
				VMUL(fpUscaleReg, fpUscaleReg, fpScratchReg);
				VMUL(fpVscaleReg, fpVscaleReg, fpScratchReg);
			}
		}
	}

	// Add code to convert matrices to 4x4.
	// Later we might want to do this when the matrices are loaded instead.
	int boneCount = 0;
	if (NEONSkinning && dec.weighttype && g_Config.bSoftwareSkinning && dec.morphcount == 1) {
		// Copying from R3 to R4
		MOVP2R(R3, gstate.boneMatrix);
		MOVP2R(R4, bones);
		MOVP2R(R5, boneMask);
		VLD1(F_32, Q3, R5, 2, ALIGN_128);
		for (int i = 0; i < 8; i++) {
			VLD1(F_32, Q4, R3, 2);  // Load 128 bits even though we just want 96
			VMUL(F_32, Q4, Q4, Q3);
			ADD(R3, R3, 12);
			VLD1(F_32, Q5, R3, 2);
			VMUL(F_32, Q5, Q5, Q3);
			ADD(R3, R3, 12);
			VLD1(F_32, Q6, R3, 2);
			VMUL(F_32, Q6, Q6, Q3);
			ADD(R3, R3, 12);
			VLD1(F_32, Q7, R3, 2);
			VMUL(F_32, Q7, Q7, Q3);
			ADD(R3, R3, 12);
			// First two matrices are in registers.
			if (i == 0) {
				VMOV(Q8, Q4);
				VMOV(Q9, Q5);
				VMOV(Q10, Q6);
				VMOV(Q11, Q7);
				ADD(R4, R4, 16 * 4);
			} else if (i == 1) {
				VMOV(Q12, Q4);
				VMOV(Q13, Q5);
				VMOV(Q14, Q6);
				VMOV(Q15, Q7);
				ADD(R4, R4, 16 * 4);
			} else {
				VST1(F_32, Q4, R4, 2, ALIGN_128, REG_UPDATE);
				VST1(F_32, Q5, R4, 2, ALIGN_128, REG_UPDATE);
				VST1(F_32, Q6, R4, 2, ALIGN_128, REG_UPDATE);
				VST1(F_32, Q7, R4, 2, ALIGN_128, REG_UPDATE);
			}
		}
	}

	// TODO: NEON skinning register mapping
	// The matrix will be built in Q12-Q15.
	// The temporary matrix to be added to the built matrix will be in Q8-Q11.

	if (skinning) {
		// TODO: Preload scale factors
	}

	if (dec.col) {
		// Or LDB and skip the conditional?  This is probably cheaper.
		MOV(fullAlphaReg, 0xFF);
	}

	JumpTarget loopStart = GetCodePtr();
	// Preload data cache ahead of reading. This offset seems pretty good.
	PLD(srcReg, 64);
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			// Reset the code ptr and return zero to indicate that we failed.
			SetCodePtr(const_cast<u8 *>(start));
			char temp[1024] = {0};
			dec.ToString(temp);
			INFO_LOG(HLE, "Could not compile vertex decoder: %s", temp);
			return 0;
		}
	}

	ADDI2R(srcReg, srcReg, dec.VertexSize(), scratchReg);
	ADDI2R(dstReg, dstReg, dec.decFmt.stride, scratchReg);
	SUBS(counterReg, counterReg, 1);
	B_CC(CC_NEQ, loopStart);

	if (dec.col) {
		MOVP2R(tempReg1, &gstate_c.vertexFullAlpha);
		CMP(fullAlphaReg, 0);
		SetCC(CC_EQ);
		STRB(fullAlphaReg, tempReg1, 0);
		SetCC(CC_AL);
	}

	if (NEONSkinning || NEONMorphing) {
		VPOP(D8, 8);
	}
	POP(6, R4, R5, R6, R7, R8, R_PC);

	FlushLitPool();
	FlushIcache();

	/*
	DisassembleArm(start, GetCodePtr() - start);
	char temp[1024] = {0};
	dec.ToString(temp);
	INFO_LOG(HLE, "%s", temp);
	*/

	return (JittedVertexDecoder)start;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	// Basic implementation - a byte at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDRB(tempReg1, srcReg, dec_->weightoff + j);
		STRB(tempReg1, dstReg, dec_->decFmt.w0off + j);
	}
	if (j & 3) {
		// Create a zero register. Might want to make a fixed one.
		EOR(scratchReg, scratchReg, scratchReg);
	}
	while (j & 3) {
		STRB(scratchReg, dstReg, dec_->decFmt.w0off + j);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	// Basic implementation - a short at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDRH(tempReg1, srcReg, dec_->weightoff + j * 2);
		STRH(tempReg1, dstReg, dec_->decFmt.w0off + j * 2);
	}
	if (j & 3) {
		// Create a zero register. Might want to make a fixed one.
		EOR(scratchReg, scratchReg, scratchReg);
	}
	while (j & 3) {
		STRH(scratchReg, dstReg, dec_->decFmt.w0off + j * 2);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDR(tempReg1, srcReg, dec_->weightoff + j * 4);
		STR(tempReg1, dstReg, dec_->decFmt.w0off + j * 4);
	}
	if (j & 3) {
		EOR(tempReg1, tempReg1, tempReg1);
	}
	while (j & 3) {  // Zero additional weights rounding up to 4.
		STR(tempReg1, dstReg, dec_->decFmt.w0off + j * 4);
		j++;
	}
}

static const ARMReg weightRegs[8] = { S8, S9, S10, S11, S12, S13, S14, S15 };
static const ARMReg neonWeightRegsD[4] = { D4, D5, D6, D7 };
static const ARMReg neonWeightRegsQ[2] = { Q2, Q3 };

void VertexDecoderJitCache::Jit_ApplyWeights() {
	if (NEONSkinning) {
		// We construct a matrix in Q4-Q7
		// We can use Q1 as temp.
		if (dec_->nweights >= 2) {
			MOVP2R(scratchReg, bones + 16 * 2);
		}
		for (int i = 0; i < dec_->nweights; i++) {
			switch (i) {
			case 0:
				VMUL_scalar(F_32, Q4, Q8, QScalar(neonWeightRegsQ[0], 0));
				VMUL_scalar(F_32, Q5, Q9, QScalar(neonWeightRegsQ[0], 0));
				VMUL_scalar(F_32, Q6, Q10, QScalar(neonWeightRegsQ[0], 0));
				VMUL_scalar(F_32, Q7, Q11, QScalar(neonWeightRegsQ[0], 0));
				break;
			case 1:
				// Krait likes VDUP + VFMA better than VMLA, and it's easy to do here.
				if (cpu_info.bVFPv4) {
					VDUP(F_32, Q1, neonWeightRegsQ[i >> 2], i & 1);
					VFMA(F_32, Q4, Q12, Q1);
					VFMA(F_32, Q5, Q13, Q1);
					VFMA(F_32, Q6, Q14, Q1);
					VFMA(F_32, Q7, Q15, Q1);
				} else {
					VMLA_scalar(F_32, Q4, Q12, QScalar(neonWeightRegsQ[0], 1));
					VMLA_scalar(F_32, Q5, Q13, QScalar(neonWeightRegsQ[0], 1));
					VMLA_scalar(F_32, Q6, Q14, QScalar(neonWeightRegsQ[0], 1));
					VMLA_scalar(F_32, Q7, Q15, QScalar(neonWeightRegsQ[0], 1));
				}
				break;
			default:
				// Matrices 2+ need to be loaded from memory.
				// Wonder if we can free up one more register so we could get some parallelism.
				// Actually Q3 is free if there are fewer than 5 weights...
				if (dec_->nweights <= 4) {
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VLD1(F_32, Q3, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q4, Q1, QScalar(neonWeightRegsQ[i >> 2], i & 3));
					VMLA_scalar(F_32, Q5, Q3, QScalar(neonWeightRegsQ[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VLD1(F_32, Q3, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q6, Q1, QScalar(neonWeightRegsQ[i >> 2], i & 3));
					VMLA_scalar(F_32, Q7, Q3, QScalar(neonWeightRegsQ[i >> 2], i & 3));
				} else {
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q4, Q1, QScalar(neonWeightRegsQ[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q5, Q1, QScalar(neonWeightRegsQ[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q6, Q1, QScalar(neonWeightRegsQ[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q7, Q1, QScalar(neonWeightRegsQ[i >> 2], i & 3));
				}
				break;
			}
		}
	} else {
		MOVP2R(tempReg2, skinMatrix);
		// This approach saves a few stores but accesses the matrices in a more
		// sparse order.
		const float *bone = &gstate.boneMatrix[0];
		MOVP2R(tempReg1, bone);
		for (int i = 0; i < 12; i++) {
			VLDR(fpScratchReg3, tempReg1, i * 4);
			VMUL(fpScratchReg3, fpScratchReg3, weightRegs[0]);
			for (int j = 1; j < dec_->nweights; j++) {
				VLDR(fpScratchReg2, tempReg1, i * 4 + j * 4 * 12);
				VMLA(fpScratchReg3, fpScratchReg2, weightRegs[j]);
			}
			VSTR(fpScratchReg3, tempReg2, i * 4);
		}
	}
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
	if (NEONSkinning) {
		// Weight is first so srcReg is correct.
		switch (dec_->nweights) {
		case 1: VLD1_lane(I_8, neonScratchReg, srcReg, 0, false); break;
		case 2: VLD1_lane(I_16, neonScratchReg, srcReg, 0, false); break;
		default:
			// For 3, we over read, for over 4, we read more later.
			VLD1_lane(I_32, neonScratchReg, srcReg, 0, false);
			break;
		}
		// This can be represented as a constant.
		VMOV_neon(F_32, Q3, by128);
		VMOVL(I_8 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, neonWeightRegsQ[0], neonScratchRegQ, Q3);

		if (dec_->nweights > 4) {
			ADD(tempReg1, srcReg, 4 * sizeof(u8));
			switch (dec_->nweights) {
			case 5: VLD1_lane(I_8, neonScratchReg, tempReg1, 0, false); break;
			case 6: VLD1_lane(I_16, neonScratchReg, tempReg1, 0, false); break;
			case 7:
			case 8:
				VLD1_lane(I_32, neonScratchReg, tempReg1, 0, false);
				break;
			}
			VMOVL(I_8 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
			VMUL(F_32, neonWeightRegsQ[1], neonScratchRegQ, Q3);
		}
	} else {
		for (int j = 0; j < dec_->nweights; j++) {
			LDRB(tempReg1, srcReg, dec_->weightoff + j);
			VMOV(fpScratchReg, tempReg1);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
			MOVI2F(fpScratchReg2, by128, scratchReg);
			VMUL(weightRegs[j], fpScratchReg, fpScratchReg2);
		}
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
	if (NEONSkinning) {
		switch (dec_->nweights) {
		case 1: VLD1_lane(I_16, neonScratchReg, srcReg, 0, true); break;
		case 2: VLD1_lane(I_32, neonScratchReg, srcReg, 0, false); break;
		default:
			// For 3, we over read, for over 4, we read more later.
			VLD1(I_32, neonScratchReg, srcReg, 1, ALIGN_NONE);
			break;
		}
		// This can be represented as a constant.
		VMOV_neon(F_32, Q3, by32768);
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, neonWeightRegsQ[0], neonScratchRegQ, Q3);

		if (dec_->nweights > 4) {
			ADD(tempReg1, srcReg, 4 * sizeof(u16));
			switch (dec_->nweights) {
			case 5: VLD1_lane(I_16, neonScratchReg, tempReg1, 0, true); break;
			case 6: VLD1_lane(I_32, neonScratchReg, tempReg1, 0, false); break;
			case 7:
			case 8:
				VLD1(I_32, neonScratchReg, tempReg1, 1, ALIGN_NONE);
				break;
			}
			VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
			VMUL(F_32, neonWeightRegsQ[1], neonScratchRegQ, Q3);
		}
	} else {
		// Fallback and non-neon
		for (int j = 0; j < dec_->nweights; j++) {
			LDRH(tempReg1, srcReg, dec_->weightoff + j * 2);
			VMOV(fpScratchReg, tempReg1);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
			MOVI2F(fpScratchReg2, by32768, scratchReg);
			VMUL(weightRegs[j], fpScratchReg, fpScratchReg2);
		}
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
	for (int i = 1; i < dec_->nweights; ++i) {
		_dbg_assert_msg_(JIT, weightRegs[i - 1] + 1 == weightRegs[i], "VertexDecoder weightRegs must be in order.");
	}

	// Weights are always first, so we can use srcReg directly.
	if (NEONSkinning) {
		// if (false) because this path breaks Daxter. VLDMIA with d registers doesn't seem to work as expected.
		if (dec_->nweights == 1) {
			VLD1_lane(F_32, neonWeightRegsD[0], srcReg, 0, true);
		} else {
			// We may over-read by one float but this is not a tragedy.
			VLD1(F_32, neonWeightRegsD[0], srcReg, (dec_->nweights + 1) / 2);
		}
	} else {
		VLDMIA(srcReg, false, weightRegs[0], dec_->nweights);
	}
	Jit_ApplyWeights();
}

// Fill last two bytes with zeroes to align to 4 bytes. LDRH does it for us, handy.
void VertexDecoderJitCache::Jit_TcU8() {
	LDRB(tempReg1, srcReg, dec_->tcoff);
	LDRB(tempReg2, srcReg, dec_->tcoff + 1);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 8));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloat() {
	LDR(tempReg1, srcReg, dec_->tcoff);
	LDR(tempReg2, srcReg, dec_->tcoff + 4);
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
	STR(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16Through() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
	LDR(tempReg1, srcReg, dec_->tcoff);
	LDR(tempReg2, srcReg, dec_->tcoff + 4);
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
	STR(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16Double() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	LSL(tempReg1, tempReg1, 1);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 17));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16ThroughDouble() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	LSL(tempReg1, tempReg1, 1);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 17));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
	if (cpu_info.bNEON) {
		// TODO: Needs testing
		ADD(scratchReg, srcReg, dec_->tcoff);
		VLD1_lane(I_16, neonScratchReg, scratchReg, 0, false);
		VMOVL(I_8 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 16-bit
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		ADD(scratchReg2, dstReg, dec_->decFmt.uvoff);
		VMUL(F_32, neonScratchReg, neonScratchReg, neonUVScaleReg);
		VADD(F_32, neonScratchReg, neonScratchReg, neonUVOffsetReg);
		VST1(F_32, neonScratchReg, scratchReg2, 1, ALIGN_NONE);
	} else {
		LDRB(tempReg1, srcReg, dec_->tcoff);
		LDRB(tempReg2, srcReg, dec_->tcoff + 1);
		VMOV(fpScratchReg, tempReg1);
		VMOV(fpScratchReg2, tempReg2);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
		VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
		// Could replace VMUL + VADD with VMLA but would require 2 more regs as we don't want to destroy fp*offsetReg. Later.
		VMUL(fpScratchReg, fpScratchReg, fpUscaleReg);
		VMUL(fpScratchReg2, fpScratchReg2, fpVscaleReg);
		VADD(fpScratchReg, fpScratchReg, fpUoffsetReg);
		VADD(fpScratchReg2, fpScratchReg2, fpVoffsetReg);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.uvoff);
		VSTR(fpScratchReg2, dstReg, dec_->decFmt.uvoff + 4);
	}
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
	if (cpu_info.bNEON) {
		// TODO: Needs testing
		ADD(scratchReg, srcReg, dec_->tcoff);
		VLD1_lane(I_32, neonScratchReg, scratchReg, 0, false);
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		ADD(scratchReg2, dstReg, dec_->decFmt.uvoff);
		VMUL(F_32, neonScratchReg, neonScratchReg, neonUVScaleReg);
		VADD(F_32, neonScratchReg, neonScratchReg, neonUVOffsetReg);
		VST1(F_32, neonScratchReg, scratchReg2, 1, ALIGN_NONE);
	} else {
		LDRH(tempReg1, srcReg, dec_->tcoff);
		LDRH(tempReg2, srcReg, dec_->tcoff + 2);
		VMOV(fpScratchReg, tempReg1);
		VMOV(fpScratchReg2, tempReg2);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
		VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
		VMUL(fpScratchReg, fpScratchReg, fpUscaleReg);
		VMUL(fpScratchReg2, fpScratchReg2, fpVscaleReg);
		VADD(fpScratchReg, fpScratchReg, fpUoffsetReg);
		VADD(fpScratchReg2, fpScratchReg2, fpVoffsetReg);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.uvoff);
		VSTR(fpScratchReg2, dstReg, dec_->decFmt.uvoff + 4);
	}
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
	if (cpu_info.bNEON) {
		ADD(scratchReg, srcReg, dec_->tcoff);
		VLD1(F_32, neonScratchReg, scratchReg, 1, ALIGN_NONE);
		ADD(scratchReg2, dstReg, dec_->decFmt.uvoff);
		VMUL(F_32, neonScratchReg, neonScratchReg, neonUVScaleReg);
		VADD(F_32, neonScratchReg, neonScratchReg, neonUVOffsetReg);
		VST1(F_32, neonScratchReg, scratchReg2, 1, ALIGN_NONE);
	} else {
		VLDR(fpScratchReg, srcReg, dec_->tcoff);
		VLDR(fpScratchReg2, srcReg, dec_->tcoff + 4);
		VMUL(fpScratchReg, fpScratchReg, fpUscaleReg);
		VMUL(fpScratchReg2, fpScratchReg2, fpVscaleReg);
		VADD(fpScratchReg, fpScratchReg, fpUoffsetReg);
		VADD(fpScratchReg2, fpScratchReg2, fpVoffsetReg);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.uvoff);
		VSTR(fpScratchReg2, dstReg, dec_->decFmt.uvoff + 4);
	}
}

void VertexDecoderJitCache::Jit_Color8888() {
	LDR(tempReg1, srcReg, dec_->coloff);
	// Set flags to determine if alpha != 0xFF.
	MVNS(tempReg2, Operand2(tempReg1, ST_ASR, 24));
	STR(tempReg1, dstReg, dec_->decFmt.c0off);
	SetCC(CC_NEQ);
	MOV(fullAlphaReg, 0);
	SetCC(CC_AL);
}

void VertexDecoderJitCache::Jit_Color4444() {
	LDRH(tempReg1, srcReg, dec_->coloff);

	// Spread out the components.
	ANDI2R(tempReg2, tempReg1, 0x000F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0x00F0, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 4));
	ANDI2R(tempReg3, tempReg1, 0x0F00, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 8));
	ANDI2R(tempReg3, tempReg1, 0xF000, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 12));

	// And expand to 8 bits.
	ORR(tempReg1, tempReg2, Operand2(tempReg2, ST_LSL, 4));

	STR(tempReg1, dstReg, dec_->decFmt.c0off);

	// Set flags to determine if alpha != 0xFF.
	MVNS(tempReg2, Operand2(tempReg1, ST_ASR, 24));
	SetCC(CC_NEQ);
	MOV(fullAlphaReg, 0);
	SetCC(CC_AL);
}

void VertexDecoderJitCache::Jit_Color565() {
	LDRH(tempReg1, srcReg, dec_->coloff);

	// Spread out R and B first.  This puts them in 0x001F001F.
	ANDI2R(tempReg2, tempReg1, 0x001F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0xF800, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 5));

	// Expand 5 -> 8.
	LSL(tempReg3, tempReg2, 3);
	ORR(tempReg2, tempReg3, Operand2(tempReg2, ST_LSR, 2));
	ANDI2R(tempReg2, tempReg2, 0xFFFF00FF, scratchReg);

	// Now finally G.  We start by shoving it into a wall.
	LSR(tempReg1, tempReg1, 5);
	ANDI2R(tempReg1, tempReg1, 0x003F, scratchReg);
	LSL(tempReg3, tempReg1, 2);
	// Don't worry, shifts into a wall.
	ORR(tempReg3, tempReg3, Operand2(tempReg1, ST_LSR, 4));
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 8));

	// Add in full alpha.  No need to update fullAlphaReg.
	ORI2R(tempReg1, tempReg2, 0xFF000000, scratchReg);

	STR(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color5551() {
	LDRSH(tempReg1, srcReg, dec_->coloff);

	ANDI2R(tempReg2, tempReg1, 0x001F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0x03E0, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 3));
	ANDI2R(tempReg3, tempReg1, 0x7C00, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 6));

	// Expand 5 -> 8.
	LSR(tempReg3, tempReg2, 2);
	// Clean up the bits that were shifted right.
	BIC(tempReg3, tempReg3, AssumeMakeOperand2(0x000000F8));
	BIC(tempReg3, tempReg3, AssumeMakeOperand2(0x0000F800));
	ORR(tempReg2, tempReg3, Operand2(tempReg2, ST_LSL, 3));

	// Now we just need alpha.  Since we loaded as signed, it'll be extended.
	ANDI2R(tempReg1, tempReg1, 0xFF000000, scratchReg);
	ORR(tempReg2, tempReg2, tempReg1);
	
	// Set flags to determine if alpha != 0xFF.
	MVNS(tempReg3, Operand2(tempReg1, ST_ASR, 24));
	STR(tempReg2, dstReg, dec_->decFmt.c0off);
	SetCC(CC_NEQ);
	MOV(fullAlphaReg, 0);
	SetCC(CC_AL);
}

void VertexDecoderJitCache::Jit_Color8888Morph() {
	const bool useNEON = NEONMorphing;
	ADDI2R(tempReg1, srcReg, dec_->coloff, scratchReg);
	MOVP2R(tempReg2, &gstate_c.morphWeights[0]);

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		if (useNEON) {
			VLD1_lane(I_32, neonScratchReg, tempReg1, 0, true);
			VLD1_all_lanes(F_32, Q3, tempReg2, true, REG_UPDATE);

			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VMOVL(I_8 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);

			if (first) {
				first = false;
				VMUL(F_32, Q2, neonScratchRegQ, Q3);
			} else if (cpu_info.bVFPv4) {
				VFMA(F_32, Q2, neonScratchRegQ, Q3);
			} else {
				VMLA(F_32, Q2, neonScratchRegQ, Q3);
			}
		} else {
			LDRB(scratchReg, tempReg1, 0);
			LDRB(scratchReg2, tempReg1, 1);
			LDRB(scratchReg3, tempReg1, 2);
			LDRB(tempReg3, tempReg1, 3);
			VMOV(fpScratchReg, scratchReg);
			VMOV(fpScratchReg2, scratchReg2);
			VMOV(fpScratchReg3, scratchReg3);
			VMOV(fpScratchReg4, tempReg3);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
			VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
			VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT);
			VCVT(fpScratchReg4, fpScratchReg4, TO_FLOAT);

			VLDR(S12, tempReg2, sizeof(float) * n);

			if (first) {
				first = false;
				VMUL(S8, fpScratchReg, S12);
				VMUL(S9, fpScratchReg2, S12);
				VMUL(S10, fpScratchReg3, S12);
				VMUL(S11, fpScratchReg4, S12);
			} else {
				VMLA(S8, fpScratchReg, S12);
				VMLA(S9, fpScratchReg2, S12);
				VMLA(S10, fpScratchReg3, S12);
				VMLA(S11, fpScratchReg4, S12);
			}
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

// First is the left shift, second is the right shift (against walls, to get the RGBA values.)
static const s16 MEMORY_ALIGNED16(color4444Shift[2][4]) = {{12, 8, 4, 0}, {-12, -12, -12, -12}};

void VertexDecoderJitCache::Jit_Color4444Morph() {
	const bool useNEON = NEONMorphing;
	ADDI2R(tempReg1, srcReg, dec_->coloff, scratchReg);
	MOVP2R(tempReg2, &gstate_c.morphWeights[0]);

	if (useNEON) {
		MOVP2R(scratchReg, color4444Shift);
		MOVI2FR(scratchReg2, 255.0f / 15.0f);
		VDUP(I_32, Q5, scratchReg2);
		VLD1(I_16, D8, scratchReg, 2, ALIGN_128);
	} else {
		MOVI2F(S13, 255.0f / 15.0f, scratchReg);
	}

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		if (useNEON) {
			VLD1_all_lanes(I_16, neonScratchReg, tempReg1, true);
			VLD1_all_lanes(F_32, Q3, tempReg2, true, REG_UPDATE);

			// Shift against walls and then back to get R, G, B, A in each 16-bit lane.
			VSHL(I_16 | I_UNSIGNED, neonScratchReg, neonScratchReg, D8);
			VSHL(I_16 | I_UNSIGNED, neonScratchReg, neonScratchReg, D9);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);

			VMUL(F_32, Q3, Q3, Q5);

			if (first) {
				first = false;
				VMUL(F_32, Q2, neonScratchRegQ, Q3);
			} else if (cpu_info.bVFPv4) {
				VFMA(F_32, Q2, neonScratchRegQ, Q3);
			} else {
				VMLA(F_32, Q2, neonScratchRegQ, Q3);
			}
		} else {
			LDRB(scratchReg, tempReg1, 0);
			ANDI2R(scratchReg2, scratchReg, 0x000F, scratchReg3);
			VMOV(fpScratchReg, scratchReg2);

			MOV(scratchReg2, Operand2(scratchReg, ST_LSR, 4));
			VMOV(fpScratchReg2, scratchReg2);

			LDRB(scratchReg, tempReg1, 1);
			ANDI2R(scratchReg2, scratchReg, 0x000F, scratchReg3);
			VMOV(fpScratchReg3, scratchReg2);

			MOV(scratchReg2, Operand2(scratchReg, ST_LSR, 4));
			VMOV(fpScratchReg4, scratchReg2);

			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
			VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
			VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT);
			VCVT(fpScratchReg4, fpScratchReg4, TO_FLOAT);

			VLDR(S12, tempReg2, sizeof(float) * n);
			VMUL(S12, S12, S13);

			if (first) {
				first = false;
				VMUL(S8, fpScratchReg, S12);
				VMUL(S9, fpScratchReg2, S12);
				VMUL(S10, fpScratchReg3, S12);
				VMUL(S11, fpScratchReg4, S12);
			} else {
				VMLA(S8, fpScratchReg, S12);
				VMLA(S9, fpScratchReg2, S12);
				VMLA(S10, fpScratchReg3, S12);
				VMLA(S11, fpScratchReg4, S12);
			}
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

// First is the left shift, second is the right shift (against walls, to get the RGBA values.)
static const s16 MEMORY_ALIGNED16(color565Shift[2][4]) = {{11, 5, 0, 0}, {-11, -10, -11, 0}};
static const float MEMORY_ALIGNED16(byColor565[4]) = {255.0f / 31.0f, 255.0f / 63.0f, 255.0f / 31.0f, 0.0f};

void VertexDecoderJitCache::Jit_Color565Morph() {
	const bool useNEON = NEONMorphing;
	ADDI2R(tempReg1, srcReg, dec_->coloff, scratchReg);
	MOVP2R(tempReg2, &gstate_c.morphWeights[0]);
	MOVI2FR(tempReg3, 255.0f);

	if (useNEON) {
		MOVP2R(scratchReg, color565Shift);
		MOVP2R(scratchReg2, byColor565);
		VLD1(I_16, D8, scratchReg, 2, ALIGN_128);
		VLD1(F_32, D10, scratchReg2, 2, ALIGN_128);
	} else {
		MOVI2F(S14, 255.0f / 31.0f, scratchReg);
		MOVI2F(S15, 255.0f / 63.0f, scratchReg);
	}

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		if (useNEON) {
			VLD1_all_lanes(I_16, neonScratchReg, tempReg1, true);
			VLD1_all_lanes(F_32, Q3, tempReg2, true, REG_UPDATE);

			VSHL(I_16 | I_UNSIGNED, neonScratchReg, neonScratchReg, D8);
			VSHL(I_16 | I_UNSIGNED, neonScratchReg, neonScratchReg, D9);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);

			VMUL(F_32, Q3, Q3, Q5);

			if (first) {
				first = false;
				VMUL(F_32, Q2, neonScratchRegQ, Q3);
			} else if (cpu_info.bVFPv4) {
				VFMA(F_32, Q2, neonScratchRegQ, Q3);
			} else {
				VMLA(F_32, Q2, neonScratchRegQ, Q3);
			}
		} else {
			LDRH(scratchReg, tempReg1, 0);
			ANDI2R(scratchReg2, scratchReg, 0x001F, scratchReg3);
			VMOV(fpScratchReg, scratchReg2);

			MOV(scratchReg2, Operand2(scratchReg, ST_LSR, 6));
			ANDI2R(scratchReg2, scratchReg2, 0x003F, scratchReg3);
			VMOV(fpScratchReg2, scratchReg2);

			MOV(scratchReg2, Operand2(scratchReg, ST_LSR, 11));
			VMOV(fpScratchReg3, scratchReg2);

			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
			VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
			VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT);

			VLDR(S12, tempReg2, sizeof(float) * n);
			VMUL(S13, S12, S15);
			VMUL(S12, S12, S14);

			if (first) {
				first = false;
				VMUL(S8, fpScratchReg, S12);
				VMUL(S9, fpScratchReg2, S13);
				VMUL(S10, fpScratchReg3, S12);
			} else {
				VMLA(S8, fpScratchReg, S12);
				VMLA(S9, fpScratchReg2, S13);
				VMLA(S10, fpScratchReg3, S12);
			}
		}
	}

	// Overwrite A with 255.0f.
	if (useNEON) {
		VMOV_neon(F_32, D5, tempReg3, 1);
	} else {
		VMOV(S11, tempReg3);
	}
	Jit_WriteMorphColor(dec_->decFmt.c0off, false);
}

// First is the left shift, second is the right shift (against walls, to get the RGBA values.)
static const s16 MEMORY_ALIGNED16(color5551Shift[2][4]) = {{11, 6, 1, 0}, {-11, -11, -11, -15}};
static const float MEMORY_ALIGNED16(byColor5551[4]) = {255.0f / 31.0f, 255.0f / 31.0f, 255.0f / 31.0f, 255.0f / 1.0f};

void VertexDecoderJitCache::Jit_Color5551Morph() {
	const bool useNEON = NEONMorphing;
	ADDI2R(tempReg1, srcReg, dec_->coloff, scratchReg);
	MOVP2R(tempReg2, &gstate_c.morphWeights[0]);

	if (useNEON) {
		MOVP2R(scratchReg, color5551Shift);
		MOVP2R(scratchReg2, byColor5551);
		VLD1(I_16, D8, scratchReg, 2, ALIGN_128);
		VLD1(F_32, D10, scratchReg2, 2, ALIGN_128);
	} else {
		MOVI2F(S14, 255.0f / 31.0f, scratchReg);
		MOVI2F(S15, 255.0f, scratchReg);
	}

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		if (useNEON) {
			VLD1_all_lanes(I_16, neonScratchReg, tempReg1, true);
			VLD1_all_lanes(F_32, Q3, tempReg2, true, REG_UPDATE);

			VSHL(I_16 | I_UNSIGNED, neonScratchReg, neonScratchReg, D8);
			VSHL(I_16 | I_UNSIGNED, neonScratchReg, neonScratchReg, D9);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);

			VMUL(F_32, Q3, Q3, Q5);

			if (first) {
				first = false;
				VMUL(F_32, Q2, neonScratchRegQ, Q3);
			} else if (cpu_info.bVFPv4) {
				VFMA(F_32, Q2, neonScratchRegQ, Q3);
			} else {
				VMLA(F_32, Q2, neonScratchRegQ, Q3);
			}
		} else {
			LDRH(scratchReg, tempReg1, 0);
			ANDI2R(scratchReg2, scratchReg, 0x001F, scratchReg3);
			VMOV(fpScratchReg, scratchReg2);

			MOV(scratchReg2, Operand2(scratchReg, ST_LSR, 5));
			ANDI2R(scratchReg2, scratchReg2, 0x001F, scratchReg3);
			VMOV(fpScratchReg2, scratchReg2);

			MOV(scratchReg2, Operand2(scratchReg, ST_LSR, 10));
			ANDI2R(scratchReg2, scratchReg2, 0x001F, scratchReg3);
			VMOV(fpScratchReg3, scratchReg2);

			MOV(scratchReg2, Operand2(scratchReg, ST_LSR, 15));
			VMOV(fpScratchReg4, scratchReg2);

			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
			VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
			VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT);
			VCVT(fpScratchReg4, fpScratchReg4, TO_FLOAT);

			VLDR(S12, tempReg2, sizeof(float) * n);
			VMUL(S13, S12, S15);
			VMUL(S12, S12, S14);

			if (first) {
				first = false;
				VMUL(S8, fpScratchReg, S12);
				VMUL(S9, fpScratchReg2, S12);
				VMUL(S10, fpScratchReg3, S12);
				VMUL(S11, fpScratchReg4, S13);
			} else {
				VMLA(S8, fpScratchReg, S12);
				VMLA(S9, fpScratchReg2, S12);
				VMLA(S10, fpScratchReg3, S12);
				VMLA(S11, fpScratchReg4, S13);
			}
		}
	}

	Jit_WriteMorphColor(dec_->decFmt.c0off);
}

// Expects RGBA color in S8 - S11, which is Q2.
void VertexDecoderJitCache::Jit_WriteMorphColor(int outOff, bool checkAlpha) {
	if (NEONMorphing) {
		ADDI2R(tempReg1, dstReg, outOff, scratchReg);
		VCVT(I_32 | I_UNSIGNED, Q2, Q2);
		VQMOVN(I_32 | I_UNSIGNED, D4, Q2);
		VQMOVN(I_16 | I_UNSIGNED, D4, Q2);
		VST1_lane(I_32, D4, tempReg1, 0, true);
		if (checkAlpha) {
			VMOV_neon(I_32, scratchReg, D4, 0);
		}
	} else {
		VCVT(S8, S8, TO_INT);
		VCVT(S9, S9, TO_INT);
		VCVT(S10, S10, TO_INT);
		VCVT(S11, S11, TO_INT);
		VMOV(scratchReg, S8);
		VMOV(scratchReg2, S9);
		VMOV(scratchReg3, S10);
		VMOV(tempReg3, S11);
		ORR(scratchReg, scratchReg, Operand2(scratchReg2, ST_LSL, 8));
		ORR(scratchReg, scratchReg, Operand2(scratchReg3, ST_LSL, 16));
		ORR(scratchReg, scratchReg, Operand2(tempReg3, ST_LSL, 24));
		STR(scratchReg, dstReg, outOff);
	}

	// Set flags to determine if alpha != 0xFF.
	if (checkAlpha) {
		MVNS(tempReg2, Operand2(scratchReg, ST_ASR, 24));
		SetCC(CC_NEQ);
		MOV(fullAlphaReg, 0);
		SetCC(CC_AL);
	}
}

void VertexDecoderJitCache::Jit_NormalS8() {
	LDRB(tempReg1, srcReg, dec_->nrmoff);
	LDRB(tempReg2, srcReg, dec_->nrmoff + 1);
	LDRB(tempReg3, srcReg, dec_->nrmoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 8));
	ORR(tempReg1, tempReg1, Operand2(tempReg3, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.nrmoff);

	// Copy 3 bytes and then a zero. Might as well copy four.
	// LDR(tempReg1, srcReg, dec_->nrmoff);
	// ANDI2R(tempReg1, tempReg1, 0x00FFFFFF, scratchReg);
	// STR(tempReg1, dstReg, dec_->decFmt.nrmoff);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	LDRH(tempReg1, srcReg, dec_->nrmoff);
	LDRH(tempReg2, srcReg, dec_->nrmoff + 2);
	LDRH(tempReg3, srcReg, dec_->nrmoff + 4);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.nrmoff);
	STR(tempReg3, dstReg, dec_->decFmt.nrmoff + 4);
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	ADD(scratchReg, srcReg, dec_->nrmoff);
	LDMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
	ADD(scratchReg, dstReg, dec_->decFmt.nrmoff);
	STMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS8Through() {
	DEBUG_LOG_REPORT_ONCE(vertexS8Through, G3D, "Using S8 positions in throughmode");
	_dbg_assert_msg_(JIT, fpScratchReg + 1 == fpScratchReg2, "VertexDecoder fpScrathRegs must be in order.");
	_dbg_assert_msg_(JIT, fpScratchReg2 + 1 == fpScratchReg3, "VertexDecoder fpScrathRegs must be in order.");

	// TODO: SIMD
	LDRSB(tempReg1, srcReg, dec_->posoff);
	LDRSB(tempReg2, srcReg, dec_->posoff + 1);
	LDRSB(tempReg3, srcReg, dec_->posoff + 2);
	static const ARMReg tr[3] = { tempReg1, tempReg2, tempReg3 };
	static const ARMReg fr[3] = { fpScratchReg, fpScratchReg2, fpScratchReg3 };
	ADD(scratchReg, dstReg, dec_->decFmt.posoff);
	if (cpu_info.bNEON) {
		VMOV(neonScratchReg, tempReg1, tempReg2);
		VMOV(neonScratchReg2, tempReg3, tempReg3);
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VST1(F_32, neonScratchReg, scratchReg, 2, ALIGN_NONE);
	} else {
		for (int i = 0; i < 3; i++) {
			VMOV(fr[i], tr[i]);
			VCVT(fr[i], fr[i], TO_FLOAT | IS_SIGNED);
		}
		VSTMIA(scratchReg, false, fr[0], 3);
	}
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS16Through() {
	_dbg_assert_msg_(JIT, fpScratchReg + 1 == fpScratchReg2, "VertexDecoder fpScrathRegs must be in order.");
	_dbg_assert_msg_(JIT, fpScratchReg2 + 1 == fpScratchReg3, "VertexDecoder fpScrathRegs must be in order.");

	// TODO: SIMD
	LDRSH(tempReg1, srcReg, dec_->posoff);
	LDRSH(tempReg2, srcReg, dec_->posoff + 2);
	LDRH(tempReg3, srcReg, dec_->posoff + 4);
	static const ARMReg tr[3] = { tempReg1, tempReg2, tempReg3 };
	static const ARMReg fr[3] = { fpScratchReg, fpScratchReg2, fpScratchReg3 };
	ADD(scratchReg, dstReg, dec_->decFmt.posoff);
	if (cpu_info.bNEON) {
		VMOV(neonScratchReg, tempReg1, tempReg2);
		VMOV(neonScratchReg2, tempReg3, tempReg3);
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VST1(F_32, neonScratchReg, scratchReg, 2, ALIGN_NONE);
	} else {
		for (int i = 0; i < 3; i++) {
			VMOV(fr[i], tr[i]);
			VCVT(fr[i], fr[i], TO_FLOAT | IS_SIGNED);
		}
		VSTMIA(scratchReg, false, fr[0], 3);
	}
}

// Copy 3 bytes and then a zero. Might as well copy four.
void VertexDecoderJitCache::Jit_PosS8() {
	LDRB(tempReg1, srcReg, dec_->posoff);
	LDRB(tempReg2, srcReg, dec_->posoff + 1);
	LDRB(tempReg3, srcReg, dec_->posoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 8));
	ORR(tempReg1, tempReg1, Operand2(tempReg3, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.posoff);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_PosS16() {
	LDRH(tempReg1, srcReg, dec_->posoff);
	LDRH(tempReg2, srcReg, dec_->posoff + 2);
	LDRH(tempReg3, srcReg, dec_->posoff + 4);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.posoff);
	STR(tempReg3, dstReg, dec_->decFmt.posoff + 4);
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
	ADD(scratchReg, srcReg, dec_->posoff);
	LDMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
	ADD(scratchReg, dstReg, dec_->decFmt.posoff);
	STMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
}

void VertexDecoderJitCache::Jit_NormalS8Skin() {
	if (NEONSkinning) {
		ADD(scratchReg, srcReg, dec_->nrmoff);
		VMOV_neon(F_32, Q3, by128);
		VLD1_lane(I_32, neonScratchReg, scratchReg, 0, false);
		VMOVL(I_8 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 16-bit
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, srcNEON, neonScratchReg, Q3);
	} else {
		LDRSB(tempReg1, srcReg, dec_->nrmoff);
		LDRSB(tempReg2, srcReg, dec_->nrmoff + 1);
		LDRSB(tempReg3, srcReg, dec_->nrmoff + 2);
		VMOV(src[0], tempReg1);
		VMOV(src[1], tempReg2);
		VMOV(src[2], tempReg3);
		MOVI2F(S15, by128, scratchReg);
		VCVT(src[0], src[0], TO_FLOAT | IS_SIGNED);
		VCVT(src[1], src[1], TO_FLOAT | IS_SIGNED);
		VCVT(src[2], src[2], TO_FLOAT | IS_SIGNED);
		VMUL(src[0], src[0], S15);
		VMUL(src[1], src[1], S15);
		VMUL(src[2], src[2], S15);
	}
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalS16Skin() {
	if (NEONSkinning) {
		ADD(scratchReg, srcReg, dec_->nrmoff);
		VMOV_neon(F_32, Q3, by32768);
		VLD1(I_32, neonScratchReg, scratchReg, 1, ALIGN_NONE);
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, srcNEON, neonScratchReg, Q3);
	} else {
		LDRSH(tempReg1,  srcReg, dec_->nrmoff);
		LDRSH(tempReg2, srcReg, dec_->nrmoff + 2);
		LDRSH(tempReg3, srcReg, dec_->nrmoff + 4);
		VMOV(fpScratchReg, tempReg1);
		VMOV(fpScratchReg2, tempReg2);
		VMOV(fpScratchReg3, tempReg3);
		MOVI2F(S15, by32768, scratchReg);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
		VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT | IS_SIGNED);
		VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT | IS_SIGNED);
		VMUL(src[0], fpScratchReg, S15);
		VMUL(src[1], fpScratchReg2, S15);
		VMUL(src[2], fpScratchReg3, S15);
	}
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalFloatSkin() {
	for (int i = 1; i < 3; ++i) {
		_dbg_assert_msg_(JIT, src[i - 1] + 1 == src[i], "VertexDecoder src regs must be in order.");
	}

	ADD(tempReg1, srcReg, dec_->nrmoff);
	if (NEONSkinning) {
		VLD1(F_32, srcNEON, tempReg1, 2, ALIGN_NONE);
	} else {
		VLDMIA(tempReg1, false, src[0], 3);
	}
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_WriteMatrixMul(int outOff, bool pos) {
	if (NEONSkinning) {
		// Multiply with the matrix sitting in Q4-Q7.
		ADD(scratchReg, dstReg, outOff);
		VMUL_scalar(F_32, accNEON, Q4, QScalar(srcNEON, 0));
		VMLA_scalar(F_32, accNEON, Q5, QScalar(srcNEON, 1));
		VMLA_scalar(F_32, accNEON, Q6, QScalar(srcNEON, 2));
		if (pos) {
			VADD(F_32, accNEON, accNEON, Q7);
		}
		VST1(F_32, accNEON, scratchReg, 2);
	} else {
		_dbg_assert_msg_(JIT, fpScratchReg + 1 == fpScratchReg2, "VertexDecoder fpScrathRegs must be in order.");
		_dbg_assert_msg_(JIT, fpScratchReg2 + 1 == fpScratchReg3, "VertexDecoder fpScrathRegs must be in order.");

		MOVP2R(tempReg1, skinMatrix);
		VLDMIA(tempReg1, true, fpScratchReg, 3);
		for (int i = 0; i < 3; i++) {
			VMUL(acc[i], ARMReg(fpScratchReg + i), src[0]);
		}
		VLDMIA(tempReg1, true, fpScratchReg, 3);
		for (int i = 0; i < 3; i++) {
			VMLA(acc[i], ARMReg(fpScratchReg + i), src[1]);
		}
		VLDMIA(tempReg1, true, fpScratchReg, 3);
		for (int i = 0; i < 3; i++) {
			VMLA(acc[i], ARMReg(fpScratchReg + i), src[2]);
		}
		if (pos) {
			VLDMIA(tempReg1, true, fpScratchReg, 3);
			for (int i = 0; i < 3; i++) {
				VADD(acc[i], acc[i], ARMReg(fpScratchReg + i));
			}
		}
		ADD(tempReg1, dstReg, outOff);
		VSTMIA(tempReg1, false, acc[0], 3);
	}
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
	if (NEONSkinning) {
		ADD(scratchReg, srcReg, dec_->posoff);
		VMOV_neon(F_32, Q3, by128);
		VLD1_lane(I_32, neonScratchReg, scratchReg, 0, false);
		VMOVL(I_8 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 16-bit
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, srcNEON, neonScratchReg, Q3);
	} else {
		LDRSB(tempReg1, srcReg, dec_->posoff);
		LDRSB(tempReg2, srcReg, dec_->posoff + 1);
		LDRSB(tempReg3, srcReg, dec_->posoff + 2);
		VMOV(src[0], tempReg1);
		VMOV(src[1], tempReg2);
		VMOV(src[2], tempReg3);
		MOVI2F(S15, 1.0f/128.0f, scratchReg);
		VCVT(src[0], src[0], TO_FLOAT | IS_SIGNED);
		VCVT(src[1], src[1], TO_FLOAT | IS_SIGNED);
		VCVT(src[2], src[2], TO_FLOAT | IS_SIGNED);
		VMUL(src[0], src[0], S15);
		VMUL(src[1], src[1], S15);
		VMUL(src[2], src[2], S15);
	}
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS16Skin() {
	if (NEONSkinning) {
		ADD(scratchReg, srcReg, dec_->posoff);
		VMOV_neon(F_32, Q3, by32768);
		VLD1(I_32, neonScratchReg, scratchReg, 1, ALIGN_NONE);
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, srcNEON, neonScratchReg, Q3);
	} else {
		LDRSH(tempReg1, srcReg, dec_->posoff);
		LDRSH(tempReg2, srcReg, dec_->posoff + 2);
		LDRSH(tempReg3, srcReg, dec_->posoff + 4);
		VMOV(src[0], tempReg1);
		VMOV(src[1], tempReg2);
		VMOV(src[2], tempReg3);
		MOVI2F(S15, by32768, scratchReg);
		VCVT(src[0], src[0], TO_FLOAT | IS_SIGNED);
		VCVT(src[1], src[1], TO_FLOAT | IS_SIGNED);
		VCVT(src[2], src[2], TO_FLOAT | IS_SIGNED);
		VMUL(src[0], src[0], S15);
		VMUL(src[1], src[1], S15);
		VMUL(src[2], src[2], S15);
	}
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosFloatSkin() {
	for (int i = 1; i < 3; ++i) {
		_dbg_assert_msg_(JIT, src[i - 1] + 1 == src[i], "VertexDecoder src regs must be in order.");
	}

	ADD(tempReg1, srcReg, dec_->posoff);
	if (NEONSkinning) {
		VLD1(F_32, srcNEON, tempReg1, 2, ALIGN_NONE);
	} else {
		VLDMIA(tempReg1, false, src[0], 3);
	}
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_AnyS8Morph(int srcoff, int dstoff) {
	const bool useNEON = NEONMorphing;
	ADDI2R(tempReg1, srcReg, srcoff, scratchReg);
	MOVP2R(tempReg2, &gstate_c.morphWeights[0]);

	if (useNEON) {
		MOVI2FR(scratchReg2, by127);
		VDUP(I_32, Q5, scratchReg2);
	} else {
		MOVI2F(S13, by127, scratchReg);
	}

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		if (useNEON) {
			VLD1_lane(I_32, neonScratchReg, tempReg1, 0, false);
			VLD1_all_lanes(F_32, Q3, tempReg2, true, REG_UPDATE);

			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VMOVL(I_8 | I_SIGNED, neonScratchRegQ, neonScratchReg);
			VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);

			VMUL(F_32, Q3, Q3, Q5);

			if (first) {
				first = false;
				VMUL(F_32, Q2, neonScratchRegQ, Q3);
			} else if (cpu_info.bVFPv4) {
				VFMA(F_32, Q2, neonScratchRegQ, Q3);
			} else {
				VMLA(F_32, Q2, neonScratchRegQ, Q3);
			}
		} else {
			LDRSB(scratchReg, tempReg1, 0);
			LDRSB(scratchReg2, tempReg1, 1);
			LDRSB(scratchReg3, tempReg1, 2);
			VMOV(fpScratchReg, scratchReg);
			VMOV(fpScratchReg2, scratchReg2);
			VMOV(fpScratchReg3, scratchReg3);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
			VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT | IS_SIGNED);
			VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT | IS_SIGNED);

			VLDR(S12, tempReg2, sizeof(float) * n);
			VMUL(S12, S12, S13);

			if (first) {
				first = false;
				VMUL(S8, fpScratchReg, S12);
				VMUL(S9, fpScratchReg2, S12);
				VMUL(S10, fpScratchReg3, S12);
			} else {
				VMLA(S8, fpScratchReg, S12);
				VMLA(S9, fpScratchReg2, S12);
				VMLA(S10, fpScratchReg3, S12);
			}
		}
	}

	ADDI2R(tempReg1, dstReg, dstoff, scratchReg);
	if (useNEON) {
		// TODO: Is it okay that we're over-writing by 4 bytes?  Probably...
		VSTMIA(tempReg1, false, D4, 2);
	} else {
		VSTMIA(tempReg1, false, S8, 3);
	}
}

void VertexDecoderJitCache::Jit_AnyS16Morph(int srcoff, int dstoff) {
	const bool useNEON = NEONMorphing;
	ADDI2R(tempReg1, srcReg, srcoff, scratchReg);
	MOVP2R(tempReg2, &gstate_c.morphWeights[0]);

	if (useNEON) {
		MOVI2FR(scratchReg, by32767);
		VDUP(I_32, Q5, scratchReg);
	} else {
		MOVI2F(S13, by32767, scratchReg);
	}

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		if (useNEON) {
			VLD1(I_32, neonScratchReg, tempReg1, 1, ALIGN_NONE);
			VLD1_all_lanes(F_32, Q3, tempReg2, true, REG_UPDATE);

			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);
			VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);

			VMUL(F_32, Q3, Q3, Q5);

			if (first) {
				first = false;
				VMUL(F_32, Q2, neonScratchRegQ, Q3);
			} else if (cpu_info.bVFPv4) {
				VFMA(F_32, Q2, neonScratchRegQ, Q3);
			} else {
				VMLA(F_32, Q2, neonScratchRegQ, Q3);
			}
		} else {
			LDRSH(scratchReg, tempReg1, 0);
			LDRSH(scratchReg2, tempReg1, 2);
			LDRSH(scratchReg3, tempReg1, 4);
			VMOV(fpScratchReg, scratchReg);
			VMOV(fpScratchReg2, scratchReg2);
			VMOV(fpScratchReg3, scratchReg3);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
			VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT | IS_SIGNED);
			VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT | IS_SIGNED);

			VLDR(S12, tempReg2, sizeof(float) * n);
			VMUL(S12, S12, S13);

			if (first) {
				first = false;
				VMUL(S8, fpScratchReg, S12);
				VMUL(S9, fpScratchReg2, S12);
				VMUL(S10, fpScratchReg3, S12);
			} else {
				VMLA(S8, fpScratchReg, S12);
				VMLA(S9, fpScratchReg2, S12);
				VMLA(S10, fpScratchReg3, S12);
			}
		}
	}

	ADDI2R(tempReg1, dstReg, dstoff, scratchReg);
	if (useNEON) {
		// TODO: Is it okay that we're over-writing by 4 bytes?  Probably...
		VSTMIA(tempReg1, false, D4, 2);
	} else {
		VSTMIA(tempReg1, false, S8, 3);
	}
}

void VertexDecoderJitCache::Jit_AnyFloatMorph(int srcoff, int dstoff) {
	const bool useNEON = NEONMorphing;
	ADDI2R(tempReg1, srcReg, srcoff, scratchReg);
	MOVP2R(tempReg2, &gstate_c.morphWeights[0]);

	bool first = true;
	for (int n = 0; n < dec_->morphcount; ++n) {
		if (useNEON) {
			// Load an extra float to stay in NEON mode.
			VLD1(F_32, neonScratchRegQ, tempReg1, 2, ALIGN_NONE);
			VLD1_all_lanes(F_32, Q3, tempReg2, true, REG_UPDATE);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);

			if (first) {
				first = false;
				VMUL(F_32, Q2, neonScratchRegQ, Q3);
			} else if (cpu_info.bVFPv4) {
				VFMA(F_32, Q2, neonScratchRegQ, Q3);
			} else {
				VMLA(F_32, Q2, neonScratchRegQ, Q3);
			}
		} else {
			// Load an extra float to stay in NEON mode.
			VLDMIA(tempReg1, false, fpScratchReg, 3);
			// Using VLDMIA to get writeback.
			VLDMIA(tempReg2, true, S12, 1);
			ADDI2R(tempReg1, tempReg1, dec_->onesize_, scratchReg);

			if (first) {
				first = false;
				VMUL(S8, fpScratchReg, S12);
				VMUL(S9, fpScratchReg2, S12);
				VMUL(S10, fpScratchReg3, S12);
			} else {
				VMLA(S8, fpScratchReg, S12);
				VMLA(S9, fpScratchReg2, S12);
				VMLA(S10, fpScratchReg3, S12);
			}
		}
	}

	ADDI2R(tempReg1, dstReg, dstoff, scratchReg);
	if (useNEON) {
		// TODO: Is it okay that we're over-writing by 4 bytes?  Probably...
		VSTMIA(tempReg1, false, D4, 2);
	} else {
		VSTMIA(tempReg1, false, S8, 3);
	}
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
