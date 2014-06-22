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

// SAS is a software mixing engine that runs on the Media Engine CPU. We just HLE it.
// This is a very rough implementation that needs lots of work.
//
// This file just contains the API, the real stuff is in HW/SasAudio.cpp/h.
//
// JPCSP is, as it often is, a pretty good reference although I didn't actually use it much yet:
// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/sceSasCore.java
//
// This should be multithreaded and improved at some point. Some discussion here:
// https://github.com/hrydgard/ppsspp/issues/1078

#include <cstdlib>
#include "base/basictypes.h"
#include "Common/Log.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HW/SasAudio.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"

#include "Core/HLE/sceSas.h"
#include "Core/HLE/sceKernel.h"

enum {
	ERROR_SAS_INVALID_GRAIN           = 0x80420001,
	ERROR_SAS_INVALID_MAX_VOICES      = 0x80420002,
	ERROR_SAS_INVALID_OUTPUT_MODE     = 0x80420003,
	ERROR_SAS_INVALID_SAMPLE_RATE     = 0x80420004,
	ERROR_SAS_BAD_ADDRESS             = 0x80420005,
	ERROR_SAS_INVALID_VOICE           = 0x80420010,
	ERROR_SAS_INVALID_NOISE_FREQ      = 0x80420011,
	ERROR_SAS_INVALID_PITCH           = 0x80420012,
	ERROR_SAS_INVALID_ADSR_CURVE_MODE = 0x80420013,
	ERROR_SAS_INVALID_PARAMETER       = 0x80420014,
	ERROR_SAS_INVALID_LOOP_POS        = 0x80420015,
	ERROR_SAS_VOICE_PAUSED            = 0x80420016,
	ERROR_SAS_INVALID_VOLUME          = 0x80420018,
	ERROR_SAS_INVALID_ADSR_RATE       = 0x80420019,
	ERROR_SAS_INVALID_PCM_SIZE        = 0x8042001A,
	ERROR_SAS_REV_INVALID_VOLUME      = 0x80420023,
	ERROR_SAS_BUSY                    = 0x80420030,
	ERROR_SAS_NOT_INIT                = 0x80420100,
};

// TODO - allow more than one, associating each with one Core pointer (passed in to all the functions)
// No known games use more than one instance of Sas though.
static SasInstance *sas = NULL;

void __SasInit() {
	sas = new SasInstance();
}

void __SasDoState(PointerWrap &p) {
	auto s = p.Section("sceSas", 1);
	if (!s)
		return;

	p.DoClass(sas);
}

void __SasShutdown() {
	delete sas;
	sas = 0;
}


u32 sceSasInit(u32 core, u32 grainSize, u32 maxVoices, u32 outputMode, u32 sampleRate) {
	if (!Memory::IsValidAddress(core) || (core & 0x3F) != 0) {
		ERROR_LOG_REPORT(SCESAS, "sceSasInit(%08x, %i, %i, %i, %i): bad core address", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_BAD_ADDRESS;
	}
	if (maxVoices == 0 || maxVoices > PSP_SAS_VOICES_MAX) {
		ERROR_LOG_REPORT(SCESAS, "sceSasInit(%08x, %i, %i, %i, %i): bad max voices", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_INVALID_MAX_VOICES;
	}
	if (grainSize < 0x40 || grainSize > 0x800 || (grainSize & 0x1F) != 0) {
		ERROR_LOG_REPORT(SCESAS, "sceSasInit(%08x, %i, %i, %i, %i): bad grain size", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_INVALID_GRAIN;
	}
	if (outputMode != 0 && outputMode != 1) {
		ERROR_LOG_REPORT(SCESAS, "sceSasInit(%08x, %i, %i, %i, %i): bad output mode", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_INVALID_OUTPUT_MODE;
	}
	if (sampleRate != 44100) {
		ERROR_LOG_REPORT(SCESAS, "sceSasInit(%08x, %i, %i, %i, %i): bad sample rate", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_INVALID_SAMPLE_RATE;
	}
	INFO_LOG(SCESAS, "sceSasInit(%08x, %i, %i, %i, %i)", core, grainSize, maxVoices, outputMode, sampleRate);

	sas->SetGrainSize(grainSize);
	// Seems like maxVoices is actually ignored for all intents and purposes.
	sas->maxVoices = PSP_SAS_VOICES_MAX;
	sas->outputMode = outputMode;
	for (int i = 0; i < sas->maxVoices; i++) {
		sas->voices[i].sampleRate = sampleRate;
		sas->voices[i].playing = false;
		sas->voices[i].loop = false;
	}
	return 0;
}

u32 sceSasGetEndFlag(u32 core) {
	u32 endFlag = 0;
	for (int i = 0; i < sas->maxVoices; i++) {
		if (!sas->voices[i].playing)
			endFlag |= (1 << i);
	}

	DEBUG_LOG(SCESAS, "%08x=sceSasGetEndFlag(%08x)", endFlag, core);
	return endFlag;
}

// Runs the mixer
u32 _sceSasCore(u32 core, u32 outAddr) {
	if (!Memory::IsValidAddress(outAddr)) {
		ERROR_LOG_REPORT(SCESAS, "sceSasCore(%08x, %08x): invalid address", core, outAddr);
		return ERROR_SAS_INVALID_PARAMETER;
	}

	DEBUG_LOG(SCESAS, "sceSasCore(%08x, %08x)", core, outAddr);
	sas->Mix(outAddr);

	// Actual delay time seems to between 240 and 1000 us, based on grain and possibly other factors.
	return hleDelayResult(0, "sas core", 240);
}

// Another way of running the mixer, the inoutAddr should be both input and output
u32 _sceSasCoreWithMix(u32 core, u32 inoutAddr, int leftVolume, int rightVolume) {
	if (!Memory::IsValidAddress(inoutAddr)) {
		ERROR_LOG_REPORT(SCESAS, "sceSasCoreWithMix(%08x, %08x, %i, %i): invalid address", core, inoutAddr, leftVolume, rightVolume);
		return ERROR_SAS_INVALID_PARAMETER;
	}
	if (sas->outputMode == PSP_SAS_OUTPUTMODE_RAW) {
		ERROR_LOG_REPORT(SCESAS, "sceSasCoreWithMix(%08x, %08x, %i, %i): unsupported outputMode", core, inoutAddr, leftVolume, rightVolume);
		return 0x80000004;
	}
	
	DEBUG_LOG(SCESAS, "sceSasCoreWithMix(%08x, %08x, %i, %i)", core, inoutAddr, leftVolume, rightVolume);
	sas->Mix(inoutAddr, inoutAddr, leftVolume, rightVolume);

	// Actual delay time seems to between 240 and 1000 us, based on grain and possibly other factors.
	return hleDelayResult(0, "sas core", 240);
}

u32 sceSasSetVoice(u32 core, int voiceNum, u32 vagAddr, int size, int loop) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}


	if (size == 0 || ((u32)size & 0xF) != 0) {
		if (size == 0) {
			DEBUG_LOG(SCESAS, "%s: invalid size %d", __FUNCTION__, size);
		} else {
			WARN_LOG(SCESAS, "%s: invalid size %d", __FUNCTION__, size);
		}
		return ERROR_SAS_INVALID_PARAMETER;
	}
	if (loop != 0 && loop != 1) {
		WARN_LOG_REPORT(SCESAS, "%s: invalid loop mode %d", __FUNCTION__, loop);
		return ERROR_SAS_INVALID_LOOP_POS;
	}

	if (!Memory::IsValidAddress(vagAddr)) {
		ERROR_LOG(SCESAS, "%s: Ignoring invalid VAG audio address %08x", __FUNCTION__, vagAddr);
		return 0;
	}

	if (size < 0) {
		// POSSIBLE HACK
		// SetVoice with negative sizes returns OK (0) unlike SetVoicePCM, but should not
		// play any audio, it seems. So let's bail and not do anything.
		// Needs more rigorous testing perhaps, but this fixes issue https://github.com/hrydgard/ppsspp/issues/5652
		// while being fairly low risk to other games.
		size = 0;
		DEBUG_LOG(SCESAS, "sceSasSetVoice(%08x, %i, %08x, %i, %i) : HACK: Negative size changed to 0", core, voiceNum, vagAddr, size, loop);
	} else {
		DEBUG_LOG(SCESAS, "sceSasSetVoice(%08x, %i, %08x, %i, %i)", core, voiceNum, vagAddr, size, loop);
	}

	SasVoice &v = sas->voices[voiceNum];
	u32 prevVagAddr = v.vagAddr;
	v.type = VOICETYPE_VAG;
	v.vagAddr = vagAddr;  // Real VAG header is 0x30 bytes behind the vagAddr
	v.vagSize = size;
	v.loop = loop ? true : false;
	v.ChangedParams(vagAddr == prevVagAddr);
	return 0;
}

u32 sceSasSetVoicePCM(u32 core, int voiceNum, u32 pcmAddr, int size, int loopPos)
{
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}
	if (size <= 0 || size > 0x10000) {
		WARN_LOG(SCESAS, "%s: invalid size %d", __FUNCTION__, size);
		return ERROR_SAS_INVALID_PCM_SIZE;
	}
	if (loopPos >= size) {
		ERROR_LOG_REPORT(SCESAS, "sceSasSetVoicePCM(%08x, %i, %08x, %i, %i): bad loop pos", core, voiceNum, pcmAddr, size, loopPos);
		return ERROR_SAS_INVALID_LOOP_POS;
	}
	if (!Memory::IsValidAddress(pcmAddr)) {
		ERROR_LOG(SCESAS, "Ignoring invalid PCM audio address %08x", pcmAddr);
		return 0;
	}

	DEBUG_LOG(SCESAS, "sceSasSetVoicePCM(%08x, %i, %08x, %i, %i)", core, voiceNum, pcmAddr, size, loopPos);

	SasVoice &v = sas->voices[voiceNum];
	u32 prevPcmAddr = v.pcmAddr;
	v.type = VOICETYPE_PCM;
	v.pcmAddr = pcmAddr;
	v.pcmSize = size;
	v.pcmIndex = 0;
	v.pcmLoopPos = loopPos >= 0 ? loopPos : 0;
	v.loop = loopPos >= 0 ? true : false;
	v.playing = true;
	v.ChangedParams(pcmAddr == prevPcmAddr);
	return 0;
}

u32 sceSasGetPauseFlag(u32 core) {
	u32 pauseFlag = 0;
	for (int i = 0; i < sas->maxVoices; i++) {
		if (sas->voices[i].paused)
			pauseFlag |= (1 << i);
	}

	DEBUG_LOG(SCESAS, "sceSasGetPauseFlag(%08x)", pauseFlag)
	return pauseFlag;
}

u32 sceSasSetPause(u32 core, u32 voicebit, int pause) {
	DEBUG_LOG(SCESAS, "sceSasSetPause(%08x, %08x, %i)", core, voicebit, pause);

	for (int i = 0; voicebit != 0; i++, voicebit >>= 1) {
		if (i < PSP_SAS_VOICES_MAX && i >= 0) {
			if ((voicebit & 1) != 0)
				sas->voices[i].paused = pause ? true : false;
		}
	}

	return 0;
}

u32 sceSasSetVolume(u32 core, int voiceNum, int leftVol, int rightVol, int effectLeftVol, int effectRightVol) {
	DEBUG_LOG(SCESAS, "sceSasSetVolume(%08x, %i, %i, %i, %i, %i)", core, voiceNum, leftVol, rightVol, effectLeftVol, effectRightVol);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}
	bool overVolume = abs(leftVol) > PSP_SAS_VOL_MAX || abs(rightVol) > PSP_SAS_VOL_MAX;
	overVolume = overVolume || abs(effectLeftVol) > PSP_SAS_VOL_MAX || abs(effectRightVol) > PSP_SAS_VOL_MAX;
	if (overVolume)
		return ERROR_SAS_INVALID_VOLUME;

	SasVoice &v = sas->voices[voiceNum];
	v.volumeLeft = leftVol;
	v.volumeRight = rightVol;
	v.effectLeft = effectLeftVol;
	v.effectRight = effectRightVol;
	return 0;
}

u32 sceSasSetPitch(u32 core, int voiceNum, int pitch) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}
	if (pitch < PSP_SAS_PITCH_MIN || pitch > PSP_SAS_PITCH_MAX) {
		WARN_LOG(SCESAS, "sceSasSetPitch(%08x, %i, %i): bad pitch", core, voiceNum, pitch);
		return ERROR_SAS_INVALID_PITCH;
	}

	DEBUG_LOG(SCESAS, "sceSasSetPitch(%08x, %i, %i)", core, voiceNum, pitch);
	SasVoice &v = sas->voices[voiceNum];
	v.pitch = pitch;
	v.ChangedParams(false);
	return 0;
}

u32 sceSasSetKeyOn(u32 core, int voiceNum) {
	DEBUG_LOG(SCESAS, "sceSasSetKeyOn(%08x, %i)", core, voiceNum);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	if (sas->voices[voiceNum].paused || sas->voices[voiceNum].on) {
		return ERROR_SAS_VOICE_PAUSED;
	}

	SasVoice &v = sas->voices[voiceNum];
	v.KeyOn();
	return 0;
}

// sceSasSetKeyOff can be used to start sounds, that just sound during the Release phase!
u32 sceSasSetKeyOff(u32 core, int voiceNum) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0) {
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	} else {
		DEBUG_LOG(SCESAS, "sceSasSetKeyOff(%08x, %i)", core, voiceNum);

		if (sas->voices[voiceNum].paused || !sas->voices[voiceNum].on) {
			return ERROR_SAS_VOICE_PAUSED;
		}

		SasVoice &v = sas->voices[voiceNum];
		v.KeyOff();
		return 0;
	}
}

u32 sceSasSetNoise(u32 core, int voiceNum, int freq) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}
	if (freq < 0 || freq >= 64) {
		DEBUG_LOG(SCESAS, "sceSasSetNoise(%08x, %i, %i)", core, voiceNum, freq);
		return ERROR_SAS_INVALID_NOISE_FREQ;
	}

	DEBUG_LOG(SCESAS, "sceSasSetNoise(%08x, %i, %i)", core, voiceNum, freq);

	SasVoice &v = sas->voices[voiceNum];
	v.type = VOICETYPE_NOISE;
	v.noiseFreq = freq;
	v.ChangedParams(true);
	return 0;
}

u32 sceSasSetSL(u32 core, int voiceNum, int level) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	DEBUG_LOG(SCESAS, "sceSasSetSL(%08x, %i, %08x)", core, voiceNum, level);
	SasVoice &v = sas->voices[voiceNum];
	v.envelope.sustainLevel = level;
	return 0;
}

u32 sceSasSetADSR(u32 core, int voiceNum, int flag, int a, int d, int s, int r) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}
	// Create a mask like flag for the invalid values.
	int invalid = (a < 0 ? 0x1 : 0) | (d < 0 ? 0x2 : 0) | (s < 0 ? 0x4 : 0) | (r < 0 ? 0x8 : 0);
	if (invalid & flag) {
		WARN_LOG_REPORT(SCESAS, "sceSasSetADSR(%08x, %i, %i, %08x, %08x, %08x, %08x): invalid value", core, voiceNum, flag, a, d, s, r)
		return ERROR_SAS_INVALID_ADSR_RATE;
	}

	DEBUG_LOG(SCESAS, "0=sceSasSetADSR(%08x, %i, %i, %08x, %08x, %08x, %08x)", core, voiceNum, flag, a, d, s, r)

	SasVoice &v = sas->voices[voiceNum];
	if ((flag & 0x1) != 0) v.envelope.attackRate  = a;
	if ((flag & 0x2) != 0) v.envelope.decayRate   = d;
	if ((flag & 0x4) != 0) v.envelope.sustainRate = s;
	if ((flag & 0x8) != 0) v.envelope.releaseRate = r;
	return 0;
}

u32 sceSasSetADSRMode(u32 core, int voiceNum, int flag, int a, int d, int s, int r) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	// Probably by accident (?), the PSP ignores the top bit of these values.
	a = a & ~0x80000000;
	d = d & ~0x80000000;
	s = s & ~0x80000000;
	r = r & ~0x80000000;

	// This will look like the update flag for the invalid modes.
	int invalid = 0;
	if (a > 5 || (a & 1) != 0) {
		invalid |= 0x1;
	}
	if (d > 5 || (d & 1) != 1) {
		invalid |= 0x2;
	}
	if (s > 5) {
		invalid |= 0x4;
	}
	if (r > 5 || (r & 1) != 1) {
		invalid |= 0x8;
	}
	if (invalid & flag) {
		if (a == 5 && d == 5 && s == 5 && r == 5) {
			// Some games do this right at init.  It seems to fail even on a PSP, but let's not report it.
			DEBUG_LOG(SCESAS, "sceSasSetADSRMode(%08x, %i, %i, %08x, %08x, %08x, %08x): invalid modes", core, voiceNum, flag, a, d, s, r);
		} else {
			WARN_LOG_REPORT(SCESAS, "sceSasSetADSRMode(%08x, %i, %i, %08x, %08x, %08x, %08x): invalid modes", core, voiceNum, flag, a, d, s, r);
		}
		return ERROR_SAS_INVALID_ADSR_CURVE_MODE;
	}

	DEBUG_LOG(SCESAS, "sceSasSetADSRMode(%08x, %i, %i, %08x, %08x, %08x, %08x)", core, voiceNum, flag, a, d, s, r);
	SasVoice &v = sas->voices[voiceNum];
	if ((flag & 0x1) != 0) v.envelope.attackType  = a;
	if ((flag & 0x2) != 0) v.envelope.decayType   = d;
	if ((flag & 0x4) != 0) v.envelope.sustainType = s;
	if ((flag & 0x8) != 0) v.envelope.releaseType = r;
	return 0;
}


u32 sceSasSetSimpleADSR(u32 core, int voiceNum, u32 ADSREnv1, u32 ADSREnv2) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}
	// This bit could be related to decay type or systain type, but gives an error if you try to set it.
	if ((ADSREnv2 >> 13) & 1) {
		WARN_LOG_REPORT(SCESAS, "sceSasSetSimpleADSR(%08x, %d, %04x, %04x): Invalid ADSREnv2", core, voiceNum, ADSREnv1, ADSREnv2);
		return ERROR_SAS_INVALID_ADSR_CURVE_MODE;
	}

	DEBUG_LOG(SCESAS, "sasSetSimpleADSR(%08x, %i, %08x, %08x)", core, voiceNum, ADSREnv1, ADSREnv2);

	SasVoice &v = sas->voices[voiceNum];
	v.envelope.SetSimpleEnvelope(ADSREnv1 & 0xFFFF, ADSREnv2 & 0xFFFF);
	return 0;
}

u32 sceSasGetEnvelopeHeight(u32 core, int voiceNum) {
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		ERROR_LOG(SCESAS, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	int height = v.envelope.GetHeight();
	DEBUG_LOG(SCESAS, "%i = sceSasGetEnvelopeHeight(%08x, %i)", height, core, voiceNum);
	return height;
}

u32 sceSasRevType(u32 core, int type) {
	DEBUG_LOG(SCESAS, "sceSasRevType(%08x, %i)", core, type);
	sas->waveformEffect.type = type;
	return 0;
}

u32 sceSasRevParam(u32 core, int delay, int feedback) {
	DEBUG_LOG(SCESAS, "sceSasRevParam(%08x, %i, %i)", core, delay, feedback);
	sas->waveformEffect.delay = delay;
	sas->waveformEffect.feedback = feedback;
	return 0;
}

u32 sceSasRevEVOL(u32 core, u32 lv, u32 rv) {
	if (lv > 0x1000 || rv > 0x1000) {
		WARN_LOG_REPORT(SCESAS, "sceSasRevEVOL(%08x, %i, %i): invalid volume", core, lv, rv);
		return ERROR_SAS_REV_INVALID_VOLUME;
	}
	DEBUG_LOG(SCESAS, "sceSasRevEVOL(%08x, %i, %i)", core, lv, rv);
	sas->waveformEffect.leftVol = lv;
	sas->waveformEffect.rightVol = rv;
	return 0;
}

u32 sceSasRevVON(u32 core, int dry, int wet) {
	DEBUG_LOG(SCESAS, "sceSasRevVON(%08x, %i, %i)", core, dry, wet);
	sas->waveformEffect.isDryOn = dry != 0;
	sas->waveformEffect.isWetOn = wet != 0;
	return 0;
}

u32 sceSasGetGrain(u32 core) {
	DEBUG_LOG(SCESAS, "sceSasGetGrain(%08x)", core);
	return sas->GetGrainSize();
}

u32 sceSasSetGrain(u32 core, int grain) {
	INFO_LOG(SCESAS, "sceSasSetGrain(%08x, %i)", core, grain);
	sas->SetGrainSize(grain);
	return 0;
}

u32 sceSasGetOutputMode(u32 core) {
	DEBUG_LOG(SCESAS, "sceSasGetOutputMode(%08x)", core);
	return sas->outputMode;
}

u32 sceSasSetOutputMode(u32 core, u32 outputMode) {
	if (outputMode != 0 && outputMode != 1) {
		ERROR_LOG_REPORT(SCESAS, "sceSasSetOutputMode(%08x, %i): bad output mode", core, outputMode);
		return ERROR_SAS_INVALID_OUTPUT_MODE;
	}
	DEBUG_LOG(SCESAS, "sceSasSetOutputMode(%08x, %i)", core, outputMode);
	sas->outputMode = outputMode;

	return 0;
}

u32 sceSasGetAllEnvelopeHeights(u32 core, u32 heightsAddr) {
	DEBUG_LOG(SCESAS, "sceSasGetAllEnvelopeHeights(%08x, %i)", core, heightsAddr);

	if (!Memory::IsValidAddress(heightsAddr)) {
		return ERROR_SAS_INVALID_PARAMETER;
	}

	for (int i = 0; i < PSP_SAS_VOICES_MAX; i++) {
		int voiceHeight = sas->voices[i].envelope.GetHeight();
		Memory::Write_U32(voiceHeight, heightsAddr + i * 4);
	}

	return 0;
}

u32 sceSasSetTriangularWave(u32 sasCore, int voice, int unknown) {
	ERROR_LOG_REPORT(SCESAS, "UNIMPL sceSasSetTriangularWave(%08x, %i, %i)", sasCore, voice, unknown);
	return 0;
}

u32 sceSasSetSteepWave(u32 sasCore, int voice, int unknown) {
	ERROR_LOG_REPORT(SCESAS, "UNIMPL sceSasSetSteepWave(%08x, %i, %i)", sasCore, voice, unknown);
	return 0;
}

u32 __sceSasSetVoiceATRAC3(u32 core, int voiceNum, u32 atrac3Context) {
	DEBUG_LOG_REPORT(SCESAS, "__sceSasSetVoiceATRAC3(%08x, %i, %08x)", core, voiceNum, atrac3Context);
	SasVoice &v = sas->voices[voiceNum];
	v.type = VOICETYPE_ATRAC3;
	v.loop = false;
	v.playing = true;
	v.atrac3.setContext(atrac3Context);
	Memory::Write_U32(atrac3Context, core + 56 * voiceNum + 20);
	return 0;
}

u32 __sceSasConcatenateATRAC3(u32 core, int voiceNum, u32 atrac3DataAddr, int atrac3DataLength) {
	DEBUG_LOG_REPORT(SCESAS, "__sceSasConcatenateATRAC3(%08x, %i, %08x, %i)", core, voiceNum, atrac3DataAddr, atrac3DataLength);
	SasVoice &v = sas->voices[voiceNum];
	if (Memory::IsValidAddress(atrac3DataAddr))
		v.atrac3.addStreamData(Memory::GetPointer(atrac3DataAddr), atrac3DataLength);
	return 0;
}

u32 __sceSasUnsetATRAC3(u32 core, int voiceNum) {
	DEBUG_LOG_REPORT(SCESAS, "__sceSasUnsetATRAC3(%08x, %i)", core, voiceNum);
	Memory::Write_U32(0, core + 56 * voiceNum + 20);
	return 0;
}

const HLEFunction sceSasCore[] =
{
	{0x42778a9f, WrapU_UUUUU<sceSasInit>, "__sceSasInit"},
	{0xa3589d81, WrapU_UU<_sceSasCore>, "__sceSasCore"},
	{0x50a14dfc, WrapU_UUII<_sceSasCoreWithMix>, "__sceSasCoreWithMix"},
	{0x68a46b95, WrapU_U<sceSasGetEndFlag>, "__sceSasGetEndFlag"},
	{0x440ca7d8, WrapU_UIIIII<sceSasSetVolume>, "__sceSasSetVolume"},
	{0xad84d37f, WrapU_UII<sceSasSetPitch>, "__sceSasSetPitch"},
	{0x99944089, WrapU_UIUII<sceSasSetVoice>, "__sceSasSetVoice"},
	{0xb7660a23, WrapU_UII<sceSasSetNoise>, "__sceSasSetNoise"},
	{0x019b25eb, WrapU_UIIIIII<sceSasSetADSR>, "__sceSasSetADSR"},
	{0x9ec3676a, WrapU_UIIIIII<sceSasSetADSRMode>, "__sceSasSetADSRmode"},
	{0x5f9529f6, WrapU_UII<sceSasSetSL>, "__sceSasSetSL"},
	{0x74ae582a, WrapU_UI<sceSasGetEnvelopeHeight>, "__sceSasGetEnvelopeHeight"},
	{0xcbcd4f79, WrapU_UIUU<sceSasSetSimpleADSR>, "__sceSasSetSimpleADSR"},
	{0xa0cf2fa4, WrapU_UI<sceSasSetKeyOff>, "__sceSasSetKeyOff"},
	{0x76f01aca, WrapU_UI<sceSasSetKeyOn>, "__sceSasSetKeyOn"},
	{0xf983b186, WrapU_UII<sceSasRevVON>, "__sceSasRevVON"},
	{0xd5a229c9, WrapU_UUU<sceSasRevEVOL>, "__sceSasRevEVOL"},
	{0x33d4ab37, WrapU_UI<sceSasRevType>, "__sceSasRevType"},
	{0x267a6dd2, WrapU_UII<sceSasRevParam>, "__sceSasRevParam"},
	{0x2c8e6ab3, WrapU_U<sceSasGetPauseFlag>, "__sceSasGetPauseFlag"},
	{0x787d04d5, WrapU_UUI<sceSasSetPause>, "__sceSasSetPause"},
	{0xa232cbe6, WrapU_UII<sceSasSetTriangularWave>, "__sceSasSetTrianglarWave"}, // Typo.
	{0xd5ebbbcd, WrapU_UII<sceSasSetSteepWave>, "__sceSasSetSteepWave"},
	{0xBD11B7C2, WrapU_U<sceSasGetGrain>, "__sceSasGetGrain"},
	{0xd1e0a01e, WrapU_UI<sceSasSetGrain>, "__sceSasSetGrain"},
	{0xe175ef66, WrapU_U<sceSasGetOutputMode>, "__sceSasGetOutputmode"},
	{0xe855bf76, WrapU_UU<sceSasSetOutputMode>, "__sceSasSetOutputmode"},
	{0x07f58c24, WrapU_UU<sceSasGetAllEnvelopeHeights>, "__sceSasGetAllEnvelopeHeights"},
	{0xE1CD9561, WrapU_UIUII<sceSasSetVoicePCM>, "__sceSasSetVoicePCM"},
	{0x4AA9EAD6, WrapU_UIU<__sceSasSetVoiceATRAC3>,"__sceSasSetVoiceATRAC3"},
	{0x7497EA85, WrapU_UIUI<__sceSasConcatenateATRAC3>,"__sceSasConcatenateATRAC3"},
	{0xF6107F00, WrapU_UI<__sceSasUnsetATRAC3>,"__sceSasUnsetATRAC3"},
};

void Register_sceSasCore()
{
	RegisterModule("sceSasCore", ARRAY_SIZE(sceSasCore), sceSasCore);
}

