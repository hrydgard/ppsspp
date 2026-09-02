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

#include <algorithm>
#include <cstdlib>

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
static u32 vaudioOutputCalls = 0;
static u32 vaudioSubmittedFrames = 0;
static u32 vaudioNonSilentCalls = 0;
static u32 vaudioPeakSample = 0;
static int vaudioEffectType = 0;
static int vaudioEffectVolume = 0;
static int vaudioAlcMode = 0;

void __VaudioInit() {
	vaudioReserved = false;
	vaudioOutputCalls = 0;
	vaudioSubmittedFrames = 0;
	vaudioNonSilentCalls = 0;
	vaudioPeakSample = 0;
	vaudioEffectType = 0;
	vaudioEffectVolume = 0;
	vaudioAlcMode = 0;
}

void __VaudioDoState(PointerWrap &p) {
	auto s = p.Section("sceVaudio", 1, 3);
	if (!s)
		return;

	Do(p, vaudioReserved);
	if (s >= 2) {
		Do(p, vaudioOutputCalls);
		Do(p, vaudioSubmittedFrames);
		Do(p, vaudioEffectType);
		Do(p, vaudioEffectVolume);
		Do(p, vaudioAlcMode);
		if (s >= 3) {
			Do(p, vaudioNonSilentCalls);
			Do(p, vaudioPeakSample);
		} else if (p.mode == PointerWrap::MODE_READ) {
			vaudioNonSilentCalls = 0;
			vaudioPeakSample = 0;
		}
	} else if (p.mode == PointerWrap::MODE_READ) {
		vaudioOutputCalls = 0;
		vaudioSubmittedFrames = 0;
		vaudioEffectType = 0;
		vaudioEffectVolume = 0;
		vaudioAlcMode = 0;
		vaudioNonSilentCalls = 0;
		vaudioPeakSample = 0;
	}
}

static u32 sceVaudioChReserve(int sampleCount, int freq, int format) {
	if (vaudioReserved) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - already reserved", sampleCount, freq, format);
		return SCE_KERNEL_ERROR_BUSY;
	}
	// We still have to check the channel also, which gives a different error.
	if (g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].reserved) {
		ERROR_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i) - channel already reserved", sampleCount, freq, format);
		return SCE_ERROR_AUDIO_CHANNEL_ALREADY_RESERVED;
	}
	DEBUG_LOG(Log::sceAudio, "sceVaudioChReserve(%i, %i, %i)", sampleCount, freq, format);
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].reserved = true;
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].sampleCount = sampleCount;
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].format = format == 2 ? PSP_AUDIO_FORMAT_STEREO : PSP_AUDIO_FORMAT_MONO;
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].leftVolume = 0;
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].rightVolume = 0;
	vaudioReserved = true;
	__AudioSetSRCFrequency(freq);
	return 0;
}

static u32 sceVaudioChRelease() {
	DEBUG_LOG(Log::sceAudio, "sceVaudioChRelease(...)");
	if (!g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].reserved) {
		return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
	} else {
		g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].reset();
		g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].reserved = false;
		vaudioReserved = false;
		return 0;
	}
}

static u32 sceVaudioOutputBlocking(int vol, u32 buffer) {
	DEBUG_LOG(Log::sceAudio, "sceVaudioOutputBlocking(%i, %08x)", vol, buffer);
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].leftVolume = vol;
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].rightVolume = vol;
	// TODO: This may be wrong, not sure if's in a different format?
	g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].sampleAddress = buffer;
	vaudioOutputCalls++;
	vaudioSubmittedFrames += g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].sampleCount;
	const u32 channels = g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].format == PSP_AUDIO_FORMAT_STEREO ? 2 : 1;
	const u32 sampleValues = g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO].sampleCount * channels;
	if (Memory::IsValidRange(buffer, sampleValues * sizeof(s16))) {
		u32 callPeak = 0;
		for (u32 i = 0; i < sampleValues; ++i) {
			const int sample = (s16)Memory::ReadUnchecked_U16(buffer + i * sizeof(s16));
			callPeak = std::max(callPeak, (u32)std::abs(sample));
		}
		if (callPeak != 0) {
			vaudioNonSilentCalls++;
			vaudioPeakSample = std::max(vaudioPeakSample, callPeak);
		}
	}
	return __AudioEnqueue(g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO], PSP_AUDIO_CHANNEL_VAUDIO, true);
}

static u32 sceVaudioSetEffectType(int effectType, int vol) {
	vaudioEffectType = effectType;
	vaudioEffectVolume = vol;
	return hleLogDebug(Log::sceAudio, 0, "type=%d volume=%d", effectType, vol);
}

// SensMe shows that this controls the automatic audio volume normalizer
static u32 sceVaudioSetAlcMode(int alcMode) {
	vaudioAlcMode = alcMode;
	return hleLogDebug(Log::sceAudio, 0, "mode=%d", alcMode);
}

// Jpcsp models both of these entry points as the buffering form of the same
// VAUDIO reservation. PPSSPP's audio queue already blocks and applies its own
// bounded buffering, so sharing the regular reservation path preserves the
// guest-visible channel state without creating another host queue.
static u32 sceVaudioChReserveBuffering(int sampleCount, int freq, int format) {
	return sceVaudioChReserve(sampleCount, freq, format);
}

static u32 sceVaudio_504E4745(int unknown) {
	return hleLogDebug(Log::sceAudio, 0, "unknown=%d", unknown);
}

const HLEFunction sceVaudio[] = {
	{0X8986295E, &WrapU_IU<sceVaudioOutputBlocking>, "sceVaudioOutputBlocking",     'x', "ix" },
	{0X03B6807D, &WrapU_III<sceVaudioChReserve>,     "sceVaudioChReserve",          'x', "iii"},
	{0X67585DFD, &WrapU_V<sceVaudioChRelease>,       "sceVaudioChRelease",          'x', ""   },
	{0X346FBE94, &WrapU_II<sceVaudioSetEffectType>,  "sceVaudioSetEffectType",      'x', "ii" },
	{0XCBD4AC51, &WrapU_I<sceVaudioSetAlcMode>,      "sceVaudioSetAlcMode",         'x', "i"  },
	{0X504E4745, &WrapU_I<sceVaudio_504E4745>,       "sceVaudio_504E4745",          'x', "i"  },
	{0X27ACC20B, &WrapU_III<sceVaudioChReserveBuffering>, "sceVaudioChReserveBuffering", 'x', "iii"},
	{0XE8E78DC8, &WrapU_III<sceVaudioChReserveBuffering>, "sceVaudio_E8E78DC8",          'x', "iii"},
	{0XA3B71098, &WrapU_II<sceVaudioSetEffectType>,  "sceVaudioSetEffectType",      'x', "ii", HLE_KERNEL_SYSCALL },
};

void Register_sceVaudio() {
	RegisterHLEModule("sceVaudio",ARRAY_SIZE(sceVaudio), sceVaudio );
}

void Register_sceVaudio_driver() {
	// Sony's VSH bridge imports the kernel alias while games normally import
	// sceVaudio. Both names address the same physical VAUDIO channel.
	RegisterHLEModule("sceVaudio_driver", ARRAY_SIZE(sceVaudio), sceVaudio);
}

VSHVaudioDebugStatus __VaudioGetDebugStatus() {
	const AudioChannel &channel = g_audioChans[PSP_AUDIO_CHANNEL_VAUDIO];
	VSHVaudioDebugStatus status;
	status.reserved = vaudioReserved && channel.reserved;
	status.outputCalls = vaudioOutputCalls;
	status.submittedFrames = vaudioSubmittedFrames;
	status.nonSilentCalls = vaudioNonSilentCalls;
	status.peakSample = vaudioPeakSample;
	for (int channelIndex = 0; channelIndex < PSP_AUDIO_CHANNEL_MAX; ++channelIndex) {
		const AudioChannelSubmitStats regular = __AudioGetChannelSubmitStats(channelIndex);
		status.regularOutputCalls += regular.calls;
		status.regularNonSilentCalls += regular.nonSilentCalls;
		status.regularPeakSample = std::max(status.regularPeakSample, regular.peakSample);
	}
	status.sampleCount = channel.sampleCount;
	status.queuedSampleValues = __AudioGetQueueSampleValues(PSP_AUDIO_CHANNEL_VAUDIO);
	status.leftVolume = channel.leftVolume;
	status.rightVolume = channel.rightVolume;
	status.effectType = vaudioEffectType;
	status.effectVolume = vaudioEffectVolume;
	status.alcMode = vaudioAlcMode;
	return status;
}
