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

class PointerWrap;

struct VSHVaudioDebugStatus {
	bool reserved = false;
	u32 outputCalls = 0;
	u32 submittedFrames = 0;
	u32 nonSilentCalls = 0;
	u32 peakSample = 0;
	u32 regularOutputCalls = 0;
	u32 regularNonSilentCalls = 0;
	u32 regularPeakSample = 0;
	u32 sampleCount = 0;
	u32 queuedSampleValues = 0;
	u32 leftVolume = 0;
	u32 rightVolume = 0;
	int effectType = 0;
	int effectVolume = 0;
	int alcMode = 0;
};

void Register_sceVaudio();
void Register_sceVaudio_driver();

void __VaudioInit();
void __VaudioDoState(PointerWrap &p);
VSHVaudioDebugStatus __VaudioGetDebugStatus();

