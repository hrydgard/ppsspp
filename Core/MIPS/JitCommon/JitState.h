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


#pragma once

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Core/MIPS/MIPS.h"

struct JitBlock;
class JitBlockCache;

namespace MIPSComp {

	enum CompileDelaySlotFlags
	{
		// Easy, nothing extra.
		DELAYSLOT_NICE = 0,
		// Flush registers after delay slot.
		DELAYSLOT_FLUSH = 1,
		// Preserve flags.
		DELAYSLOT_SAFE = 2,
		// Flush registers after and preserve flags.
		DELAYSLOT_SAFE_FLUSH = DELAYSLOT_FLUSH | DELAYSLOT_SAFE,
	};

	struct JitState
	{
		enum PrefixState
		{
			PREFIX_UNKNOWN = 0x00,
			PREFIX_KNOWN = 0x01,
			PREFIX_DIRTY = 0x10,
			PREFIX_KNOWN_DIRTY = 0x11,
		};

		enum AfterOp
		{
			AFTER_NONE = 0x00,
			AFTER_CORE_STATE = 0x01,
		};

		u32 compilerPC;
		u32 blockStart;
		u32 lastContinuedPC;
		u32 initialBlockSize;
		int nextExit;
		bool cancel;
		bool inDelaySlot;
		// See JitState::AfterOp for values.
		int afterOp;
		int downcountAmount;
		int numInstructions;
		bool compiling;	// TODO: get rid of this in favor of using analysis results to determine end of block
		bool hadBreakpoints;
		bool preloading = false;
		JitBlock *curBlock;

		u8 hasSetRounding = 0;
		u8 lastSetRounding = 0;
		const u8 *currentRoundingFunc = nullptr;

		// VFPU prefix magic
		bool startDefaultPrefix = true;
		bool blockWrotePrefixes = false;
		u32 prefixS;
		u32 prefixT;
		u32 prefixD;
		PrefixState prefixSFlag = PREFIX_UNKNOWN;
		PrefixState prefixTFlag = PREFIX_UNKNOWN;
		PrefixState prefixDFlag = PREFIX_UNKNOWN;

		void PrefixStart() {
			if (startDefaultPrefix) {
				EatPrefix();
			} else {
				PrefixUnknown();
			}
		}

		void PrefixUnknown() {
			prefixSFlag = PREFIX_UNKNOWN;
			prefixTFlag = PREFIX_UNKNOWN;
			prefixDFlag = PREFIX_UNKNOWN;
		}

		bool HasSPrefix() const {
			return (prefixSFlag & PREFIX_KNOWN) == 0 || prefixS != 0xE4;
		}

		bool HasTPrefix() const {
			return (prefixTFlag & PREFIX_KNOWN) == 0 || prefixT != 0xE4;
		}

		bool HasDPrefix() const {
			return (prefixDFlag & PREFIX_KNOWN) == 0 || prefixD != 0x0;
		}

		bool MayHavePrefix() const {
			if (HasUnknownPrefix()) {
				return true;
			} else if (prefixS != 0xE4 || prefixT != 0xE4 || prefixD != 0) {
				return true;
			}
			return false;
		}

		bool HasUnknownPrefix() const {
			if (!(prefixSFlag & PREFIX_KNOWN) || !(prefixTFlag & PREFIX_KNOWN) || !(prefixDFlag & PREFIX_KNOWN)) {
				return true;
			}
			return false;
		}

		bool HasNoPrefix() const {
			return !HasSPrefix() && !HasTPrefix() && !HasDPrefix();
		}

		void EatPrefix() {
			if (HasSPrefix())
				prefixSFlag = PREFIX_KNOWN_DIRTY;
			prefixS = 0xE4;
			if (HasTPrefix())
				prefixTFlag = PREFIX_KNOWN_DIRTY;
			prefixT = 0xE4;
			if (HasDPrefix())
				prefixDFlag = PREFIX_KNOWN_DIRTY;
			prefixD = 0x0;
		}

		u8 VfpuWriteMask() const {
			_assert_(prefixDFlag & JitState::PREFIX_KNOWN);
			return (prefixD >> 8) & 0xF;
		}

		bool VfpuWriteMask(int i) const {
			_assert_(prefixDFlag & JitState::PREFIX_KNOWN);
			return (prefixD >> (8 + i)) & 1;
		}

		void LogPrefix() {
			LogSTPrefix("S", prefixS, prefixSFlag);
			LogSTPrefix("T", prefixT, prefixTFlag);
			LogDPrefix();
		}

	private:
		void LogSTPrefix(const char *name, int p, int pflag) {
			if ((prefixSFlag & PREFIX_KNOWN) == 0) {
				ERROR_LOG(Log::JIT, "%s: unknown  (%08x %i)", name, p, pflag);
			} else if (prefixS != 0xE4) {
				ERROR_LOG(Log::JIT, "%s: %08x flag: %i", name, p, pflag);
			} else {
				WARN_LOG(Log::JIT, "%s: %08x flag: %i", name, p, pflag);
			}
		}
		void LogDPrefix() {
			if ((prefixDFlag & PREFIX_KNOWN) == 0) {
				ERROR_LOG(Log::JIT, "D: unknown (%08x %i)", prefixD, prefixDFlag);
			} else if (prefixD != 0) {
				ERROR_LOG(Log::JIT, "D: (%08x %i)", prefixD, prefixDFlag);
			} else {
				WARN_LOG(Log::JIT, "D: %08x flag: %i", prefixD, prefixDFlag);
			}
		}
	};

	enum class JitDisable {
		ALU = 0x0001,
		ALU_IMM = 0x0002,
		ALU_BIT = 0x0004,
		MULDIV = 0x0008,

		FPU = 0x0010,
		FPU_COMP = 0x0040,
		FPU_XFER = 0x0080,

		VFPU_VEC = 0x0100,
		VFPU_MTX_VTFM = 0x0200,
		VFPU_COMP = 0x0400,
		VFPU_XFER = 0x0800,

		LSU = 0x1000,
		LSU_UNALIGNED = 0x2000,
		LSU_FPU = 0x4000,
		LSU_VFPU = 0x8000,

		SIMD = 0x00100000,
		BLOCKLINK = 0x00200000,
		POINTERIFY = 0x00400000,
		STATIC_ALLOC = 0x00800000,
		CACHE_POINTERS = 0x01000000,
		REGALLOC_GPR = 0x02000000,  // Doesn't really disable regalloc, but flushes after every instr.
		REGALLOC_FPR = 0x04000000,
		VFPU_MTX_VMMOV = 0x08000000,
		VFPU_MTX_VMMUL = 0x10000000,
		VFPU_MTX_VMSCL = 0x20000000,

		ALL_FLAGS = 0x3FFFFFFF,
	};

	struct JitOptions {
		JitOptions();

		bool Disabled(JitDisable bit);

		uint32_t disableFlags;

		// x86
		bool enableVFPUSIMD;
		bool reserveR15ForAsm;

		// ARM/ARM64
		bool useBackJump;
		bool useForwardJump;
		bool cachePointers;
		// ARM only
		bool useNEONVFPU;
		bool downcountInRegister;
		// ARM64 only
		bool useASIMDVFPU;
		// ARM64 and RV64
		bool useStaticAlloc;
		bool enablePointerify;
		// IR Interpreter
		bool optimizeForInterpreter;

		// Common
		bool enableBlocklink;
		bool immBranches;
		bool continueBranches;
		bool continueJumps;
		int continueMaxInstructions;
	};
}
