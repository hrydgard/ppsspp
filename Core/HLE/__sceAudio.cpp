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

#include <atomic>
#include <mutex>
#include <algorithm>

#include "Common/Common.h"
#include "Common/File/Path.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Data/Collections/FixedSizeQueue.h"
#include "Common/System/System.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/WaveFile.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceKernelTime.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HLE/sceAudio.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Util/AudioFormat.h"

// Should be used to lock anything related to the outAudioQueue.
// atomic locks are used on the lock. TODO: make this lock-free
std::atomic_flag atomicLock_;

// We copy samples as they are written into this simple ring buffer.
// Might try something more efficient later.
FixedSizeQueue<s16, 32768 * 8> legacyChanSampleQueues[PSP_AUDIO_CHANNEL_MAX + 1];

int eventAudioUpdate = -1;

// TODO: This is now useless and should be removed. Just scared of breaking states.
int eventHostAudioUpdate = -1;

int mixFrequency = 44100;
int srcFrequency = 0;

static constexpr int hwSampleRate = 44100;
static constexpr int hwBlockSize = 64;

static int audioIntervalCycles;
static int audioHostIntervalCycles;

static s32 mixBuffer[hwBlockSize * 2];
static s16 clampedMixBuffer[hwBlockSize * 2];
#ifndef MOBILE_DEVICE
WaveFileWriter g_wave_writer;
static bool m_logAudio;
#endif

// High and low watermarks, basically.  For perfect emulation, the correct values are 0 and 1, respectively.
// TODO: Tweak. Hm, there aren't actually even used currently...
static int chanQueueMaxSizeFactor;
static int chanQueueMinSizeFactor;

static void hleAudioUpdate(u64 userdata, int cyclesLate) {
	// Schedule the next cycle first.  __AudioUpdate() may consume cycles.
	CoreTiming::ScheduleEvent(audioIntervalCycles - cyclesLate, eventAudioUpdate, 0);

	__AudioUpdate();
}

static void hleHostAudioUpdate(u64 userdata, int cyclesLate) {
	CoreTiming::ScheduleEvent(audioHostIntervalCycles - cyclesLate, eventHostAudioUpdate, 0);
}

static void __AudioCPUMHzChange() {
	audioIntervalCycles = (int)(usToCycles(1000000ULL) * hwBlockSize / hwSampleRate);

	// Soon to be removed.
	audioHostIntervalCycles = (int)(usToCycles(1000000ULL) * 512 / hwSampleRate);
}

void __AudioInit() {
	System_AudioResetStatCounters();
	mixFrequency = 44100;
	srcFrequency = 0;

	chanQueueMaxSizeFactor = 2;
	chanQueueMinSizeFactor = 1;

	__AudioCPUMHzChange();

	eventAudioUpdate = CoreTiming::RegisterEvent("AudioUpdate", &hleAudioUpdate);
	eventHostAudioUpdate = CoreTiming::RegisterEvent("AudioUpdateHost", &hleHostAudioUpdate);

	CoreTiming::ScheduleEvent(audioIntervalCycles, eventAudioUpdate, 0);
	CoreTiming::ScheduleEvent(audioHostIntervalCycles, eventHostAudioUpdate, 0);
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++) {
		g_audioChans[i].index = i;
		g_audioChans[i].clear();
	}

	memset(mixBuffer, 0, hwBlockSize * 2 * sizeof(s32));

	System_AudioClear();
	CoreTiming::RegisterMHzChangeCallback(&__AudioCPUMHzChange);
}

void __AudioDoState(PointerWrap &p) {
	auto s = p.Section("sceAudio", 1, 2);
	if (!s)
		return;

	Do(p, eventAudioUpdate);
	CoreTiming::RestoreRegisterEvent(eventAudioUpdate, "AudioUpdate", &hleAudioUpdate);
	Do(p, eventHostAudioUpdate);
	CoreTiming::RestoreRegisterEvent(eventHostAudioUpdate, "AudioUpdateHost", &hleHostAudioUpdate);

	Do(p, mixFrequency);
	if (s >= 2) {
		Do(p, srcFrequency);
	} else {
		// Assume that it was actually the SRC channel frequency.
		srcFrequency = mixFrequency;
		mixFrequency = 44100;
	}

	if (s >= 2) {
		// TODO: Next time we bump, get rid of this. It's kinda useless.
		auto s = p.Section("resampler", 1);
		if (p.mode == p.MODE_READ) {
			System_AudioClear();
		}
	} else {
		// Only to preserve the previous file format. Might cause a slight audio glitch on upgrades?
		FixedSizeQueue<s16, 512 * 16> outAudioQueue;
		outAudioQueue.DoState(p);

		System_AudioClear();
	}

	int chanCount = ARRAY_SIZE(g_audioChans);
	Do(p, chanCount);
	if (chanCount != ARRAY_SIZE(g_audioChans)) {
		ERROR_LOG(Log::sceAudio, "Savestate failure: different number of audio channels.");
		p.SetError(p.ERROR_FAILURE);
		return;
	}
	for (int i = 0; i < chanCount; ++i) {
		g_audioChans[i].index = i;
		g_audioChans[i].DoState(p);
	}

	__AudioCPUMHzChange();
}

void __AudioShutdown() {
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++) {
		g_audioChans[i].index = i;
		g_audioChans[i].clear();
	}

#ifndef MOBILE_DEVICE
	if (g_Config.bDumpAudio) {
		__StopLogAudio();
	}
#endif
}

u32 __AudioEnqueue(AudioChannel &chan, int chanNum, int leftVol, int rightVol, int samplePtr, bool blocking) {
	u32 ret = chan.sampleCount;

	if (samplePtr == 0) {
		// For some reason, multichannel audio lies and returns the sample count here.
		if (chanNum == PSP_AUDIO_CHANNEL_SRC) {  // equals OUTPUT2, they share channel 8
			ret = 0;
		}
	}

	// Update channel volume.
	if (leftVol) {
		chan.leftVolume = leftVol;
	}
	if (rightVol) {
		chan.rightVolume = rightVol;
	}

	// Check for blocking when pushing the *second* block.
	if (chan.queueLength == 1) {
		if (blocking) {
			if (__KernelIsDispatchEnabled()) {
				DEBUG_LOG(Log::sceAudio, "Blocking thread %i on audio channel %i", __KernelGetCurThread(), chanNum);
				chan.waitingThreads.push_back(__KernelGetCurThread());
				// Also remember the value to return in the waitValue.
				__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, (SceUID)chanNum + 1, ret, 0, false, "blocking audio");
			} else {
				// If the channel is already busy, we can't enqueue.
				ret = SCE_KERNEL_ERROR_CAN_NOT_WAIT;
			}
		} else {
			return SCE_ERROR_AUDIO_CHANNEL_BUSY;
		}
	} else if (chan.queueLength == 0) {
		// This we shouldn't see very much, or?	
		DEBUG_LOG(Log::sceAudio, "Empty, not blocking thread %i on audio channel %i", __KernelGetCurThread(), chanNum);
	} else {
		return SCE_ERROR_AUDIO_CHANNEL_BUSY;
	}

	if (samplePtr == 0) {
		return ret;
	}

	// NOTE: The below is WRONG! See issue #20095.
	//
	// What we should be queueing here is just the sampleAddress and sampleCount. Then when dequeuing is when we should
	// read the actual data.

	QueueEntry entry;
	entry.leftVol = chan.leftVolume;
	entry.rightVol = chan.rightVolume;
	entry.sampleAddress = samplePtr;
	entry.sampleCount = chan.sampleCount;

	chan.queue[chan.queueLength++] = entry;
	_dbg_assert_(chan.queueLength <= AudioChannel::MAX_QUEUE_LENGTH);
	return ret;
}

void __AudioWakeThreads(AudioChannel &chan, int result) {
	u32 error;
	bool wokeThreads = false;
	for (size_t w = 0; w < chan.waitingThreads.size(); ++w) {
		const SceUID waitInfoThreadID = chan.waitingThreads[w];

		// If it's done (there will still be samples on queue) and actually still waiting, wake it up.
		const u32 waitID = __KernelGetWaitID(waitInfoThreadID, WAITTYPE_AUDIOCHANNEL, error);
		if (waitID != 0) {
			// DEBUG_LOG(Log::sceAudio, "Woke thread %i for some buffer filling", waitingThread);
			u32 ret = result == 0 ? __KernelGetWaitValue(waitInfoThreadID, error) : SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
			__KernelResumeThreadFromWait(waitInfoThreadID, ret);
			wokeThreads = true;
			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
		}
		// This means the thread stopped waiting, so stop trying to wake it.
		else if (waitID == 0) {
			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
		}
	}

	if (wokeThreads) {
		__KernelReSchedule("audio drain");
	}
}

void __AudioSetOutputFrequency(int freq) {
	if (freq != 44100) {
		WARN_LOG_REPORT(Log::sceAudio, "Switching audio frequency to %i", freq);
	} else {
		DEBUG_LOG(Log::sceAudio, "Switching audio frequency to %i", freq);
	}
	mixFrequency = freq;
}

void __AudioSetSRCFrequency(int freq) {
	srcFrequency = freq;
}

// Mix samples from the various audio channels into a single sample queue, managed by the backend implementation.
void __AudioUpdate(bool resetRecording) {
	DEBUG_LOG(Log::sceAudio, "=========== Audio update ============ ");

	// AUDIO throttle doesn't really work on the PSP since the mixing intervals are so closely tied
	// to the CPU. Much better to throttle the frame rate on frame display and just throw away audio
	// if the buffer somehow gets full.
	bool firstChannel = true;
	const int16_t srcBufferSize = hwBlockSize * 2;
	int16_t srcBuffer[srcBufferSize];

	// We always consume "hwBlockSize" samples currently. This should be changed.
	memset(mixBuffer, 0, hwBlockSize * 2 * sizeof(s32));

	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)	{
		AudioChannel &chan = g_audioChans[i];

		if (!chan.reserved) {
			continue;
		}

		if (chan.queueLength == 0) {
			chan.numUnderruns++;
			// Can't play an empty queue.
			// Log?
			continue;
		}

		if (!chan.queue[0].sampleAddress) {
			DEBUG_LOG(Log::sceAudio, "Channel %i queue[0] had a zero sampleAddress, skipping", i);
			chan.queue[0] = chan.queue[1];
			chan.queueLength--;
			__AudioWakeThreads(chan, 0);
			continue;
		}

		_dbg_assert_(Memory::IsValid4AlignedAddress(chan.queue[0].sampleAddress));

		const int channels = chan.format == PSP_AUDIO_FORMAT_STEREO ? 2 : 1;

		const bool needsResample = i == PSP_AUDIO_CHANNEL_SRC && srcFrequency != 0 && srcFrequency != mixFrequency;
		const size_t sz = needsResample ? (hwBlockSize * srcFrequency) / mixFrequency : hwBlockSize;

		static_assert(chan.MAX_QUEUE_LENGTH == 2);

		const s16 *buf1 = 0, *buf2 = 0;
		size_t sz1 = 0, sz2 = 0;

		buf1 = (s16 *)(Memory::base + chan.queue[0].sampleAddress) + chan.queuePlayOffset * channels;

		if (sz <= chan.queue[0].sampleCount - chan.queuePlayOffset) {
			// Easy case, no wrapping to the next buffer.
			chan.queuePlayOffset += (int)sz;
			sz1 = sz * channels;

			// If reached the end?
			if (chan.queuePlayOffset == chan.queue[0].sampleCount) {
				// If we reached the end of the queue, pop. Let's do it the easy way.
				chan.queue[0] = chan.queue[1];
				chan.queue[1] = {};
				chan.queueLength--;
				chan.queuePlayOffset = 0;
				__AudioWakeThreads(chan, 0);
				if (chan.queueLength == 0) {
					DEBUG_LOG(Log::sceAudio, "Channel %i queue ran empty at offset %d", i, chan.queuePlayOffset);
				}
			}
		} else {
			// We reached the end. Figure out how much is left in the first buffer.
			int left = (chan.queue[0].sampleCount - chan.queuePlayOffset);
			int right = sz - left;
			sz1 = left * channels;

			__AudioWakeThreads(chan, 0);
			if (chan.queueLength == 0) {
				DEBUG_LOG(Log::sceAudio, "Channel %i queue ran empty at offset %d", i, chan.queuePlayOffset);
			} else {
				// Then fill up with data from the second buffer, then pop.
				buf2 = (s16 *)(Memory::base + chan.queue[1].sampleAddress);
				sz2 = right * channels;
				chan.queue[0] = chan.queue[1];
				chan.queue[1] = {};
			}

			// _dbg_assert_(false);
			/*
			if (sz > legacyChanSampleQueues[i].size()) {
				ERROR_LOG(Log::sceAudio, "Channel %i buffer underrun at %i of %i", i, (int)legacyChanSampleQueues[i].size() / 2, (int)sz / 2);
			}*/
		}

		// We do this check as the very last thing before mixing, to maximize compatibility.
		if (g_audioChans[i].mute) {
			continue;
		}

		if (needsResample) {
			_dbg_assert_(channels == 2);
			auto read = [&](size_t i) {
				if (i < sz1)
					return buf1[i];
				if (i < sz1 + sz2)
					return buf2[i - sz1];
				if (buf2)
					return buf2[sz2 - 1];
				return buf1[sz1 - 1];
			};

			// TODO: This is terrible, since it's doing it by small chunk and discarding frac.
			// Also, this code assumes that channels == 2.
			const uint32_t ratio = (uint32_t)(65536.0 * (double)srcFrequency / (double)mixFrequency);
			uint32_t frac = 0;
			size_t readIndex = 0;
			for (size_t outIndex = 0; readIndex < sz && outIndex < srcBufferSize; outIndex += 2) {
				size_t readIndex2 = readIndex + 2;
				int16_t l1 = read(readIndex);
				int16_t r1 = read(readIndex + 1);
				int16_t l2 = read(readIndex2);
				int16_t r2 = read(readIndex2 + 1);
				int sampleL = ((l1 << 16) + (l2 - l1) * (uint16_t)frac) >> 16;
				int sampleR = ((r1 << 16) + (r2 - r1) * (uint16_t)frac) >> 16;
				srcBuffer[outIndex] = sampleL;
				srcBuffer[outIndex + 1] = sampleR;
				frac += ratio;
				readIndex += 2 * (uint16_t)(frac >> 16);
				frac &= 0xffff;
			}

			buf1 = srcBuffer;
			sz1 = srcBufferSize;
			buf2 = nullptr;
			sz2 = 0;
		}

		// Surprisingly hard to SIMD efficiently on SSE2 due to lack of 16-to-32-bit sign extension. NEON should be straight-forward though, and SSE4.1 can do it nicely.
		// Actually, the cmple/pack trick should work fine...

		// TODO: Before merge, we must incorporate the volume here.

		// sz1 includes the channels multiplier, so the below core is channel-neutral.
		for (size_t s = 0; s < sz1; s++) {
			_dbg_assert_(s < ARRAY_SIZE(mixBuffer));
			mixBuffer[s] += buf1[s];
		}
		
		if (buf2) {
			for (size_t s = 0; s < sz2; s++)
				mixBuffer[s + sz1] += buf2[s];
		}
	}

	if (g_Config.bEnableSound) {
		float multiplier = Volume100ToMultiplier(std::clamp(g_Config.iGameVolume, 0, VOLUMEHI_FULL));
		if (PSP_CoreParameter().fpsLimit != FPSLimit::NORMAL || PSP_CoreParameter().fastForward) {
			if (g_Config.iAltSpeedVolume != -1) {
				// Multiply in the alt speed volume instead of replacing like before.
				multiplier *= Volume100ToMultiplier(g_Config.iAltSpeedVolume);
			}
		}

		System_AudioPushSamples(mixBuffer, hwBlockSize, multiplier);

#ifndef MOBILE_DEVICE
		if (g_Config.bSaveLoadResetsAVdumping && resetRecording) {
			__StopLogAudio();
			std::string discID = g_paramSFO.GetDiscID();
			Path audio_file_name = GetSysDirectory(DIRECTORY_AUDIO) / StringFromFormat("%s_%s.wav", discID.c_str(), KernelTimeNowFormatted().c_str()).c_str();
			INFO_LOG(Log::Common, "Restarted audio recording to: %s", audio_file_name.c_str());
			if (!File::Exists(GetSysDirectory(DIRECTORY_AUDIO)))
				File::CreateDir(GetSysDirectory(DIRECTORY_AUDIO));
			File::CreateEmptyFile(audio_file_name);
			__StartLogAudio(audio_file_name);
		}
		if (!m_logAudio) {
			if (g_Config.bDumpAudio) {
				// Use gameID_EmulatedTimestamp for filename
				std::string discID = g_paramSFO.GetDiscID();
				Path audio_file_name = GetSysDirectory(DIRECTORY_AUDIO) / StringFromFormat("%s_%s.wav", discID.c_str(), KernelTimeNowFormatted().c_str());
				INFO_LOG(Log::Common,"Recording audio to: %s", audio_file_name.c_str());
				// Create the path just in case it doesn't exist
				if (!File::Exists(GetSysDirectory(DIRECTORY_AUDIO)))
					File::CreateDir(GetSysDirectory(DIRECTORY_AUDIO));
				File::CreateEmptyFile(audio_file_name);
				__StartLogAudio(audio_file_name);
			}
		} else {
			if (g_Config.bDumpAudio) {
				for (int i = 0; i < hwBlockSize * 2; i++) {
					clampedMixBuffer[i] = clamp_s16(mixBuffer[i]);
				}
				g_wave_writer.AddStereoSamples(clampedMixBuffer, hwBlockSize);
			} else {
				__StopLogAudio();
			}
		}
#endif
	}
}

#ifndef MOBILE_DEVICE
void __StartLogAudio(const Path& filename) {
	if (!m_logAudio) {
		m_logAudio = true;
		g_wave_writer.Start(filename, 44100);
		g_wave_writer.SetSkipSilence(false);
		NOTICE_LOG(Log::sceAudio, "Starting Audio logging");
	} else {
		WARN_LOG(Log::sceAudio, "Audio logging has already been started");
	}
}

void __StopLogAudio() {
	if (m_logAudio)	{
		m_logAudio = false;
		g_wave_writer.Stop();
		NOTICE_LOG(Log::sceAudio, "Stopping Audio logging");
	} else {
		WARN_LOG(Log::sceAudio, "Audio logging has already been stopped");
	}
}
#endif

void WAVDump::Reset() {
	__AudioUpdate(true);
}
