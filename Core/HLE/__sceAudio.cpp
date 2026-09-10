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

static void hleAudioUpdate(u64 userdata, int cyclesLate) {
	// Schedule the next cycle first.  __AudioUpdate() may consume cycles.
	CoreTiming::ScheduleEvent(audioIntervalCycles - cyclesLate, eventAudioUpdate, 0);

	__AudioUpdate();
}

static void hleHostAudioUpdate(u64 userdata, int cyclesLate) {
	CoreTiming::ScheduleEvent(audioHostIntervalCycles - cyclesLate, eventHostAudioUpdate, 0);
}

void __AudioCPUMHzChange() {
	audioIntervalCycles = (int)(usToCycles(1000000ULL) * hwBlockSize / hwSampleRate);

	// Soon to be removed.
	audioHostIntervalCycles = (int)(usToCycles(1000000ULL) * 512 / hwSampleRate);
}

void __AudioInit() {
	System_AudioResetStatCounters();
	mixFrequency = 44100;
	srcFrequency = 0;

	__AudioCPUMHzChange();

	eventAudioUpdate = CoreTiming::RegisterEvent("AudioUpdate", &hleAudioUpdate);
	eventHostAudioUpdate = CoreTiming::RegisterEvent("AudioUpdateHost", &hleHostAudioUpdate);

	CoreTiming::ScheduleEvent(audioIntervalCycles, eventAudioUpdate, 0);
	CoreTiming::ScheduleEvent(audioHostIntervalCycles, eventHostAudioUpdate, 0);
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++) {
		g_audioChans[i].index = i;
		g_audioChans[i].clear();
	}

	mixBuffer = new s32[hwBlockSize * 2];
	clampedMixBuffer = new s16[hwBlockSize * 2];
	memset(mixBuffer, 0, hwBlockSize * 2 * sizeof(s32));

	System_AudioClear();
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
	if (chanCount != ARRAY_SIZE(g_audioChans))
	{
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
	delete [] mixBuffer;
	delete [] clampedMixBuffer;

	mixBuffer = 0;
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

// The audio driver never copies a buffer on the way in. It stores the pointer, and its mixer
// thread reads 64 samples straight out of the game's memory every DMA block, walking
// sampleAddress forward until the buffer is spent. Everything below follows that shape; see
// docs/sceAudio.md for the behavior this is modelled on.

// Unity gain on the PSP is 0x8000. Accumulate at full width and clamp once at the end, the way
// the driver's 32-bit mix accumulator does, rather than clamping each channel separately.
// 64-bit because the SRC channel accepts volumes up to 0xFFFFF, which overflows a 32-bit
// product against a full-scale sample.
static inline int ApplyChannelVolume(int sample, int vol) {
	return (int)(((s64)sample * vol) >> 15);
}

// Set while __AudioUpdate is running, so a buffer accepted from inside it - the retry a parked
// thread gets when its predecessor finishes - doesn't try to start the DMA again.
static bool audioMixing;

// Only channels 0-7. The SRC channel is on its own DMA that the mixer never touches, so the
// two start independently of each other.
static bool __AudioAnyChannelPlaying() {
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX; i++) {
		if (g_audioChans[i].sampleAddress != 0) {
			return true;
		}
	}
	return false;
}

// The driver starts the mixer's DMA the moment the first buffer arrives, and its mixer thread -
// which outranks whoever called - immediately fills a block from it. So the first 64 samples
// are gone before the output call has returned, and a channel reserved for exactly 64 samples
// is free again right away. Re-phasing the mix event to the buffer's arrival reproduces that
// and costs nothing: the interval, and so the sample rate, is unchanged.
static void __AudioStartMixerDMA() {
	if (audioMixing) {
		return;
	}
	CoreTiming::UnscheduleEvent(eventAudioUpdate, 0);
	__AudioUpdate();
	CoreTiming::ScheduleEvent(audioIntervalCycles, eventAudioUpdate, 0);
}

// The SRC channel's DMA also starts when its first buffer arrives, but it feeds the codec
// directly rather than going through the mixer, so nothing is read early - only the phase moves.
// Without this the buffer would retire somewhere in the next 1.5ms depending on where the mix
// event happened to be, and a game polling sceAudioOutput2GetRestSample would see a different
// answer every run. Only safe to do while no mixer channel is playing, since the two share one
// event here and the mixer's phase is the one that has samples riding on it.
static void __AudioStartSRCDMA() {
	if (audioMixing || __AudioAnyChannelPlaying()) {
		return;
	}
	CoreTiming::UnscheduleEvent(eventAudioUpdate, 0);
	CoreTiming::ScheduleEvent(audioIntervalCycles, eventAudioUpdate, 0);
}

u32 __AudioEnqueue(AudioChannel &chan, u32 samplePtr, int leftVol, int rightVol) {
	if (!chan.reserved) {
		return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	}
	// One buffer slot per channel, with no queue behind it.
	if (chan.sampleAddress != 0) {
		return SCE_ERROR_AUDIO_CHANNEL_BUSY;
	}

	chan.remainingSamples = chan.sampleCount;
	if (leftVol >= 0) {
		chan.leftVolume = leftVol;
	}
	if (rightVol >= 0) {
		chan.rightVolume = rightVol;
	}
	// A null pointer is accepted and leaves the channel idle, but still counts as a buffer's
	// worth of remaining samples - which is the one case where the two rest-length calls
	// disagree with each other.
	const bool wasIdle = !__AudioAnyChannelPlaying();
	chan.sampleAddress = samplePtr;
	if (samplePtr != 0 && wasIdle) {
		__AudioStartMixerDMA();
	}
	return chan.sampleCount;
}

u32 __AudioEnqueueBlocking(AudioChannel &chan, u32 samplePtr, int leftVol, int rightVol) {
	u32 result = __AudioEnqueue(chan, samplePtr, leftVol, rightVol);
	if (result != SCE_ERROR_AUDIO_CHANNEL_BUSY) {
		return result;
	}

	// The driver keeps a single "a thread is waiting" flag per channel, so the second thread
	// to arrive is turned away rather than lining up behind the first. A game that runs a
	// movie thread and a sound-effect thread over one channel depends on being told this -
	// blocking it instead makes the two take turns and halves the movie's audio rate.
	if (chan.waitingThread != 0) {
		return SCE_ERROR_AUDIO_CHANNEL_BUSY;
	}
	if (!__KernelIsDispatchEnabled()) {
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}

	chan.waitingThread = __KernelGetCurThread();
	chan.waitingAddress = samplePtr;
	chan.waitingLeftVolume = leftVol;
	chan.waitingRightVolume = rightVol;
	// __AudioChannelFinished retries the enqueue and supplies the real return value.
	__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, (SceUID)chan.index + 1, chan.sampleCount, 0, false, "blocking audio");
	return chan.sampleCount;
}

// The buffer ran out - the driver's mixer would set this channel's bit in its event flag here.
static bool __AudioChannelFinished(AudioChannel &chan) {
	chan.sampleAddress = 0;
	chan.remainingSamples = 0;
	if (chan.waitingThread == 0) {
		return false;
	}

	const SceUID threadID = chan.waitingThread;
	chan.waitingThread = 0;

	u32 error;
	if (__KernelGetWaitID(threadID, WAITTYPE_AUDIOCHANNEL, error) == 0) {
		// It stopped waiting on its own - deleted, or the wait was cancelled.
		return false;
	}

	__KernelResumeThreadFromWait(threadID, __AudioEnqueue(chan, chan.waitingAddress, chan.waitingLeftVolume, chan.waitingRightVolume));
	return true;
}

u32 __AudioSRCEnqueueBlocking(AudioChannel &chan, u32 samplePtr, int vol) {
	if (!chan.reserved) {
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	// Two DMA descriptors, so two buffers fit. The third caller is refused outright - unlike
	// the mixer channels it does not even get the chance to wait for a slot.
	if (chan.SRCFull()) {
		return SCE_ERROR_AUDIO_CHANNEL_BUSY;
	}

	if (vol >= 0) {
		chan.leftVolume = vol;
		chan.rightVolume = vol;
	}

	u32 result = 0;
	if (samplePtr != 0) {
		if (chan.srcBufferCount == 0) {
			// Starting the DMA signals a completion by itself, which is why the first
			// output after an idle stretch returns without blocking.
			chan.srcCompletion = true;
			chan.srcPlayedSamples = 0;
			chan.srcFrac = 0;
		}
		const bool wasIdle = chan.srcBufferCount == 0;
		chan.srcBuffers[chan.srcBufferCount].address = samplePtr;
		chan.srcBuffers[chan.srcBufferCount].samples = chan.sampleCount;
		chan.srcBufferCount++;
		result = chan.sampleCount;
		if (wasIdle) {
			__AudioStartSRCDMA();
		}
	} else if (chan.srcBufferCount == 0) {
		// Nothing playing and nothing handed over, so there is no completion to wait for.
		return 0;
	}

	if (chan.srcCompletion) {
		chan.srcCompletion = false;
		return result;
	}
	if (!__KernelIsDispatchEnabled()) {
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}

	chan.srcWaitingThreads.push_back(__KernelGetCurThread());
	__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, (SceUID)chan.index + 1, result, 0, false, "blocking audio");
	return result;
}

void __AudioSRCSignal(AudioChannel &chan) {
	chan.srcCompletion = true;
}

// One of the two SRC buffers finished playing.
static bool __AudioSRCCompleted(AudioChannel &chan) {
	if (chan.srcWaitingThreads.empty()) {
		// Nobody is listening, so the completion sits there for the next caller to consume.
		chan.srcCompletion = true;
		return false;
	}

	const SceUID threadID = chan.srcWaitingThreads.front();
	chan.srcWaitingThreads.erase(chan.srcWaitingThreads.begin());

	u32 error;
	if (__KernelGetWaitID(threadID, WAITTYPE_AUDIOCHANNEL, error) == 0) {
		return false;
	}
	__KernelResumeThreadFromWait(threadID, __KernelGetWaitValue(threadID, error));
	return true;
}

void __AudioWakeThreads(AudioChannel &chan, int result) {
	bool woke = false;

	if (chan.waitingThread != 0) {
		const SceUID threadID = chan.waitingThread;
		chan.waitingThread = 0;
		u32 error;
		if (__KernelGetWaitID(threadID, WAITTYPE_AUDIOCHANNEL, error) != 0) {
			__KernelResumeThreadFromWait(threadID, result);
			woke = true;
		}
	}

	for (SceUID threadID : chan.srcWaitingThreads) {
		u32 error;
		if (__KernelGetWaitID(threadID, WAITTYPE_AUDIOCHANNEL, error) != 0) {
			__KernelResumeThreadFromWait(threadID, result);
			woke = true;
		}
	}
	chan.srcWaitingThreads.clear();

	if (woke) {
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

// Mixes one block from a mixer channel, reading straight out of the game's buffer.
static bool __AudioMixChannel(AudioChannel &chan) {
	if (chan.sampleAddress == 0 || chan.remainingSamples == 0) {
		return false;
	}

	const u32 count = std::min(chan.remainingSamples, (u32)hwBlockSize);
	const bool mono = chan.format == PSP_AUDIO_FORMAT_MONO;
	const u32 stride = mono ? 2 : 4;

	// The samples are consumed either way; muting only drops them on the floor.
	if (!chan.mute && Memory::IsValidRange(chan.sampleAddress, count * stride)) {
		const s16_le *src = (const s16_le *)Memory::GetPointerUnchecked(chan.sampleAddress);
		const int leftVol = chan.leftVolume;
		const int rightVol = chan.rightVolume;
		if (mono) {
			// A mono channel reads the same sample into both sides, which is how the
			// hardware expands it - there is no separate mono path in the mixer.
			for (u32 s = 0; s < count; s++) {
				const s16 sample = src[s];
				mixBuffer[s * 2] += ApplyChannelVolume(sample, leftVol);
				mixBuffer[s * 2 + 1] += ApplyChannelVolume(sample, rightVol);
			}
		} else {
			for (u32 s = 0; s < count; s++) {
				mixBuffer[s * 2] += ApplyChannelVolume(src[s * 2], leftVol);
				mixBuffer[s * 2 + 1] += ApplyChannelVolume(src[s * 2 + 1], rightVol);
			}
		}
	}

	chan.sampleAddress += count * stride;
	chan.remainingSamples -= count;
	if (chan.remainingSamples == 0) {
		return __AudioChannelFinished(chan);
	}
	return false;
}

// Channel 8 never reaches the mixer on hardware - the DMA feeds the codec directly and the
// codec resamples. Model that as a read straight through the pending buffers at the ratio
// between the reserved frequency and the output rate.
static bool __AudioMixSRC(AudioChannel &chan) {
	if (chan.srcBufferCount == 0) {
		return false;
	}

	// Zero means "whatever the output is running at", so no conversion.
	const int inRate = srcFrequency != 0 ? srcFrequency : mixFrequency;
	const u32 ratio = (u32)(((u64)(u32)inRate << 16) / (u32)mixFrequency);
	const bool mono = chan.format == PSP_AUDIO_FORMAT_MONO;
	const u32 stride = mono ? 2 : 4;
	const int leftVol = chan.leftVolume;
	const int rightVol = chan.rightVolume;

	bool woke = false;
	for (int out = 0; out < hwBlockSize; out++) {
		if (chan.srcBufferCount == 0) {
			// Underrun. The rest of the block stays silent, like a descriptor the game
			// never got around to arming.
			break;
		}

		const AudioPendingBuffer &buf = chan.srcBuffers[0];
		const u32 addr = buf.address + chan.srcPlayedSamples * stride;
		// Interpolating against the following sample matters when a game reserved 22050Hz
		// or similar; at the native rate the fraction is always zero and this reduces to a
		// plain copy.
		const u32 avail = std::min(buf.samples - chan.srcPlayedSamples, 2u);
		if (!chan.mute && Memory::IsValidRange(addr, avail * stride)) {
			const s16_le *src = (const s16_le *)Memory::GetPointerUnchecked(addr);
			const int l0 = src[0];
			const int r0 = mono ? l0 : src[1];
			const int l1 = avail > 1 ? (int)src[stride / 2] : l0;
			const int r1 = avail > 1 ? (mono ? l1 : (int)src[stride / 2 + 1]) : r0;
			// 15 bits of fraction, not 16 - a full 16 would overflow the product against a
			// full-scale difference.
			const int frac = (int)(chan.srcFrac >> 1);
			mixBuffer[out * 2] += ApplyChannelVolume(l0 + (((l1 - l0) * frac) >> 15), leftVol);
			mixBuffer[out * 2 + 1] += ApplyChannelVolume(r0 + (((r1 - r0) * frac) >> 15), rightVol);
		}

		chan.srcFrac += ratio;
		u32 step = chan.srcFrac >> 16;
		chan.srcFrac &= 0xFFFF;
		while (step > 0 && chan.srcBufferCount > 0) {
			const u32 take = std::min(step, chan.srcBuffers[0].samples - chan.srcPlayedSamples);
			chan.srcPlayedSamples += take;
			step -= take;
			if (chan.srcPlayedSamples >= chan.srcBuffers[0].samples) {
				chan.srcBuffers[0] = chan.srcBuffers[1];
				chan.srcBufferCount--;
				chan.srcPlayedSamples = 0;
				woke |= __AudioSRCCompleted(chan);
			}
		}
	}
	return woke;
}

// Mix samples from the various audio channels into a single sample queue, managed by the backend implementation.
void __AudioUpdate(bool resetRecording) {
	// AUDIO throttle doesn't really work on the PSP since the mixing intervals are so closely tied
	// to the CPU. Much better to throttle the frame rate on frame display and just throw away audio
	// if the buffer somehow gets full.
	memset(mixBuffer, 0, hwBlockSize * 2 * sizeof(s32));

	audioMixing = true;
	bool woke = false;
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX; i++) {
		// Deliberately not gated on `reserved`: sceAudioChRelease only clears the
		// reservation, and a buffer already in flight keeps playing out.
		woke |= __AudioMixChannel(g_audioChans[i]);
	}
	woke |= __AudioMixSRC(g_audioChans[PSP_AUDIO_CHANNEL_SRC]);
	audioMixing = false;

	if (woke) {
		__KernelReSchedule("audio drain");
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
		if (!g_wave_writer.Start(filename, 44100)) {
			// Start() logs the reason. Leave m_logAudio false, or every mixed block from here on
			// would hand samples to a closed file - and turn the setting off too, since otherwise
			// the caller below retries this (creating the file, opening it) once per block.
			ERROR_LOG(Log::sceAudio, "Failed to start audio logging, disabling it");
			g_Config.bDumpAudio = false;
			return;
		}
		m_logAudio = true;
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
