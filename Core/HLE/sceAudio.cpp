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

#include "Common/ChunkFile.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Host.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceAudio.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/Reporting.h"

const u32 PSP_AUDIO_SAMPLE_MAX = 65536 - 64;
const int PSP_AUDIO_ERROR_SRC_FORMAT_4 = 0x80000003;
const int AUDIO_ROUTING_SPEAKER_OFF = 0;
const int AUDIO_ROUTING_SPEAKER_ON = 1;
int defaultRoutingMode = AUDIO_ROUTING_SPEAKER_ON;
int defaultRoutingVolMode = AUDIO_ROUTING_SPEAKER_ON;

void AudioChannel::DoState(PointerWrap &p)
{
	auto s = p.Section("AudioChannel", 1, 2);
	if (!s)
		return;

	p.Do(reserved);
	p.Do(sampleAddress);
	p.Do(sampleCount);
	p.Do(leftVolume);
	p.Do(rightVolume);
	p.Do(format);
	p.Do(waitingThreads);
	if (s >= 2) {
		p.Do(defaultRoutingMode);
		p.Do(defaultRoutingVolMode);
	}
	sampleQueue.DoState(p);
}

void AudioChannel::reset()
{
	__AudioWakeThreads(*this, SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED);
	clear();
}

void AudioChannel::clear()
{
	reserved = false;
	leftVolume = 0;
	rightVolume = 0;
	format = 0;
	sampleAddress = 0;
	sampleCount = 0;
	sampleQueue.clear();
	waitingThreads.clear();
}

// There's a second Audio api called Audio2 that only has one channel, I guess the 8 channel api was overkill.
// We simply map it to an extra channel after the 8 channels, since they can be used concurrently.

// The extra channel is for SRC/Output2/Vaudio.
AudioChannel chans[PSP_AUDIO_CHANNEL_MAX + 1];

// Enqueues the buffer pointer on the channel. If channel buffer queue is full (2 items?) will block until it isn't.
// For solid audio output we'll need a queue length of 2 buffers at least, we'll try that first.

// Not sure about the range of volume, I often see 0x800 so that might be either
// max or 50%?

u32 sceAudioOutputBlocking(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputBlocking() - invalid volume");
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputBlocking() - bad channel");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputBlocking() - channel not reserved");
		return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		DEBUG_LOG(SCEAUDIO, "sceAudioOutputBlocking(%08x, %08x, %08x)", chan, vol, samplePtr);
		if (vol >= 0) {
			chans[chan].leftVolume = vol;
			chans[chan].rightVolume = vol;
		}
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, true);
	}
}

u32 sceAudioOutputPannedBlocking(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	int result = 0;
	// For some reason, this is the only one that checks for negative.
	if (leftvol > 0xFFFF || rightvol > 0xFFFF || leftvol < 0 || rightvol < 0) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputPannedBlocking() - invalid volume");
		result = SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputPannedBlocking() - bad channel");
		result = SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputPannedBlocking() - channel not reserved");
		result = SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		if (leftvol >= 0) {
			chans[chan].leftVolume = leftvol;
		}
		if (rightvol >= 0) {
			chans[chan].rightVolume = rightvol;
		}
		chans[chan].sampleAddress = samplePtr;
		result = __AudioEnqueue(chans[chan], chan, true);
	}

	DEBUG_LOG(SCEAUDIO, "%08x = sceAudioOutputPannedBlocking(%08x, %08x, %08x, %08x)", result, chan, leftvol, rightvol, samplePtr);
	return result;

}

u32 sceAudioOutput(u32 chan, int vol, u32 samplePtr) {
	if (vol > 0xFFFF) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutput() - invalid volume");
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutput() - bad channel");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved)	{
		ERROR_LOG(SCEAUDIO, "sceAudioOutput(%08x, %08x, %08x) - channel not reserved", chan, vol, samplePtr);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		DEBUG_LOG(SCEAUDIO, "sceAudioOutputPanned(%08x, %08x, %08x)", chan, vol, samplePtr);
		if (vol >= 0) {
			chans[chan].leftVolume = vol;
			chans[chan].rightVolume = vol;
		}
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, false);
	}
}

u32 sceAudioOutputPanned(u32 chan, int leftvol, int rightvol, u32 samplePtr) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputPanned() - invalid volume");
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputPanned() - bad channel");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutputPanned(%08x, %08x, %08x, %08x) - channel not reserved", chan, leftvol, rightvol, samplePtr);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else {
		DEBUG_LOG(SCEAUDIO, "sceAudioOutputPanned(%08x, %08x, %08x, %08x)", chan, leftvol, rightvol, samplePtr);
		if (leftvol >= 0) {
			chans[chan].leftVolume = leftvol;
		}
		if (rightvol >= 0) {
			chans[chan].rightVolume = rightvol;
		}
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, false);
	}
}

int sceAudioGetChannelRestLen(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioGetChannelRestLen(%08x) - bad channel", chan);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	int remainingSamples = (int)chans[chan].sampleQueue.size() / 2;
	DEBUG_LOG(SCEAUDIO, "%d=sceAudioGetChannelRestLen(%08x)", remainingSamples, chan);
	return remainingSamples;
}

int sceAudioGetChannelRestLength(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioGetChannelRestLength(%08x) - bad channel", chan);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	int remainingSamples = (int)chans[chan].sampleQueue.size() / 2;
	DEBUG_LOG(SCEAUDIO, "%d=sceAudioGetChannelRestLength(%08x)", remainingSamples, chan);
	return remainingSamples;
}

static u32 GetFreeChannel() {
	for (u32 i = PSP_AUDIO_CHANNEL_MAX - 1; i > 0; --i) {
		if (!chans[i].reserved)
			return i;
	}
	return -1;
}

u32 sceAudioChReserve(int chan, u32 sampleCount, u32 format) {
	if (chan < 0) {
		chan = GetFreeChannel();
		if (chan < 0) {
			ERROR_LOG(SCEAUDIO, "sceAudioChReserve - no channels remaining");
			return SCE_ERROR_AUDIO_NO_CHANNELS_AVAILABLE;
		}
	} 
	if ((u32)chan >= PSP_AUDIO_CHANNEL_MAX)	{
		ERROR_LOG(SCEAUDIO, "sceAudioChReserve(%08x, %08x, %08x) - bad channel", chan, sampleCount, format);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	if ((sampleCount & 63) != 0 || sampleCount == 0 || sampleCount > PSP_AUDIO_SAMPLE_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioChReserve(%08x, %08x, %08x) - invalid sample count", chan, sampleCount, format);
		return SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED;
	}
	if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO) {
		ERROR_LOG(SCEAUDIO, "sceAudioChReserve(%08x, %08x, %08x) - invalid format", chan, sampleCount, format);
		return SCE_ERROR_AUDIO_INVALID_FORMAT;
	}
	if (chans[chan].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioChReserve - reserve channel failed");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	DEBUG_LOG(SCEAUDIO, "sceAudioChReserve(%08x, %08x, %08x)", chan, sampleCount, format);
	chans[chan].sampleCount = sampleCount;
	chans[chan].format = format;
	chans[chan].reserved = true;
	chans[chan].leftVolume = 0;
	chans[chan].rightVolume = 0;
	return chan;
}

u32 sceAudioChRelease(u32 chan) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioChRelease(%i) - bad channel", chan);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	if (!chans[chan].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioChRelease(%i) - channel not reserved", chan);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	DEBUG_LOG(SCEAUDIO, "sceAudioChRelease(%i)", chan);
	chans[chan].reset();
	chans[chan].reserved = false;
	return 1;
}

u32 sceAudioSetChannelDataLen(u32 chan, u32 len) {
	int result = 0;
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioSetChannelDataLen(%08x, %08x) - bad channel", chan, len);
		result = SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved)	{
		ERROR_LOG(SCEAUDIO, "sceAudioSetChannelDataLen(%08x, %08x) - channel not reserved", chan, len);
		result = SCE_ERROR_AUDIO_CHANNEL_NOT_INIT;
	} else if ((len & 63) != 0 || len == 0 || len > PSP_AUDIO_SAMPLE_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioSetChannelDataLen(%08x, %08x) - invalid sample count", chan, len);
		result = SCE_ERROR_AUDIO_OUTPUT_SAMPLE_DATA_SIZE_NOT_ALIGNED;
	} else {
		chans[chan].sampleCount = len;
	}
	DEBUG_LOG(SCEAUDIO, "%08x = sceAudioSetChannelDataLen(%08x, %08x)", result , chan, len);
	return result;
}

u32 sceAudioChangeChannelConfig(u32 chan, u32 format) {
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioChangeChannelConfig(%08x, %08x) - invalid channel number", chan, format);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioChangeChannelConfig(%08x, %08x) - channel not reserved", chan, format);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		DEBUG_LOG(SCEAUDIO, "sceAudioChangeChannelConfig(%08x, %08x)", chan, format);
		chans[chan].format = format;
		return 0;
	}
}

u32 sceAudioChangeChannelVolume(u32 chan, u32 leftvol, u32 rightvol) {
	if (leftvol > 0xFFFF || rightvol > 0xFFFF) {
		ERROR_LOG(SCEAUDIO, "sceAudioChangeChannelVolume(%08x, %08x, %08x) - invalid volume", chan, leftvol, rightvol);
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(SCEAUDIO, "sceAudioChangeChannelVolume(%08x, %08x, %08x) - invalid channel number", chan, leftvol, rightvol);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved)	{
		ERROR_LOG(SCEAUDIO, "sceAudioChangeChannelVolume(%08x, %08x, %08x) - channel not reserved", chan, leftvol, rightvol);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		DEBUG_LOG(SCEAUDIO, "sceAudioChangeChannelVolume(%08x, %08x, %08x)", chan, leftvol, rightvol);
		chans[chan].leftVolume = leftvol;
		chans[chan].rightVolume = rightvol;
		return 0;
	}
}

u32 sceAudioInit(){
	DEBUG_LOG(SCEAUDIO, "sceAudioInit()");
	// Don't need to do anything
	return 0;
}

u32 sceAudioEnd(){
	DEBUG_LOG(SCEAUDIO, "sceAudioEnd()");
	// Don't need to do anything
	return 0;
}

u32 sceAudioOutput2Reserve(u32 sampleCount) {
	if (sampleCount < 17 || sampleCount > 4111) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutput2Reserve(%08x) - invalid sample count", sampleCount);
		return SCE_KERNEL_ERROR_INVALID_SIZE;
	} else if (chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutput2Reserve(%08x) - channel already reserved", sampleCount);
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	} else {
		DEBUG_LOG(SCEAUDIO, "sceAudioOutput2Reserve(%08x)", sampleCount);
		chans[PSP_AUDIO_CHANNEL_OUTPUT2].sampleCount = sampleCount;
		chans[PSP_AUDIO_CHANNEL_OUTPUT2].format = PSP_AUDIO_FORMAT_STEREO;
		chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved = true;
	}
	return 0;
}

u32 sceAudioOutput2OutputBlocking(u32 vol, u32 dataPtr){
	// Note: 0xFFFFF, not 0xFFFF!
	if (vol > 0xFFFFF) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutput2OutputBlocking(%08x, %08x) - invalid volume", vol, dataPtr);
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	}
	DEBUG_LOG(SCEAUDIO, "sceAudioOutput2OutputBlocking(%08x, %08x)", vol, dataPtr);
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].leftVolume = vol;
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].rightVolume = vol;
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].sampleAddress = dataPtr;
	return __AudioEnqueue(chans[PSP_AUDIO_CHANNEL_OUTPUT2], PSP_AUDIO_CHANNEL_OUTPUT2, true);
}

u32 sceAudioOutput2ChangeLength(u32 sampleCount){
	if (!chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutput2ChangeLength(%08x) - channel not reserved ", sampleCount);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	DEBUG_LOG(SCEAUDIO, "sceAudioOutput2ChangeLength(%08x)", sampleCount);
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].sampleCount = sampleCount;
	return 0;
}

u32 sceAudioOutput2GetRestSample(){
	if (!chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioOutput2GetRestSample() - channel not reserved ");
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	DEBUG_LOG(SCEAUDIO, "sceAudioOutput2GetRestSample()");
	return (u32) chans[PSP_AUDIO_CHANNEL_OUTPUT2].sampleQueue.size() / 2;
}

u32 sceAudioOutput2Release(){
	DEBUG_LOG(SCEAUDIO, "sceAudioOutput2Release()");
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].reset();
	chans[PSP_AUDIO_CHANNEL_OUTPUT2].reserved = false;
	return 0;
}

u32 sceAudioSetFrequency(u32 freq) {
	if (freq == 44100 || freq == 48000) {
		INFO_LOG(SCEAUDIO, "sceAudioSetFrequency(%08x)", freq);
		__AudioSetOutputFrequency(freq);
		return 0;
	} else {
		ERROR_LOG(SCEAUDIO, "sceAudioSetFrequency(%08x) - invalid frequency (must be 44.1 or 48 khz)", freq);
		return SCE_ERROR_AUDIO_INVALID_FREQUENCY;
	}
}

u32 sceAudioSetVolumeOffset() {
	ERROR_LOG(SCEAUDIO, "UNIMPL sceAudioSetVolumeOffset()");
	return 0;
}

u32 sceAudioSRCChReserve(u32 sampleCount, u32 freq, u32 format) {
	if (format == 4) {
		ERROR_LOG(SCEAUDIO, "sceAudioSRCChReserve(%08x, %08x, %08x) - unexpected format", sampleCount, freq, format);
		return PSP_AUDIO_ERROR_SRC_FORMAT_4;
	} else if (format != 2) {
		ERROR_LOG(SCEAUDIO, "sceAudioSRCChReserve(%08x, %08x, %08x) - unexpected format", sampleCount, freq, format);
		return SCE_KERNEL_ERROR_INVALID_SIZE;
	} else if (sampleCount < 17 || sampleCount > 4111) {
		ERROR_LOG(SCEAUDIO, "sceAudioSRCChReserve(%08x, %08x, %08x) - invalid sample count", sampleCount, freq, format);
		return SCE_KERNEL_ERROR_INVALID_SIZE;
	} else if (chans[PSP_AUDIO_CHANNEL_SRC].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioSRCChReserve(%08x, %08x, %08x) - channel already reserved ", sampleCount, freq, format);
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	} else {
		DEBUG_LOG(SCEAUDIO, "sceAudioSRCChReserve(%08x, %08x, %08x)", sampleCount, freq, format);
		chans[PSP_AUDIO_CHANNEL_SRC].reserved = true;
		chans[PSP_AUDIO_CHANNEL_SRC].sampleCount = sampleCount;
		chans[PSP_AUDIO_CHANNEL_SRC].format = format == 2 ? PSP_AUDIO_FORMAT_STEREO : PSP_AUDIO_FORMAT_MONO;
		__AudioSetOutputFrequency(freq);
	}
	return 0;
}

u32 sceAudioSRCChRelease() {
	if (!chans[PSP_AUDIO_CHANNEL_SRC].reserved) {
		ERROR_LOG(SCEAUDIO, "sceAudioSRCChRelease() - channel not reserved ");
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	DEBUG_LOG(SCEAUDIO, "sceAudioSRCChRelease()");
	chans[PSP_AUDIO_CHANNEL_SRC].reset();
	chans[PSP_AUDIO_CHANNEL_SRC].reserved = false;
	return 0;
}

u32 sceAudioSRCOutputBlocking(u32 vol, u32 buf) {
	if (vol > 0xFFFFF) {
		ERROR_LOG(SCEAUDIO, "sceAudioSRCOutputBlocking(%08x, %08x) - invalid volume", vol, buf);
		return SCE_ERROR_AUDIO_INVALID_VOLUME;
	}
	DEBUG_LOG(SCEAUDIO, "sceAudioSRCOutputBlocking(%08x, %08x)", vol, buf);
	chans[PSP_AUDIO_CHANNEL_SRC].leftVolume = vol;
	chans[PSP_AUDIO_CHANNEL_SRC].rightVolume = vol;
	chans[PSP_AUDIO_CHANNEL_SRC].sampleAddress = buf;
	return __AudioEnqueue(chans[PSP_AUDIO_CHANNEL_SRC], PSP_AUDIO_CHANNEL_SRC, true);
}

u32 sceAudioRoutingSetMode(u32 mode) {
	ERROR_LOG_REPORT(SCEAUDIO, "sceAudioRoutingSetMode(%08x)", mode);
	int previousMode = defaultRoutingMode;
	defaultRoutingMode = mode;
	return previousMode;
}

u32 sceAudioRoutingGetMode() {
	ERROR_LOG_REPORT(SCEAUDIO, "sceAudioRoutingGetMode()");
	return defaultRoutingMode;
}

u32 sceAudioRoutingSetVolumeMode(u32 mode) {
	ERROR_LOG_REPORT(SCEAUDIO, "sceAudioRoutingSetVolumeMode(%08x)", mode);
	int previousMode = defaultRoutingVolMode;
	defaultRoutingVolMode = mode;
	return previousMode;
}

u32 sceAudioRoutingGetVolumeMode() {
	ERROR_LOG_REPORT(SCEAUDIO, "sceAudioRoutingGetVolumeMode()");
	return defaultRoutingVolMode;
}

const HLEFunction sceAudio[] = 
{
	// Newer simplified single channel audio output. Presumably for games that use Atrac3
	// directly from Sas instead of playing it on a separate audio channel.
	{0x01562ba3, WrapU_U<sceAudioOutput2Reserve>, "sceAudioOutput2Reserve"},
	{0x2d53f36e, WrapU_UU<sceAudioOutput2OutputBlocking>, "sceAudioOutput2OutputBlocking"},
	{0x63f2889c, WrapU_U<sceAudioOutput2ChangeLength>, "sceAudioOutput2ChangeLength"},
	{0x647cef33, WrapU_V<sceAudioOutput2GetRestSample>, "sceAudioOutput2GetRestSample"},
	{0x43196845, WrapU_V<sceAudioOutput2Release>, "sceAudioOutput2Release"},

	// "Traditional" audio channel interface
	{0x80F1F7E0, WrapU_V<sceAudioInit>, "sceAudioInit"},
	{0x210567F7, WrapU_V<sceAudioEnd>, "sceAudioEnd"},
	{0xA2BEAA6C, WrapU_U<sceAudioSetFrequency>, "sceAudioSetFrequency"},
	{0x927AC32B, WrapU_V<sceAudioSetVolumeOffset>, "sceAudioSetVolumeOffset"},
	{0x8c1009b2, WrapU_UIU<sceAudioOutput>, "sceAudioOutput"},
	{0x136CAF51, WrapU_UIU<sceAudioOutputBlocking>, "sceAudioOutputBlocking"},
	{0xE2D56B2D, WrapU_UIIU<sceAudioOutputPanned>, "sceAudioOutputPanned"},
	{0x13F592BC, WrapU_UIIU<sceAudioOutputPannedBlocking>, "sceAudioOutputPannedBlocking"},
	{0x5EC81C55, WrapU_IUU<sceAudioChReserve>, "sceAudioChReserve"},
	{0x6FC46853, WrapU_U<sceAudioChRelease>, "sceAudioChRelease"},
	{0xE9D97901, WrapI_U<sceAudioGetChannelRestLen>, "sceAudioGetChannelRestLen"},
	{0xB011922F, WrapI_U<sceAudioGetChannelRestLength>, "sceAudioGetChannelRestLength"},
	{0xCB2E439E, WrapU_UU<sceAudioSetChannelDataLen>, "sceAudioSetChannelDataLen"},
	{0x95FD0C2D, WrapU_UU<sceAudioChangeChannelConfig>, "sceAudioChangeChannelConfig"},
	{0xB7E1D8E7, WrapU_UUU<sceAudioChangeChannelVolume>, "sceAudioChangeChannelVolume"},

	// Not sure about the point of these, maybe like traditional but with ability to do sample rate conversion?
	{0x38553111, WrapU_UUU<sceAudioSRCChReserve>, "sceAudioSRCChReserve"},
	{0x5C37C0AE, WrapU_V<sceAudioSRCChRelease>, "sceAudioSRCChRelease"},
	{0xE0727056, WrapU_UU<sceAudioSRCOutputBlocking>, "sceAudioSRCOutputBlocking"},

	// Never seen these used
	{0x41efade7, 0, "sceAudioOneshotOutput"},
	{0xB61595C0, 0, "sceAudioLoopbackTest"},

	// Microphone interface
	{0x7de61688, 0, "sceAudioInputInit"},
	{0xE926D3FB, 0, "sceAudioInputInitEx"},
	{0x6d4bec68, 0, "sceAudioInput"},
	{0x086e5895, 0, "sceAudioInputBlocking"},
	{0xa708c6a6, 0, "sceAudioGetInputLength"},
	{0xA633048E, 0, "sceAudioPollInputEnd"},
	{0x87b2e651, 0, "sceAudioWaitInputEnd"},

	{0x36FD8AA9, WrapU_U<sceAudioRoutingSetMode>, "sceAudioRoutingSetMode" },
	{0x39240E7D, WrapU_V<sceAudioRoutingGetMode>, "sceAudioRoutingGetMode" },
	{0xBB548475, WrapU_U<sceAudioRoutingSetVolumeMode>, "sceAudioRoutingSetVolumeMode" },
	{0x28235C56, WrapU_V<sceAudioRoutingGetVolumeMode>, "sceAudioRoutingGetVolumeMode" },

};

void Register_sceAudio()
{
	RegisterModule("sceAudio", ARRAY_SIZE(sceAudio), sceAudio);
}
