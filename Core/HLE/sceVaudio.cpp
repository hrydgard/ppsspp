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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceVaudio.h"
#include "Core/HLE/sceAudio.h"
#include "Core/HLE/__sceAudio.h"

// Ultra hacky Vaudio implementation. Not sure what the point of this API is.

bool vaudioReserved = false;

void __VaudioInit() {
	vaudioReserved = false;
}

void __VaudioDoState(PointerWrap &p) {
	auto s = p.Section("sceVaudio", 1);
	if (!s)
		return;

	Do(p, vaudioReserved);
}

static u32 sceVaudioChReserve(int sampleCount, int freq, int format) {
	if (vaudioReserved) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - already reserved", sampleCount, freq, format);
		return SCE_KERNEL_ERROR_BUSY;
	}
	// We still have to check the channel also, which gives a different error.
	if (chans[PSP_AUDIO_CHANNEL_VAUDIO].reserved) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - channel already reserved", sampleCount, freq, format);
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	}
	DEBUG_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i)", sampleCount, freq, format);
	chans[PSP_AUDIO_CHANNEL_VAUDIO].reserved = true;
	chans[PSP_AUDIO_CHANNEL_VAUDIO].sampleCount = sampleCount;
	chans[PSP_AUDIO_CHANNEL_VAUDIO].format = format == 2 ? PSP_AUDIO_FORMAT_STEREO : PSP_AUDIO_FORMAT_MONO;
	chans[PSP_AUDIO_CHANNEL_VAUDIO].leftVolume = 0;
	chans[PSP_AUDIO_CHANNEL_VAUDIO].rightVolume = 0;
	vaudioReserved = true;
	__AudioSetSRCFrequency(freq);
	return 0;
}

static u32 sceVaudioChRelease() {
	DEBUG_LOG(Log::sceAudio, "sceVaudioChRelease(...)");
	if (!chans[PSP_AUDIO_CHANNEL_VAUDIO].reserved) {
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		chans[PSP_AUDIO_CHANNEL_VAUDIO].reset();
		chans[PSP_AUDIO_CHANNEL_VAUDIO].reserved = false;
		vaudioReserved = false;
		return 0;
	}
}

static u32 sceVaudioOutputBlocking(int vol, u32 buffer) {
	DEBUG_LOG(Log::sceAudio, "sceVaudioOutputBlocking(%i, %08x)", vol, buffer);
	chans[PSP_AUDIO_CHANNEL_VAUDIO].leftVolume = vol;
	chans[PSP_AUDIO_CHANNEL_VAUDIO].rightVolume = vol;
	// TODO: This may be wrong, not sure if's in a different format?
	chans[PSP_AUDIO_CHANNEL_VAUDIO].sampleAddress = buffer;
	return __AudioEnqueue(chans[PSP_AUDIO_CHANNEL_VAUDIO], PSP_AUDIO_CHANNEL_VAUDIO, true);
}

static u32 sceVaudioSetEffectType(int effectType, int vol) {
	ERROR_LOG_REPORT(Log::sceAudio, "UNIMPL sceVaudioSetEffectType(%i, %i)", effectType, vol);
	return 0;
}

static u32 sceVaudioSetAlcMode(int alcMode) {
	ERROR_LOG_REPORT(Log::sceAudio, "UNIMPL sceVaudioSetAlcMode(%i)", alcMode);
	return 0;
}

const HLEFunction sceVaudio[] = {
	{0X8986295E, &WrapU_IU<sceVaudioOutputBlocking>, "sceVaudioOutputBlocking",     'x', "ix" },
	{0X03B6807D, &WrapU_III<sceVaudioChReserve>,     "sceVaudioChReserve",          'x', "iii"},
	{0X67585DFD, &WrapU_V<sceVaudioChRelease>,       "sceVaudioChRelease",          'x', ""   },
	{0X346FBE94, &WrapU_II<sceVaudioSetEffectType>,  "sceVaudioSetEffectType",      'x', "ii" },
	{0XCBD4AC51, &WrapU_I<sceVaudioSetAlcMode>,      "sceVaudioSetAlcMode",         'x', "i"  },
	{0X504E4745, nullptr,                            "sceVaudio_504E4745",          '?', ""   },
	{0X27ACC20B, nullptr,                            "sceVaudioChReserveBuffering", '?', ""   },
	{0XE8E78DC8, nullptr,                            "sceVaudio_E8E78DC8",          '?', ""   },
};

void Register_sceVaudio() {
	RegisterModule("sceVaudio",ARRAY_SIZE(sceVaudio), sceVaudio );
}
