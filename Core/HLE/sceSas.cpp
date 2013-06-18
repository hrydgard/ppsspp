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

#include <cstdlib>
#include "base/basictypes.h"
#include "Log.h"
#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../HW/SasAudio.h"
#include "Core/Reporting.h"

#include "sceSas.h"
#include "sceKernel.h"

enum {
	ERROR_SAS_INVALID_GRAIN = 0x80420001,
	ERROR_SAS_INVALID_MAX_VOICES = 0x80420002,
	ERROR_SAS_INVALID_OUTPUT_MODE = 0x80420003,
	ERROR_SAS_INVALID_SAMPLE_RATE = 0x80420004,
	ERROR_SAS_BAD_ADDRESS = 0x80420005,
	ERROR_SAS_INVALID_VOICE = 0x80420010,
	ERROR_SAS_INVALID_ADSR_CURVE_MODE = 0x80420013,
	ERROR_SAS_INVALID_PARAMETER = 0x80420014,
	ERROR_SAS_VOICE_PAUSED = 0x80420016,
	ERROR_SAS_INVALID_VOLUME = 0x80420018,
	ERROR_SAS_INVALID_SIZE = 0x8042001A,
	ERROR_SAS_BUSY = 0x80420030,
	ERROR_SAS_NOT_INIT = 0x80420100,
};

// TODO - allow more than one, associating each with one Core pointer (passed in to all the functions)
// No known games use more than one instance of Sas though.
static SasInstance *sas = NULL;

void __SasInit() {
	sas = new SasInstance();
}

void __SasDoState(PointerWrap &p) {
	p.DoClass(sas);
	p.DoMarker("sceSas");
}

void __SasShutdown() {
	delete sas;
	sas = 0;
}


u32 sceSasInit(u32 core, u32 grainSize, u32 maxVoices, u32 outputMode, u32 sampleRate) {
	if (!Memory::IsValidAddress(core) || (core & 0x3F) != 0) {
		ERROR_LOG_REPORT(HLE, "sceSasInit(%08x, %i, %i, %i, %i): bad core address", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_BAD_ADDRESS;
	}
	if (maxVoices == 0 || maxVoices > PSP_SAS_VOICES_MAX) {
		ERROR_LOG_REPORT(HLE, "sceSasInit(%08x, %i, %i, %i, %i): bad max voices", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_INVALID_MAX_VOICES;
	}
	if (grainSize < 0x40 || grainSize > 0x800 || (grainSize & 0x1F) != 0) {
		ERROR_LOG_REPORT(HLE, "sceSasInit(%08x, %i, %i, %i, %i): bad grain size", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_INVALID_GRAIN;
	}
	if (outputMode != 0 && outputMode != 1) {
		ERROR_LOG_REPORT(HLE, "sceSasInit(%08x, %i, %i, %i, %i): bad output mode", core, grainSize, maxVoices, outputMode, sampleRate);
		return ERROR_SAS_INVALID_OUTPUT_MODE;
	}
	INFO_LOG(HLE, "sceSasInit(%08x, %i, %i, %i, %i)", core, grainSize, maxVoices, outputMode, sampleRate);

	sas->SetGrainSize(grainSize);
	// Seems like maxVoiecs is actually ignored for all intents and purposes.
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

	DEBUG_LOG(HLE,"sceSasGetEndFlag(%08x)", endFlag);
	return endFlag;
}

// Runs the mixer
u32 _sceSasCore(u32 core, u32 outAddr) {
	DEBUG_LOG(HLE,"sceSasCore(%08x, %08x)", core, outAddr);

	if (!Memory::IsValidAddress(outAddr)) {
		return ERROR_SAS_INVALID_PARAMETER;
	}

	sas->Mix(outAddr);
	// Actual delay time seems to between 240 and 1000 us, based on grain and possibly other factors.
	// Let's aim low for now.
	return hleDelayResult(0, "sas core", 240);
}

// Another way of running the mixer, the inoutAddr should be both input and output
u32 _sceSasCoreWithMix(u32 core, u32 inoutAddr, int leftVolume, int rightVolume) {
	DEBUG_LOG(HLE,"sceSasCoreWithMix(%08x, %08x, %i, %i)", core , inoutAddr, leftVolume, rightVolume);

	if (!Memory::IsValidAddress(inoutAddr)) {
		return ERROR_SAS_INVALID_PARAMETER;
	}

	sas->Mix(inoutAddr, inoutAddr, leftVolume, rightVolume);
	// Actual delay time seems to between 240 and 1000 us, based on grain and possibly other factors.
	// Let's aim low for now.
	return hleDelayResult(0, "sas core", 240);
}

u32 sceSasSetVoice(u32 core, int voiceNum, u32 vagAddr, int size, int loop) {
	DEBUG_LOG(HLE,"sceSasSetVoice(%08x, %i, %08x, %i, %i)", core, voiceNum, vagAddr, size, loop);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	if (size <= 0 || (size & 0xF) != 0) {
		if (size == 0) {
			DEBUG_LOG(HLE, "%s: invalid size %d", __FUNCTION__, size);
		} else {
			WARN_LOG(HLE, "%s: invalid size %d", __FUNCTION__, size);
		}
		return ERROR_SAS_INVALID_SIZE;
	}

	if (!Memory::IsValidAddress(vagAddr)) {
		ERROR_LOG(HLE, "Ignoring invalid VAG audio address %08x", vagAddr);
		return 0;
	}

	//Real VAG header is 0x30 bytes behind the vagAddr
	SasVoice &v = sas->voices[voiceNum];
	u32 prevVagAddr = v.vagAddr;
	v.type = VOICETYPE_VAG;
	v.vagAddr = vagAddr;
	v.vagSize = size;
	v.loop = loop ? true : false;
	v.ChangedParams(vagAddr == prevVagAddr);
	return 0;
}

u32 sceSasSetVoicePCM(u32 core, int voiceNum, u32 pcmAddr, int size, int loop)
{
	INFO_LOG_REPORT(HLE, "sceSasSetVoicePCM(%08x, %i, %08x, %i, %i)", core, voiceNum, pcmAddr, size, loop);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	if (!Memory::IsValidAddress(pcmAddr)) {
		ERROR_LOG(HLE, "Ignoring invalid PCM audio address %08x", pcmAddr);
		return 0;
	}

	if (size <= 0 || size > 0x10000) {
		WARN_LOG(HLE, "%s: invalid size %d", __FUNCTION__, size);
		return ERROR_SAS_INVALID_SIZE;
	}

	SasVoice &v = sas->voices[voiceNum];
	u32 prevPcmAddr = v.pcmAddr;
	v.type = VOICETYPE_PCM;
	v.pcmAddr = pcmAddr;
	v.pcmSize = size;
	v.pcmIndex = 0;
	v.loop = loop ? true : false;
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

	DEBUG_LOG(HLE,"sceSasGetPauseFlag(%08x)", pauseFlag)
	return pauseFlag;
}

u32 sceSasSetPause(u32 core, u32 voicebit, int pause) {
	DEBUG_LOG(HLE,"sceSasSetPause(%08x, %08x, %i)", core, voicebit, pause);

	for (int i = 0; voicebit != 0; i++, voicebit >>= 1) {
		if (i < PSP_SAS_VOICES_MAX && i >= 0) {
			if ((voicebit & 1) != 0)
				sas->voices[i].paused = pause ? true : false;
		}
	}

	return 0;
}

u32 sceSasSetVolume(u32 core, int voiceNum, int leftVol, int rightVol, int effectLeftVol, int effectRightVol) {
	DEBUG_LOG(HLE,"sceSasSetVolume(%08x, %i, %i, %i, %i, %i)", core, voiceNum, leftVol, rightVol, effectLeftVol, effectRightVol);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
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
	DEBUG_LOG(HLE,"sceSasSetPitch(%08x, %i, %i)", core, voiceNum, pitch);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	v.pitch = pitch;
	v.ChangedParams(false);
	return 0;
}

u32 sceSasSetKeyOn(u32 core, int voiceNum) {
	DEBUG_LOG(HLE,"sceSasSetKeyOn(%08x, %i)", core, voiceNum);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
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
	if (voiceNum == -1) {
		// TODO: Some games (like Every Extend Extra) deliberately pass voiceNum = -1. Does that mean all voices? for now let's ignore.
		DEBUG_LOG(HLE,"sceSasSetKeyOff(%08x, %i) - voiceNum = -1???", core, voiceNum);
		return 0;
	} else if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0) {
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	} else {
		DEBUG_LOG(HLE,"sceSasSetKeyOff(%08x, %i)", core, voiceNum);

		if (sas->voices[voiceNum].paused || !sas->voices[voiceNum].on) {
			return ERROR_SAS_VOICE_PAUSED;
		}

		SasVoice &v = sas->voices[voiceNum];
		v.KeyOff();
		return 0;
	}
}

u32 sceSasSetNoise(u32 core, int voiceNum, int freq) {
	DEBUG_LOG(HLE,"sceSasSetNoise(%08x, %i, %i)", core, voiceNum, freq);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	v.type = VOICETYPE_NOISE;
	v.noiseFreq = freq;
	v.ChangedParams(true);
	return 0;
}

u32 sceSasSetSL(u32 core, int voiceNum, int level) {
	DEBUG_LOG(HLE,"sceSasSetSL(%08x, %i, %i)", core, voiceNum, level);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	v.envelope.sustainLevel = level;
	return 0;
}

u32 sceSasSetADSR(u32 core, int voiceNum, int flag , int a, int d, int s, int r) {
	DEBUG_LOG(HLE,"0=sceSasSetADSR(%08x, %i, %i, %08x, %08x, %08x, %08x)",core, voiceNum, flag, a, d, s, r)

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	if ((flag & 0x1) != 0) v.envelope.attackRate  = a;
	if ((flag & 0x2) != 0) v.envelope.decayRate   = d;
	if ((flag & 0x4) != 0) v.envelope.sustainRate = s;
	if ((flag & 0x8) != 0) v.envelope.releaseRate = r;
	return 0;
}

u32 sceSasSetADSRMode(u32 core, int voiceNum,int flag ,int a, int d, int s, int r) {
	DEBUG_LOG(HLE,"sceSasSetADSRMode(%08x, %i, %i, %08x, %08x, %08x, %08x)",core, voiceNum, flag, a,d,s,r)

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	if ((flag & 0x1) != 0) v.envelope.attackType  = a;
	if ((flag & 0x2) != 0) v.envelope.decayType   = d;
	if ((flag & 0x4) != 0) v.envelope.sustainType = s;
	if ((flag & 0x8) != 0) v.envelope.releaseType = r;
	return 0 ;
}


u32 sceSasSetSimpleADSR(u32 core, int voiceNum, u32 ADSREnv1, u32 ADSREnv2) {
	DEBUG_LOG(HLE,"sasSetSimpleADSR(%08x, %i, %08x, %08x)", core, voiceNum, ADSREnv1, ADSREnv2);

	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	v.envelope.SetSimpleEnvelope(ADSREnv1 & 0xFFFF, ADSREnv2 & 0xFFFF);
	return 0;
}

u32 sceSasGetEnvelopeHeight(u32 core, int voiceNum) {
	DEBUG_LOG(HLE,"sceSasGetEnvelopeHeight(%08x, %i)", core, voiceNum);
	
	if (voiceNum >= PSP_SAS_VOICES_MAX || voiceNum < 0)	{
		WARN_LOG(HLE, "%s: invalid voicenum %d", __FUNCTION__, voiceNum);
		return ERROR_SAS_INVALID_VOICE;
	}

	SasVoice &v = sas->voices[voiceNum];
	return v.envelope.GetHeight();
}

u32 sceSasRevType(u32 core, int type) {
	DEBUG_LOG(HLE,"sceSasRevType(%08x, %i)", core, type);
	sas->waveformEffect.type = type;
	return 0;
}

u32 sceSasRevParam(u32 core, int delay, int feedback) {
	DEBUG_LOG(HLE,"sceSasRevParam(%08x, %i, %i)", core, delay, feedback);
	sas->waveformEffect.delay = delay;
	sas->waveformEffect.feedback = feedback;
	return 0;
}

u32 sceSasRevEVOL(u32 core, int lv, int rv) {
	DEBUG_LOG(HLE,"sceSasRevEVOL(%08x, %i, %i)", core, lv, rv);
	sas->waveformEffect.leftVol = lv;
	sas->waveformEffect.rightVol = rv;
	return 0;
}

u32 sceSasRevVON(u32 core, int dry, int wet) {
	DEBUG_LOG(HLE,"sceSasRevVON(%08x, %i, %i)", core, dry, wet);
	sas->waveformEffect.isDryOn = dry & 1;
	sas->waveformEffect.isWetOn = wet & 1;
	return 0;
}

u32 sceSasGetGrain(u32 core) {
	DEBUG_LOG(HLE,"sceSasGetGrain(%08x)", core);
	return sas->GetGrainSize();
}

u32 sceSasSetGrain(u32 core, int grain) {
	INFO_LOG(HLE,"sceSasSetGrain(%08x, %i)", core, grain);
	sas->SetGrainSize(grain);
	return 0;
}

u32 sceSasGetOutputMode(u32 core) {
	DEBUG_LOG(HLE,"sceSasGetOutputMode(%08x)", core);
	return sas->outputMode;
}

u32 sceSasSetOutputMode(u32 core, u32 outputMode) {
	DEBUG_LOG(HLE,"sceSasSetOutputMode(%08x, %i)", core, outputMode);
	sas->outputMode = outputMode;
	return 0;
}


u32 sceSasGetAllEnvelopeHeights(u32 core, u32 heightsAddr) {
	DEBUG_LOG(HLE,"sceSasGetAllEnvelopeHeights(%08x, %i)", core, heightsAddr);

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
	ERROR_LOG_REPORT(HLE, "UNIMPL sceSasSetTriangularWave(%08x, %i, %i)", sasCore, voice, unknown);
	return 0;
}

u32 sceSasSetSteepWave(u32 sasCore, int voice, int unknown) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceSasSetSteepWave(%08x, %i, %i)", sasCore, voice, unknown);
	return 0;
}

u32 __sceSasSetVoiceATRAC3(u32 core, int voiceNum, u32 atrac3Context) {
	INFO_LOG_REPORT(HLE, "__sceSasSetVoiceATRAC3(%08x, %i, %08x)", core, voiceNum, atrac3Context);
	SasVoice &v = sas->voices[voiceNum];
	v.type = VOICETYPE_ATRAC3;
	v.loop = false;
	v.playing = true;
	v.atrac3.setContext(atrac3Context);
	Memory::Write_U32(atrac3Context, core + 56 * voiceNum + 20);
	return 0;
}

u32 __sceSasConcatenateATRAC3(u32 core, int voiceNum, u32 atrac3DataAddr, int atrac3DataLength) {
	INFO_LOG_REPORT(HLE, "__sceSasConcatenateATRAC3(%08x, %i, %08x, %i)", core, voiceNum, atrac3DataAddr, atrac3DataLength);
	SasVoice &v = sas->voices[voiceNum];
	if (Memory::IsValidAddress(atrac3DataAddr))
		v.atrac3.addStreamData(Memory::GetPointer(atrac3DataAddr), atrac3DataLength);
	return 0;
}

u32 __sceSasUnsetATRAC3(u32 core, int voiceNum) {
	INFO_LOG_REPORT(HLE, "__sceSasUnsetATRAC3(%08x, %i)", core, voiceNum);
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
	{0xd5a229c9, WrapU_UII<sceSasRevEVOL>, "__sceSasRevEVOL"},  
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

