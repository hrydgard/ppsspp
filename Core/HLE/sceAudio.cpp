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

// The extra channel is for SRC/Output2/Vaudio.
AudioChannel g_audioChans[PSP_AUDIO_CHANNEL_MAX + 1];

void AudioChannel::DoState(PointerWrap &p)
{
	struct LegacyAudioChannelWaitInfo {
		SceUID threadID;
		int unusedNumsamples;
	};

	auto s = p.Section("AudioChannel", 1, 3);
	if (!s)
		return;
	Do(p, reserved);
	if (s >= 3) {
		Do(p, sampleCount);
		Do(p, queueLength);
		Do(p, queue);
		Do(p, queuePlayOffset);
		Do(p, format);
		Do(p, leftVolume);
		Do(p, rightVolume);
		Do(p, defaultRoutingMode);
		Do(p, defaultRoutingVolMode);
		Do(p, waitingThreads);
	} else {
		Do(p, sampleAddressUnused);
		Do(p, sampleCount);
		Do(p, leftVolume);
		Do(p, rightVolume);
		Do(p, format);

		std::vector<LegacyAudioChannelWaitInfo> waitingThreadsLegacy;
		Do(p, waitingThreadsLegacy);
		if (p.GetMode() == PointerWrap::MODE_READ) {
			waitingThreads.clear();
			// Convert old wait info to new.
			for (const auto &waitInfo : waitingThreadsLegacy) {
				waitingThreads.push_back(waitInfo.threadID);
			}
		}
		if (s >= 2) {
			Do(p, defaultRoutingMode);
			Do(p, defaultRoutingVolMode);
		}
		FixedSizeQueue<s16, 32768 * 8> legacySampleQueues[PSP_AUDIO_CHANNEL_MAX + 1];
		legacySampleQueues[index].DoState(p);
	}
}

void AudioChannel::reset()
{
	__AudioWakeThreads(*this, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED);
	clear();
}

void AudioChannel::clear() {
	reserved = false;
	leftVolume = 0;
	rightVolume = 0;
	format = 0;
	sampleAddressUnused = 0;
	sampleCount = 0;
	// chanSampleQueues[index].clear();
	queueLength = 0;
	queuePlayOffset = 0;

	waitingThreads.clear();
}

// Enqueues the buffer pointed to on the channel. If channel buffer queue is full (2 items?) will block until it isn't.
// For solid audio output we'll need a queue length of 2 buffers at least.

// Not sure about the range of volume, I often see 0x800 so that might be either
// max or 50%?
static u32 sceAudioOutputBlocking(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	} else if (!g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_INIT, "channel not reserved");
	}

	return hleLogDebug(Log::sceAudio, __AudioEnqueue(g_audioChans[chan], chan, vol, vol, samplePtr, true));
}

static u32 sceAudioOutputPannedBlocking(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	// For some reason, this is the only one that checks for negative.
	if (leftvol > 0xFFFF || rightvol > 0xFFFF || leftvol < 0 || rightvol < 0) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	} else if (!g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_INIT, "channel not reserved");
	}

	u32 result = __AudioEnqueue(g_audioChans[chan], chan, leftvol, rightvol, samplePtr, true);
	return hleLogDebug(Log::sceAudio, result);
}

static u32 sceAudioOutput(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	} else if (!g_audioChans[chan].reserved)	{
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_INIT, "channel not reserved");
	}

	u32 result = __AudioEnqueue(g_audioChans[chan], chan, vol, vol, samplePtr, false);
	return hleLogDebug(Log::sceAudio, result);
}

static u32 sceAudioOutputPanned(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	} else if (!g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_INIT, "channel not reserved");
	} else {
		u32 result = __AudioEnqueue(g_audioChans[chan], chan, leftvol, rightvol, samplePtr, false);
		return hleLogDebug(Log::sceAudio, result);
	}
}

static int sceAudioGetChannelRestLen(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}

	int remainingSamples = (int)g_audioChans[chan].queueLength * g_audioChans[chan].sampleCount;
	return hleLogVerbose(Log::sceAudio, remainingSamples);
}

static int sceAudioGetChannelRestLength(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel");
	}

	int remainingSamples = (int)g_audioChans[chan].queueLength * g_audioChans[chan].sampleCount;
	return hleLogVerbose(Log::sceAudio, remainingSamples);
}

static u32 GetFreeChannel() {
	for (u32 i = PSP_AUDIO_CHANNEL_MAX - 1; i > 0; --i) {
		if (!g_audioChans[i].reserved)
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
	if ((sampleCount & 63) != 0 || sampleCount == 0 || sampleCount > PSP_AUDIO_SAMPLE_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED, "invalid sample count (not aligned)");
	}
	if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_FORMAT, "invalid format");
	}
	if (g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "reserve channel failed");
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
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_INIT, "channel %d not reserved", chan);
	}

	// TODO: Does this error if busy?
	g_audioChans[chan].reset();
	g_audioChans[chan].reserved = false;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSetChannelDataLen(u32 chan, u32 len) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "bad channel %d", chan);
	} else if (!g_audioChans[chan].reserved)	{
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_INIT, "channel %d not reserved", chan);
	} else if ((len & 63) != 0 || len == 0 || len > PSP_AUDIO_SAMPLE_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED, "invalid sample count");
	}
	
	g_audioChans[chan].sampleCount = len;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioChangeChannelConfig(u32 chan, u32 format) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "invalid channel number %d", chan);
	} else if (!g_audioChans[chan].reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel %d not reserved", chan);
	}

	g_audioChans[chan].format = format;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioChangeChannelVolume(u32 chan, u32 leftvol, u32 rightvol) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid chan %d volume %d %d", chan, leftvol, rightvol);
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_CHANNEL, "invalid channel %d", chan);
	} else if (!g_audioChans[chan].reserved)	{
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel %d not reserved", chan);
	}

	g_audioChans[chan].leftVolume = leftvol;
	g_audioChans[chan].rightVolume = rightvol;
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
	auto &chan = g_audioChans[PSP_AUDIO_CHANNEL_OUTPUT2];
	// This seems to ignore the MSB, for some reason.
	sampleCount &= 0x7FFFFFFF;
	if (sampleCount < 17 || sampleCount > 4111) {
		return hleLogError(Log::sceAudio, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid sample count");
	} else if (chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED, "channel already reserved");
	}

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

	auto &chan = g_audioChans[PSP_AUDIO_CHANNEL_OUTPUT2];
	if (!chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	}

	hleEatCycles(10000);
	int result = __AudioEnqueue(chan, PSP_AUDIO_CHANNEL_OUTPUT2, vol, vol, dataPtr, true);
	if (result < 0)
		return hleLogError(Log::sceAudio, result);
	return hleLogDebug(Log::sceAudio, result);
}

static u32 sceAudioOutput2ChangeLength(u32 sampleCount) {
	AudioChannel &chan = g_audioChans[PSP_AUDIO_CHANNEL_OUTPUT2];
	if (!chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	}
	chan.sampleCount = sampleCount;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioOutput2GetRestSample() {
	const AudioChannel &chan = g_audioChans[PSP_AUDIO_CHANNEL_OUTPUT2];
	if (!chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	}
	int size = (int)chan.queueLength * chan.sampleCount;
	if (size > (int)chan.sampleCount) {
		// If ChangeLength reduces the size, it still gets output but this return is clamped.
		size = (int)chan.sampleCount;
	}
	return hleLogDebug(Log::sceAudio, size);
}

static u32 sceAudioOutput2Release() {
	AudioChannel &chan = g_audioChans[PSP_AUDIO_CHANNEL_OUTPUT2];
	if (!chan.reserved)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	if (chan.queueLength > 0)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED, "output busy");

	chan.reset();
	chan.reserved = false;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSetFrequency(u32 freq) {
	// TODO: Not available from user code. Actually, all games using 44.1kHz, so this is probably not needed.
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

static bool SRCFrequencyAllowed(int freq) {
	if (freq == 44100 || freq == 22050 || freq == 11025)
		return true;
	if (freq == 48000 || freq == 32000 || freq == 24000 || freq == 16000 || freq == 12000 || freq == 8000)
		return true;
	return false;
}

static u32 sceAudioSRCChReserve(u32 sampleCount, u32 freq, u32 format) {
	auto &chan = g_audioChans[PSP_AUDIO_CHANNEL_SRC];
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

	chan.reserved = true;
	chan.sampleCount = sampleCount;
	chan.format = format == 2 ? PSP_AUDIO_FORMAT_STEREO : PSP_AUDIO_FORMAT_MONO;
	// Zero means default to 44.1kHz.
	__AudioSetSRCFrequency(freq);
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSRCChRelease() {
	auto &chan = g_audioChans[PSP_AUDIO_CHANNEL_SRC];
	if (!chan.reserved)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	if (chan.queueLength > 0)
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED, "output busy");

	chan.reset();
	chan.reserved = false;
	return hleLogDebug(Log::sceAudio, 0);
}

static u32 sceAudioSRCOutputBlocking(u32 vol, u32 buf) {
	if (vol > 0xFFFFF) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_INVALID_VOLUME, "invalid volume");
	}

	auto &chan = g_audioChans[PSP_AUDIO_CHANNEL_SRC];
	if (!chan.reserved) {
		return hleLogError(Log::sceAudio, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED, "channel not reserved");
	}

	chan.leftVolume = vol;
	chan.rightVolume = vol;
	chan.sampleAddressUnused = buf;

	hleEatCycles(10000);
	int result = __AudioEnqueue(chan, PSP_AUDIO_CHANNEL_SRC, vol, vol, buf, true);
	if (result < 0)
		return hleLogError(Log::sceAudio, result);
	return hleLogDebug(Log::sceAudio, result);
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
	{0XB7E1D8E7, &WrapU_UUU<sceAudioChangeChannelVolume>,   "sceAudioChangeChannelVolume",   'x', "ixx" },

	// Like Output2, but with ability to do sample rate conversion.
	{0X38553111, &WrapU_UUU<sceAudioSRCChReserve>,          "sceAudioSRCChReserve",          'x', "iii" },
	{0X5C37C0AE, &WrapU_V<sceAudioSRCChRelease>,            "sceAudioSRCChRelease",          'x', ""    },
	{0XE0727056, &WrapU_UU<sceAudioSRCOutputBlocking>,      "sceAudioSRCOutputBlocking",     'x', "xx"  },

	// Never seen these used
	{0X41EFADE7, nullptr,                                   "sceAudioOneshotOutput",         '?', ""    },
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
