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

struct JitBlock;

#ifdef USING_QT_UI
#undef emit
#endif

#if defined(ARM)
#include "../ARM/ArmJit.h"
typedef MIPSComp::ArmJit NativeJit;
#elif defined(ARM64)
#include "../ARM64/Arm64Jit.h"
typedef MIPSComp::Arm64Jit NativeJit;
#elif defined(_M_IX86) || defined(_M_X64)
#include "../x86/Jit.h"
typedef MIPSComp::Jit NativeJit;
#elif defined(MIPS)
#include "../MIPS/MipsJit.h"
typedef MIPSComp::MipsJit NativeJit;
#else
#include "../fake/FakeJit.h"
typedef MIPSComp::FakeJit NativeJit;
#endif

namespace MIPSComp {
	extern NativeJit *jit;

	typedef void (NativeJit::*MIPSCompileFunc)(MIPSOpcode opcode);
	typedef int (NativeJit::*MIPSReplaceFunc)();
}
