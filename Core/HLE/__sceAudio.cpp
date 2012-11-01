// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "__sceAudio.h"
#include "sceAudio.h"
#include "sceKernel.h"
#include "sceKernelThread.h"
#include "StdMutex.h"
#include "CommonTypes.h"
#include "../CoreTiming.h"
#include "../MemMap.h"
#include "../Host.h"

// While buffers == MAX_BUFFERS, block on blocking write
// non-blocking writes will return busy, I guess

#define MAX_BUFFERS 2
#define MIN_BUFFERS 1

std::recursive_mutex section;

int eventAudioUpdate = -1;

const int audioIntervalMs = 20;

void hleAudioUpdate(u64 userdata, int cyclesLate)
{
	host->UpdateSound();
	__AudioUpdate();

	CoreTiming::ScheduleEvent(msToCycles(audioIntervalMs), eventAudioUpdate, 0);
}

void __AudioInit()
{
	eventAudioUpdate = CoreTiming::RegisterEvent("AudioUpdate", &hleAudioUpdate);

	CoreTiming::ScheduleEvent(msToCycles(1), eventAudioUpdate, 0);
	for (int i = 0; i < 8; i++)
		chans[i].clear();
}

void __AudioShutdown()
{
}

void __AudioUpdate()
{
	// DEBUG_LOG(HLE, "Updating audio");
	section.lock();
	for (int i = 0; i < MAX_CHANNEL; i++)
	{
		if (chans[i].triggered)
		{
			chans[i].triggered = false;

			// Instead of looping through all threads, which this does, we should keep track of which threads are waiting and just
			// resume them.
			__KernelTriggerWait(WAITTYPE_AUDIOCHANNEL, (SceUID)i, true);
		}
	}
	section.unlock();
}

u32 __AudioEnqueue(AudioChannel &chan, int chanNum, bool blocking)
{
	section.lock();
	if (chan.sampleAddress == 0)
		return SCE_ERROR_AUDIO_NOT_OUTPUT;
	if (chan.sampleQueue.size() > chan.sampleCount*2) {
		// Block!
		if (blocking) {
			__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, (SceUID)chanNum, 0, 0, false);
			section.unlock();
			return 0;
		}
		else
		{
			return SCE_ERROR_AUDIO_CHANNEL_BUSY;
		}
	}
	if (chan.format == PSP_AUDIO_FORMAT_STEREO)
	{
		for (u32 i = 0; i < chan.sampleCount * 2; i++)
		{
			chan.sampleQueue.push((s16)Memory::Read_U16(chan.sampleAddress + 2 * i));
		}
	}
	else if (chan.format == PSP_AUDIO_FORMAT_MONO)
	{
		for (u32 i = 0; i < chan.sampleCount; i++)
		{
			// Expand to stereo
			s16 sample = (s16)Memory::Read_U16(chan.sampleAddress + 2 * i);
			chan.sampleQueue.push(sample);
			chan.sampleQueue.push(sample);
		}
	}
	section.unlock();
	return 0;
}


int __AudioMix(short *outstereo, int numSamples)
{
	memset(outstereo, 0, numSamples*sizeof(short)*2);

	// Disable Audio for now.
	// return numSamples;

	section.lock();

	int numActiveChans = 0;

	for (int j = 0; j < MAX_CHANNEL; j++)
	{
		numActiveChans += chans[j].running ? 1 : 0;
	}

	if (!numActiveChans)
	{
		section.unlock();
		return numSamples;
	}

	for (int i = 0; i < MAX_CHANNEL; i++)
	{
		for (int s = 0; s < numSamples; s++)
		{
			if (chans[i].sampleQueue.size() >= 2)
			{
				s16 sample1 = chans[i].sampleQueue.front();
				chans[i].sampleQueue.pop();
				s16 sample2 = chans[i].sampleQueue.front();
				chans[i].sampleQueue.pop();

				outstereo[s*2] += sample1 / 4;//(sample * chans[i].vol1) >> 8;
				outstereo[s*2+1] += sample2 / 4;//.(sample * chans[i].vol2) >> 8;
			}
		}
		if (chans[i].sampleQueue.size() < chans[i].sampleCount)
		{
			chans[i].triggered = true;
		}
	}
	section.unlock();
	return numSamples;
}
