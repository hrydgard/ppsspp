// Copyright (C) 2012 PPSSPP Project

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

#include "MemMap.h"
#include "Host.h"

#include "System.h"
#include "PSPMixer.h"

#include "HLE/__sceAudio.h"
#include "base/NativeApp.h"

int PSPMixer::Mix(short *stereoout, int numSamples)
{
	int numFrames = __AudioMix(stereoout, numSamples);
#ifdef _WIN32
	if (numFrames < numSamples) {
		// Our dsound backend will not stop playing, let's just feed it zeroes if we miss data.
		memset(stereoout + 2 * 2 * numFrames, 0, 2 * 2 * (numSamples - numFrames));
		numFrames = numSamples;
	}
#endif
	return numFrames;
}
