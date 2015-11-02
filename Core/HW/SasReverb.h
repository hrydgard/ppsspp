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
	int GetPreset() { return preset_; }

	static const char *GetPresetName(int preset);

	// Input should be a mixdown of all the channels that have reverb enabled, at 22khz.
	// Output is written back at 44khz.
	void ProcessReverb(int16_t *output, const int16_t *input, size_t inputSize, uint16_t volLeft, uint16_t volRight);

private:
	enum {
		BUFSIZE = 0x20000,
	};

	int16_t *workspace_;
	int preset_;
	int pos_;
};