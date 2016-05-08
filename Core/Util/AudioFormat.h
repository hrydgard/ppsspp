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

#include "Common/Common.h"
#include "Globals.h"

static inline s16 ApplySampleVolume(s16 sample, int vol) {
#ifdef ARM
	register int r;
	asm volatile("smulwb %0, %1, %2\n\t" \
	             "ssat %0, #16, %0" \
	             : "=r"(r) : "r"(vol), "r"(sample));
	return r;
#else
	return clamp_s16((sample * vol) >> 16);
#endif
}

void SetupAudioFormats();
void AdjustVolumeBlockStandard(s16 *out, s16 *in, size_t size, int leftVol, int rightVol);
void ConvertS16ToF32(float *ou, const s16 *in, size_t size);

#ifdef _M_SSE
#define AdjustVolumeBlock AdjustVolumeBlockStandard
#else
typedef void (*AdjustVolumeBlockFunc)(s16 *out, s16 *in, size_t size, int leftVol, int rightVol);
extern AdjustVolumeBlockFunc AdjustVolumeBlock;
#endif
