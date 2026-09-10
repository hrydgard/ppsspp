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

#include "Common/File/Path.h"

#include "sceAudio.h"

struct AudioDebugStats {
	int buffered;
	int watermark;
	int bufsize;
	int underrunCount;
	int overrunCount;
	int instantSampleRate;
	int targetSampleRate;
	int lastPushSize;
};

// Easy interface for sceAudio to write to, to keep the complexity in check.

void __AudioInit();
void __AudioDoState(PointerWrap &p);
void __AudioUpdate(bool resetRecording = false);
void __AudioShutdown();
void __AudioSetOutputFrequency(int freq);
void __AudioSetSRCFrequency(int freq);

// The driver's single enqueue point for channels 0-7. Returns the channel's reserved sample
// count, or SCE_ERROR_AUDIO_CHANNEL_BUSY when a buffer is already in flight. A negative
// volume means "leave it alone".
u32 __AudioEnqueue(AudioChannel &chan, u32 samplePtr, int leftVol, int rightVol);
// Same, but parks the calling thread until the channel frees up. Only one thread can be
// parked; a second one gets SCE_ERROR_AUDIO_CHANNEL_BUSY straight back.
u32 __AudioEnqueueBlocking(AudioChannel &chan, u32 samplePtr, int leftVol, int rightVol);

// Channel 8 - Output2, SRC and Vaudio all share it. Two buffers fit; a third caller gets
// SCE_ERROR_AUDIO_CHANNEL_BUSY without waiting. A successful call waits for one buffer to
// finish before returning, except when nothing was playing to begin with.
u32 __AudioSRCEnqueueBlocking(AudioChannel &chan, u32 samplePtr, int vol);
// Hands the SRC channel a completion that the next caller can consume without waiting.
void __AudioSRCSignal(AudioChannel &chan);

// Wakes whoever is parked on the channel with the given error, and forgets their buffer.
void __AudioWakeThreads(AudioChannel &chan, int result);

void __AudioCPUMHzChange();

// AUDIO Dumping stuff
void __StartLogAudio(const Path &filename);
void __StopLogAudio();

class WAVDump {
public:
	static void Reset();
};
