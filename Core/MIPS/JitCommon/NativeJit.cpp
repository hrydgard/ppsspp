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

#include "Core/MIPS/JitCommon/NativeJit.h"
#include "Core/MIPS/JitCommon/JitState.h"

#if defined(ARM)
#include "../ARM/ArmJit.h"
#elif defined(ARM64)
#include "../ARM64/Arm64Jit.h"
#elif defined(_M_IX86) || defined(_M_X64)
#include "../x86/Jit.h"
#elif defined(MIPS)
#include "../MIPS/MipsJit.h"
#else
#include "../fake/FakeJit.h"
#endif

namespace MIPSComp {
	JitInterface *jit;
	void JitAt() {
		jit->Compile(currentMIPS->pc);
	}

JitInterface *CreateNativeJit(MIPSState *mips) {
#if defined(ARM)
	return new MIPSComp::ArmJit(mips);
#elif defined(ARM64)
	return new MIPSComp::Arm64Jit(mips);
#elif defined(_M_IX86) || defined(_M_X64)
	return new MIPSComp::Jit(mips);
#elif defined(MIPS)
	return new MIPSComp::MipsJit(mips);
#else
	return new MIPSComp::FakeJit(mips);
#endif
}

}