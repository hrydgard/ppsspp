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

#include <vector>
#include "Common/CommonTypes.h"

namespace MIPSStackWalk {
	struct StackFrame {
		// Beginning of function symbol (may be estimated.)
		u32 entry;
		// Next position within function.
		u32 pc;
		// Value of SP inside this function (assuming no alloca()...)
		u32 sp;
		// Size of stack frame in bytes.
		int stackSize;
	};

	std::vector<StackFrame> Walk(u32 pc, u32 ra, u32 sp, u32 threadEntry, u32 threadStackTop);
};
