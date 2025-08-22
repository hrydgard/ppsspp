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

#include "Common/System/System.h"
#include "Core/Config.h"
#include "Core/HW/StereoResampler.h"  // TODO: doesn't belong in Core/HW...
#include "Core/HW/GranularMixer.h"
#include "UI/AudioCommon.h"
#include "UI/BackgroundAudio.h"
#include "Core/HW/Display.h"

StereoResampler g_resampler;
GranularMixer g_granular;

// numFrames is number of stereo frames.
// This is called from *outside* the emulator thread.
void NativeMix(int16_t *outStereo, int numFrames, int sampleRateHz, void *userdata) {
	// Mix UI sound effects on top.
	if (g_Config.iAudioSyncMode == (int)AudioSyncMode::GRANULAR) {
		// We use the FPS estimate, because to maintain smooth audio even though our
		// frame execution is very front (or back) heavy (as we can't count on "real time clock sync"
		// to be enabled), we need at least one whole frame buffered, plus a bit of extra.
		float fpsEstimate, vps, actualFps;
		__DisplayGetFPS(&vps, &fpsEstimate, &actualFps);

		g_granular.Mix(outStereo, numFrames, sampleRateHz, fpsEstimate);
	} else {
		g_resampler.Mix(outStereo, numFrames, false, sampleRateHz);
	}
	g_BackgroundAudio.SFX().Mix(outStereo, numFrames, sampleRateHz);
}

void System_AudioGetDebugStats(char *buf, size_t bufSize) {
	if (buf) {
		if (g_Config.iAudioSyncMode == (int)AudioSyncMode::GRANULAR) {
			snprintf(buf, bufSize, "(No stats available for granular yet)");
		} else {
			g_resampler.GetAudioDebugStats(buf, bufSize);
		}
	} else {
		g_resampler.ResetStatCounters();
	}
}

void System_AudioClear() {
	g_resampler.Clear();
}

void System_AudioPushSamples(const int32_t *audio, int numSamples, float volume) {
	if (audio) {
		if (g_Config.iAudioSyncMode == (int)AudioSyncMode::GRANULAR) {
			g_granular.PushSamples(audio, numSamples, volume);
		} else {
			g_resampler.PushSamples(audio, numSamples, volume);
		}
	} else {
		g_resampler.Clear();
	}
}
