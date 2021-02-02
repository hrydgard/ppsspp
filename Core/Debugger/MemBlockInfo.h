// Copyright (c) 2021- PPSSPP Project.

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

#include <cstdint>
#include <string>
#include "Common/Common.h"

enum class MemBlockFlags {
	FREE = 0,
	ALLOC = 1,
	SUB_ALLOC = 2,
	WRITE = 4,
	// Not actually logged.
	READ = 8,
	SUB_FREE = 16,
};
ENUM_CLASS_BITOPS(MemBlockFlags);

void NotifyMemInfo(MemBlockFlags flags, uint32_t start, uint32_t size, const std::string &tag);
void NotifyMemInfoPC(MemBlockFlags flags, uint32_t start, uint32_t size, uint32_t pc, const std::string &tag);
