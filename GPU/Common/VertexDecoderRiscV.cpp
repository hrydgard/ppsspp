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
static const RiscVReg fpScratchReg4 = F13;

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
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

void VertexDecoderJitCache::Jit_TcFloat() {
	// Just copy 64 bits.  Might be nice if we could detect misaligned load perf.
	LW(tempReg1, srcReg, dec_->tcoff);
	LW(tempReg2, srcReg, dec_->tcoff + 4);
	SW(tempReg1, dstReg, dec_->decFmt.uvoff);
	SW(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

#endif // PPSSPP_ARCH(RISCV64)
