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

#include "Core/MIPS/MIPS.h"

// Fast switch-tree interpreter dispatcher, generated into InterpreterDispatch.cpp by
// GenerateInterpreterDispatch() in MIPSTables.cpp. Executes op on mips (same convention as
// the MIPSInt::Int_* handlers it calls into) and returns the number of cycles it consumed -
// or -1 if op isn't a recognized instruction (an invalid encoding, or one of the handful of
// real instructions with no interpreter implementation, e.g. tge/tlt/teq/...). Not total by
// design: the caller must fall back to MIPSInterpret() on a negative return, since deciding
// what "unhandled" means is a caller policy, not something a mechanically generated dispatch
// tree should embed.
int ExecInstruction(MIPSState *mips, MIPSOpcode op);
