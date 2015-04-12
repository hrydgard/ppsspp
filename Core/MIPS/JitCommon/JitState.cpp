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

#include "Common/CPUDetect.h"
#include "Core/MIPS/JitCommon/JitState.h"

namespace MIPSComp {
	JitOptions::JitOptions() {
		// x86
		enableVFPUSIMD = true;
		// Set by Asm if needed.
		reserveR15ForAsm = false;

		// ARM/ARM64
		useBackJump = false;
		useForwardJump = false;
		cachePointers = true;

		// ARM only
		downcountInRegister = true;
		useNEONVFPU = false;  // true
		if (!cpu_info.bNEON)
			useNEONVFPU = false;

		//ARM64
		useASIMDVFPU = false;  // true

		// Common
		enableBlocklink = true;
		immBranches = false;
		continueBranches = false;
		continueJumps = false;
		continueMaxInstructions = 300;
	}
}
