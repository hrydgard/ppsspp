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

#include "Common/Common.h"

struct JitBlock;

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
			AFTER_REWIND_PC_BAD_STATE = 0x02,
			AFTER_MEMCHECK_CLEANUP = 0x04,
		};

		JitState()
			: hasSetRounding(0),
			lastSetRounding(0),
			startDefaultPrefix(true),
			prefixSFlag(PREFIX_UNKNOWN),
			prefixTFlag(PREFIX_UNKNOWN),
			prefixDFlag(PREFIX_UNKNOWN) {}

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
		JitBlock *curBlock;

		u8 hasSetRounding;
		u8 lastSetRounding;

		// VFPU prefix magic
		bool startDefaultPrefix;
		u32 prefixS;
		u32 prefixT;
		u32 prefixD;
		PrefixState prefixSFlag;
		PrefixState prefixTFlag;
		PrefixState prefixDFlag;

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

		bool MayHavePrefix() const {
			if (HasUnknownPrefix()) {
				return true;
			} else if (prefixS != 0xE4 || prefixT != 0xE4 || prefixD != 0) {
				return true;
			} else if (VfpuWriteMask() != 0) {
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
			return (prefixDFlag & PREFIX_KNOWN) && (prefixSFlag & PREFIX_KNOWN) && (prefixTFlag & PREFIX_KNOWN) && (prefixS == 0xE4 && prefixT == 0xE4 && prefixD == 0);
		}

		void EatPrefix() {
			if ((prefixSFlag & PREFIX_KNOWN) == 0 || prefixS != 0xE4) {
				prefixSFlag = PREFIX_KNOWN_DIRTY;
				prefixS = 0xE4;
			}
			if ((prefixTFlag & PREFIX_KNOWN) == 0 || prefixT != 0xE4) {
				prefixTFlag = PREFIX_KNOWN_DIRTY;
				prefixT = 0xE4;
			}
			if ((prefixDFlag & PREFIX_KNOWN) == 0 || prefixD != 0x0) {
				prefixDFlag = PREFIX_KNOWN_DIRTY;
				prefixD = 0x0;
			}
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
				ERROR_LOG(JIT, "%s: unknown  (%08x %i)", name, p, pflag);
			} else if (prefixS != 0xE4) {
				ERROR_LOG(JIT, "%s: %08x flag: %i", name, p, pflag);
			} else {
				WARN_LOG(JIT, "%s: %08x flag: %i", name, p, pflag);
			}
		}
		void LogDPrefix() {
			if ((prefixDFlag & PREFIX_KNOWN) == 0) {
				ERROR_LOG(JIT, "D: unknown (%08x %i)", prefixD, prefixDFlag);
			} else if (prefixD != 0) {
				ERROR_LOG(JIT, "D: (%08x %i)", prefixD, prefixDFlag);
			} else {
				WARN_LOG(JIT, "D: %08x flag: %i", prefixD, prefixDFlag);
			}
		}
	};

	struct JitOptions {
		JitOptions();

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

		// Common
		bool enableBlocklink;
		bool immBranches;
		bool continueBranches;
		bool continueJumps;
		int continueMaxInstructions;
	};
}
