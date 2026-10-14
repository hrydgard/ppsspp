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

#include "ppsspp_config.h"

#include "GPU/Math3D.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/RasterizerTypes.h"

namespace Rasterizer {

// Our std::unordered_map argument will ignore the alignment attribute, but that doesn't matter.
// We'll still have and want it for the actual function call, to keep the args in vector registers.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

typedef void (SOFTRAST_CALL *SingleFunc)(int x, int y, int z, int fog, Vec4IntArg color_in, const PixelFuncID &pixelID);
SingleFunc GetSingleFunc(const PixelFuncID &id);

// The pixels x to x + 3 of row y with mask[i] >= 0. z and fog are per pixel, colors a channel at a time:
// channel c of pixel i is colors[c * colorStride + i].
typedef void (SOFTRAST_CALL *SpanFunc)(int x, int y, const int *mask, const int *z, const int *fog, const int *colors, int colorStride, const PixelFuncID &pixelID);
SpanFunc GetSpanFunc(const PixelFuncID &id);

bool CheckDepthTestPassed(GEComparison func, int x, int y, int stride, u16 z);

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

};
