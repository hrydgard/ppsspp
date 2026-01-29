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

#include "ppsspp_config.h"
#include "Common/CPUDetect.h"
#include "Core/Config.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "Common/MemoryUtil.h"

namespace MIPSComp {
	JitOptions::JitOptions() {
		disableFlags = g_Config.uJitDisableFlags;

		// x86
		enableVFPUSIMD = !Disabled(JitDisable::SIMD);
		// Set by Asm if needed.
		reserveR15ForAsm = false;

		// ARM/ARM64
		useBackJump = false;
		useForwardJump = false;
		cachePointers = !Disabled(JitDisable::CACHE_POINTERS);

		// ARM only
		downcountInRegister = true;

		// Common

		// We can get block linking to work with W^X by doing even more unprotect/re-protect, but let's try without first.
		// enableBlocklink = !PlatformIsWXExclusive();  // Revert to this line if block linking is slow in W^X mode
#if PPSSPP_ARCH(MIPS)
		enableBlocklink = false;
#else
		enableBlocklink = !Disabled(JitDisable::BLOCKLINK);
#endif
		immBranches = false;
		continueJumps = false;
		continueMaxInstructions = 300;

		useStaticAlloc = false;
		enablePointerify = false;
#if PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(RISCV64) || PPSSPP_ARCH(LOONGARCH64)
		useStaticAlloc = !Disabled(JitDisable::STATIC_ALLOC);
		// iOS/etc. may disable at runtime if Memory::base is not nicely aligned.
		enablePointerify = !Disabled(JitDisable::POINTERIFY);
#endif
#if PPSSPP_ARCH(RISCV64)
		// Seems to perform slightly better than a checked entry at the start.
		useBackJump = true;
#endif
	}

	bool JitOptions::Disabled(JitDisable bit) {
		return (disableFlags & (uint32_t)bit) != 0;
	}
}
