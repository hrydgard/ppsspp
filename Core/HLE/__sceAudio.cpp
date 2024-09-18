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

#include "Common/Common.h"
#include "Common/File/Path.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Data/Collections/FixedSizeQueue.h"
#include "Common/System/System.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#ifndef MOBILE_DEVICE
#include "Core/WaveFile.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceKernelTime.h"
#include "StringUtils.h"
#endif
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
FixedSizeQueue<s16, 32768 * 8> chanSampleQueues[PSP_AUDIO_CHANNEL_MAX + 1];

int eventAudioUpdate = -1;

// TODO: This is now useless and should be removed. Just scared of breaking states.
int eventHostAudioUpdate = -1;

int mixFrequency = 44100;
int srcFrequency = 0;

const int hwSampleRate = 44100;
const int hwBlockSize = 64;

static int audioIntervalCycles;
static int audioHostIntervalCycles;

static s32 *mixBuffer;
static s16 *clampedMixBuffer;
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
		chans[i].index = i;
		chans[i].clear();
	}

	mixBuffer = new s32[hwBlockSize * 2];
	clampedMixBuffer = new s16[hwBlockSize * 2];
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

	int chanCount = ARRAY_SIZE(chans);
	Do(p, chanCount);
	if (chanCount != ARRAY_SIZE(chans))
	{
		ERROR_LOG(Log::sceAudio, "Savestate failure: different number of audio channels.");
		p.SetError(p.ERROR_FAILURE);
		return;
	}
	for (int i = 0; i < chanCount; ++i) {
		chans[i].index = i;
		chans[i].DoState(p);
	}

	__AudioCPUMHzChange();
}

void __AudioShutdown() {
	delete [] mixBuffer;
	delete [] clampedMixBuffer;

	mixBuffer = 0;
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++) {
		chans[i].index = i;
		chans[i].clear();
	}

#ifndef MOBILE_DEVICE
	if (g_Config.bDumpAudio) {
		__StopLogAudio();
	}
#endif
}

u32 __AudioEnqueue(AudioChannel &chan, int chanNum, bool blocking) {
	u32 ret = chan.sampleCount;

	if (chan.sampleAddress == 0) {
		// For some reason, multichannel audio lies and returns the sample count here.
		if (chanNum == PSP_AUDIO_CHANNEL_SRC || chanNum == PSP_AUDIO_CHANNEL_OUTPUT2) {
			ret = 0;
		}
	}

	// If there's anything on the queue at all, it should be busy, but we try to be a bit lax.
	//if (chanSampleQueues[chanNum].size() > chan.sampleCount * 2 * chanQueueMaxSizeFactor || chan.sampleAddress == 0) {
	if (chanSampleQueues[chanNum].size() > 0) {
		if (blocking) {
			// TODO: Regular multichannel audio seems to block for 64 samples less?  Or enqueue the first 64 sync?
			int blockSamples = (int)chanSampleQueues[chanNum].size() / 2 / chanQueueMinSizeFactor;

			if (__KernelIsDispatchEnabled()) {
				AudioChannelWaitInfo waitInfo = {__KernelGetCurThread(), blockSamples};
				chan.waitingThreads.push_back(waitInfo);
				// Also remember the value to return in the waitValue.
				__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, (SceUID)chanNum + 1, ret, 0, false, "blocking audio");
			} else {
				// TODO: Maybe we shouldn't take this audio after all?
				ret = SCE_KERNEL_ERROR_CAN_NOT_WAIT;
			}

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

	int leftVol = chan.leftVolume;
	int rightVol = chan.rightVolume;

	if (leftVol == (1 << 15) && rightVol == (1 << 15) && chan.format == PSP_AUDIO_FORMAT_STEREO && IS_LITTLE_ENDIAN) {
		// TODO: Add mono->stereo conversion to this path.

		// Good news: the volume doesn't affect the values at all.
		// We can just do a direct memory copy.
		const u32 totalSamples = chan.sampleCount * (chan.format == PSP_AUDIO_FORMAT_STEREO ? 2 : 1);
		s16 *buf1 = 0, *buf2 = 0;
		size_t sz1, sz2;
		chanSampleQueues[chanNum].pushPointers(totalSamples, &buf1, &sz1, &buf2, &sz2);

		if (Memory::IsValidAddress(chan.sampleAddress + (totalSamples - 1) * sizeof(s16_le))) {
			Memory::Memcpy(buf1, chan.sampleAddress, (u32)sz1 * sizeof(s16));
			if (buf2)
				Memory::Memcpy(buf2, chan.sampleAddress + (u32)sz1 * sizeof(s16), (u32)sz2 * sizeof(s16));
		}
	} else {
		// Remember that maximum volume allowed is 0xFFFFF so left shift is no issue.
		// This way we can optimally shift by 16.
		leftVol <<=1;
		rightVol <<=1;

		if (chan.format == PSP_AUDIO_FORMAT_STEREO) {
			const u32 totalSamples = chan.sampleCount * 2;

			s16_le *sampleData = (s16_le *) Memory::GetPointer(chan.sampleAddress);

			// Walking a pointer for speed.  But let's make sure we wouldn't trip on an invalid ptr.
			if (Memory::IsValidAddress(chan.sampleAddress + (totalSamples - 1) * sizeof(s16_le))) {
				s16 *buf1 = 0, *buf2 = 0;
				size_t sz1, sz2;
				chanSampleQueues[chanNum].pushPointers(totalSamples, &buf1, &sz1, &buf2, &sz2);
				AdjustVolumeBlock(buf1, sampleData, sz1, leftVol, rightVol);
				if (buf2) {
					AdjustVolumeBlock(buf2, sampleData + sz1, sz2, leftVol, rightVol);
				}
			}
		} else if (chan.format == PSP_AUDIO_FORMAT_MONO) {
			// Rare, so unoptimized. Expands to stereo.
			for (u32 i = 0; i < chan.sampleCount; i++) {
				s16 sample = (s16)Memory::Read_U16(chan.sampleAddress + 2 * i);
				chanSampleQueues[chanNum].push(ApplySampleVolume(sample, leftVol));
				chanSampleQueues[chanNum].push(ApplySampleVolume(sample, rightVol));
			}
		}
	}
	return ret;
}

inline void __AudioWakeThreads(AudioChannel &chan, int result, int step) {
	u32 error;
	bool wokeThreads = false;
	for (size_t w = 0; w < chan.waitingThreads.size(); ++w) {
		AudioChannelWaitInfo &waitInfo = chan.waitingThreads[w];
		waitInfo.numSamples -= step;

		// If it's done (there will still be samples on queue) and actually still waiting, wake it up.
		u32 waitID = __KernelGetWaitID(waitInfo.threadID, WAITTYPE_AUDIOCHANNEL, error);
		if (waitInfo.numSamples <= 0 && waitID != 0) {
			// DEBUG_LOG(Log::sceAudio, "Woke thread %i for some buffer filling", waitingThread);
			u32 ret = result == 0 ? __KernelGetWaitValue(waitInfo.threadID, error) : SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
			__KernelResumeThreadFromWait(waitInfo.threadID, ret);
			wokeThreads = true;

			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
		}
		// This means the thread stopped waiting, so stop trying to wake it.
		else if (waitID == 0)
			chan.waitingThreads.erase(chan.waitingThreads.begin() + w--);
	}

	if (wokeThreads) {
		__KernelReSchedule("audio drain");
	}
}

void __AudioWakeThreads(AudioChannel &chan, int result) {
	__AudioWakeThreads(chan, result, 0x7FFFFFFF);
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
	// AUDIO throttle doesn't really work on the PSP since the mixing intervals are so closely tied
	// to the CPU. Much better to throttle the frame rate on frame display and just throw away audio
	// if the buffer somehow gets full.
	bool firstChannel = true;
	const int16_t srcBufferSize = hwBlockSize * 2;
	int16_t srcBuffer[srcBufferSize];

	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++)	{
		if (!chans[i].reserved)
			continue;

		__AudioWakeThreads(chans[i], 0, hwBlockSize);

		if (!chanSampleQueues[i].size()) {
			continue;
		}

		bool needsResample = i == PSP_AUDIO_CHANNEL_SRC && srcFrequency != 0 && srcFrequency != mixFrequency;
		size_t sz = needsResample ? (srcBufferSize * srcFrequency) / mixFrequency : srcBufferSize;
		if (sz > chanSampleQueues[i].size()) {
			ERROR_LOG(Log::sceAudio, "Channel %i buffer underrun at %i of %i", i, (int)chanSampleQueues[i].size() / 2, (int)sz / 2);
		}

		const s16 *buf1 = 0, *buf2 = 0;
		size_t sz1, sz2;

		chanSampleQueues[i].popPointers(sz, &buf1, &sz1, &buf2, &sz2);

		if (needsResample) {
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
			const uint32_t ratio = (uint32_t)(65536.0 * srcFrequency / (double)mixFrequency);
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

		if (firstChannel) {
			for (size_t s = 0; s < sz1; s++)
				mixBuffer[s] = buf1[s];
			if (buf2) {
				for (size_t s = 0; s < sz2; s++)
					mixBuffer[s + sz1] = buf2[s];
			}
			firstChannel = false;
		} else {
			// Surprisingly hard to SIMD efficiently on SSE2 due to lack of 16-to-32-bit sign extension. NEON should be straight-forward though, and SSE4.1 can do it nicely.
			// Actually, the cmple/pack trick should work fine...
			for (size_t s = 0; s < sz1; s++)
				mixBuffer[s] += buf1[s];
			if (buf2) {
				for (size_t s = 0; s < sz2; s++)
					mixBuffer[s + sz1] += buf2[s];
			}
		}
	}

	if (firstChannel) {
		// Nothing was written above, let's memset.
		memset(mixBuffer, 0, hwBlockSize * 2 * sizeof(s32));
	}

	if (g_Config.bEnableSound) {
		System_AudioPushSamples(mixBuffer, hwBlockSize);
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
