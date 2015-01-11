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

#include "Common/Common.h"
#include "Core/Util/AudioFormat.h"
#include "Core/Util/AudioFormatNEON.h"
#include "Globals.h"

#ifdef _M_SSE
#include <xmmintrin.h>
#endif

void AdjustVolumeBlockStandard(s16 *out, s16 *in, size_t size, int leftVol, int rightVol) {
#ifdef _M_SSE
	if (leftVol <= 0x7fff && rightVol <= 0x7fff) {
		__m128i volume = _mm_set_epi16(leftVol, rightVol, leftVol, rightVol, leftVol, rightVol, leftVol, rightVol);
		while (size >= 16) {
			__m128i indata1 = _mm_loadu_si128((__m128i *)in);
			__m128i indata2 = _mm_loadu_si128((__m128i *)(in + 8));
			_mm_storeu_si128((__m128i *)out, _mm_mulhi_epi16(indata1, volume));
			_mm_storeu_si128((__m128i *)(out + 8), _mm_mulhi_epi16(indata2, volume));
			in += 16;
			out += 16;
			size -= 16;
		}
	} else {
		// We have to shift inside the loop to avoid the signed multiply issue.
		leftVol >>= 1;
		rightVol >>= 1;
		__m128i volume = _mm_set_epi16(leftVol, rightVol, leftVol, rightVol, leftVol, rightVol, leftVol, rightVol);
		while (size >= 16) {
			__m128i indata1 = _mm_loadu_si128((__m128i *)in);
			__m128i indata2 = _mm_loadu_si128((__m128i *)(in + 8));
			_mm_storeu_si128((__m128i *)out, _mm_slli_epi16(_mm_mulhi_epi16(indata1, volume), 1));
			_mm_storeu_si128((__m128i *)(out + 8), _mm_slli_epi16(_mm_mulhi_epi16(indata2, volume), 1));
			in += 16;
			out += 16;
			size -= 16;
		}
	}
#endif
	for (size_t i = 0; i < size; i += 2) {
		out[i] = ApplySampleVolume(in[i], leftVol);
		out[i + 1] = ApplySampleVolume(in[i + 1], rightVol);
	}
}

#ifndef _M_SSE
AdjustVolumeBlockFunc AdjustVolumeBlock = &AdjustVolumeBlockStandard;

// This has to be done after CPUDetect has done its magic.
void SetupAudioFormats() {
#ifdef HAVE_ARMV7
	if (cpu_info.bNEON) {
		AdjustVolumeBlock = &AdjustVolumeBlockNEON;
	}
#endif
}
#else
void SetupAudioFormats() {
}
#endif
