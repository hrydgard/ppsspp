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

#include <queue>

#include "CommonTypes.h"
#include "sceKernel.h"

class PointerWrap;

enum PspAudioFormats { PSP_AUDIO_FORMAT_STEREO = 0, PSP_AUDIO_FORMAT_MONO = 0x10 };
enum PspAudioFrequencies { PSP_AUDIO_FREQ_44K = 44100, PSP_AUDIO_FREQ_48K = 48000 };

const u32 PSP_AUDIO_CHANNEL_MAX = 8;

const int PSP_AUDIO_CHANNEL_SRC = 8;
const int PSP_AUDIO_CHANNEL_OUTPUT2 = 8;
const int PSP_AUDIO_CHANNEL_VAUDIO = 8;

struct QueueEntry {
	u32 leftVol;
	u32 rightVol;
	u32 sampleAddress;
};

struct AudioChannel {
	int index = 0;
	bool reserved = false;

	// last sample address
	u32 sampleAddressUnused = 0;  // no longer used
	u32 sampleCount = 0;  // Number of samples written in each OutputBlocking
	u32 leftVolume = 0;  // no longer used
	u32 rightVolume = 0;   // no longer used
	u32 format = 0;

	// Audio queues seem to only be two entries deep given delay behavior on blocking enqueues,
	// however, games seem to often cycle between three addresses, which I don't really understand.
	enum {
		MAX_QUEUE_LENGTH = 2,
	};
	QueueEntry queue[MAX_QUEUE_LENGTH];
	int queueLength = 0;
	int queuePlayOffset = 0;  // Position in queue[0] currently being read.

	// For the debugger only. Not saved.
	bool mute = false;

	std::vector<SceUID> waitingThreads;

	void DoState(PointerWrap &p);

	void reset();
	void clear();
};

// The extra channel is for SRC/Output2/Vaudio (who all share, apparently.)
extern AudioChannel g_audioChans[PSP_AUDIO_CHANNEL_MAX + 1];

void Register_sceAudio();

