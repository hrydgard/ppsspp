// Copyright (c) 2012- PPSSPP Project.

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

#include <cmath>
#include <limits>
#include <mutex>
#include <utility>

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS) && PPSSPP_ARCH(ARM64)
#include <arm64intr.h>
#endif

#include "Common/CommonTypes.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/ConfigValues.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/Interpreter.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Core/Reporting.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"

MIPSState mipsr4k;
MIPSState *currentMIPS = &mipsr4k;
MIPSDebugInterface debugr4k(&mipsr4k);
MIPSDebugInterface *currentDebugMIPS = &debugr4k;

#if PPSSPP_ARCH(ARM64)

static inline u64 ARM64ReadFPCR() {
#if PPSSPP_PLATFORM(WINDOWS)
	return _ReadStatusReg(ARM64_FPCR);
#else
	// TODO: Try __builtin_arm_get_fpcr()
	u64 fpcr;  // not really 64-bit, just to match the register size.
	asm volatile ("mrs %0, fpcr" : "=r" (fpcr));
	return fpcr;
#endif
}

static inline void ARM64WriteFPCR(u64 fpcr) {
#if PPSSPP_PLATFORM(WINDOWS)
	_WriteStatusReg(ARM64_FPCR, fpcr);
#else
	// TODO: Try __builtin_arm_set_fpcr()
	// Write back the modified FPCR
	asm volatile ("msr fpcr, %0" : : "r" (fpcr));
#endif
}

#endif

void ApplyHostRoundingMode(const MIPSState *mips) {
	u32 fcr1Bits = mips->fcr31 & 0x01000003;
	// If these are 0, we just leave things as they are.
	if (fcr1Bits) {
		int rmode = fcr1Bits & 3;
		bool ftz = (fcr1Bits & 0x01000000) != 0;
#if PPSSPP_ARCH(SSE2)
		u32 csr = _mm_getcsr() & ~0x6000;
		// Translate the rounding mode bits to X86, the same way as in Asm.cpp.
		if (rmode & 1) {
			rmode ^= 2;
		}
		csr |= rmode << 13;

		if (ftz) {
			// Flush to zero
			csr |= 0x8000;
		}
		_mm_setcsr(csr);
#elif PPSSPP_ARCH(ARM64)
		u64 fpcr = ARM64ReadFPCR();
		// Translate MIPS to ARM rounding mode
		static const u8 lookup[4] = {0, 3, 1, 2};

		fpcr &= ~(3 << 22);    // Clear bits [23:22]
		fpcr |= ((u64)lookup[rmode] << 22);

		if (ftz) {
			fpcr |= 1 << 24;
		}

		ARM64WriteFPCR(fpcr);
#endif
	}
}

void RestoreHostRoundingMode() {
	// TODO: We should avoid this if we didn't apply rounding in the first place.
	// In the meantime, clear out FTZ and rounding mode bits.
#if PPSSPP_ARCH(SSE2)
	u32 csr = _mm_getcsr();
	csr &= ~(7 << 13);
	_mm_setcsr(csr);
#elif PPSSPP_ARCH(ARM64)
	u64 fpcr = ARM64ReadFPCR();  // not really 64-bit, just to match the register size.
	fpcr &= ~(7 << 22);    // Clear bits [23:22] for rounding, 24 for FTZ
	// Write back the modified FPCR
	ARM64WriteFPCR(fpcr);
#endif
}

u8 voffset[128];
u8 fromvoffset[128];

#ifndef M_LOG2E
#define M_E        2.71828182845904523536f
#define M_LOG2E    1.44269504088896340736f
#define M_LOG10E   0.434294481903251827651f
#define M_LN2      0.693147180559945309417f
#define M_LN10     2.30258509299404568402f
#undef M_PI
#define M_PI       3.14159265358979323846f

#ifndef M_PI_2
#define M_PI_2     1.57079632679489661923f
#endif
#define M_PI_4     0.785398163397448309616f
#define M_1_PI     0.318309886183790671538f
#define M_2_PI     0.636619772367581343076f
#define M_2_SQRTPI 1.12837916709551257390f
#define M_SQRT2    1.41421356237309504880f
#define M_SQRT1_2  0.707106781186547524401f
#endif

const float cst_constants[32] = {
	0,
	std::numeric_limits<float>::max(),  // all these are verified on real PSP
	sqrtf(2.0f),
	sqrtf(0.5f),
	2.0f/sqrtf((float)M_PI),
	2.0f/(float)M_PI,
	1.0f/(float)M_PI,
	(float)M_PI/4,
	(float)M_PI/2,
	(float)M_PI,
	(float)M_E,
	(float)M_LOG2E,
	(float)M_LOG10E,
	(float)M_LN2,
	(float)M_LN10,
	2*(float)M_PI,
	(float)M_PI/6,
	log10f(2.0f),
	logf(10.0f)/logf(2.0f),
	sqrtf(3.0f)/2.0f,
};

MIPSState::MIPSState() {
	MIPSComp::jit = nullptr;

	// Initialize vorder

	// This reordering of the VFPU registers in RAM means that instead of being like this:

	// 0x00 0x20 0x40 0x60 -> "columns", the most common direction
	// 0x01 0x21 0x41 0x61
	// 0x02 0x22 0x42 0x62
	// 0x03 0x23 0x43 0x63

	// 0x04 0x24 0x44 0x64
	// 0x06 0x26 0x45 0x65
	// ....

	// the VPU registers are effectively organized like this:
	// 0x00 0x01 0x02 0x03
	// 0x04 0x05 0x06 0x07
	// 0x08 0x09 0x0a 0x0b
	// ....

	// This is because the original indices look like this:
	// 0XXMMMYY where M is the matrix number.

	// We will now map 0YYMMMXX to 0MMMXXYY.

	// Advantages:
	// * Columns can be flushed and reloaded faster "at once"
	// * 4x4 Matrices are contiguous in RAM, making them, too, fast-loadable in NEON

	// Disadvantages:
	// * Extra indirection, can be confusing and slower (interpreter only, however we can often skip the table by rerranging formulas)
	// * Flushing and reloading row registers is now slower

	int i = 0;
	for (int m = 0; m < 8; m++) {
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				voffset[m * 4 + x * 32 + y] = i++;
			}
		}
	}

	// And the inverse.
	for (int i = 0; i < 128; i++) {
		fromvoffset[voffset[i]] = i;
	}

	// Sanity check that things that should be ordered are ordered.
	static const u8 firstThirtyTwo[] = {
		0x0, 0x20, 0x40, 0x60,
		0x1, 0x21, 0x41, 0x61,
		0x2, 0x22, 0x42, 0x62,
		0x3, 0x23, 0x43, 0x63,

		0x4, 0x24, 0x44, 0x64,
		0x5, 0x25, 0x45, 0x65,
		0x6, 0x26, 0x46, 0x66,
		0x7, 0x27, 0x47, 0x67,
	};

	for (int i = 0; i < (int)ARRAY_SIZE(firstThirtyTwo); i++) {
		if (voffset[firstThirtyTwo[i]] != i) {
			ERROR_LOG(Log::CPU, "Wrong voffset order! %i: %i should have been %i", firstThirtyTwo[i], voffset[firstThirtyTwo[i]], i);
		}
	}
}

MIPSState::~MIPSState() {
}

void MIPSState::Shutdown() {
	MIPSComp::JitInterface *oldjit = MIPSComp::jit;
	if (oldjit) {
		MIPSComp::jit = nullptr;
		delete oldjit;
	}
	pendingInvalidates_.clear();
	invalidateAll_ = false;
}

void MIPSState::Reset() {
	Shutdown();
	Init();
}

void MIPSState::Init() {
	memset(r, 0, sizeof(r));
	memset(f, 0, sizeof(f));
	memset(v, 0, sizeof(v));
	memset(vfpuCtrl, 0, sizeof(vfpuCtrl));

	vfpuCtrl[VFPU_CTRL_SPREFIX] = 0xe4; //passthru
	vfpuCtrl[VFPU_CTRL_TPREFIX] = 0xe4; //passthru
	vfpuCtrl[VFPU_CTRL_DPREFIX] = 0;
	vfpuCtrl[VFPU_CTRL_CC] = 0x3f;
	vfpuCtrl[VFPU_CTRL_INF4] = 0;
	vfpuCtrl[VFPU_CTRL_REV] = 0x7772ceab;
	vfpuCtrl[VFPU_CTRL_RCX0] = 0x3f800001;
	vfpuCtrl[VFPU_CTRL_RCX1] = 0x3f800002;
	vfpuCtrl[VFPU_CTRL_RCX2] = 0x3f800004;
	vfpuCtrl[VFPU_CTRL_RCX3] = 0x3f800008;
	vfpuCtrl[VFPU_CTRL_RCX4] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX5] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX6] = 0x3f800000;
	vfpuCtrl[VFPU_CTRL_RCX7] = 0x3f800000;

	pc = 0;
	hi = 0;
	lo = 0;
	fpcond = 0;
	fcr31 = 0;
	debugCount = 0;
	currentMIPS = this;
	inDelaySlot = false;
	llBit = 0;
	nextPC = 0;
	downcount = 0;

	memset(vcmpResult, 0, sizeof(vcmpResult));

	if (PSP_CoreParameter().cpuCore == CPUCore::JIT || PSP_CoreParameter().cpuCore == CPUCore::JIT_IR) {
		MIPSComp::jit = MIPSComp::CreateNativeJit(this, PSP_CoreParameter().cpuCore == CPUCore::JIT_IR);
	} else if (PSP_CoreParameter().cpuCore == CPUCore::IR_INTERPRETER) {
		MIPSComp::jit = new MIPSComp::IRJit(this, false);
	} else {
		MIPSComp::jit = nullptr;
	}
}

bool MIPSState::HasDefaultPrefix() const {
	return vfpuCtrl[VFPU_CTRL_SPREFIX] == 0xe4 && vfpuCtrl[VFPU_CTRL_TPREFIX] == 0xe4 && vfpuCtrl[VFPU_CTRL_DPREFIX] == 0;
}

void MIPSState::UpdateCore(CPUCore desired) {
	if (PSP_CoreParameter().cpuCore == desired) {
		return;
	}

	IncrementDebugCounter(DebugCounter::CPUCORE_SWITCHES);

	// Get rid of the old JIT first, before switching.
	if (MIPSComp::jit) {
		delete MIPSComp::jit;
		MIPSComp::jit = nullptr;
	}

	PSP_CoreParameter().cpuCore = desired;

	MIPSComp::JitInterface *newjit = nullptr;
	switch (PSP_CoreParameter().cpuCore) {
	case CPUCore::JIT:
	case CPUCore::JIT_IR:
		INFO_LOG(Log::CPU, "Switching to JIT%s", PSP_CoreParameter().cpuCore == CPUCore::JIT_IR ? " IR" : "");
		newjit = MIPSComp::CreateNativeJit(this, PSP_CoreParameter().cpuCore == CPUCore::JIT_IR);
		break;

	case CPUCore::IR_INTERPRETER:
		INFO_LOG(Log::CPU, "Switching to IR interpreter");
		newjit = new MIPSComp::IRJit(this, false);
		break;

	case CPUCore::INTERPRETER:
		INFO_LOG(Log::CPU, "Switching to interpreter");
		// Leaving newjit as null.
		break;

	default:
		WARN_LOG(Log::CPU, "Invalid value for cpuCore, falling back to interpreter");
		break;
	}

	MIPSComp::jit = newjit;
}

void MIPSState::DoState(PointerWrap &p) {
	auto s = p.Section("MIPSState", 1, 4);
	if (!s)
		return;

	// Reset the jit if we're loading.
	if (p.mode == p.MODE_READ)
		Reset();
	// Assume we're not saving state during a CPU core reset, so no lock.
	if (MIPSComp::jit)
		MIPSComp::jit->DoState(p);
	else
		MIPSComp::DoDummyJitState(p);

	DoArray(p, r, sizeof(r) / sizeof(r[0]));
	DoArray(p, f, sizeof(f) / sizeof(f[0]));
	if (s <= 2) {
		float vtemp[128];
		DoArray(p, vtemp, sizeof(v) / sizeof(v[0]));
		for (int i = 0; i < 128; i++) {
			v[voffset[i]] = vtemp[i];
		}
	} else {
		DoArray(p, v, sizeof(v) / sizeof(v[0]));
	}
	DoArray(p, vfpuCtrl, sizeof(vfpuCtrl) / sizeof(vfpuCtrl[0]));
	Do(p, pc);
	Do(p, nextPC);
	Do(p, downcount);
	// Reversed, but we can just leave it that way.
	Do(p, hi);
	Do(p, lo);
	Do(p, fpcond);
	if (s <= 1) {
		u32 fcr0_unused = 0;
		Do(p, fcr0_unused);
	}
	Do(p, fcr31);
	if (s <= 3) {
		uint32_t dummy;
		Do(p, dummy);  // rng.m_w
		Do(p, dummy);  // rng.m_z
	}

	Do(p, inDelaySlot);
	Do(p, llBit);
	Do(p, debugCount);

	if (p.mode == p.MODE_READ && MIPSComp::jit) {
		// Now that we've loaded fcr31, update any jit state associated.
		MIPSComp::jit->UpdateFCR31();
	}
}

void MIPSState::SingleStep() {
	ProcessPendingInvalidates();
	int cycles = MIPS_InterpretSingleStep(this);
	downcount -= cycles;
}

// returns 1 if reached ticks limit
int MIPSState::RunLoopUntil(u64 globalTicks) {
	switch (PSP_CoreParameter().cpuCore) {
	case CPUCore::JIT:
	case CPUCore::JIT_IR:
	case CPUCore::IR_INTERPRETER:
		while (inDelaySlot) {
			// We must get out of the delay slot before going into jit.
			// This normally should never take more than one step...
			int cycles = MIPS_InterpretSingleStep(this);
			downcount -= cycles;
		}
		ProcessPendingInvalidates();
		MIPSComp::jit->RunLoopUntil(globalTicks);
		break;

	case CPUCore::INTERPRETER:
		return MIPSInterpret_RunUntil(this, globalTicks);
	}
	return 1;
}

void MIPSState::InvalidateICacheRangeImmediate(u32 address, u32 length) {
	if (!MIPSComp::jit) {
		// Nothing to do.
		return;
	}
	if (length != 0) {
		MIPSComp::jit->InvalidateCacheAt(address, length);
	}
}

void MIPSState::ProcessPendingInvalidates() {
	if (!MIPSComp::jit) {
		// Nothing to do. Clear any old state from CPU core switching.
		pendingInvalidates_.clear();
		return;
	}

	if (invalidateAll_) {
		MIPSComp::jit->ClearCache();
		pendingInvalidates_.clear();
		invalidateAll_ = false;
		return;
	}

	if (!pendingInvalidates_.empty()) {
		for (const auto &p : pendingInvalidates_) {
			MIPSComp::jit->InvalidateCacheAt(p.addr, p.size);
		}
		pendingInvalidates_.clear();
	}
}

void MIPSState::InvalidateICacheRangeDeferred(u32 address, u32 length) {
	if (!MIPSComp::jit) {
		// Nothing to do.
		return;
	}

	// TODO: We can try to merge the invalidate with the previous one.
	pendingInvalidates_.push_back(PendingCacheOperation{address, length});
	Core_ReenterDispatcher();
}

void MIPSState::ClearJitCacheDeferred() {
	if (!MIPSComp::jit) {
		return;
	}

	pendingInvalidates_.clear();
	invalidateAll_ = true;
	Core_ReenterDispatcher();
}
