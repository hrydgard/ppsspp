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

#pragma once

#include <map>

#include "Core/MIPS/MIPSTables.h"

class MIPSNames {
private:
	std::map<std::string, MIPSInstruction*> name_lookup_table;
public:
	template <size_t size>
	void RegisterInstructions(MIPSInstruction (&table)[size]) {
		for (uint32_t i = 0; i < size; ++i) {
			// register the name if it's valid
			if (table[i].name) {
				name_lookup_table.emplace(table[i].name, &table[i]);
			}
		}
	}

	MIPSInstruction* GetInstructionByName(std::string name) {
		auto it = name_lookup_table.find(name);
		if (it == name_lookup_table.end()) {
			return nullptr;
		}
		return it->second;
	}
};

extern MIPSNames MIPSNameLookupTable;

namespace MIPSHooks {
	void Init(); // implemented in the MIPSTables.cpp
	void Reset();
	void Hook(const char* name, MIPSInterpretFunc func);
}

