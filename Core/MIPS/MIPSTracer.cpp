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

#include "Core/MIPS/MIPSTracer.h"

#include <cstring> // for std::memcpy
#include "Core/MIPS/MIPSTables.h" // for MIPSDisAsm


bool TraceBlockStorage::push_block(u32* instructions, u32 size) {
	if (cur_offset + size >= raw_instructions.size()) {
		return false;
	}
	std::memcpy(cur_data_ptr, instructions, size);
	cur_offset += size;
	cur_data_ptr += size;
	return true;
}

void TraceBlockStorage::initialize(u32 capacity) {
	raw_instructions.resize(capacity);
	cur_offset = 0;
	cur_data_ptr = raw_instructions.data();
}


MIPSTracer mipsTracer;
