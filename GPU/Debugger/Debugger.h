// Copyright (c) 2018- PPSSPP Project.

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

#include <string_view>
#include <vector>
#include <string>
#include "Common/CommonTypes.h"

namespace GPUDebug {

enum class BreakNext {
	NONE,
	OP,
	DRAW,
	TEX,
	NONTEX,
	FRAME,
	VSYNC,
	PRIM,
	CURVE,
	BLOCK_TRANSFER,
	DEBUG_RUN,  // This is just running as normal, but with debug instrumentation.
	COUNT,
};

enum class NotifyResult {
	Execute,
	Skip,
	Break
};

const char *BreakNextToString(GPUDebug::BreakNext next);
bool ParsePrimRanges(std::string_view rule, std::vector<std::pair<int, int>> *output);

}  // namespace
