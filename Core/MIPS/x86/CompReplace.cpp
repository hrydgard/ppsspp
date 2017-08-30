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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/x86/RegCache.h"
#include "Core/MIPS/x86/Jit.h"

alignas(16) static const u64 ssNoSignMask[2] = {0x7FFFFFFF7FFFFFFFULL, 0x7FFFFFFF7FFFFFFFULL};

namespace MIPSComp {
using namespace Gen;

int Jit::Replace_fabsf() {
	fpr.SpillLock(0, 12);
	fpr.MapReg(0, false, true);
	MOVSS(fpr.RX(0), fpr.R(12));
	MOV(PTRBITS, R(RAX), ImmPtr(&ssNoSignMask));
	ANDPS(fpr.RX(0), MatR(RAX));
	fpr.ReleaseSpillLocks();
	return 4;  // Number of instructions in the MIPS function
}

}

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
