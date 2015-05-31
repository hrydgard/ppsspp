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

#include "GPU/Common/TextureDecoder.h"

u32 QuickTexHashNEON(const void *checkp, u32 size);
void DoUnswizzleTex16NEON(const u8 *texptr, u32 *ydestp, int bxc, int byc, u32 pitch, u32 rowWidth);
u32 ReliableHash32NEON(const void *input, size_t len, u32 seed);

CheckAlphaResult CheckAlphaRGBA8888NEON(const u32 *pixelData, int stride, int w, int h);
CheckAlphaResult CheckAlphaABGR4444NEON(const u32 *pixelData, int stride, int w, int h);
CheckAlphaResult CheckAlphaABGR1555NEON(const u32 *pixelData, int stride, int w, int h);
