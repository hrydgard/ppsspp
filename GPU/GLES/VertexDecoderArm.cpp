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
#include "GPU/GLES/VertexDecoder.h"

extern void DisassembleArm(const u8 *data, int size);

bool NEONSkinning = false;

// Used only in non-NEON mode.
static float MEMORY_ALIGNED16(skinMatrix[12]);

// Will be used only in NEON mode.
static float MEMORY_ALIGNED16(bones[16 * 8]);  // First two are kept in registers

// NEON register allocation:
// Q0: Texture scaling parameters
// Q1: Temp storage
// Q2: Vector-by-matrix accumulator
// Q3: Unused
//
// We'll use Q4-Q7 as the "matrix accumulator".
// First two matrices will be preloaded into Q8-Q11 and Q12-Q15 to reduce
// memory bandwidth requirements.
// The rest will be dumped to bones as on x86.


static const float by128 = 1.0f / 128.0f;
static const float by256 = 1.0f / 256.0f;
static const float by32768 = 1.0f / 32768.0f;

using namespace ArmGen;

static const ARMReg tempReg1 = R3;
static const ARMReg tempReg2 = R4;
static const ARMReg tempReg3 = R5;
static const ARMReg scratchReg = R6;
static const ARMReg scratchReg2 = R7;
static const ARMReg scratchReg3 = R12;
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
};

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec) {
	dec_ = &dec;
	const u8 *start = AlignCode16();

	bool prescaleStep = false;
	bool skinning = false;

	NEONSkinning = cpu_info.bNEON;

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

	PUSH(6, R4, R5, R6, R7, R8, _LR);

	// Keep the scale/offset in a few fp registers if we need it.
	// This step can be NEON-ized but the savings would be miniscule.
	if (prescaleStep) {
		MOVI2R(R3, (u32)(&gstate_c.uv), scratchReg);
		VLDR(fpUscaleReg, R3, 0);
		VLDR(fpVscaleReg, R3, 4);
		VLDR(fpUoffsetReg, R3, 8);
		VLDR(fpVoffsetReg, R3, 12);
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

	// Add code to convert matrices to 4x4.
	// Later we might want to do this when the matrices are loaded instead.
	int boneCount = 0;
	if (NEONSkinning && dec.weighttype && g_Config.bSoftwareSkinning) {
		// Copying from R3 to R4
		MOVP2R(R3, gstate.boneMatrix);
		MOVP2R(R4, bones);
		MOVI2F(fpScratchReg, 0.0f, scratchReg);
		for (int i = 0; i < 8; i++) {
			VLD1(F_32, Q4, R3, 2);  // Load 128 bits even though we just want 96
			VMOV(S19, fpScratchReg);
			ADD(R3, R3, 12);
			VLD1(F_32, Q5, R3, 2);
			VMOV(S23, fpScratchReg);
			ADD(R3, R3, 12);
			VLD1(F_32, Q6, R3, 2);
			VMOV(S27, fpScratchReg);
			ADD(R3, R3, 12);
			VLD1(F_32, Q7, R3, 2);
			VMOV(S31, fpScratchReg);
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

	JumpTarget loopStart = GetCodePtr();
	// Preload data cache ahead of reading. TODO: Experiment with the offset.
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

	POP(6, R4, R5, R6, R7, R8, _PC);

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
static const ARMReg neonWeightRegs[2] = { Q2, Q3 };

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
				VMUL_scalar(F_32, Q4, Q8, QScalar(neonWeightRegs[0], 0));
				VMUL_scalar(F_32, Q5, Q9, QScalar(neonWeightRegs[0], 0));
				VMUL_scalar(F_32, Q6, Q10, QScalar(neonWeightRegs[0], 0));
				VMUL_scalar(F_32, Q7, Q11, QScalar(neonWeightRegs[0], 0));
				break;
			case 1:
				// Krait likes VDUP + VFMA better than VMLA, and it's easy to do here.
				if (cpu_info.bVFPv4) {
					VDUP(F_32, Q1, neonWeightRegs[i >> 2], i & 1);
					VFMA(Q4, Q12, Q1);
					VFMA(Q5, Q13, Q1);
					VFMA(Q6, Q14, Q1);
					VFMA(Q7, Q15, Q1);
				} else {
					VMLA_scalar(F_32, Q4, Q12, QScalar(neonWeightRegs[0], 1));
					VMLA_scalar(F_32, Q5, Q13, QScalar(neonWeightRegs[0], 1));
					VMLA_scalar(F_32, Q6, Q14, QScalar(neonWeightRegs[0], 1));
					VMLA_scalar(F_32, Q7, Q15, QScalar(neonWeightRegs[0], 1));
				}
				break;
			default:
				// Matrices 2+ need to be loaded from memory.
				// Wonder if we can free up one more register so we could get some parallelism.
				// Actually Q3 is free if there are fewer than 5 weights...
				if (dec_->nweights <= 4) {
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VLD1(F_32, Q3, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q4, Q1, QScalar(neonWeightRegs[i >> 2], i & 3));
					VMLA_scalar(F_32, Q5, Q3, QScalar(neonWeightRegs[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VLD1(F_32, Q3, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q6, Q1, QScalar(neonWeightRegs[i >> 2], i & 3));
					VMLA_scalar(F_32, Q7, Q3, QScalar(neonWeightRegs[i >> 2], i & 3));
				} else {
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q4, Q1, QScalar(neonWeightRegs[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q5, Q1, QScalar(neonWeightRegs[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q6, Q1, QScalar(neonWeightRegs[i >> 2], i & 3));
					VLD1(F_32, Q1, scratchReg, 2, ALIGN_128, REG_UPDATE);
					VMLA_scalar(F_32, Q7, Q1, QScalar(neonWeightRegs[i >> 2], i & 3));
				}
				break;
			}
		}
	} else {
		MOVI2R(tempReg2, (u32)skinMatrix, scratchReg);
		// This approach saves a few stores but accesses the matrices in a more
		// sparse order.
		const float *bone = &gstate.boneMatrix[0];
		MOVI2R(tempReg1, (u32)bone, scratchReg);
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
	if (NEONSkinning && dec_->nweights <= 4) {
		// Most common cases.
		// Weight is first so srcReg is correct.
		switch (dec_->nweights) {
		case 1: LDRB(scratchReg2, srcReg, 0); break;
		case 2: LDRH(scratchReg2, srcReg, 0); break;
		case 3:
			LDR(scratchReg2, srcReg, 0);
			ANDI2R(scratchReg2, scratchReg2, 0xFFFFFF, scratchReg);
			break;
		case 4:
			LDR(scratchReg2, srcReg, 0);
			break;
		}
		VMOV(fpScratchReg, scratchReg2);
		MOVI2F(S12, by128, scratchReg);
		VMOVL(I_8 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL_scalar(F_32, neonWeightRegs[0], neonScratchRegQ, DScalar(D6, 0));
	} else {
		// Fallback and non-neon
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
	if (NEONSkinning && dec_->nweights <= 4) {
		// Most common cases.
		switch (dec_->nweights) {
		case 1: LDRH(scratchReg, srcReg, 0); break;
		case 2: LDR(scratchReg, srcReg, 0); break;
		case 3:
			LDR(scratchReg, srcReg, 0);
			LDRH(scratchReg2, srcReg, 4);
			break;
		case 4:
			LDR(scratchReg, srcReg, 0);
			LDR(scratchReg2, srcReg, 4);
			break;
		}
		VMOV(fpScratchReg, scratchReg);
		VMOV(fpScratchReg2, scratchReg2);
		MOVI2F(S12, by32768, scratchReg);
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL_scalar(F_32, neonWeightRegs[0], neonScratchRegQ, DScalar(D6, 0));
	} else {
		// Fallback and non-neon
		for (int j = 0; j < dec_->nweights; j++) {
			LDRH(tempReg1, srcReg, dec_->weightoff + j * 2);
			VMOV(fpScratchReg, tempReg1);
			VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
			MOVI2F(fpScratchReg2, 1.0f / 32768.0f, scratchReg);
			VMUL(weightRegs[j], fpScratchReg, fpScratchReg2);
		}
	}
	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
	// TODO: NEON-ize (barely worth)
	for (int j = 0; j < dec_->nweights; j++) {
		VLDR(weightRegs[j], srcReg, dec_->weightoff + j * 4);
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
		// TODO: SIMD
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
		// TODO: SIMD
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
	STR(tempReg1, dstReg, dec_->decFmt.c0off);
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

	// And saturate.
	ORR(tempReg1, tempReg2, Operand2(tempReg2, ST_LSL, 4));

	STR(tempReg1, dstReg, dec_->decFmt.c0off);
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

	// Add in full alpha.
	ORI2R(tempReg1, tempReg2, 0xFF000000, scratchReg);

	STR(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color5551() {
	LDRH(tempReg1, srcReg, dec_->coloff);

	ANDI2R(tempReg2, tempReg1, 0x001F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0x07E0, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 3));
	ANDI2R(tempReg3, tempReg1, 0xF800, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 6));

	// Expand 5 -> 8.
	LSR(tempReg3, tempReg2, 2);
	// Clean up the bits that were shifted right.
	BIC(tempReg3, tempReg1, AssumeMakeOperand2(0x000000F8));
	BIC(tempReg3, tempReg3, AssumeMakeOperand2(0x0000F800));
	ORR(tempReg2, tempReg3, Operand2(tempReg2, ST_LSL, 3));

	// Now we just need alpha.
	TSTI2R(tempReg1, 0x8000, scratchReg);
	SetCC(CC_NEQ);
	ORI2R(tempReg2, tempReg2, 0xFF000000, scratchReg);
	SetCC(CC_AL);

	STR(tempReg2, dstReg, dec_->decFmt.c0off);
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
	// Might not be aligned to 4, so we can't use LDMIA.
	// Actually - not true: This will always be aligned. TODO
	LDR(tempReg1, srcReg, dec_->nrmoff);
	LDR(tempReg2, srcReg, dec_->nrmoff + 4);
	LDR(tempReg3, srcReg, dec_->nrmoff + 8);
	// But this is always aligned to 4 so we're safe.
	ADD(scratchReg, dstReg, dec_->decFmt.nrmoff);
	STMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS8Through() {
	// TODO: SIMD
	LDRSB(tempReg1, srcReg, dec_->posoff);
	LDRSB(tempReg2, srcReg, dec_->posoff + 1);
	LDRSB(tempReg3, srcReg, dec_->posoff + 2);
	static const ARMReg tr[3] = { tempReg1, tempReg2, tempReg3 };
	for (int i = 0; i < 3; i++) {
		VMOV(fpScratchReg, tr[i]);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.posoff + i * 4);
	}
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS16Through() {
	// TODO: SIMD
	LDRSH(tempReg1, srcReg, dec_->posoff);
	LDRSH(tempReg2, srcReg, dec_->posoff + 2);
	LDRSH(tempReg3, srcReg, dec_->posoff + 4);
	static const ARMReg tr[3] = { tempReg1, tempReg2, tempReg3 };
	for (int i = 0; i < 3; i++) {
		VMOV(fpScratchReg, tr[i]);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.posoff + i * 4);
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
	LDR(tempReg1, srcReg, dec_->posoff);
	LDR(tempReg2, srcReg, dec_->posoff + 4);
	LDR(tempReg3, srcReg, dec_->posoff + 8);
	// But this is always aligned to 4 so we're safe.
	ADD(scratchReg, dstReg, dec_->decFmt.posoff);
	STMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
}

void VertexDecoderJitCache::Jit_NormalS8Skin() {
	if (NEONSkinning) {
		ADD(scratchReg, srcReg, dec_->nrmoff);
		VLD1_lane(I_32, neonScratchReg, scratchReg, 0, false);
		MOVI2F(S15, 1.0f/128.0f, scratchReg);
		VMOVL(I_8 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 16-bit
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL_scalar(F_32, srcNEON, neonScratchReg, QScalar(Q3, 3));  // S15
	} else {
		LDRSB(tempReg1, srcReg, dec_->nrmoff);
		LDRSB(tempReg2, srcReg, dec_->nrmoff + 1);
		LDRSB(tempReg3, srcReg, dec_->nrmoff + 2);
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
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalS16Skin() {
	if (NEONSkinning) {
		ADD(scratchReg, srcReg, dec_->nrmoff);
		VLD1(I_32, neonScratchReg, scratchReg, 1, ALIGN_NONE);
		MOVI2F(S15, 1.0f/32768, scratchReg);
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL_scalar(F_32, srcNEON, neonScratchReg, QScalar(Q3, 3));  // S15
	} else {
		LDRSH(tempReg1,  srcReg, dec_->nrmoff);
		LDRSH(tempReg2, srcReg, dec_->nrmoff + 2);
		LDRSH(tempReg3, srcReg, dec_->nrmoff + 4);
		VMOV(fpScratchReg, tempReg1);
		VMOV(fpScratchReg2, tempReg2);
		VMOV(fpScratchReg3, tempReg3);
		MOVI2F(S15, 1.0f/32768.0f, scratchReg);
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
	VLDR(src[0], srcReg, dec_->nrmoff);
	VLDR(src[1], srcReg, dec_->nrmoff + 4);
	VLDR(src[2], srcReg, dec_->nrmoff + 8);
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
		MOVI2R(tempReg1, (u32)skinMatrix, scratchReg);
		for (int i = 0; i < 3; i++) {
			VLDR(fpScratchReg, tempReg1, 4 * i);
			VMUL(acc[i], fpScratchReg, src[0]);
		}
		for (int i = 0; i < 3; i++) {
			VLDR(fpScratchReg, tempReg1, 12 + 4 * i);
			VMLA(acc[i], fpScratchReg, src[1]);
		}
		for (int i = 0; i < 3; i++) {
			VLDR(fpScratchReg, tempReg1, 24 + 4 * i);
			VMLA(acc[i], fpScratchReg, src[2]);
		}
		if (pos) {
			for (int i = 0; i < 3; i++) {
				VLDR(fpScratchReg, tempReg1, 36 + 4 * i);
				VADD(acc[i], acc[i], fpScratchReg);
			}
		}
		for (int i = 0; i < 3; i++) {
			VSTR(acc[i], dstReg, outOff + i * 4);
		}
	}
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
	if (NEONSkinning) {
		ADD(scratchReg, srcReg, dec_->posoff);
		VLD1_lane(I_32, neonScratchReg, scratchReg, 0, false);
		MOVI2F(S15, 1.0f/128.0f, scratchReg);
		VMOVL(I_8 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 16-bit
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL_scalar(F_32, srcNEON, neonScratchReg, QScalar(Q3, 3));  // S15
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
		VLD1(I_32, neonScratchReg, scratchReg, 1, ALIGN_NONE);
		MOVI2F(S15, 1.0f/32768, scratchReg);
		VMOVL(I_16 | I_SIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_SIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL_scalar(F_32, srcNEON, neonScratchReg, QScalar(Q3, 3));  // S15
	} else {
		LDRSH(tempReg1, srcReg, dec_->posoff);
		LDRSH(tempReg2, srcReg, dec_->posoff + 2);
		LDRSH(tempReg3, srcReg, dec_->posoff + 4);
		VMOV(src[0], tempReg1);
		VMOV(src[1], tempReg2);
		VMOV(src[2], tempReg3);
		MOVI2F(S15, 1.0f/32768.0f, scratchReg);
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
	VLDR(src[0], srcReg, dec_->posoff);
	VLDR(src[1], srcReg, dec_->posoff + 4);
	VLDR(src[2], srcReg, dec_->posoff + 8);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
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
