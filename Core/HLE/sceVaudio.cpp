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
#include "Core/HLE/ErrorCodes.h"
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
	// The vaudio channel is pickier than the normal ones: a fixed set of sample counts, stereo
	// only, and the same set of sample rates the SRC channel takes. It tests
	// 256/576/1024/1152 and then 2048, returning 0x80000104, before it looks at the format at all.
	// 576 and 1152 are the MPEG-2/2.5 and MPEG-1 Layer III frame sizes, so leaving them out broke
	// games that feed MP3 frames straight to the vaudio channel.
	if (sampleCount != 256 && sampleCount != 576 && sampleCount != 1024 && sampleCount != 1152 && sampleCount != 2048) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - invalid sample count", sampleCount, freq, format);
		return SCE_KERNEL_ERROR_INVALID_SIZE;
	}
	if (format != 2) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - unexpected format", sampleCount, freq, format);
		return SCE_KERNEL_ERROR_INVALID_FORMAT;
	}
	if (vaudioReserved) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - already reserved", sampleCount, freq, format);
		return SCE_KERNEL_ERROR_BUSY;
	}

	// Everything past here is the underlying sceAudioSRCChReserve, and the module marks itself
	// reserved before handing over - it does not undo that when the reserve fails. So a caller
	// that got 0x80268002 because Output2 held the channel is told 0x80000021 next time round,
	// until a release clears it.
	vaudioReserved = true;
	if (freq != 0 && !SRCFrequencyAllowed(freq)) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - invalid frequency", sampleCount, freq, format);
		return SCE_ERROR_AUDIO_INVALID_FREQUENCY;
	}
	// We still have to check the channel also, which gives a different error.
	if (g_audioSRC.reserved) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - channel already reserved", sampleCount, freq, format);
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	}
	DEBUG_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i)", sampleCount, freq, format);
	g_audioSRC.clear();
	g_audioSRC.reserved = true;
	g_audioSRC.sampleCount = sampleCount;
	g_audioSRC.format = format == 2 ? PSP_AUDIO_FORMAT_STEREO : PSP_AUDIO_FORMAT_MONO;
	__AudioSetSRCFrequency(freq);
	return 0;
}

static u32 sceVaudioChRelease() {
	DEBUG_LOG(Log::sceAudio, "sceVaudioChRelease(...)");
	// Not gated on vaudio's own flag: the release goes straight at the SRC channel, so it will
	// happily release a reservation that Output2 made. pspautotests calls that the "wrong
	// release" and the hardware allows it.
	vaudioReserved = false;
	if (!g_audioSRC.reserved) {
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	}

	// Unlike the Output2 and SRC releases, which refuse while a buffer is in flight, this one
	// hands the channel a null pointer first. That parks the caller until a buffer finishes, so
	// what was playing is played out instead of being cut off.
	__AudioSRCEnqueueBlocking(g_audioSRC, 0, -1);
	if (g_audioSRC.Full()) {
		// One drain was not enough to free a descriptor, so the release itself fails.
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	}

	// The reservation goes now rather than when the drain finishes. The caller is parked either
	// way and gets the same answer at the same time; the buffers keep playing because the mixer
	// does not look at the reservation. Only another thread peeking during that window could
	// tell the difference.
	g_audioSRC.reserved = false;
	g_audioSRC.sampleCount = 0;
	__AudioSRCSignal(g_audioSRC);
	return 0;
}

static u32 sceVaudioOutputBlocking(int vol, u32 buffer) {
	DEBUG_LOG(Log::sceAudio, "sceVaudioOutputBlocking(%i, %08x)", vol, buffer);
	// Shares the SRC channel, so it also shares the two-buffer depth and the busy return.
	return __AudioSRCEnqueueBlocking(g_audioSRC, buffer, vol);
}

static u32 sceVaudioSetEffectType(int effectType, int vol) {
	ERROR_LOG_REPORT(Log::sceAudio, "UNIMPL sceVaudioSetEffectType(%i, %i)", effectType, vol);
	return 0;
}

// SensMe shows that this controls the automatic audio volume normalizer
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
	RegisterHLEModule("sceVaudio",ARRAY_SIZE(sceVaudio), sceVaudio );
}
