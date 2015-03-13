// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPS.h"

// Runtime generated assembly routines, like the Dispatcher.

namespace MIPSComp {
	class Jit;
	struct JitOptions;
}

class AsmRoutineManager : public Gen::XCodeBlock {
private:
	void Generate(MIPSState *mips, MIPSComp::Jit *jit, MIPSComp::JitOptions *jo);
	void GenerateCommon();

public:
	AsmRoutineManager() {
	}
	~AsmRoutineManager() {
		FreeCodeSpace();
	}

	void Init(MIPSState *mips, MIPSComp::Jit *jit, MIPSComp::JitOptions *jo) {
		AllocCodeSpace(8192);
		Generate(mips, jit, jo);
		WriteProtect();
	}

	const u8 *enterCode;

	const u8 *outerLoop;
	const u8 *dispatcher;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcherNoCheck;
	const u8 *dispatcherInEAXNoCheck;

	const u8 *breakpointBailout;
};
