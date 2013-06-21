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
#include "base/mutex.h"
#include "CommonTypes.h"
#include "../CoreTiming.h"
#include "../MemMap.h"
#include "../Host.h"
#include "../Config.h"
#include "ChunkFile.h"
#include "FixedSizeQueue.h"
#include "Common/Thread.h"

// Should be used to lock anything related to the outAudioQueue.
recursive_mutex section;

int eventAudioUpdate = -1;
int eventHostAudioUpdate = -1;
int mixFrequency = 44100;

const int hwSampleRate = 44100;
const int hwBlockSize = 64;
const int hostAttemptBlockSize = 512;
const int audioIntervalUs = (int)(1000000ULL * hwBlockSize / hwSampleRate);
const int audioHostIntervalUs = (int)(1000000ULL * hostAttemptBlockSize / hwSampleRate);

// High and low watermarks, basically.  For perfect emulation, the correct values are 0 and 1, respectively.
// TODO: Tweak
const int chanQueueMaxSizeFactor = 2;
const int chanQueueMinSizeFactor = 1;

// TODO: Need to replace this with something lockless. Mutexes in the audio pipeline
// is bad mojo.
FixedSizeQueue<s16, hostAttemptBlockSize * 16> outAudioQueue;

static inline s16 clamp_s16(int i) {
	if (i > 32767)
		return 32767;
	if (i < -32768)
		return -32768;
	return i;
}

static inline s16 adjustvolume(s16 sample, int vol) {
	return clamp_s16((sample * vol) >> 15);
}

void hleAudioUpdate(u64 userdata, int cyclesLate)
{
	__AudioUpdate();

	CoreTiming::ScheduleEvent(usToCycles(audioIntervalUs) - cyclesLate, eventAudioUpdate, 0);
}

void hleHostAudioUpdate(u64 userdata, int cyclesLate)
{
	// Not all hosts need this call to poke their audio system once in a while, but those that don't
	// can just ignore it.
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
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)
		chans[i].clear();
}

void __AudioDoState(PointerWrap &p)
{
	p.Do(eventAudioUpdate);
	CoreTiming::RestoreRegisterEvent(eventAudioUpdate, "AudioUpdate", &hleAudioUpdate);
	p.Do(eventHostAudioUpdate);
	CoreTiming::RestoreRegisterEvent(eventHostAudioUpdate, "AudioUpdateHost", &hleHostAudioUpdate);

	p.Do(mixFrequency);

	{
		lock_guard guard(section);
		outAudioQueue.DoState(p);
	}

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
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)
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
	//if (chan.sampleQueue.size() > chan.sampleCount * 2 * chanQueueMaxSizeFactor || chan.sampleAddress == 0) {
	if (chan.sampleQueue.size() > 0 || chan.sampleAddress == 0) {
		if (blocking) {
			// TODO: Regular multichannel audio seems to block for 64 samples less?  Or enqueue the first 64 sync?
			int blockSamples = (int)chan.sampleQueue.size() / 2 / chanQueueMinSizeFactor;

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
#if 0
				for (u32 i = 0; i < totalSamples; i += 2) {
					chan.sampleQueue.push(adjustvolume(*sampleData++, chan.leftVolume));
					chan.sampleQueue.push(adjustvolume(*sampleData++, chan.rightVolume));
				}
#else
				s16 *buf1 = 0, *buf2 = 0;
				size_t sz1, sz2;
				chan.sampleQueue.pushPointers(totalSamples, &buf1, &sz1, &buf2, &sz2);
				int leftVol = chan.leftVolume;
				int rightVol = chan.rightVolume;

				// TODO: SSE/NEON implementations
				for (u32 i = 0; i < sz1; i += 2)
				{
					buf1[i] = adjustvolume(sampleData[i], leftVol);
					buf1[i + 1] = adjustvolume(sampleData[i + 1], rightVol);
				}
				if (buf2) {
					sampleData += sz1;
					for (u32 i = 0; i < sz2; i += 2)
					{
						buf2[i] = adjustvolume(sampleData[i], leftVol);
						buf2[i + 1] = adjustvolume(sampleData[i + 1], rightVol);
					}
				}
#endif
			}
		}
		else
		{
			for (u32 i = 0; i < totalSamples; i++) {
				s16 sampleL = (s16)Memory::Read_U16(chan.sampleAddress + sizeof(s16) * i);
				sampleL = adjustvolume(sampleL, chan.leftVolume);
				chan.sampleQueue.push(sampleL);
				i++;
				s16 sampleR = (s16)Memory::Read_U16(chan.sampleAddress + sizeof(s16) * i);
				sampleR = adjustvolume(sampleR, chan.rightVolume);
				chan.sampleQueue.push(sampleR);
			}
		}
	}
	else if (chan.format == PSP_AUDIO_FORMAT_MONO)
	{
		for (u32 i = 0; i < chan.sampleCount; i++)
		{
			// Expand to stereo
			s16 sample = (s16)Memory::Read_U16(chan.sampleAddress + 2 * i);
			chan.sampleQueue.push(adjustvolume(sample, chan.leftVolume));
			chan.sampleQueue.push(adjustvolume(sample, chan.rightVolume));
		}
	}
	return ret;
}

inline void __AudioWakeThreads(AudioChannel &chan, int result, int step)
{
	u32 error;
	for (size_t w = 0; w < chan.waitingThreads.size(); ++w)
	{
		AudioChannelWaitInfo &waitInfo = chan.waitingThreads[w];
		waitInfo.numSamples -= step;

		// If it's done (there will still be samples on queue) and actually still waiting, wake it up.
		u32 waitID = __KernelGetWaitID(waitInfo.threadID, WAITTYPE_AUDIOCHANNEL, error);
		if (waitInfo.numSamples <= 0 && waitID != 0)
		{
			// DEBUG_LOG(HLE, "Woke thread %i for some buffer filling", waitingThread);
			u32 ret = result == 0 ? __KernelGetWaitValue(waitInfo.threadID, error) : SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
			__KernelResumeThreadFromWait(waitInfo.threadID, ret);

			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
		}
		// This means the thread stopped waiting, so stop trying to wake it.
		else if (waitID == 0)
			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
	}
}

void __AudioWakeThreads(AudioChannel &chan, int result)
{
	__AudioWakeThreads(chan, result, 0x7FFFFFFF);
}

void __AudioSetOutputFrequency(int freq)
{
	WARN_LOG(HLE, "Switching audio frequency to %i", freq);
	mixFrequency = freq;
}

// Mix samples from the various audio channels into a single sample queue.
// This single sample queue is where __AudioMix should read from. If the sample queue is full, we should
// just sleep the main emulator thread a little.
void __AudioUpdate() {
	// Audio throttle doesn't really work on the PSP since the mixing intervals are so closely tied
	// to the CPU. Much better to throttle the frame rate on frame display and just throw away audio
	// if the buffer somehow gets full.
	s32 mixBuffer[hwBlockSize * 2];
	bool firstChannel = true;

	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)	{
		if (!chans[i].reserved)
			continue;

		__AudioWakeThreads(chans[i], 0, hwBlockSize);

		if (!chans[i].sampleQueue.size()) {
			continue;
		}

		if (hwBlockSize * 2 > chans[i].sampleQueue.size()) {
			ERROR_LOG(HLE, "Channel %i buffer underrun at %i of %i", i, (int)chans[i].sampleQueue.size() / 2, hwBlockSize);
		}

		const s16 *buf1 = 0, *buf2 = 0;
		size_t sz1, sz2;

		chans[i].sampleQueue.popPointers(hwBlockSize * 2, &buf1, &sz1, &buf2, &sz2);

		if (firstChannel) {
			for (int s = 0; s < sz1; s++)
				mixBuffer[s] = buf1[s];
			if (buf2) {
				for (int s = 0; s < sz2; s++)
					mixBuffer[s + sz1] = buf2[s];
			}
			firstChannel = false;
		} else {
			for (int s = 0; s < sz1; s++)
				mixBuffer[s] += buf1[s];
			if (buf2) {
				for (int s = 0; s < sz2; s++)
					mixBuffer[s + sz1] += buf2[s];
			}
		}
	}

	if (firstChannel) {
		memset(mixBuffer, 0, sizeof(mixBuffer));
	}

	if (g_Config.bEnableSound) {
		lock_guard guard(section);
		if (outAudioQueue.room() >= hwBlockSize * 2) {
			s16 *buf1 = 0, *buf2 = 0;
			size_t sz1, sz2;
			outAudioQueue.pushPointers(hwBlockSize * 2, &buf1, &sz1, &buf2, &sz2);
			for (int s = 0; s < sz1; s++)
				buf1[s] = clamp_s16(mixBuffer[s]);
			if (buf2) {
				for (int s = 0; s < sz2; s++)
					buf2[s] = clamp_s16(mixBuffer[s + sz1]);
			}
		} else {
			// This happens quite a lot. There's still something slightly off
			// about the amount of audio we produce.
			DEBUG_LOG(HLE, "Audio outbuffer overrun! room = %i / %i", outAudioQueue.room(), (u32)outAudioQueue.capacity());
		}
	}
}

// numFrames is number of stereo frames.
// This is called from *outside* the emulator thread.
int __AudioMix(short *outstereo, int numFrames)
{
	// TODO: if mixFrequency != the actual output frequency, resample!
	int underrun = -1;
	s16 sampleL = 0;
	s16 sampleR = 0;

	const s16 *buf1 = 0, *buf2 = 0;
	size_t sz1, sz2;
	{
		lock_guard guard(section);
		outAudioQueue.popPointers(numFrames * 2, &buf1, &sz1, &buf2, &sz2);
		memcpy(outstereo, buf1, sz1 * sizeof(s16));
		if (buf2) {
			memcpy(outstereo + sz1, buf2, sz2 * sizeof(s16));
		}
	}

	int remains = (int)(numFrames * 2 - sz1 - sz2);
	if (remains > 0)
		memset(outstereo + numFrames * 2 - remains, 0, remains);

	if (sz1 + sz2 < numFrames) {
		underrun = (int)(sz1 + sz2) / 2;
		DEBUG_LOG(HLE, "Audio out buffer UNDERRUN at %i of %i", underrun, numFrames);
	}
	return underrun >= 0 ? underrun : numFrames;
}
