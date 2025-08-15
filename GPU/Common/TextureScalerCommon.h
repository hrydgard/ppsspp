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

#include "Common/CommonTypes.h"
#include "Common/MemoryUtil.h"

static const int MIN_TEXSCALE_LINES_PER_THREAD = 4;

// The texture scaler requires input to be in R8G8B8A8.
// (It's OK if you flip R and B as they are not treated very differently from each other.
// They will of course not unflip during the operation so be aware of that).
class TextureScalerCommon {
public:
	TextureScalerCommon();
	~TextureScalerCommon();

	void ScaleAlways(u32 *out, u32 *src, int width, int height, int *scaledWidth, int *scaledHeight, int factor);
	bool Scale(u32 *&data, int width, int height, int *scaledWidth, int *scaledHeight, int factor);
	bool ScaleInto(u32 *out, u32 *src, int width, int height, int *scaledWidth, int *scaledHeight, int factor);

	enum { XBRZ = 0, HYBRID = 1, BICUBIC = 2, HYBRID_BICUBIC = 3 };

protected:
	static void ScaleXBRZ(int factor, u32* source, u32* dest, int width, int height);
	void ScaleBilinear(int factor, u32* source, u32* dest, int width, int height);
	static void ScaleBicubicBSpline(int factor, u32* source, u32* dest, int width, int height);
	static void ScaleBicubicMitchell(int factor, u32* source, u32* dest, int width, int height);
	void ScaleHybrid(int factor, u32* source, u32* dest, int width, int height, bool bicubic = false);

	void DePosterize(u32* source, u32* dest, int width, int height);

	static bool IsEmptyOrFlat(const u32 *data, int pixels) ;

	// depending on the factor and texture sizes, these can get pretty large 
	// maximum is (100 MB total for a 512 by 512 texture with scaling factor 5 and hybrid scaling)
	// of course, scaling factor 5 is totally silly anyway
	AlignedVector<u32, 16> bufDeposter, bufOutput, bufTmp1, bufTmp2, bufTmp3;
};
