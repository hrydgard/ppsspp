// Copyright (c) 2013- PPSSPP Project.

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

enum TextureFiltering {
	TEX_FILTER_AUTO = 1,
	TEX_FILTER_NEAREST = 2,
	TEX_FILTER_LINEAR = 3,
	TEX_FILTER_LINEAR_VIDEO = 4,
};

class TextureCacheCommon {
public:
	virtual ~TextureCacheCommon();

	virtual bool SetOffsetTexture(u32 offset);

protected:
	void GetSamplingParams(int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, int maxLevel);
};