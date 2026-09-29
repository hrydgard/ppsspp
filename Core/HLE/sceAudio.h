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

// Channels 0-7, the ones the software mixer walks.
const u32 PSP_AUDIO_CHANNEL_MAX = 8;

// Mixer channels wait on their own index plus one, so the SRC channel takes the id after them.
const int PSP_AUDIO_SRC_WAIT_ID = PSP_AUDIO_CHANNEL_MAX + 1;

// One buffer handed over and not yet fully played.
struct AudioPendingBuffer {
	u32 address;
	u32 samples;
};

// A channel the software mixer walks. It holds exactly one buffer, played straight out of the
// game's memory 64 samples at a time, and has room for exactly one parked thread. See
// docs/sceAudio.md.
struct AudioChannel {
	int index = 0;
	bool reserved = false;

	u32 sampleCount = 0;  // Buffer size agreed at reserve time.
	u32 leftVolume = 0;
	u32 rightVolume = 0;
	u32 format = 0;

	// For the debugger only. Not saved.
	bool mute = false;

	// sampleAddress walks forward as the mixer consumes the buffer and drops back to zero when
	// remainingSamples runs out. An output with a null pointer sets remainingSamples but leaves
	// sampleAddress at zero, which is the one case where the two rest-length calls disagree.
	u32 sampleAddress = 0;
	u32 remainingSamples = 0;

	// A second thread arriving while one is parked here is told the channel is busy rather than
	// queueing up behind it. These remember what the parked one wanted to hand over, so the
	// enqueue can be retried once the buffer finishes.
	SceUID waitingThread = 0;
	u32 waitingAddress = 0;
	int waitingLeftVolume = 0;
	int waitingRightVolume = 0;

	void DoState(PointerWrap &p);

	void clear();
};

// Channel 8, and there is only one of it: sceAudioOutput2, sceAudioSRC and sceVaudio are three
// names for the same channel. It skips the mixer entirely - its two DMA descriptors point
// straight at the game's buffers and the codec resamples them - so it shares nothing with the
// mixer channels beyond what reserve agrees on. Two buffers fit; a third caller is turned away.
struct AudioSRCChannel {
	bool reserved = false;

	u32 sampleCount = 0;  // Buffer size agreed at reserve time.
	u32 leftVolume = 0;
	u32 rightVolume = 0;
	u32 format = 0;

	// For the debugger only. Not saved.
	bool mute = false;

	AudioPendingBuffer buffers[2]{};
	int bufferCount = 0;
	u32 playedSamples = 0;  // Consumed from buffers[0].
	u32 frac = 0;           // 16.16 position between two input samples, for resampling.
	// The driver signals a finished buffer with an event flag bit, so one completion can sit
	// there unclaimed - which is why the first output after an idle period doesn't block.
	bool completion = false;
	std::vector<SceUID> waitingThreads;

	void DoState(PointerWrap &p);

	void reset();
	void clear();

	bool Full() const {
		return bufferCount >= (int)ARRAY_SIZE(buffers);
	}
};

extern AudioChannel g_audioChans[PSP_AUDIO_CHANNEL_MAX];
extern AudioSRCChannel g_audioSRC;

// The sample rates the SRC and VAUDIO channels will accept.
bool SRCFrequencyAllowed(int freq);

// The two routing modes are globals rather than per-channel; they get their own little block.
void __AudioRoutingDoState(PointerWrap &p);

void Register_sceAudio();

