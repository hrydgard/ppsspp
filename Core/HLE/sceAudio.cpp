// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <memory>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Data/Collections/FixedSizeQueue.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceAudio.h"
#include "Core/HLE/sceUsbMic.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/Reporting.h"

const u32 PSP_AUDIO_SAMPLE_MAX = 65536 - 64;
const int PSP_AUDIO_ERROR_SRC_FORMAT_4 = 0x80000003;
const int AUDIO_ROUTING_SPEAKER_OFF = 0;
const int AUDIO_ROUTING_SPEAKER_ON = 1;
int defaultRoutingMode = AUDIO_ROUTING_SPEAKER_ON;
int defaultRoutingVolMode = AUDIO_ROUTING_SPEAKER_ON;

// Only still here to read savestates written before the channels held buffer pointers.
struct AudioChannelWaitInfo {
	SceUID threadID;
	int numSamples;
};

AudioChannel g_audioChans[PSP_AUDIO_CHANNEL_MAX];
// sceAudioOutput2, sceAudioSRC and sceVaudio are three names for this one channel.
AudioSRCChannel g_audioSRC;

void AudioChannel::DoState(PointerWrap &p) {
	auto s = p.Section("AudioChannel", 1, 4);
	if (!s)
		return;

	Do(p, reserved);
	Do(p, sampleAddress);
	Do(p, sampleCount);
	Do(p, leftVolume);
	Do(p, rightVolume);
	Do(p, format);

	if (s >= 4) {
		Do(p, remainingSamples);
		Do(p, waitingThread);
		Do(p, waitingAddress);
		Do(p, waitingLeftVolume);
		Do(p, waitingRightVolume);
		return;
	}

	// Everything below is the old format, from when the emulator copied each buffer into a
	// ring of samples at enqueue time instead of playing out of the game's memory. There is
	// no way to turn that back into a buffer pointer and a position, so the pending audio is
	// dropped - a fraction of a second of silence on load, and then the game carries on.
	std::vector<AudioChannelWaitInfo> oldWaitingThreads;
	Do(p, oldWaitingThreads);
	if (s >= 2) {
		// These are globals rather than per-channel; the old format just happened to write
		// them out once per channel.
		Do(p, defaultRoutingMode);
		Do(p, defaultRoutingVolMode);
	}

	auto oldQueue = std::make_unique<FixedSizeQueue<s16, 32768 * 8>>();
	if (s >= 3) {
		oldQueue->DoStateCompact(p);
	} else {
		oldQueue->DoState(p);
	}

	if (p.mode == p.MODE_READ) {
		// Only the play position is dropped. reserved, sampleCount, the volumes and the format
		// were all just read out of the state and are still good - clearing those as well
		// leaves every channel unreserved, and the game's next output fails with
		// SCE_ERROR_AUDIO_CHANNEL_NOT_INIT.
		sampleAddress = 0;
		remainingSamples = 0;
		waitingThread = 0;
		waitingAddress = 0;
		waitingLeftVolume = 0;
		waitingRightVolume = 0;
		// The threads that were parked in a blocking output call are still parked, and
		// nothing is going to wake them now, so hand them their buffer back.
		for (const AudioChannelWaitInfo &waitInfo : oldWaitingThreads) {
			u32 error;
			if (__KernelGetWaitID(waitInfo.threadID, WAITTYPE_AUDIOCHANNEL, error) != 0) {
				__KernelResumeThreadFromWait(waitInfo.threadID, sampleCount);
			}
		}
	}
}

void AudioChannel::clear() {
	reserved = false;
	leftVolume = 0;
	rightVolume = 0;
	format = 0;
	sampleCount = 0;
	sampleAddress = 0;
	remainingSamples = 0;
	waitingThread = 0;
	waitingAddress = 0;
	waitingLeftVolume = 0;
	waitingRightVolume = 0;
}

void AudioSRCChannel::DoState(PointerWrap &p) {
	auto s = p.Section("AudioSRCChannel", 1);
	if (!s)
		return;

	Do(p, reserved);
	Do(p, sampleCount);
	Do(p, leftVolume);
	Do(p, rightVolume);
	Do(p, format);
	Do(p, bufferCount);
	for (AudioPendingBuffer &buf : buffers) {
		Do(p, buf.address);
		Do(p, buf.samples);
	}
	Do(p, playedSamples);
	Do(p, frac);
	Do(p, completion);
	Do(p, waitingThreads);
}

void __AudioRoutingDoState(PointerWrap &p) {
	auto s = p.Section("sceAudioRouting", 1);
	if (!s)
		return;

	Do(p, defaultRoutingMode);
	Do(p, defaultRoutingVolMode);
}

void AudioSRCChannel::reset() {
	__AudioWakeThreads(*this, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED);
	clear();
}

void AudioSRCChannel::clear() {
	reserved = false;
	leftVolume = 0;
	rightVolume = 0;
	format = 0;
	sampleCount = 0;
	bufferCount = 0;
	playedSamples = 0;
	frac = 0;
	completion = false;
	waitingThreads.clear();
	for (AudioPendingBuffer &buf : buffers) {
		buf.address = 0;
		buf.samples = 0;
	}
}

// The blocking output calls do not queue callers up. Each channel holds one buffer and at most
// one parked thread; the SRC channel holds two buffers and no parked-thread slot at all. A
// caller that finds no room is told SCE_ERROR_AUDIO_CHANNEL_BUSY and expected to come back
// later. See docs/sceAudio.md.

static u32 sceAudioOutputBlocking(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}

	return hleLogDebug(Log::sceAudio, __AudioEnqueueBlocking(g_audioChans[chan], samplePtr, vol, vol));
}

static u32 sceAudioOutputPannedBlocking(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	// This one ORs the two volumes together before comparing, so unlike the others a negative
	// volume fails instead of meaning "leave it alone".
	if ((u32)(leftvol | rightvol) > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}

	return hleLogDebug(Log::sceAudio, __AudioEnqueueBlocking(g_audioChans[chan], samplePtr, leftvol, rightvol));
}

static u32 sceAudioOutput(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}

	return hleLogDebug(Log::sceAudio, __AudioEnqueue(g_audioChans[chan], samplePtr, vol, vol));
}

static u32 sceAudioOutputPanned(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}

	return hleLogDebug(Log::sceAudio, __AudioEnqueue(g_audioChans[chan], samplePtr, leftvol, rightvol));
}

// A thread parked in a blocking output call counts as a whole extra buffer, on top of whatever
// is left of the one playing.
static int sceAudioGetChannelRestLen(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}
	const AudioChannel &c = g_audioChans[chan];
	int rest = (int)c.remainingSamples;
	if (c.waitingThread != 0) {
		rest += (int)c.sampleCount;
	}
	return hleLogVerbose(Log::sceAudio, rest);
}

static int sceAudioGetChannelRestLength(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}
	// Unlike its sibling this one checks that a buffer is really playing first, so after an
	// output with a null pointer the two disagree.
	const AudioChannel &c = g_audioChans[chan];
	int rest = c.sampleAddress != 0 ? (int)c.remainingSamples : 0;
	if (c.waitingThread != 0) {
		rest += (int)c.sampleCount;
	}
	return hleLogVerbose(Log::sceAudio, rest);
}

static int GetFreeChannel() {
	// The search runs downwards from 7, and a channel only counts as free once it has both
	// been released and finished playing whatever it still held.
	for (int i = PSP_AUDIO_CHANNEL_MAX - 1; i >= 0; --i) {
		if (g_audioChans[i].sampleCount == 0 && g_audioChans[i].sampleAddress == 0)
			return i;
	}
	return -1;
}

static u32 sceAudioChReserve(int chan, u32 sampleCount, u32 format) {
	if (chan < 0) {
		chan = GetFreeChannel();
		if (chan < 0) {
			return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_NO_CHANNELS_AVAILABLE, "no channels remaining");
		}
	}
	if ((u32)chan >= PSP_AUDIO_CHANNEL_MAX)	{
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel %d", chan);
	}
	if (g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "reserve channel failed");
	}
	if ((sampleCount & 63) != 0 || sampleCount == 0 || sampleCount > PSP_AUDIO_SAMPLE_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED, "invalid sample count (not aligned)");
	}
	if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_FORMAT, "invalid format");
	}

	g_audioChans[chan].sampleCount = sampleCount;
	g_audioChans[chan].format = format;
	g_audioChans[chan].reserved = true;
	g_audioChans[chan].leftVolume = 0;
	g_audioChans[chan].rightVolume = 0;
	return hleLogDebug(Log::sceAudio, chan);
}

static u32 sceAudioChRelease(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel %d", chan);
	} else if (!g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel %d not reserved", chan);
	} else if (g_audioChans[chan].waitingThread != 0) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_BUSY, "channel %d has a thread waiting", chan);
	}

	// Only the reservation goes away. A buffer already handed over keeps playing to the end,
	// and the channel stays unavailable to sceAudioChReserve(-1) until it does.
	g_audioChans[chan].reserved = false;
	g_audioChans[chan].sampleCount = 0;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSetChannelDataLen(u32 chan, u32 len) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel %d", chan);
	} else if ((len & 63) != 0 || len == 0 || len > PSP_AUDIO_SAMPLE_MAX) {
		// Checked before the reservation, unlike most of the others.
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED, "invalid sample count");
	} else if (g_audioChans[chan].waitingThread != 0) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_BUSY, "channel %d has a thread waiting", chan);
	} else if (!g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_INIT, "channel %d not reserved", chan);
	}

	g_audioChans[chan].sampleCount = len;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioChangeChannelConfig(u32 chan, u32 format) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "invalid channel number %d", chan);
	} else if (g_audioChans[chan].waitingThread != 0 || g_audioChans[chan].sampleAddress != 0) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_BUSY, "channel %d busy", chan);
	} else if (!g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel %d not reserved", chan);
	} else if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_FORMAT, "invalid format");
	}

	g_audioChans[chan].format = format;
	return hleLogDebug(Log::sceAudio, 0);
}

// Plays one buffer on a channel without reserving it, so the channel frees itself once the
// buffer runs out. The checks are looser than sceAudioChReserve's - any positive sample count
// goes, aligned or not - and stricter on volume, where a negative one is an error rather than
// meaning "leave it alone".
static u32 sceAudioOneshotOutput(int chan, int sampleCount, int format, int leftvol, int rightvol, u32 samplePtr) {
	if ((u32)leftvol > 0xFFFF || (u32)rightvol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	}
	if (chan < 0) {
		chan = GetFreeChannel();
		if (chan < 0) {
			return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_NO_CHANNELS_AVAILABLE, "no channels remaining");
		}
	} else if (chan >= (int)PSP_AUDIO_CHANNEL_MAX || g_audioChans[chan].reserved) {
		// A reserved channel is refused with the same error as one that doesn't exist.
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel %d", chan);
	}
	if (sampleCount <= 0) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED, "invalid sample count");
	}
	if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_FORMAT, "invalid format");
	}

	__AudioEnqueueOneshot(g_audioChans[chan], samplePtr, sampleCount, format, leftvol, rightvol);
	return hleLogDebug(Log::sceAudio, chan);
}

static u32 sceAudioChangeChannelVolume(u32 chan, int leftvol, int rightvol) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid chan %d volume %d %d", chan, leftvol, rightvol);
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "invalid channel %d", chan);
	}

	// There is no reservation check here, and a negative volume means "leave that side alone".
	if (leftvol >= 0) {
		g_audioChans[chan].leftVolume = leftvol;
	}
	if (rightvol >= 0) {
		g_audioChans[chan].rightVolume = rightvol;
	}
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioInit() {
	// Don't need to do anything
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioEnd() {
	// Don't need to do anything
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioOutput2Reserve(u32 sampleCount) {
	auto &chan = g_audioSRC;
	// This seems to ignore the MSB, for some reason.
	sampleCount &= 0x7FFFFFFF;
	if (sampleCount < 17 || sampleCount > 4111) {
		return hleLogError(Log::sceAudio, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid sample count");
	} else if (chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED, "channel already reserved");
	}

	chan.clear();
	chan.sampleCount = sampleCount;
	chan.format = PSP_AUDIO_FORMAT_STEREO;
	chan.reserved = true;
	__AudioSetSRCFrequency(0);
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioOutput2OutputBlocking(u32 vol, u32 dataPtr) {
	// Note: 0xFFFFF, not 0xFFFF!
	if (vol > 0xFFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	}

	hleEatCycles(__AudioSRCCallCycles(g_audioSRC));
	// A busy channel is an ordinary answer here that a game is expected to poll on, not a
	// fault, so this stays at debug like the mixer channels rather than filling the error log.
	return hleLogDebug(Log::sceAudio, __AudioSRCEnqueueBlocking(g_audioSRC, dataPtr, vol));
}

static u32 sceAudioOutput2ChangeLength(u32 sampleCount) {
	// The length is range-checked before the channel is, and 4111 is the same ceiling the
	// reserve takes.
	if (sampleCount - 17 >= 0xFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED, "invalid sample count");
	}
	auto &chan = g_audioSRC;
	if (!chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	}
	// Buffers already handed over keep their original length; only what is reported and what
	// is accepted from here on changes.
	chan.sampleCount = sampleCount;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioOutput2GetRestSample() {
	auto &chan = g_audioSRC;
	if (!chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	}
	// Counts armed DMA descriptors, in units of the current length - so it reports two
	// buffers' worth while both are in flight.
	return hleLogDebug(Log::sceAudio, chan.bufferCount * chan.sampleCount);
}

static u32 sceAudioOutput2Release() {
	auto &chan = g_audioSRC;
	if (!chan.reserved)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	if (chan.bufferCount != 0)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED, "output busy");

	chan.reset();
	__AudioSRCSignal(chan);
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSetFrequency(u32 freq) {
	// TODO: Not available from user code.
	if (freq == 44100 || freq == 48000) {
		INFO_LOG(Log::sceAudio, "sceAudioSetFrequency(%08x)", freq);
		__AudioSetOutputFrequency(freq);
		return 0;
	} else {
		ERROR_LOG(Log::sceAudio, "sceAudioSetFrequency(%08x) - invalid frequency (must be 44.1 or 48 khz)", freq);
		return SCE_ERROR_AUDIO_INVALID_FREQUENCY;
	}
}

static u32 sceAudioSetVolumeOffset() {
	ERROR_LOG(Log::sceAudio, "UNIMPL sceAudioSetVolumeOffset()");
	return 0;
}

bool SRCFrequencyAllowed(int freq) {
	if (freq == 44100 || freq == 22050 || freq == 11025)
		return true;
	if (freq == 48000 || freq == 32000 || freq == 24000 || freq == 16000 || freq == 12000 || freq == 8000)
		return true;
	return false;
}

static u32 sceAudioSRCChReserve(u32 sampleCount, u32 freq, u32 format) {
	auto &chan = g_audioSRC;
	// This seems to ignore the MSB, for some reason.
	sampleCount &= 0x7FFFFFFF;
	if (format == 4) {
		return hleReportError(Log::sceAudio, PSP_AUDIO_ERROR_SRC_FORMAT_4, "unexpected format");
	} else if (format != 2) {
		return hleLogError(Log::sceAudio, SCE_KERNEL_ERROR_INVALID_SIZE, "unexpected format");
	} else if (sampleCount < 17 || sampleCount > 4111) {
		return hleLogError(Log::sceAudio, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid sample count");
	} else if (freq != 0 && !SRCFrequencyAllowed(freq)) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_FREQUENCY, "invalid frequency");
	} else if (chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED, "channel already reserved");
	}

	chan.clear();
	chan.reserved = true;
	chan.sampleCount = sampleCount;
	chan.format = format == 2 ? PSP_AUDIO_FORMAT_STEREO : PSP_AUDIO_FORMAT_MONO;
	// Zero means default to 44.1kHz.
	__AudioSetSRCFrequency(freq);
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSRCChRelease() {
	auto &chan = g_audioSRC;
	if (!chan.reserved)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	if (chan.bufferCount != 0)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED, "output busy");

	chan.reset();
	// Releasing signals a completion, which the next caller after a fresh reserve consumes.
	__AudioSRCSignal(chan);
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSRCOutputBlocking(u32 vol, u32 buf) {
	if (vol > 0xFFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	}

	hleEatCycles(__AudioSRCCallCycles(g_audioSRC));
	return hleLogDebug(Log::sceAudio, __AudioSRCEnqueueBlocking(g_audioSRC, buf, vol));
}

static int sceAudioInputBlocking(u32 maxSamples, u32 sampleRate, u32 bufAddr) {
	if (!Memory::IsValidAddress(bufAddr)) {
		return hleLogError(Log::HLE, -1, "invalid address");
	}
	return hleLogInfo(Log::HLE, __MicInput(maxSamples, sampleRate, bufAddr, AUDIOINPUT));
}

static int sceAudioInput(u32 maxSamples, u32 sampleRate, u32 bufAddr) {
	if (!Memory::IsValidAddress(bufAddr)) {
		return hleLogError(Log::HLE, -1, "invalid address");
	}

	ERROR_LOG(Log::HLE, "UNTEST sceAudioInput: maxSamples: %d, samplerate: %d, bufAddr: %08x", maxSamples, sampleRate, bufAddr);
	return __MicInput(maxSamples, sampleRate, bufAddr, AUDIOINPUT, false);
}

static int sceAudioInputInit(int unknown1, int gain, int unknown2) {
	ERROR_LOG(Log::HLE, "UNIMPL sceAudioInputInit: unknown1: %d, gain: %d, unknown2: %d", unknown1, gain, unknown2);
	return 0;
}

static int sceAudioInputInitEx(u32 paramAddr) {
	ERROR_LOG(Log::HLE, "UNIMPL sceAudioInputInitEx: paramAddr: %08x", paramAddr);
	return 0;
}

static int sceAudioPollInputEnd() {
	ERROR_LOG(Log::HLE, "UNIMPL sceAudioPollInputEnd");
	return 0;
}

static int sceAudioWaitInputEnd() {
	ERROR_LOG(Log::HLE, "UNIMPL sceAudioWaitInputEnd");
	return 0;
}

static int sceAudioGetInputLength() {
	int ret = Microphone::getReadMicDataLength() / 2;
	ERROR_LOG(Log::HLE, "UNTEST sceAudioGetInputLength(ret: %d)", ret);
	return ret;
}

static u32 sceAudioRoutingSetMode(u32 mode) {
	ERROR_LOG_REPORT(Log::sceAudio, "sceAudioRoutingSetMode(%08x)", mode);
	int previousMode = defaultRoutingMode;
	defaultRoutingMode = mode;
	return previousMode;
}

static u32 sceAudioRoutingGetMode() {
	ERROR_LOG_REPORT(Log::sceAudio, "sceAudioRoutingGetMode()");
	return defaultRoutingMode;
}

static u32 sceAudioRoutingSetVolumeMode(u32 mode) {
	ERROR_LOG_REPORT(Log::sceAudio, "sceAudioRoutingSetVolumeMode(%08x)", mode);
	int previousMode = defaultRoutingVolMode;
	defaultRoutingVolMode = mode;
	return previousMode;
}

static u32 sceAudioRoutingGetVolumeMode() {
	ERROR_LOG_REPORT(Log::sceAudio, "sceAudioRoutingGetVolumeMode()");
	return defaultRoutingVolMode;
}

const HLEFunction sceAudio[] =
{
	// Newer simplified single channel audio output. Presumably for games that use Atrac3
	// directly from Sas instead of playing it on a separate audio channel.
	{0X01562BA3, &WrapU_U<sceAudioOutput2Reserve>,          "sceAudioOutput2Reserve",        'x', "i"   },
	{0X2D53F36E, &WrapU_UU<sceAudioOutput2OutputBlocking>,  "sceAudioOutput2OutputBlocking", 'x', "xx"  },
	{0X63F2889C, &WrapU_U<sceAudioOutput2ChangeLength>,     "sceAudioOutput2ChangeLength",   'x', "i"   },
	{0X647CEF33, &WrapU_V<sceAudioOutput2GetRestSample>,    "sceAudioOutput2GetRestSample",  'i', ""    },
	{0X43196845, &WrapU_V<sceAudioOutput2Release>,          "sceAudioOutput2Release",        'x', ""    },

	// "Traditional" audio channel interface
	{0X80F1F7E0, &WrapU_V<sceAudioInit>,                    "sceAudioInit",                  'x', ""    },
	{0X210567F7, &WrapU_V<sceAudioEnd>,                     "sceAudioEnd",                   'x', ""    },
	{0XA2BEAA6C, &WrapU_U<sceAudioSetFrequency>,            "sceAudioSetFrequency",          'x', "i"   },
	{0X927AC32B, &WrapU_V<sceAudioSetVolumeOffset>,         "sceAudioSetVolumeOffset",       'x', ""    },
	{0X8C1009B2, &WrapU_UIU<sceAudioOutput>,                "sceAudioOutput",                'x', "ixx" },
	{0X136CAF51, &WrapU_UIU<sceAudioOutputBlocking>,        "sceAudioOutputBlocking",        'x', "ixx" },
	{0XE2D56B2D, &WrapU_UIIU<sceAudioOutputPanned>,         "sceAudioOutputPanned",          'x', "ixxx"},
	{0X13F592BC, &WrapU_UIIU<sceAudioOutputPannedBlocking>, "sceAudioOutputPannedBlocking",  'x', "ixxx"},
	{0X5EC81C55, &WrapU_IUU<sceAudioChReserve>,             "sceAudioChReserve",             'x', "iii" },
	{0X6FC46853, &WrapU_U<sceAudioChRelease>,               "sceAudioChRelease",             'x', "i"   },
	{0XE9D97901, &WrapI_U<sceAudioGetChannelRestLen>,       "sceAudioGetChannelRestLen",     'i', "i"   },
	{0XB011922F, &WrapI_U<sceAudioGetChannelRestLength>,    "sceAudioGetChannelRestLength",  'i', "i"   },
	{0XCB2E439E, &WrapU_UU<sceAudioSetChannelDataLen>,      "sceAudioSetChannelDataLen",     'x', "ii"  },
	{0X95FD0C2D, &WrapU_UU<sceAudioChangeChannelConfig>,    "sceAudioChangeChannelConfig",   'x', "ii"  },
	{0XB7E1D8E7, &WrapU_UII<sceAudioChangeChannelVolume>,   "sceAudioChangeChannelVolume",   'x', "ixx" },

	// Like Output2, but with ability to do sample rate conversion.
	{0X38553111, &WrapU_UUU<sceAudioSRCChReserve>,          "sceAudioSRCChReserve",          'x', "iii" },
	{0X5C37C0AE, &WrapU_V<sceAudioSRCChRelease>,            "sceAudioSRCChRelease",          'x', ""    },
	{0XE0727056, &WrapU_UU<sceAudioSRCOutputBlocking>,      "sceAudioSRCOutputBlocking",     'x', "xx"  },

	{0X41EFADE7, &WrapU_IIIIIU<sceAudioOneshotOutput>,      "sceAudioOneshotOutput",         'x', "iiiiix"},

	// Never seen this used
	{0XB61595C0, nullptr,                                   "sceAudioLoopbackTest",          '?', ""    },

	// Microphone interface
	{0X7DE61688, &WrapI_III<sceAudioInputInit>,             "sceAudioInputInit",             'i', "iii" },
	{0XE926D3FB, &WrapI_U<sceAudioInputInitEx>,             "sceAudioInputInitEx",           'i', "x"   },
	{0X6D4BEC68, &WrapI_UUU<sceAudioInput>,                 "sceAudioInput",                 'i', "xxx" },
	{0X086E5895, &WrapI_UUU<sceAudioInputBlocking>,         "sceAudioInputBlocking",         'i', "xxx" },
	{0XA708C6A6, &WrapI_V<sceAudioGetInputLength>,          "sceAudioGetInputLength",        'i', ""    },
	{0XA633048E, &WrapI_V<sceAudioPollInputEnd>,            "sceAudioPollInputEnd",          'i', ""    },
	{0X87B2E651, &WrapI_V<sceAudioWaitInputEnd>,            "sceAudioWaitInputEnd",          'i', ""    },

	{0X36FD8AA9, &WrapU_U<sceAudioRoutingSetMode>,          "sceAudioRoutingSetMode",        'x', "x"   },
	{0X39240E7D, &WrapU_V<sceAudioRoutingGetMode>,          "sceAudioRoutingGetMode",        'x', ""    },
	{0XBB548475, &WrapU_U<sceAudioRoutingSetVolumeMode>,    "sceAudioRoutingSetVolumeMode",  'x', "x"   },
	{0X28235C56, &WrapU_V<sceAudioRoutingGetVolumeMode>,    "sceAudioRoutingGetVolumeMode",  'x', ""    },

};

void Register_sceAudio()
{
	RegisterHLEModule("sceAudio", ARRAY_SIZE(sceAudio), sceAudio);
}
