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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <emmintrin.h>
#include "Common/x64Emitter.h"
#include "GPU/Software/Sampler.h"
#include "GPU/ge_constants.h"

using namespace Gen;

namespace Sampler {

#ifdef _WIN32
static const X64Reg resultReg = RAX;
static const X64Reg tempReg1 = R10;
static const X64Reg tempReg2 = R11;
static const X64Reg uReg = RCX;
static const X64Reg vReg = RDX;
static const X64Reg srcReg = R8;
static const X64Reg bufwReg = R9;
// TODO: levelReg on stack
#else
static const X64Reg resultReg = RAX;
static const X64Reg tempReg1 = R9;
static const X64Reg tempReg2 = R10;
static const X64Reg uReg = RDI;
static const X64Reg vReg = RSI
static const X64Reg srcReg = RDX;
static const X64Reg bufwReg = RCX;
static const X64Reg levelReg = R8;
#endif

NearestFunc SamplerJitCache::Compile(const SamplerID &id) {
	BeginWrite();
	const u8 *start = this->AlignCode16();

	SUB(PTRBITS, R(ESP), Imm8(64));
	MOVUPS(MDisp(ESP, 0), XMM4);
	MOVUPS(MDisp(ESP, 16), XMM5);
	MOVUPS(MDisp(ESP, 32), XMM6);
	MOVUPS(MDisp(ESP, 48), XMM7);

	// Early exit on !srcPtr.
	CMP(PTRBITS, R(srcReg), Imm32(0));
	FixupBranch nonZeroSrc = J_CC(CC_NZ);
	XOR(32, R(RAX), R(RAX));
	FixupBranch zeroSrc = J(true);
	SetJumpTarget(nonZeroSrc);

	if (!Jit_ReadTextureFormat(id)) {
		EndWrite();
		SetCodePtr(const_cast<u8 *>(start));
		return nullptr;
	}

	SetJumpTarget(zeroSrc);

	MOVUPS(XMM4, MDisp(ESP, 0));
	MOVUPS(XMM5, MDisp(ESP, 16));
	MOVUPS(XMM6, MDisp(ESP, 32));
	MOVUPS(XMM7, MDisp(ESP, 48));
	ADD(PTRBITS, R(ESP), Imm8(64));

	RET();

	EndWrite();
	return (NearestFunc)start;
}

bool SamplerJitCache::Jit_ReadTextureFormat(const SamplerID &id) {
	GETextureFormat fmt = (GETextureFormat)id.texfmt;
	bool success = true;
	switch (fmt) {
	case GE_TFMT_5650:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode5650();
		break;

	case GE_TFMT_5551:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode5551();
		break;

	case GE_TFMT_4444:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_Decode4444();
		break;

	case GE_TFMT_8888:
		success = Jit_GetTexData(id, 32);
		break;

	case GE_TFMT_CLUT32:
		success = Jit_GetTexData(id, 32);
		if (success)
			success = Jit_TransformClutIndex(id);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	case GE_TFMT_CLUT16:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_TransformClutIndex(id);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	case GE_TFMT_CLUT8:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_TransformClutIndex(id);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	case GE_TFMT_CLUT4:
		success = Jit_GetTexData(id, 16);
		if (success)
			success = Jit_TransformClutIndex(id);
		if (success)
			success = Jit_ReadClutColor(id);
		break;

	default:
		success = false;
	}

	return success;
}

bool SamplerJitCache::Jit_GetTexData(const SamplerID &id, int bitsPerTexel) {
	if (id.swizzle) {
		return Jit_GetTexDataSwizzled(id, bitsPerTexel);
	}

	// srcReg might be EDX, so let's copy that before we multiply.
	switch (bitsPerTexel) {
	case 32:
	case 16:
	case 8:
		LEA(64, tempReg1, MComplex(srcReg, uReg, bitsPerTexel / 8, 0));
		break;

	case 4: {
		XOR(32, R(tempReg2), R(tempReg2));
		SHR(32, R(uReg), Imm8(1));
		FixupBranch skip = J_CC(CC_NC);
		// Track whether we shifted a 1 off or not.
		MOV(32, R(tempReg2), Imm32(4));
		SetJumpTarget(skip);
		LEA(64, tempReg1, MRegSum(srcReg, uReg));
		break;
	}

	default:
		return false;
	}

	MOV(32, R(EAX), R(vReg));
	MUL(32, R(bufwReg));

	switch (bitsPerTexel) {
	case 32:
	case 16:
	case 8:
		MOV(bitsPerTexel, R(resultReg), MComplex(tempReg1, RAX, bitsPerTexel / 8, 0));
		break;

	case 4: {
		SHR(32, R(RAX), Imm8(1));
		MOV(8, R(resultReg), MRegSum(tempReg1, RAX));
		// RCX is now free.
		MOV(8, R(RCX), R(tempReg2));
		SHR(8, R(resultReg), R(RCX));
		// Zero out any bits not shifted off.
		AND(32, R(resultReg), Imm32(0x0000000F));
		break;
	}

	default:
		return false;
	}

	return true;
}

bool SamplerJitCache::Jit_GetTexDataSwizzled(const SamplerID &id, int bitsPerTexel) {
	LEA(32, tempReg1, MScaled(vReg, SCALE_4, 0));
	AND(32, R(tempReg1), Imm32(31));
	if (bitsPerTexel != 4) {
		AND(32, R(vReg), Imm32(~7));
	}

	MOV(32, R(tempReg2), R(uReg));
	MOV(32, R(resultReg), R(uReg));
	switch (bitsPerTexel) {
	case 32:
		SHR(32, R(resultReg), Imm8(2));
		break;
	case 16:
		SHR(32, R(vReg), Imm8(1));
		SHR(32, R(tempReg2), Imm8(1));
		SHR(32, R(resultReg), Imm8(3));
		break;
	case 8:
		SHR(32, R(vReg), Imm8(2));
		SHR(32, R(tempReg2), Imm8(2));
		SHR(32, R(resultReg), Imm8(4));
		break;
	case 4:
		SHR(32, R(vReg), Imm8(3));
		SHR(32, R(tempReg2), Imm8(3));
		SHR(32, R(resultReg), Imm8(5));
		break;
	default:
		return false;
	}
	AND(32, R(tempReg2), Imm32(3));
	SHL(32, R(resultReg), Imm8(5));
	ADD(32, R(tempReg1), R(tempReg2));
	ADD(32, R(tempReg1), R(resultReg));

	// We may clobber srcReg in the MUL, so let's grab it now.
	LEA(64, tempReg1, MComplex(srcReg, tempReg1, SCALE_4, 0));

	LEA(32, EAX, MScaled(bufwReg, SCALE_4, 0));
	MUL(32, R(vReg));

	switch (bitsPerTexel) {
	case 32:
		MOV(bitsPerTexel, R(resultReg), MRegSum(tempReg1, EAX));
		break;
	case 16:
		AND(32, R(uReg), Imm32(1));
		// Multiply by two by just adding twice.
		ADD(32, R(EAX), R(uReg));
		ADD(32, R(EAX), R(uReg));
		MOV(bitsPerTexel, R(resultReg), MRegSum(tempReg1, EAX));
		break;
	case 8:
		AND(32, R(uReg), Imm32(3));
		ADD(32, R(EAX), R(uReg));
		MOV(bitsPerTexel, R(resultReg), MRegSum(tempReg1, EAX));
		break;
	case 4: {
		AND(32, R(uReg), Imm32(7));
		SHR(32, R(uReg), Imm8(1));
		// Note: LEA/MOV do not affect flags (unlike ADD.)  uReg may be RCX.
		LEA(64, tempReg1, MRegSum(tempReg1, uReg));
		MOV(8, R(resultReg), MRegSum(tempReg1, EAX));
		FixupBranch skipNonZero = J_CC(CC_NC);
		SHR(32, R(resultReg), Imm32(4));
		SetJumpTarget(skipNonZero);
		// Zero out the rest.
		AND(32, R(resultReg), Imm32(0x0000000F));
		break;
	}
	default:
		return false;
	}

	return true;
}

bool SamplerJitCache::Jit_Decode5650() {
	return false;
}

bool SamplerJitCache::Jit_Decode5551() {
	return false;
}

bool SamplerJitCache::Jit_Decode4444() {
	return false;
}

bool SamplerJitCache::Jit_TransformClutIndex(const SamplerID &id) {
	return false;
}

bool SamplerJitCache::Jit_ReadClutColor(const SamplerID &id) {
	return false;
}

};

#endif
