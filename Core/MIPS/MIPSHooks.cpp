// Copyright (c) 2024- PPSSPP Project.

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

#include "Core/MIPS/MIPSHooks.h"

#include <map>
#include <vector>


MIPSNames MIPSNameLookupTable;
static std::vector<std::pair<MIPSInstruction*, MIPSInterpretFunc>> stack;

namespace MIPSHooks {

	void Reset() {
		INFO_LOG(CPU, "Resetting the interpreter hooks");
		for (auto&[instr, func] : stack) {
			instr->interpret = func;
			VERBOSE_LOG(CPU, "Resetting %s", instr->name);
		}
		stack.clear();
	}

	void Hook(const char* name, MIPSInterpretFunc func) {
		auto current_handler = MIPSNameLookupTable.GetInstructionByName(name);
		if (!current_handler) {
			WARN_LOG(CPU, "Cannot setup a hook: unknown instruction '%s'", name);
			return;
		}

		stack.emplace_back(current_handler, current_handler->interpret);
		current_handler->interpret = func;
		INFO_LOG(CPU, "Enabled a hook for %s", name);
	}
}
