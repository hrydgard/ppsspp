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

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceAudiocodec.h"

enum {
	AUDIO_ROUTING_SPEAKER_OFF = 0,
	AUDIO_ROUTING_SPEAKER_ON = 1,
};

u32 audioRoutingMode = AUDIO_ROUTING_SPEAKER_ON;
u32 audioRoutineVolumeMode = AUDIO_ROUTING_SPEAKER_ON;

static int sceAudioRoutingGetMode() {
	INFO_LOG(Log::sceAudio, "sceAudioRoutingGetMode");
	return 0;
}

static int sceAudioRoutingSetVolumeMode(int mode) {
	INFO_LOG(Log::sceAudio, "sceAudioRoutingSetVolumeMode %d", mode);
	int previousMode = audioRoutineVolumeMode;
	audioRoutineVolumeMode = audioRoutingMode;
	return previousMode;
}

static int sceAudioRoutingGetVolumeMode() {
	INFO_LOG(Log::sceAudio, "sceAudioRoutingGetMode");
	return 0;
}

static int sceAudioRoutingSetMode(int mode) {
	INFO_LOG(Log::sceAudio, "sceAudioRoutingSetMode %d", mode);
	int previousMode = audioRoutingMode;
	audioRoutingMode = mode;
	return previousMode;
}

const HLEFunction sceAudioRouting[] =
{
	{0X39240E7D, &WrapI_V<sceAudioRoutingGetMode>,       "sceAudioRoutingGetMode",       'i', "" },
	{0X28235C56, &WrapI_V<sceAudioRoutingGetVolumeMode>, "sceAudioRoutingGetVolumeMode", 'i', "" },
	{0X36FD8AA9, &WrapI_I<sceAudioRoutingSetMode>,       "sceAudioRoutingSetMode",       'i', "i"},
	{0XBB548475, &WrapI_I<sceAudioRoutingSetVolumeMode>, "sceAudioRoutingSetVolumeMode", 'i', "i"},
};

void Register_sceAudioRouting()
{
	RegisterModule("sceAudioRouting", ARRAY_SIZE(sceAudioRouting), sceAudioRouting);
}
