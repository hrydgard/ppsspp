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

#include "../MIPS/MIPS.h"
#include "../Host.h"
#include "../../Core/CoreTiming.h"

#include "sceAudio.h"
#include "__sceAudio.h"
#include "HLE.h"


// There's a second Audio api called Audio2 that only has one channel, I guess the 8 channel api was overkill.
// We simply map it to the first of the 8 channels.


AudioChannel chans[8];

// Enqueues the buffer pointer on the channel. If channel buffer queue is full (2 items?) will block until it isn't.
// For solid audio output we'll need a queue length of 2 buffers at least, we'll try that first.

// Not sure about the range of volume, I often see 0x800 so that might be either
// max or 50%?

u32 sceAudioOutputBlocking(u32 chan, u32 vol, u32 samplePtr) {
	if (samplePtr == 0) {
		ERROR_LOG(HLE, "sceAudioOutputBlocking - Sample pointer null");
		return 0;
	}
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(HLE,"sceAudioOutputBlocking() - BAD CHANNEL");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		ERROR_LOG(HLE,"sceAudioOutputBlocking() - channel not reserved");
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		DEBUG_LOG(HLE, "sceAudioOutputBlocking(%d, %d, %08x )",chan,vol,samplePtr);
		chans[chan].leftVolume = vol;
		chans[chan].rightVolume = vol;
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, true);
	}
}

u32 sceAudioOutputPannedBlocking(u32 chan, u32 volume1, u32 volume2, u32 samplePtr) {
	if (samplePtr == 0) {
		ERROR_LOG(HLE, "sceAudioOutputPannedBlocking - Sample pointer null");
		return 0;
	} else if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(HLE,"sceAudioOutputPannedBlocking() - BAD CHANNEL");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	} else if (!chans[chan].reserved) {
		ERROR_LOG(HLE,"sceAudioOutputPannedBlocking() - CHANNEL NOT RESERVED");
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		DEBUG_LOG(HLE, "sceAudioOutputPannedBlocking(%d,%d,%d, %08x )", chan, volume1, volume2, samplePtr);
		chans[chan].leftVolume = volume1;
		chans[chan].rightVolume = volume2;
		chans[chan].sampleAddress = samplePtr;
		return __AudioEnqueue(chans[chan], chan, true);
	}
}

u32 sceAudioOutput(u32 chan, u32 vol, u32 samplePtr)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX) {
		ERROR_LOG(HLE,"sceAudioOutput() - BAD CHANNEL");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	else if (!chans[chan].reserved)
	{
		ERROR_LOG(HLE,"sceAudioOutput(%d, %d, %08x) - channel not reserved", chan, vol, samplePtr);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	else
	{
		chans[chan].leftVolume = vol;
		chans[chan].rightVolume = vol;
		chans[chan].sampleAddress = samplePtr;
		u32 retval = __AudioEnqueue(chans[chan], chan, false);
		DEBUG_LOG(HLE, "%08x=sceAudioOutputPanned(%d, %d, %08x)", retval, chan, vol, samplePtr);
		return retval;
	}
}

u32 sceAudioOutputPanned(u32 chan, u32 leftVol, u32 rightVol, u32 samplePtr)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE,"sceAudioOutputPanned() - BAD CHANNEL");
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	else if (!chans[chan].reserved)
	{
		ERROR_LOG(HLE,"sceAudioOutputPanned(%d, %d, %d, %08x) - channel not reserved", chan, leftVol, rightVol, samplePtr);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	else
	{
		chans[chan].leftVolume = leftVol;
		chans[chan].rightVolume = rightVol;
		chans[chan].sampleAddress = samplePtr;
		u32 retval = __AudioEnqueue(chans[chan], chan, false);
		DEBUG_LOG(HLE, "%08x=sceAudioOutputPanned(%d, %d, %d, %08x)", retval, chan, leftVol, rightVol, samplePtr);
		return retval;
	}
}

int sceAudioGetChannelRestLen(u32 chan)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE, "sceAudioGetChannelRestLen(%i) - BAD CHANNEL", chan);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	int sz = (int)chans[chan].sampleQueue.size() / 2;
	DEBUG_LOG(HLE,"UNTESTED %i = sceAudioGetChannelRestLen(%i)", sz, chan);
	return sz;
}

int sceAudioGetChannelRestLength(u32 chan)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE, "sceAudioGetChannelRestLength(%i) - BAD CHANNEL", chan);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	int sz = (int)chans[chan].sampleQueue.size() / 2;
	DEBUG_LOG(HLE,"UNTESTED %i = sceAudioGetChannelRestLen(%i)", sz, chan);
	return sz;
}

static int GetFreeChannel()
{
	for (int i = 0; i < PSP_AUDIO_CHANNEL_MAX ; i++)
		if (!chans[i].reserved)
			return i;
	return -1;
}

u32 sceAudioChReserve(u32 channel, u32 sampleCount, u32 format) //.Allocate sound channel
{
	if (channel == (u32)-1)
	{
		channel = GetFreeChannel();
	}
	else
	{
		ERROR_LOG(HLE,"sceAudioChReserve failed");
		return SCE_ERROR_AUDIO_NO_CHANNELS_AVAILABLE;
	}

	if (channel >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE ,"sceAudioChReserve(channel = %d, sampleCount = %d, format = %d) - BAD CHANNEL", channel, sampleCount, format);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO)
	{
		ERROR_LOG(HLE, "sceAudioChReserve(channel = %d, sampleCount = %d, format = %d): invalid format", channel, sampleCount, format);
		return SCE_ERROR_AUDIO_INVALID_FORMAT;
	}

	if (chans[channel].reserved)
	{
		WARN_LOG(HLE, "WARNING: Reserving already reserved channel. Error?");
	}
	DEBUG_LOG(HLE, "sceAudioChReserve(channel = %d, sampleCount = %d, format = %d)", channel, sampleCount, format);

	chans[channel].sampleCount = sampleCount;
	chans[channel].reserved = true;
	return channel; //return handle
}

u32 sceAudioChRelease(u32 chan)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE, "sceAudioChRelease(%i) - BAD CHANNEL", chan);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}

	if (!chans[chan].reserved)
	{
		ERROR_LOG(HLE,"sceAudioChRelease(%i): channel not reserved", chan);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	chans[chan].reserved = false;

	DEBUG_LOG(HLE, "sceAudioChRelease(%i)", chan);
	return 1;
}

u32 sceAudioSetChannelDataLen(u32 chan, u32 len)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE,"sceAudioSetChannelDataLen(%i, %i) - BAD CHANNEL", chan, len);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	else if (!chans[chan].reserved)
	{
		ERROR_LOG(HLE,"sceAudioSetChannelDataLen(%i, %i) - channel not reserved", chan, len);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	else
	{
		DEBUG_LOG(HLE, "sceAudioSetChannelDataLen(%i, %i)", chan, len);
		chans[chan].sampleCount = len;
		return 0;
	}
}

u32 sceAudioChangeChannelConfig(u32 chan, u32 format)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE,"sceAudioChangeChannelConfig(%i, %i) - invalid channel number", chan, format);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	else if (!chans[chan].reserved)
	{
		ERROR_LOG(HLE,"sceAudioChangeChannelConfig(%i, %i) - channel not reserved", chan, format);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	else
	{
		DEBUG_LOG(HLE, "sceAudioChangeChannelConfig(%i, %i)", chan, format);
		chans[chan].format = format;
		return 0;
	}
}

u32 sceAudioChangeChannelVolume(u32 chan, u32 lvolume, u32 rvolume)
{
	if (chan >= PSP_AUDIO_CHANNEL_MAX)
	{
		ERROR_LOG(HLE,"sceAudioChangeChannelVolume(%i, %i, %i) - invalid channel number", chan, lvolume, rvolume);
		return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	else if (!chans[chan].reserved)
	{
		ERROR_LOG(HLE,"sceAudioChangeChannelVolume(%i, %i, %i) - channel not reserved", chan, lvolume, rvolume);
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}
	else
	{
		DEBUG_LOG(HLE, "sceAudioChangeChannelVolume(%i, %i, %i)", chan, lvolume, rvolume);
		chans[chan].leftVolume = lvolume;
		chans[chan].rightVolume = rvolume;
		return 0;
	}
}

u32 sceAudioInit()
{
	DEBUG_LOG(HLE,"sceAudioInit()");
	// Don't need to do anything
	return 0;
}
u32 sceAudioEnd()
{
	DEBUG_LOG(HLE,"sceAudioEnd()");
	// Don't need to do anything
	return 0;
}

u32 sceAudioOutput2Reserve(u32 sampleCount)
{
	DEBUG_LOG(HLE,"sceAudioOutput2Reserve(%i)", sampleCount);
	chans[0].sampleCount = sampleCount;
	chans[0].reserved = true;
	return 0;
}

u32 sceAudioOutput2OutputBlocking(u32 vol, u32 dataPtr)
{
	DEBUG_LOG(HLE,"FAKE sceAudioOutput2OutputBlocking(%i, %08x)", vol, dataPtr);
	chans[0].leftVolume = vol;
	chans[0].rightVolume = vol;
	chans[0].sampleAddress = dataPtr;
	return __AudioEnqueue(chans[0], 0, true);
}

u32 sceAudioOutput2ChangeLength(u32 sampleCount)
{
	DEBUG_LOG(HLE,"sceAudioOutput2ChangeLength(%i)", sampleCount);
	chans[0].sampleCount = sampleCount;
	return 0;
}

u32 sceAudioOutput2GetRestSample()
{
	DEBUG_LOG(HLE,"UNTESTED sceAudioOutput2GetRestSample()");
	return (u32) chans[0].sampleQueue.size() * 2;
}

u32 sceAudioOutput2Release()
{
	DEBUG_LOG(HLE,"sceAudioOutput2Release()");
	chans[0].reserved = false;
	return 0;
}

u32 sceAudioSetFrequency(u32 freq) {
	if (freq == 44100 || freq == 48000) {
		INFO_LOG(HLE, "sceAudioSetFrequency(%i)", freq);
		__AudioSetOutputFrequency(freq);
		return 0;
	} else {
		ERROR_LOG(HLE, "sceAudioSetFrequency(%i) - invalid frequency (must be 44.1 or 48 khz)", freq);
		return -1;
	}
}

u32 sceAudioSetVolumeOffset(u32 unknown) {
	ERROR_LOG(HLE, "UNIMPL sceAudioSetVolumeOffset(%i)", unknown);
	return 0;
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

	{0x80F1F7E0, WrapU_V<sceAudioInit>, "sceAudioInit"},
	{0x210567F7, WrapU_V<sceAudioEnd>, "sceAudioEnd"},

	{0xA2BEAA6C, WrapU_U<sceAudioSetFrequency>, "sceAudioSetFrequency"},
	{0x927AC32B, WrapU_U<sceAudioSetVolumeOffset>, "sceAudioSetVolumeOffset"},

	// The oldest and standard audio interface. Supports 8 channels, most games use 1-2.
	{0x8c1009b2, WrapU_UUU<sceAudioOutput>, "sceAudioOutput"},
	{0x136CAF51, WrapU_UUU<sceAudioOutputBlocking>, "sceAudioOutputBlocking"},
	{0xE2D56B2D, WrapU_UUUU<sceAudioOutputPanned>, "sceAudioOutputPanned"},
	{0x13F592BC, WrapU_UUUU<sceAudioOutputPannedBlocking>, "sceAudioOutputPannedBlocking"}, //(u32, u32, u32, void *)Output sound, blocking
	{0x5EC81C55, WrapU_UUU<sceAudioChReserve>, "sceAudioChReserve"}, //(u32, u32 samplecount, u32) Initialize channel and allocate buffer	long, long samplecount, long);//init buffer? returns handle, minus if error
	{0x6FC46853, WrapU_U<sceAudioChRelease>, "sceAudioChRelease"}, //(long handle)Terminate channel and deallocate buffer //free buffer?
	{0xE9D97901, WrapI_U<sceAudioGetChannelRestLen>, "sceAudioGetChannelRestLen"},
	{0xB011922F, WrapI_U<sceAudioGetChannelRestLen>, "sceAudioGetChannelRestLength"},	// Is there a difference between this and sceAudioGetChannelRestLen?
	{0xCB2E439E, WrapU_UU<sceAudioSetChannelDataLen>, "sceAudioSetChannelDataLen"}, //(u32, u32)
	{0x95FD0C2D, WrapU_UU<sceAudioChangeChannelConfig>, "sceAudioChangeChannelConfig"},
	{0xB7E1D8E7, WrapU_UUU<sceAudioChangeChannelVolume>, "sceAudioChangeChannelVolume"},

	// I guess these are like the others but do sample rate conversion?
	{0x38553111, 0, "sceAudioSRCChReserve"},
	{0x5C37C0AE, 0, "sceAudioSRCChRelease"},
	{0xE0727056, 0, "sceAudioSRCOutputBlocking"},

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
};



void Register_sceAudio()
{
	RegisterModule("sceAudio", ARRAY_SIZE(sceAudio), sceAudio);
}
