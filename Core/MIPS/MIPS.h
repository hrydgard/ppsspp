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

#pragma once

#include "ppsspp_config.h"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/Opcode.h"

class PointerWrap;

namespace MIPSComp {
class JitInterface;
}

typedef Memory::Opcode MIPSOpcode;

// A fake opcode the CPU cores fetch in place of an instruction with a replacement function (see
// ReplaceTables.h), with the replacement's index in the low bits. It's never in PSP memory. Opcode
// 26, which the Allegrex doesn't have, decodes it as CallRepl.
#define MIPS_CALL_REPLACEMENT_OP 0x6A000000
#define MIPS_REPLACEMENT_INDEX_MASK 0x00FFFFFF

enum MIPSGPReg {
	MIPS_REG_ZERO=0,
	MIPS_REG_COMPILER_SCRATCH=1,

	MIPS_REG_V0=2,
	MIPS_REG_V1=3,

	MIPS_REG_A0=4,
	MIPS_REG_A1=5,
	MIPS_REG_A2=6,
	MIPS_REG_A3=7,
	MIPS_REG_A4=8,
	MIPS_REG_A5=9,

	MIPS_REG_T0=8,  //alternate names for A4/A5
	MIPS_REG_T1=9,
	MIPS_REG_T2=10,
	MIPS_REG_T3=11,
	MIPS_REG_T4=12,
	MIPS_REG_T5=13,
	MIPS_REG_T6=14,
	MIPS_REG_T7=15,

	MIPS_REG_S0=16,
	MIPS_REG_S1=17,
	MIPS_REG_S2=18,
	MIPS_REG_S3=19,
	MIPS_REG_S4=20,
	MIPS_REG_S5=21,
	MIPS_REG_S6=22,
	MIPS_REG_S7=23,
	MIPS_REG_T8=24,
	MIPS_REG_T9=25,
	MIPS_REG_K0=26,
	MIPS_REG_K1=27,
	MIPS_REG_GP=28,
	MIPS_REG_SP=29,
	MIPS_REG_FP=30,
	MIPS_REG_RA=31,

	// Not real regs, just for convenience/jit mapping.
	// NOTE: These are not the same as the offsets the IR has to use!
	MIPS_REG_HI = 32,
	MIPS_REG_LO = 33,
	MIPS_REG_FPCOND = 34,
	MIPS_REG_VFPUCC = 35,

	MIPS_REG_INVALID=-1,
};

enum {
	VFPU_CTRL_SPREFIX,
	VFPU_CTRL_TPREFIX,
	VFPU_CTRL_DPREFIX,
	VFPU_CTRL_CC,
	VFPU_CTRL_INF4,
	VFPU_CTRL_RSV5,
	VFPU_CTRL_RSV6,
	VFPU_CTRL_REV,
	VFPU_CTRL_RCX0,
	VFPU_CTRL_RCX1,
	VFPU_CTRL_RCX2,
	VFPU_CTRL_RCX3,
	VFPU_CTRL_RCX4,
	VFPU_CTRL_RCX5,
	VFPU_CTRL_RCX6,
	VFPU_CTRL_RCX7,

	VFPU_CTRL_MAX,
	//unknown....
};

// The fields of the VFPU prefixes. Lane i of an S or T prefix takes the lane its swizzle names, or
// with the constant bit, the constant the swizzle and abs bit pick, then the abs and negate. Lane i
// of a D prefix has a saturation (1 = [0:1], 3 = [-1:1]) and a write mask bit, set to keep the lane.
inline int VFPUPrefixSwizzle(u32 prefix, int i) { return (prefix >> (i * 2)) & 3; }
inline bool VFPUPrefixAbs(u32 prefix, int i) { return (prefix >> (8 + i)) & 1; }
inline bool VFPUPrefixIsConstant(u32 prefix, int i) { return (prefix >> (12 + i)) & 1; }
inline bool VFPUPrefixNegate(u32 prefix, int i) { return (prefix >> (16 + i)) & 1; }
inline int VFPUPrefixSat(u32 prefix, int i) { return (prefix >> (i * 2)) & 3; }
inline u8 VFPUPrefixWriteMask(u32 prefix) { return (prefix >> 8) & 0xF; }
inline bool VFPUPrefixWriteMask(u32 prefix, int i) { return (prefix >> (8 + i)) & 1; }

// The constants of an S or T prefix, by swizzle + 4 * abs.
extern const float vfpuPrefixConstants[8];

// The value of a lane with the constant bit, negate and all.
inline float VFPUPrefixConstant(u32 prefix, int i) {
	const float c = vfpuPrefixConstants[VFPUPrefixSwizzle(prefix, i) + (VFPUPrefixAbs(prefix, i) << 2)];
	return VFPUPrefixNegate(prefix, i) ? -c : c;
}

enum VCondition
{
	VC_FL,
	VC_EQ,
	VC_LT,
	VC_LE,
	VC_TR,
	VC_NE,
	VC_GE,
	VC_GT,
	VC_EZ,
	VC_EN,
	VC_EI,
	VC_ES,
	VC_NZ,
	VC_NN,
	VC_NI,
	VC_NS
};

// fcr31, the FPU control/status register, in the standard MIPS layout: bits 0-1 are the
// rounding mode, 2-6 the sticky Flags, 7-11 the trap Enables, 12-16 the per-instruction Cause,
// 23 the condition flag (mirrored in fpcond) and 24 flush-to-zero. Each of the three 5-bit
// fields holds the IEEE exceptions in the same order (inexact, underflow, overflow, divide by
// zero, invalid operation); only divide by zero is implemented so far, and only in the interpreter.
enum {
	FCR31_FLAG_DIV_BY_ZERO = 1 << 5,
	FCR31_ENABLE_DIV_BY_ZERO = 1 << 10,
	FCR31_CAUSE_DIV_BY_ZERO = 1 << 15,
};

// In memory, we order the VFPU registers differently. 
// Games use columns a whole lot more than rows, and it would thus be good if columns
// were contiguous in memory. Also, matrices aren't but should be.
extern u8 voffset[128];
extern u8 fromvoffset[128];

enum class CPUCore;

// Only icache invalidations currently, so no type field.
struct PendingCacheOperation {
	u32 addr;
	u32 size;
};

class MIPSState {
public:
	MIPSState();
	~MIPSState();
	// It owns its JIT.
	MIPSState(const MIPSState &) = delete;
	MIPSState &operator=(const MIPSState &) = delete;

	void Init();
	// Init without the JIT.
	void InitRegisters();
	void Shutdown();
	void Reset();
	void UpdateCore(CPUCore desired);

	void DoState(PointerWrap &p);
	// For the next DoState load: keep the JIT blocks entirely inside these (address, size) ranges,
	// code the load leaves as it is, instead of starting the JIT over.
	void KeepJitBlocksOnNextLoad(std::vector<std::pair<u32, u32>> ranges) {
		keepJitBlocks_ = std::move(ranges);
	}

	// MUST start with r and be followed by f, v, and t!
	u32 r[32];
	union {
		float f[32];
		u32 fi[32];
		int fs[32];
	};
	union {
		float v[128];
		u32 vi[128];
	};

	// Register-allocated JIT Temps don't get flushed so we don't reserve space for them.
	// However, the IR interpreter needs some temps that can stick around between ops.
	// Can be indexed through r[] using indices 192+, thanks to predictable struct layout.
	// Unfortunately, UBSAN isn't too happy about these.
	u32 t[16];     //192

	// If vfpuCtrl (prefixes) get mysterious values, check the VFPU regcache code.
	u32 vfpuCtrl[16]; // 208

	float vt[16];  //224  TODO: VFPU temp

	// ARM64 wants lo/hi to be aligned to 64 bits from the base of this struct.
	u32 padLoHi;    // 240

	union {
		struct {
			u32 pc;   //241

			u32 lo;   //242
			u32 hi;   //243

			u32 fcr31; //244 fpu control register
			u32 fpcond;  //245 cache the cond flag of fcr31  (& 1 << 23)
		};
		u32 other[6];
	};

	u32 nextPC;
	int downcount;  // This really doesn't belong here, it belongs in CoreTiming. But you gotta do what you gotta do, this needs to be reachable in the JITs without additional pointers.

	bool inDelaySlot;
	int llBit;  // ll/sc
	u32 temp;  // can be used to save temporaries during calculations when we need more than R0 and R1
	u32 mxcsrTemp;
	// Temporary used around delay slots and similar.
	u64 saved_flags;

	// Debug stuff
	u32 debugCount;	// can be used to count basic blocks before crashes, etc.

	// COP0, for kernel code. Reads of Count (9) come from CoreTiming instead.
	u32 cop0[32];

	// Temps needed for JitBranch.cpp experiments
	u32 intBranchExit;
	u32 jitBranchExit;

	u32 savedPC;

	alignas(16) u32 vcmpResult[4];

	float sincostemp[2];

	// The operands (s, then t) and result of the JITs' VFPU dot product routine (vfpu_dot()).
	alignas(16) u32 vfpuDotArgs[8];
	u32 vfpuDotResult;
	// The ARM64 JIT's multi-dot routine (IROp::Vec4VfpuMultiDot): the vector registers it saves, and
	// its operands (four term Vec4s and the vector) and result for inf and NaN.
	alignas(16) u32 vfpuMultiDotSave[64];
	alignas(16) float vfpuMultiDotArgs[20];
	alignas(16) float vfpuMultiDotResult[4];

	static const u32 FCR0_VALUE = 0x00003351;

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
	enum {
		NUM_X86_FPU_TEMPS = 16,
	};
	// FPU TEMP0, etc. are swapped in here if necessary (e.g. on x86.)
	float tempValues[NUM_X86_FPU_TEMPS];
#endif

	u8 VfpuWriteMask() const {
		return VFPUPrefixWriteMask(vfpuCtrl[VFPU_CTRL_DPREFIX]);
	}
	bool VfpuWriteMask(int i) const {
		return VFPUPrefixWriteMask(vfpuCtrl[VFPU_CTRL_DPREFIX], i);
	}

	bool HasDefaultPrefix() const;

	void SingleStep();
	int RunLoopUntil(u64 globalTicks);

	// To clear jit caches, etc.

	// The immediate functions have the risk of invalidating the currently running block -
	// might not behave correctly in all cases. Use Deferred when possible.
	void InvalidateICacheRangeImmediate(u32 address, u32 length);

	// Actual clearing is deferred until execution begins again.
	void InvalidateICacheRangeDeferred(u32 address, u32	length);
	void ClearJitCacheDeferred();

	void ProcessPendingInvalidates();

	// Don't make private, see the static_assert below.
	// Doesn't need save stating.
	std::vector<PendingCacheOperation> pendingInvalidates_;
	bool invalidateAll_ = false;
	std::vector<std::pair<u32, u32>> keepJitBlocks_;

	// Owned. Null on the interpreter.
	MIPSComp::JitInterface *jit = nullptr;
};

// Access control ('private') break standard layout. Let's not allow it, to avoid excessive
// compiler warnings. Hm, this seems to go wrong on MSVC in debug builds...
#ifndef _MSC_VER
static_assert(std::is_standard_layout_v<MIPSState>, "the JITs use offsetof on MIPSState");
#endif

class MIPSDebugInterface;

// The one we are compiling or running currently
// TODO: These globals should be refactored away.
extern MIPSState *currentMIPS;
extern MIPSDebugInterface *currentDebugMIPS;
extern MIPSState mipsr4k;

// With FS set, the host flushes a denormal input to a float->int conversion on ARM64 and x86 (DAZ),
// but ARM32's libm rounds with integer code that doesn't, so it's flushed here
// (cpu/fpu/denormal, cpu/vfpu/fcr31).
inline float FlushForConversion(const MIPSState *mips, float f) {
#if PPSSPP_ARCH(ARM)
	u32 bits;
	memcpy(&bits, &f, 4);
	if ((mips->fcr31 & 0x01000000) != 0 && (bits & 0x7F800000) == 0) {
		bits &= 0x80000000;
		memcpy(&f, &bits, 4);
	}
#endif
	return f;
}

extern const float cst_constants[32];

// The guest's rounding mode and flush-to-zero flag (fcr31 bits 0-1 and 24) are emulated by putting
// the *host* FPU into the matching mode, since we do the arithmetic with plain host float ops.
// That mode must not be left on while running anything that isn't emulating a guest instruction -
// HLE syscalls, replacement functions, the GPU - so an emulation loop applies it on entry and
// restores it before calling out, the way the JITs do (see Jit::ApplyRoundingMode).
// Both are cheap when the guest is in the default mode (by far the common case): they do nothing.
// Otherwise apply reads the register once and restore just writes it back.
void ApplyHostRoundingMode(const MIPSState *mips);
// force also clears our bits when apply didn't set them, for callers that may run under a native
// JIT, whose own asm stubs apply the mode.
void RestoreHostRoundingMode(bool force = false);
