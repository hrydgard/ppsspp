// Copyright (c) 2015- PPSSPP Project.

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

struct SasReverbData;

class SasReverb {
public:
	SasReverb();
	~SasReverb();

	void SetPreset(int preset);
	// sceSasRevParam's delay and feedback, both 0..127. Only the Echo and Delay presets use
	// them; the hardware recomputes most of their parameters from these.
	void SetParams(int delay, int feedback);
	int GetPreset() { return preset_; }

	static const char *GetPresetName(int preset);

	// Halves the rate of the 44khz send (interleaved stereo, outputFrames * 2 frames of it), into
	// ProcessReverb's input.
	void DownsampleSend(int16_t *output, const int32_t *input, int outputFrames);

	// Input should be a mixdown of all the channels that have reverb enabled, at 22khz.
	// Output is written back at 44khz.
	void ProcessReverb(int16_t *output, const int16_t *input, size_t inputSize, int volLeft, int volRight);

private:
	enum {
		BUFSIZE = 0x20000,
		// The 22khz wet signal's interpolation filter: taps per side, and the history it keeps
		// between calls (both sides).
		UPSAMPLE_HALF_TAPS = 12,
		UPSAMPLE_HISTORY = UPSAMPLE_HALF_TAPS * 2,
		// The most 22khz frames one call can produce: half of PSP_SAS_MAX_GRAIN.
		MAX_INPUT_FRAMES = 1024,
	};

	// Copies the current preset into data_, then patches it for Echo/Delay. Called whenever the
	// preset or the parameters change.
	void ApplyParams();
	// Writes count * 2 stereo frames at 44khz from the count newest frames in wet_.
	void UpsampleWet(int16_t *out, int count);

	int16_t *workspace_;
	// The preset in effect, as a mutable copy: Echo and Delay are partly computed at runtime.
	SasReverbData *data_;
	int preset_;
	int pos_;
	int delay_ = 0;
	int feedback_ = 0;
	// Wet output at 22khz after volume, interleaved stereo: UPSAMPLE_HISTORY frames kept from the
	// previous call, then this call's. Not saved in savestates: the history is about a millisecond
	// of audio, and the delay line itself isn't saved either.
	float *wet_;
	// The send at 44khz split into its even and its odd frames (interleaved stereo each), with
	// UPSAMPLE_HISTORY frames of history, for DownsampleSend. Not saved either.
	float *sendEven_;
	float *sendOdd_;
};
