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

#include <vector>

#include "CommonTypes.h"
#include "sceKernel.h"

class PointerWrap;

enum PspAudioFormats { PSP_AUDIO_FORMAT_STEREO = 0, PSP_AUDIO_FORMAT_MONO = 0x10 };
enum PspAudioFrequencies { PSP_AUDIO_FREQ_44K = 44100, PSP_AUDIO_FREQ_48K = 48000 };

const u32 PSP_AUDIO_CHANNEL_MAX = 8;

const int PSP_AUDIO_CHANNEL_SRC = 8;
const int PSP_AUDIO_CHANNEL_OUTPUT2 = 8;
const int PSP_AUDIO_CHANNEL_VAUDIO = 8;

// One buffer handed over and not yet fully played.
struct AudioPendingBuffer {
	u32 address;
	u32 samples;
};

// Mirrors the 16-byte channel struct the audio driver keeps, plus the two DMA descriptors it
// uses for the SRC/Output2 channel. See docs/sceAudio.md for how the hardware behaves; the
// short version is that it plays out of the game's own memory rather than taking a copy, and
// that it holds one buffer per mixer channel and two for SRC.
struct AudioChannel {
	int index = 0;
	bool reserved = false;

	u32 sampleCount = 0;  // Buffer size agreed at reserve time.
	u32 leftVolume = 0;
	u32 rightVolume = 0;
	u32 format = 0;

	// For the debugger only. Not saved.
	bool mute = false;

	// Channels 0-7. sampleAddress walks forward as the mixer consumes the buffer and drops
	// back to zero when remainingSamples runs out. An output with a null pointer sets
	// remainingSamples but leaves sampleAddress at zero, which is the one case where the two
	// rest-length calls disagree.
	u32 sampleAddress = 0;
	u32 remainingSamples = 0;

	// Only one thread can be parked in a blocking output call on a channel. A second one is
	// told the channel is busy rather than queueing up behind the first. These remember what
	// it wanted to hand over, so the enqueue can be retried when the buffer finishes.
	SceUID waitingThread = 0;
	u32 waitingAddress = 0;
	int waitingLeftVolume = 0;
	int waitingRightVolume = 0;

	// Channel 8 (Output2/SRC/Vaudio) instead has two DMA descriptors, so two buffers can be
	// in flight at once and the third caller is turned away.
	AudioPendingBuffer srcBuffers[2]{};
	int srcBufferCount = 0;
	u32 srcPlayedSamples = 0;  // Consumed from srcBuffers[0].
	u32 srcFrac = 0;           // 16.16 position between two input samples, for resampling.
	// The driver signals a finished buffer with an event flag bit, so one completion can sit
	// there unclaimed - which is why the first output after an idle period doesn't block.
	bool srcCompletion = false;
	std::vector<SceUID> srcWaitingThreads;

	void DoState(PointerWrap &p);

	void reset();
	void clear();

	bool SRCFull() const {
		return srcBufferCount >= (int)ARRAY_SIZE(srcBuffers);
	}
};

// The extra channel is for SRC/Output2/Vaudio (who all share, apparently.)
extern AudioChannel g_audioChans[PSP_AUDIO_CHANNEL_MAX + 1];

// The sample rates the SRC and VAUDIO channels will accept.
bool SRCFrequencyAllowed(int freq);

void Register_sceAudio();

