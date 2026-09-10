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
#include "Core/HLE/HLE.h"
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
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX; i++) {
		g_audioChans[i].index = i;
		g_audioChans[i].clear();
	}
	g_audioSRC.clear();

	mixBuffer = new s32[hwBlockSize * 2];
	clampedMixBuffer = new s16[hwBlockSize * 2];
	memset(mixBuffer, 0, hwBlockSize * 2 * sizeof(s32));

	System_AudioClear();
}

void __AudioDoState(PointerWrap &p) {
	auto s = p.Section("sceAudio", 1, 3);
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

	// Before v3 the SRC channel was a ninth entry in this array rather than its own thing, so
	// older states carry one extra record here. Its contents are the old sample-ring format
	// that can't be converted anyway, so it gets read into a throwaway and dropped.
	int chanCount = ARRAY_SIZE(g_audioChans);
	Do(p, chanCount);
	const int expected = s >= 3 ? (int)ARRAY_SIZE(g_audioChans) : (int)ARRAY_SIZE(g_audioChans) + 1;
	if (chanCount != expected) {
		ERROR_LOG(Log::sceAudio, "Savestate failure: different number of audio channels.");
		p.SetError(p.ERROR_FAILURE);
		return;
	}
	for (int i = 0; i < chanCount; ++i) {
		if (i < (int)ARRAY_SIZE(g_audioChans)) {
			g_audioChans[i].index = i;
			g_audioChans[i].DoState(p);
		} else {
			AudioChannel discarded;
			discarded.index = i;
			discarded.DoState(p);
		}
	}

	if (s >= 3) {
		g_audioSRC.DoState(p);
		__AudioRoutingDoState(p);
	} else if (p.mode == p.MODE_READ) {
		// The old format read the routing modes back once per channel, above.
		g_audioSRC.clear();
	}

	__AudioCPUMHzChange();
}

void __AudioShutdown() {
	delete [] mixBuffer;
	delete [] clampedMixBuffer;

	mixBuffer = 0;
	for (u32 i = 0; i < PSP_AUDIO_CHANNEL_MAX; i++) {
		g_audioChans[i].index = i;
		g_audioChans[i].clear();
	}
	g_audioSRC.clear();

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
// Set while that mixing is happening underneath a syscall rather than from the timing event.
static bool audioMixingInSyscall;

// Switching threads is fine from the timing event, but not from inside an output call: the
// syscall's return value is written after the call body runs, so a context switch here would
// put it in the wrong thread's registers. hleReSchedule defers to after the syscall instead.
static void __AudioReScheduleAfterWake() {
	if (audioMixingInSyscall) {
		hleReSchedule("audio drain");
	} else {
		__KernelReSchedule("audio drain");
	}
}

// Only channels 0-7. The SRC channel is on its own DMA that the mixer never touches, so the
// two start independently of each other.
static bool __AudioAnyChannelPlaying() {
	for (const AudioChannel &chan : g_audioChans) {
		if (chan.sampleAddress != 0) {
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
	// A mixer output is cheap - well under 10us - except for this one, which brings the DMA and
	// the codec up and costs over 100us on hardware.
	hleEatCycles(25000);
	CoreTiming::UnscheduleEvent(eventAudioUpdate, 0);
	audioMixingInSyscall = true;
	__AudioUpdate();
	audioMixingInSyscall = false;
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
	// Handing over a buffer while nothing was playing is what starts the DMA.
	const bool startsDMA = samplePtr != 0 && !__AudioAnyChannelPlaying();
	// A null pointer is accepted and leaves the channel idle, but still counts as a buffer's
	// worth of remaining samples - which is the one case where the two rest-length calls
	// disagree with each other.
	chan.sampleAddress = samplePtr;
	if (startsDMA) {
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

// Measured on hardware with tests/audio/blocking/overhead. Every SRC output ends up querying
// the codec, which costs upwards of 100us, whether it armed a buffer, found the channel
// unreserved, or did nothing at all. The one shortcut is a channel with both descriptors
// already armed, which lands in the 30-100us range instead.
int __AudioSRCCallCycles(const AudioSRCChannel &chan) {
	return chan.Full() ? 10000 : 25000;
}

u32 __AudioSRCEnqueueBlocking(AudioSRCChannel &chan, u32 samplePtr, int vol) {
	if (!chan.reserved) {
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	// Two DMA descriptors, so two buffers fit. The third caller is refused outright - unlike
	// the mixer channels it does not even get the chance to wait for a slot.
	if (chan.Full()) {
		return SCE_ERROR_AUDIO_CHANNEL_BUSY;
	}

	u32 result = 0;
	if (samplePtr != 0) {
		// The volume rides along with a buffer, so a null pointer leaves it alone. A negative
		// one means the same thing.
		if (vol >= 0) {
			chan.leftVolume = vol;
			chan.rightVolume = vol;
		}
		const bool wasIdle = chan.bufferCount == 0;
		if (wasIdle) {
			// Starting the DMA signals a completion by itself, which is why the first
			// output after an idle stretch returns without blocking.
			chan.completion = true;
			chan.playedSamples = 0;
			chan.frac = 0;
		}
		chan.buffers[chan.bufferCount].address = samplePtr;
		chan.buffers[chan.bufferCount].samples = chan.sampleCount;
		chan.bufferCount++;
		result = chan.sampleCount;
		if (wasIdle) {
			__AudioStartSRCDMA();
		}
	} else if (chan.bufferCount == 0) {
		// Nothing playing and nothing handed over, so there is no completion to wait for.
		return 0;
	}

	if (chan.completion) {
		chan.completion = false;
		return result;
	}
	if (!__KernelIsDispatchEnabled()) {
		return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	}

	chan.waitingThreads.push_back(__KernelGetCurThread());
	__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, PSP_AUDIO_SRC_WAIT_ID, result, 0, false, "blocking audio");
	return result;
}

void __AudioSRCSignal(AudioSRCChannel &chan) {
	chan.completion = true;
}

// One of the two SRC buffers finished playing.
static bool __AudioSRCCompleted(AudioSRCChannel &chan) {
	// Threads that gave up on their own are dropped rather than counted as woken, so a
	// completion is never spent on one - the next real waiter, or the flag, gets it.
	while (!chan.waitingThreads.empty()) {
		const SceUID threadID = chan.waitingThreads.front();
		chan.waitingThreads.erase(chan.waitingThreads.begin());

		u32 error;
		if (__KernelGetWaitID(threadID, WAITTYPE_AUDIOCHANNEL, error) != 0) {
			__KernelResumeThreadFromWait(threadID, __KernelGetWaitValue(threadID, error));
			return true;
		}
	}

	// Nobody is listening, so the completion sits there for the next caller to consume.
	chan.completion = true;
	return false;
}

void __AudioWakeThreads(AudioSRCChannel &chan, int result) {
	bool woke = false;
	for (SceUID threadID : chan.waitingThreads) {
		u32 error;
		if (__KernelGetWaitID(threadID, WAITTYPE_AUDIOCHANNEL, error) != 0) {
			__KernelResumeThreadFromWait(threadID, result);
			woke = true;
		}
	}
	chan.waitingThreads.clear();

	if (woke) {
		// Only ever called from one of the release calls, so always inside a syscall.
		hleReSchedule("audio drain");
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
static bool __AudioMixSRC(AudioSRCChannel &chan) {
	if (chan.bufferCount == 0) {
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
		if (chan.bufferCount == 0) {
			// Underrun. The rest of the block stays silent, like a descriptor the game
			// never got around to arming.
			break;
		}

		const AudioPendingBuffer &buf = chan.buffers[0];
		const u32 addr = buf.address + chan.playedSamples * stride;
		// Interpolating against the following sample matters when a game reserved 22050Hz
		// or similar; at the native rate the fraction is always zero and this reduces to a
		// plain copy.
		const u32 avail = std::min(buf.samples - chan.playedSamples, 2u);
		if (!chan.mute && Memory::IsValidRange(addr, avail * stride)) {
			const s16_le *src = (const s16_le *)Memory::GetPointerUnchecked(addr);
			const int l0 = src[0];
			const int r0 = mono ? l0 : src[1];
			const int l1 = avail > 1 ? (int)src[stride / 2] : l0;
			const int r1 = avail > 1 ? (mono ? l1 : (int)src[stride / 2 + 1]) : r0;
			// 15 bits of fraction, not 16 - a full 16 would overflow the product against a
			// full-scale difference.
			const int frac = (int)(chan.frac >> 1);
			mixBuffer[out * 2] += ApplyChannelVolume(l0 + (((l1 - l0) * frac) >> 15), leftVol);
			mixBuffer[out * 2 + 1] += ApplyChannelVolume(r0 + (((r1 - r0) * frac) >> 15), rightVol);
		}

		chan.frac += ratio;
		u32 step = chan.frac >> 16;
		chan.frac &= 0xFFFF;
		while (step > 0 && chan.bufferCount > 0) {
			const u32 take = std::min(step, chan.buffers[0].samples - chan.playedSamples);
			chan.playedSamples += take;
			step -= take;
			if (chan.playedSamples >= chan.buffers[0].samples) {
				chan.buffers[0] = chan.buffers[1];
				chan.bufferCount--;
				chan.playedSamples = 0;
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
	for (AudioChannel &chan : g_audioChans) {
		// Deliberately not gated on `reserved`: sceAudioChRelease only clears the
		// reservation, and a buffer already in flight keeps playing out.
		woke |= __AudioMixChannel(chan);
	}
	woke |= __AudioMixSRC(g_audioSRC);
	audioMixing = false;

	if (woke) {
		__AudioReScheduleAfterWake();
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
