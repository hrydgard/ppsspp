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

#include "__sceAudio.h"
#include "sceAudio.h"
#include "sceKernel.h"
#include "sceKernelThread.h"
#include "StdMutex.h"
#include "CommonTypes.h"
#include "../CoreTiming.h"
#include "../MemMap.h"
#include "../Host.h"
#include "../Config.h"
#include "ChunkFile.h"
#include "FixedSizeQueue.h"
#include "Common/Thread.h"

// Should be used to lock anything related to the outAudioQueue.
std::recursive_mutex section;

int eventAudioUpdate = -1;
int eventHostAudioUpdate = -1;
int mixFrequency = 44100;

const int hwSampleRate = 44100;
const int hwBlockSize = 64;

// TODO: Tweak
#ifdef ANDROID
const int hostAttemptBlockSize = 2048;
#else
const int hostAttemptBlockSize = 512;
#endif

const int audioIntervalUs = (int)(1000000ULL * hwBlockSize / hwSampleRate);
const int audioHostIntervalUs = (int)(1000000ULL * hostAttemptBlockSize / hwSampleRate);

// High and low watermarks, basically.  For perfect emulation, the correct values are 0 and 1, respectively.
// TODO: Tweak
#ifdef ANDROID
	const int chanQueueMaxSizeFactor = 4;
	const int chanQueueMinSizeFactor = 2;
#else
	const int chanQueueMaxSizeFactor = 2;
	const int chanQueueMinSizeFactor = 1;
#endif

FixedSizeQueue<s16, hostAttemptBlockSize * 16> outAudioQueue;


void hleAudioUpdate(u64 userdata, int cyclesLate)
{
	__AudioUpdate();

	CoreTiming::ScheduleEvent(usToCycles(audioIntervalUs) - cyclesLate, eventAudioUpdate, 0);
}

void hleHostAudioUpdate(u64 userdata, int cyclesLate)
{
	host->UpdateSound();
	CoreTiming::ScheduleEvent(usToCycles(audioHostIntervalUs) - cyclesLate, eventHostAudioUpdate, 0);
}

void __AudioInit()
{
	mixFrequency = 44100;

	eventAudioUpdate = CoreTiming::RegisterEvent("AudioUpdate", &hleAudioUpdate);
	eventHostAudioUpdate = CoreTiming::RegisterEvent("AudioUpdateHost", &hleHostAudioUpdate);

	CoreTiming::ScheduleEvent(usToCycles(audioIntervalUs), eventAudioUpdate, 0);
	CoreTiming::ScheduleEvent(usToCycles(audioHostIntervalUs), eventHostAudioUpdate, 0);
	for (int i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)
		chans[i].clear();
}

void __AudioDoState(PointerWrap &p)
{
	p.Do(eventAudioUpdate);
	CoreTiming::RestoreRegisterEvent(eventAudioUpdate, "AudioUpdate", &hleAudioUpdate);
	p.Do(eventHostAudioUpdate);
	CoreTiming::RestoreRegisterEvent(eventHostAudioUpdate, "AudioUpdateHost", &hleHostAudioUpdate);

	p.Do(mixFrequency);

	section.lock();
	outAudioQueue.DoState(p);
	section.unlock();

	int chanCount = ARRAY_SIZE(chans);
	p.Do(chanCount);
	if (chanCount != ARRAY_SIZE(chans))
	{
		ERROR_LOG(HLE, "Savestate failure: different number of audio channels.");
		return;
	}
	for (int i = 0; i < chanCount; ++i)
		chans[i].DoState(p);

	p.DoMarker("sceAudio");
}

void __AudioShutdown()
{
	for (int i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)
		chans[i].clear();
}

u32 __AudioEnqueue(AudioChannel &chan, int chanNum, bool blocking)
{
	u32 ret = chan.sampleCount;

	if (chan.sampleAddress == 0) {
		// For some reason, multichannel audio lies and returns the sample count here.
		if (chanNum == PSP_AUDIO_CHANNEL_SRC || chanNum == PSP_AUDIO_CHANNEL_OUTPUT2) {
			ret = 0;
		}
	}

	// If there's anything on the queue at all, it should be busy, but we try to be a bit lax.
	if (chan.sampleQueue.size() > chan.sampleCount * 2 * chanQueueMaxSizeFactor || chan.sampleAddress == 0) {
		if (blocking) {
			// TODO: Regular multichannel audio seems to block for 64 samples less?  Or enqueue the first 64 sync?
			int blockSamples = chan.sampleQueue.size() / 2 / chanQueueMinSizeFactor;

			AudioChannelWaitInfo waitInfo = {__KernelGetCurThread(), blockSamples};
			chan.waitingThreads.push_back(waitInfo);
			// Also remember the value to return in the waitValue.
			__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, (SceUID)chanNum + 1, ret, 0, false, "blocking audio waited");

			// Fall through to the sample queueing, don't want to lose the samples even though
			// we're getting full.  The PSP would enqueue after blocking.
		} else {
			// Non-blocking doesn't even enqueue, but it's not commonly used.
			return SCE_ERROR_AUDIO_CHANNEL_BUSY;
		}
	}

	if (chan.sampleAddress == 0) {
		return ret;
	}

	if (chan.format == PSP_AUDIO_FORMAT_STEREO)
	{
		const u32 totalSamples = chan.sampleCount * 2;

		if (IS_LITTLE_ENDIAN)
		{
			s16 *sampleData = (s16 *) Memory::GetPointer(chan.sampleAddress);

			// Walking a pointer for speed.  But let's make sure we wouldn't trip on an invalid ptr.
			if (Memory::IsValidAddress(chan.sampleAddress + (totalSamples - 1) * sizeof(s16)))
			{
				for (u32 i = 0; i < totalSamples; i++)
					chan.sampleQueue.push(*sampleData++);
			}
		}
		else
		{
			for (u32 i = 0; i < totalSamples; i++)
				chan.sampleQueue.push((s16)Memory::Read_U16(chan.sampleAddress + sizeof(s16) * i));
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
	return ret;
}

static inline s16 clamp_s16(int i) {
	if (i > 32767)
		return 32767;
	if (i < -32768)
		return -32768;
	return i;
}

inline void __AudioWakeThreads(AudioChannel &chan, int step)
{
	u32 error;
	for (size_t w = 0; w < chan.waitingThreads.size(); ++w)
	{
		AudioChannelWaitInfo &waitInfo = chan.waitingThreads[w];
		waitInfo.numSamples -= hwBlockSize;

		// If it's done (there will still be samples on queue) and actually still waiting, wake it up.
		if (waitInfo.numSamples <= 0 && __KernelGetWaitID(waitInfo.threadID, WAITTYPE_AUDIOCHANNEL, error) != 0)
		{
			// DEBUG_LOG(HLE, "Woke thread %i for some buffer filling", waitingThread);
			u32 ret = __KernelGetWaitValue(waitInfo.threadID, error);
			__KernelResumeThreadFromWait(waitInfo.threadID, ret);

			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
		}
	}
}

void __AudioWakeThreads(AudioChannel &chan)
{
	__AudioWakeThreads(chan, 0x7FFFFFFF);
}

// Mix samples from the various audio channels into a single sample queue.
// This single sample queue is where __AudioMix should read from. If the sample queue is full, we should
// just sleep the main emulator thread a little.
void __AudioUpdate()
{
	// Audio throttle doesn't really work on the PSP since the mixing intervals are so closely tied
	// to the CPU. Much better to throttle the frame rate on frame display and just throw away audio
	// if the buffer somehow gets full.

	s32 mixBuffer[hwBlockSize * 2];
	memset(mixBuffer, 0, sizeof(mixBuffer));

	for (int i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)
	{
		if (!chans[i].reserved)
			continue;
		__AudioWakeThreads(chans[i], hwBlockSize);

		if (!chans[i].sampleQueue.size()) {
			// ERROR_LOG(HLE, "No queued samples, skipping channel %i", i);
			continue;
		}

		for (int s = 0; s < hwBlockSize; s++)
		{
			if (chans[i].sampleQueue.size() >= 2)
			{
				s16 sampleL = chans[i].sampleQueue.pop_front();
				s16 sampleR = chans[i].sampleQueue.pop_front();
				// The channel volume should be done here?
				mixBuffer[s * 2 + 0] += sampleL * (s32)chans[i].leftVolume >> 15;
				mixBuffer[s * 2 + 1] += sampleR * (s32)chans[i].rightVolume >> 15;
			} 
			else
			{
				ERROR_LOG(HLE, "Channel %i buffer underrun at %i of %i", i, s, hwBlockSize);
				break;
			}
		}
	}

	if (g_Config.bEnableSound) {
		section.lock();
		if (outAudioQueue.room() >= hwBlockSize * 2) {
			// Push the mixed samples onto the output audio queue.
			for (int i = 0; i < hwBlockSize; i++) {
				s16 sampleL = clamp_s16(mixBuffer[i * 2 + 0]);
				s16 sampleR = clamp_s16(mixBuffer[i * 2 + 1]);

				outAudioQueue.push((s16)sampleL);
				outAudioQueue.push((s16)sampleR);
			}
		} else {
			// This happens quite a lot. There's still something slightly off
			// about the amount of audio we produce.
			DEBUG_LOG(HLE, "Audio outbuffer overrun! room = %i / %i", outAudioQueue.room(), (u32)outAudioQueue.capacity());
		}
		section.unlock();
	}
	
}

void __AudioSetOutputFrequency(int freq)
{
	WARN_LOG(HLE, "Switching audio frequency to %i", freq);
	mixFrequency = freq;
}

// numFrames is number of stereo frames.
// This is called from *outside* the emulator thread.
int __AudioMix(short *outstereo, int numFrames)
{
	// TODO: if mixFrequency != the actual output frequency, resample!

	section.lock();
	int underrun = -1;
	s16 sampleL = 0;
	s16 sampleR = 0;
	bool anythingToPlay = false;
	for (int i = 0; i < numFrames; i++) {
		if (outAudioQueue.size() >= 2)
		{
			sampleL = outAudioQueue.pop_front();
			sampleR = outAudioQueue.pop_front();
			outstereo[i * 2 + 0] = sampleL;
			outstereo[i * 2 + 1] = sampleR;
			anythingToPlay = true;
		} else {
			if (underrun == -1) underrun = i;
			outstereo[i * 2 + 0] = sampleL;  // repeat last sample, can reduce clicking
			outstereo[i * 2 + 1] = sampleR;  // repeat last sample, can reduce clicking
		}
	}
	if (anythingToPlay && underrun >= 0) {
		DEBUG_LOG(HLE, "Audio out buffer UNDERRUN at %i of %i", underrun, numFrames);
	} else {
		// DEBUG_LOG(HLE, "No underrun, mixed %i samples fine", numFrames);
	}
	section.unlock();
	return underrun >= 0 ? underrun : numFrames;
}
