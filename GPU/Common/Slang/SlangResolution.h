// Copyright (c) 2026- PPSSPP Project.

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

#include <algorithm>
#include <cmath>
#include "GPU/Common/Slang/SlangPreset.h"

struct SlangSize { int w, h; };

inline int ResolveAxis(SlangScaleType type, float scale, int input, int viewport) {
	float v;
	switch (type) {
	case SlangScaleType::Viewport: v = viewport * scale; break;
	case SlangScaleType::Absolute: v = scale; break;
	case SlangScaleType::Source:
	default: v = input * scale; break;
	}
	int r = (int)std::lround(v);
	return std::max(1, r);
}

inline SlangSize ResolvePassSize(const SlangPassDesc &pass, SlangSize inputSize, SlangSize viewportSize) {
	SlangSize out;
	out.w = ResolveAxis(pass.scaleTypeX, pass.scaleX, inputSize.w, viewportSize.w);
	out.h = ResolveAxis(pass.scaleTypeY, pass.scaleY, inputSize.h, viewportSize.h);
	return out;
}
