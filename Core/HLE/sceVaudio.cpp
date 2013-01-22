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

#include "HLE.h"
#include "FunctionWrappers.h"
#include "sceVaudio.h"
#include "sceAudio.h"
#include "__sceAudio.h"

// Ultra hacky Vaudio implementation. Not sure what the point of this API is.

u32 sceVaudioChReserve(int sampleCount, int freq, int format) {
	WARN_LOG(HLE, "HACK sceVaudioChReserve(%i, %i, %i)", sampleCount, freq, format);
	chans[0].reserved = true;
	chans[0].sampleCount = sampleCount;
	chans[0].format = format;
	__AudioSetOutputFrequency(freq);
	return 0;
}

u32 sceVaudioChRelease() {
	WARN_LOG(HLE, "HACK sceVaudioChRelease(...)");
	if (!chans[0].reserved) {
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		chans[0].reserved = false;
		return 0;
	}
}

u32 sceVaudioOutputBlocking(int vol, u32 buffer) {
	WARN_LOG(HLE, "HACK sceVaudioOutputBlocking(%i, %08x)", vol, buffer);
	return __AudioEnqueue(chans[0], 0, true);
}

u32 sceVaudioSetEffectType(int effectType, int vol) {
	ERROR_LOG(HLE, "UNIMPL sceVaudioSetEffectType(%i, %i)", effectType, vol);
	return 0;
}

u32 sceVaudioSetAlcMode(int alcMode) {
	ERROR_LOG(HLE, "UNIMPL sceVaudioSetAlcMode(%i)", alcMode);
	return 0;
}

const HLEFunction sceVaudio[] = {
	{0x03b6807d, WrapU_IU<sceVaudioOutputBlocking>, "sceVaudioOutputBlockingFunction"},
	{0x67585dfd, WrapU_III<sceVaudioChReserve>, "sceVaudioChReserveFunction"},
	{0x8986295e, WrapU_V<sceVaudioChRelease>, "sceVaudioChReleaseFunction"},
	{0x346FBE94, WrapU_II<sceVaudioSetEffectType>, "sceVaudioSetEffectType"},
	{0xCBD4AC51, WrapU_I<sceVaudioSetAlcMode>, "sceVaudioSetAlcMode"},
};

void Register_sceVaudio() {
	RegisterModule("sceVaudio",ARRAY_SIZE(sceVaudio), sceVaudio );
}
