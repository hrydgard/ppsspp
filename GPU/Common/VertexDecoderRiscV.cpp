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

#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Common/RiscVEmitter.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "GPU/GPUState.h"
#include "GPU/Common/VertexDecoderCommon.h"

static const float by128 = 1.0f / 128.0f;
static const float by16384 = 1.0f / 16384.0f;
static const float by32768 = 1.0f / 32768.0f;
static const float const65535 = 65535.0f;

using namespace RiscVGen;

static const RiscVReg srcReg = X10;
static const RiscVReg dstReg = X11;
static const RiscVReg counterReg = X12;

static const RiscVReg tempReg1 = X13;
static const RiscVReg tempReg2 = X14;
static const RiscVReg tempReg3 = X15;
static const RiscVReg scratchReg = X16;

static const RiscVReg fullAlphaReg = X17;
static const RiscVReg boundsMinUReg = X28;
static const RiscVReg boundsMinVReg = X29;
static const RiscVReg boundsMaxUReg = X30;
static const RiscVReg boundsMaxVReg = X31;

static const RiscVReg fpScratchReg1 = F10;
static const RiscVReg fpScratchReg2 = F11;
static const RiscVReg fpScratchReg3 = F12;
static const RiscVReg fpSrc[3] = { F13, F14, F15 };

// TODO: Use vector, where supported.

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_TcU8ToFloat, &VertexDecoderJitCache::Jit_TcU8ToFloat},
	{&VertexDecoder::Step_TcU16ToFloat, &VertexDecoderJitCache::Jit_TcU16ToFloat},
	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},

	{&VertexDecoder::Step_TcU16ThroughToFloat, &VertexDecoderJitCache::Jit_TcU16ThroughToFloat},
	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},

	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoder::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoder::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},

	{&VertexDecoder::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoder::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoder::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	{&VertexDecoder::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloatThrough},

	{&VertexDecoder::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	{&VertexDecoder::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoder::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoder::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},
};

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec, int32_t *jittedSize) {
	dec_ = &dec;

	BeginWrite(4096);
	const u8 *start = AlignCode16();
	SetAutoCompress(true);

	bool log = false;

	if (dec.col) {
		// Or LDB and skip the conditional?  This is probably cheaper.
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

	// TODO: Skipping, prescale.

	const u8 *loopStart = GetCodePtr();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			EndWrite();
			// Reset the code ptr (effectively undoing what we generated) and return zero to indicate that we failed.
			ResetCodePtr(GetOffset(start));
			char temp[1024]{};
			dec.ToString(temp);
			ERROR_LOG(G3D, "Could not compile vertex decoder, failed at step %d: %s", i, temp);
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
		dec.ToString(temp);
		INFO_LOG(JIT, "=== %s (%d bytes) ===", temp, (int)(GetCodePtr() - start));
		std::vector<std::string> lines = DisassembleRV64(start, (int)(GetCodePtr() - start));
		for (auto line : lines) {
			INFO_LOG(JIT, "%s", line.c_str());
		}
		INFO_LOG(JIT, "==========");
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

	if (cpu_info.RiscV_B) {
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

void VertexDecoderJitCache::Jit_NormalS8() {
	LB(tempReg1, srcReg, dec_->nrmoff);
	LB(tempReg2, srcReg, dec_->nrmoff + 1);
	LB(tempReg3, srcReg, dec_->nrmoff + 2);
	SB(tempReg1, dstReg, dec_->decFmt.nrmoff);
	SB(tempReg2, dstReg, dec_->decFmt.nrmoff + 1);
	SB(tempReg3, dstReg, dec_->decFmt.nrmoff + 2);
	SB(R_ZERO, dstReg, dec_->decFmt.nrmoff + 3);
}

void VertexDecoderJitCache::Jit_NormalS16() {
	LH(tempReg1, srcReg, dec_->nrmoff);
	LH(tempReg2, srcReg, dec_->nrmoff + 2);
	LH(tempReg3, srcReg, dec_->nrmoff + 4);
	SH(tempReg1, dstReg, dec_->decFmt.nrmoff);
	SH(tempReg2, dstReg, dec_->decFmt.nrmoff + 2);
	SH(tempReg3, dstReg, dec_->decFmt.nrmoff + 4);
	SH(R_ZERO, dstReg, dec_->decFmt.nrmoff + 6);
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	// Just copy 12 bytes, play with over read/write later.
	LW(tempReg1, srcReg, dec_->nrmoff);
	LW(tempReg2, srcReg, dec_->nrmoff + 4);
	LW(tempReg3, srcReg, dec_->nrmoff + 8);
	SW(tempReg1, dstReg, dec_->decFmt.nrmoff);
	SW(tempReg2, dstReg, dec_->decFmt.nrmoff + 4);
	SW(tempReg3, dstReg, dec_->decFmt.nrmoff + 8);
}

void VertexDecoderJitCache::Jit_PosS8() {
	Jit_AnyS8ToFloat(dec_->posoff);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.posoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.posoff + 4);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16() {
	Jit_AnyS16ToFloat(dec_->posoff);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.posoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.posoff + 4);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosFloat() {
	// Just copy 12 bytes, play with over read/write later.
	LW(tempReg1, srcReg, dec_->posoff);
	LW(tempReg2, srcReg, dec_->posoff + 4);
	LW(tempReg3, srcReg, dec_->posoff + 8);
	SW(tempReg1, dstReg, dec_->decFmt.posoff);
	SW(tempReg2, dstReg, dec_->decFmt.posoff + 4);
	SW(tempReg3, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS8Through() {
	// 8-bit positions in throughmode always decode to 0, depth included.
	SW(R_ZERO, dstReg, dec_->decFmt.posoff);
	SW(R_ZERO, dstReg, dec_->decFmt.posoff + 4);
	SW(R_ZERO, dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosS16Through() {
	// Start with X and Y (which are signed.)
	LH(tempReg1, srcReg, dec_->posoff + 0);
	LH(tempReg2, srcReg, dec_->posoff + 2);
	// This one, Z, has to be unsigned.
	LHU(tempReg3, srcReg, dec_->posoff + 4);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::WU, fpSrc[2], tempReg3, Round::TOZERO);
	FS(32, fpSrc[0], dstReg, dec_->decFmt.posoff);
	FS(32, fpSrc[1], dstReg, dec_->decFmt.posoff + 4);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_PosFloatThrough() {
	// Start by copying 8 bytes, then handle Z separately to clamp it.
	LW(tempReg1, srcReg, dec_->posoff);
	LW(tempReg2, srcReg, dec_->posoff + 4);
	FL(32, fpSrc[2], srcReg, dec_->posoff + 8);
	SW(tempReg1, dstReg, dec_->decFmt.posoff);
	SW(tempReg2, dstReg, dec_->decFmt.posoff + 4);

	// Load the constants to clamp.  Maybe could static alloc this constant in a reg.
	LI(scratchReg, const65535);
	FMV(FMv::W, FMv::X, fpScratchReg1, R_ZERO);
	FMV(FMv::W, FMv::X, fpScratchReg2, scratchReg);

	FMAX(32, fpSrc[2], fpSrc[2], fpScratchReg1);
	FMIN(32, fpSrc[2], fpSrc[2], fpScratchReg2);
	FS(32, fpSrc[2], dstReg, dec_->decFmt.posoff + 8);
}

void VertexDecoderJitCache::Jit_Color8888() {
	LW(tempReg1, srcReg, dec_->coloff);

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

	// Set tempReg3 to -1 if the alpha bit is set.
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

void VertexDecoderJitCache::Jit_AnyS8ToFloat(int srcoff) {
	LB(tempReg1, srcReg, srcoff + 0);
	LB(tempReg2, srcReg, srcoff + 1);
	LB(tempReg3, srcReg, srcoff + 2);
	// TODO: Could maybe static alloc?
	LI(scratchReg, by128);
	FMV(FMv::W, FMv::X, fpScratchReg1, scratchReg);
	FCVT(FConv::S, FConv::W, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg1);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg1);
	FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg1);
}

void VertexDecoderJitCache::Jit_AnyS16ToFloat(int srcoff) {
	LH(tempReg1, srcReg, srcoff + 0);
	LH(tempReg2, srcReg, srcoff + 2);
	LH(tempReg3, srcReg, srcoff + 4);
	// TODO: Could maybe static alloc?
	LI(scratchReg, by32768);
	FMV(FMv::W, FMv::X, fpScratchReg1, scratchReg);
	FCVT(FConv::S, FConv::W, fpSrc[0], tempReg1, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[1], tempReg2, Round::TOZERO);
	FCVT(FConv::S, FConv::W, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg1);
	FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg1);
	FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg1);
}

void VertexDecoderJitCache::Jit_AnyU8ToFloat(int srcoff, u32 bits) {
	_dbg_assert_msg_((bits & ~(16 | 8)) == 0, "Bits must be a multiple of 8.");
	_dbg_assert_msg_(bits >= 8 && bits <= 24, "Bits must be a between 8 and 24.");

	LBU(tempReg1, srcReg, srcoff + 0);
	if (bits >= 16)
		LBU(tempReg2, srcReg, srcoff + 1);
	if (bits >= 24)
		LBU(tempReg3, srcReg, srcoff + 2);
	// TODO: Could maybe static alloc?
	LI(scratchReg, by128);
	FMV(FMv::W, FMv::X, fpScratchReg1, scratchReg);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	if (bits >= 16)
		FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	if (bits >= 24)
		FCVT(FConv::S, FConv::WU, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg1);
	if (bits >= 16)
		FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg1);
	if (bits >= 24)
		FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg1);
}

void VertexDecoderJitCache::Jit_AnyU16ToFloat(int srcoff, u32 bits) {
	_dbg_assert_msg_((bits & ~(32 | 16)) == 0, "Bits must be a multiple of 16.");
	_dbg_assert_msg_(bits >= 16 && bits <= 48, "Bits must be a between 16 and 48.");

	LHU(tempReg1, srcReg, srcoff + 0);
	if (bits >= 32)
		LHU(tempReg2, srcReg, srcoff + 2);
	if (bits >= 48)
		LHU(tempReg3, srcReg, srcoff + 4);
	// TODO: Could maybe static alloc?
	LI(scratchReg, by32768);
	FMV(FMv::W, FMv::X, fpScratchReg1, scratchReg);
	FCVT(FConv::S, FConv::WU, fpSrc[0], tempReg1, Round::TOZERO);
	if (bits >= 32)
		FCVT(FConv::S, FConv::WU, fpSrc[1], tempReg2, Round::TOZERO);
	if (bits >= 48)
		FCVT(FConv::S, FConv::WU, fpSrc[2], tempReg3, Round::TOZERO);
	FMUL(32, fpSrc[0], fpSrc[0], fpScratchReg1);
	if (bits >= 32)
		FMUL(32, fpSrc[1], fpSrc[1], fpScratchReg1);
	if (bits >= 48)
		FMUL(32, fpSrc[2], fpSrc[2], fpScratchReg1);
}

#endif // PPSSPP_ARCH(RISCV64)
