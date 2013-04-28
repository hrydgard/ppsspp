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

#include "MediaEngine.h"
#include "../MemMap.h"

static const int modeBpp[4] = { 2, 2, 2, 4 };


void MediaEngine::writeVideoImage(u32 bufferPtr, int frameWidth, int videoPixelMode)
{
	if (videoPixelMode > (int)(sizeof(modeBpp) / sizeof(modeBpp[0])) || videoPixelMode < 0)
	{
		ERROR_LOG(ME, "Unexpected videoPixelMode %d, using 0 instead.", videoPixelMode);
		videoPixelMode = 0;
	}

	int bpp = modeBpp[videoPixelMode];

	// fake image. To be improved.
	if (Memory::IsValidAddress(bufferPtr))
		// Use Dark Grey to identify CG is running 
		Memory::Memset(bufferPtr, 0x69, frameWidth * videoHeight_ * bpp);
}

void MediaEngine::feedPacketData(u32 addr, int size)
{
	// This media engine is totally incompetent and will just ignore all data sent to it.
}
