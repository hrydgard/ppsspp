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

#include <vector>
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "GPU/GPU.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/Stepping.h"

namespace GPUDebug {

const char *BreakNextToString(BreakNext next) {
	switch (next) {
	case BreakNext::NONE: return "NONE,";
	case BreakNext::OP: return "OP";
	case BreakNext::DRAW: return "DRAW";
	case BreakNext::TEX: return "TEX";
	case BreakNext::NONTEX: return "NONTEX";
	case BreakNext::FRAME: return "FRAME";
	case BreakNext::VSYNC: return "VSYNC";
	case BreakNext::PRIM: return "PRIM";
	case BreakNext::CURVE: return "CURVE";
	case BreakNext::BLOCK_TRANSFER: return "BLOCK_TRANSFER";
	case BreakNext::COUNT: return "COUNT";
	case BreakNext::DEBUG_RUN: return "DEBUG_RUN";
	default: return "N/A";
	}
}

static bool ParseRange(const std::string &s, std::pair<int, int> &range) {
	int c = sscanf(s.c_str(), "%d-%d", &range.first, &range.second);
	if (c == 0)
		return false;
	if (c == 1)
		range.second = range.first;
	return true;
}

bool ParsePrimRanges(std::string_view rule, std::vector<std::pair<int, int>> *output) {
	constexpr int MAX_PRIMS = 0x7FFFFFFF;

	std::vector<std::string> parts;
	SplitString(rule, ',', parts);

	// Parse expressions like: 0  or  0-1,4-5  or  !2  or  !2-3  or  !2,!3
	std::vector<std::pair<int, int>> updated;
	for (auto &part : parts) {
		std::pair<int, int> range;
		if (part.size() > 1 && part[0] == '!') {
			if (!ParseRange(part.substr(1), range))
				return false;

			// If there's nothing yet, add everything else.
			if (updated.empty()) {
				if (range.first > 0)
					updated.emplace_back(0, range.first - 1);
				if (range.second < MAX_PRIMS)
					updated.emplace_back(range.second + 1, MAX_PRIMS);
				continue;
			}

			// Otherwise, remove this range from any existing.
			for (size_t i = 0; i < updated.size(); ++i) {
				auto &sub = updated[i];
				if (sub.second < range.first || sub.first > range.second)
					continue;
				if (sub.first >= range.first && sub.second <= range.second) {
					// Entire subrange is inside the deleted entries, nuke.
					sub.first = -1;
					sub.second = -1;
					continue;
				}
				if (sub.first < range.first && sub.second > range.second) {
					// We're slicing a hole in this subrange.
					int next = sub.second;
					sub.second = range.first - 1;
					updated.emplace_back(range.second + 1, next);
					continue;
				}

				// If we got here, we're simply clipping the subrange.
				if (sub.first < range.first && sub.second >= range.first && sub.second <= range.second)
					sub.second = range.first - 1;
				if (sub.first >= range.first && sub.first <= range.second && sub.second < range.second)
					sub.first = range.second + 1;
			}
		} else {
			if (!ParseRange(part, range))
				return false;

			updated.push_back(range);
		}
	}
	*output = updated;
	return true;
}

}
