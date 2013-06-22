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
#include "FixedSizeQueue.h"

class PointerWrap;

enum  	PspAudioFormats { PSP_AUDIO_FORMAT_STEREO = 0, PSP_AUDIO_FORMAT_MONO = 0x10 };
enum  	PspAudioFrequencies { PSP_AUDIO_FREQ_44K = 44100, PSP_AUDIO_FREQ_48K = 48000 };

#define SCE_ERROR_AUDIO_CHANNEL_NOT_INIT                        0x80260001
#define SCE_ERROR_AUDIO_CHANNEL_BUSY                            0x80260002
#define SCE_ERROR_AUDIO_INVALID_CHANNEL                         0x80260003
#define SCE_ERROR_AUDIO_PRIV_REQUIRED                           0x80260004
#define SCE_ERROR_AUDIO_NO_CHANNELS_AVAILABLE                   0x80260005
#define SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED     0x80260006
#define SCE_ERROR_AUDIO_INVALID_FORMAT                          0x80260007
#define SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED                    0x80260008
#define SCE_ERROR_AUDIO_NOT_OUTPUT                              0x80260009
#define SCE_ERROR_AUDIO_INVALID_FREQUENCY						0x8026000A
#define SCE_ERROR_AUDIO_INVALID_VOLUME							0x8026000B
#define SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED				0x80268002


const u32 PSP_AUDIO_CHANNEL_MAX = 8;

const int PSP_AUDIO_CHANNEL_SRC = 8;
const int PSP_AUDIO_CHANNEL_OUTPUT2 = 8;
const int PSP_AUDIO_CHANNEL_VAUDIO = 8;

struct AudioChannelWaitInfo
{
	SceUID threadID;
	int numSamples;
};

struct AudioChannel
{
	AudioChannel() {
		clear();
	}

	// PSP side

	bool reserved;

	// last sample address
	u32 sampleAddress;
	u32 sampleCount;  // Number of samples written in each OutputBlocking
	u32 leftVolume;
	u32 rightVolume;
	u32 format;

	std::vector<AudioChannelWaitInfo> waitingThreads;

	// PC side - should probably split out

	// We copy samples as they are written into this simple ring buffer.
	// Might try something more efficient later.
	FixedSizeQueue<s16, 32768 * 8> sampleQueue;

	void DoState(PointerWrap &p);

	void reset();
	void clear();
};

// The extra channel is for SRC/Output2/Vaudio (who all share, apparently.)
extern AudioChannel chans[PSP_AUDIO_CHANNEL_MAX + 1];

void Register_sceAudio();
