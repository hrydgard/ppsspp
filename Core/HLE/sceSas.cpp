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
// JPCSP is, as it often is, a pretty good reference although I didn't actually use it much yet:
// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/sceSasCore.java

#include "base/basictypes.h"

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceSas.h"
#include "sceKernel.h"

static const int PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE=0;
static const int PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE=1;
static const int PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT=2;
static const int PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE=3;
static const int PSP_SAS_ADSR_CURVE_MODE_EXPONENT_INCREASE=4;
static const int PSP_SAS_ADSR_CURVE_MODE_DIRECT= 5;

static const int PSP_SAS_ADSR_ATTACK=1;
static const int PSP_SAS_ADSR_DECAY=2;
static const int PSP_SAS_ADSR_SUSTAIN=4;
static const int PSP_SAS_ADSR_RELEASE=8;


struct WaveformEffect
{
	int type;
	int delay;
	int feedback ;
	int leftVol;
	int rightVol;
	int isDryOn;
	int isWetOn;
};

static const double f[5][2] = 
{ { 0.0, 0.0 },
{	 60.0 / 64.0,	0.0 },
{	115.0 / 64.0, -52.0 / 64.0 },
{	 98.0 / 64.0, -55.0 / 64.0 },
{	122.0 / 64.0, -60.0 / 64.0 } };

// VAG is a Sony ADPCM audio compression format, which goes all the way back to the PSX.
// It compresses 28 16-bit samples into a block of 16 bytes.
// TODO: Get rid of the doubles, making sure it does not impact sound quality.
// Doubles are pretty fast on Android devices these days though.
class VagDecoder
{
public:
	void Start(u8 *data)
	{
		data_ = data;
		curSample = 28;
		s_1 = 0.0;	// per block?
		s_2 = 0.0;
	}

	int GetSample()
	{
		if (end_)
			return 0;
		if (curSample == 28)
			Decode();
		if (end_)
			return 0;
		return samples[curSample++];
	}

	bool Decode();
	bool End() const { return end_; }

	u8 GetByte() {
		return *data_++;
	}

private:
	double samples[28];
	int curSample;

	u8 *data_;

	// rolling state. start at 0, should probably reset to 0 on loops?
	double s_1;
	double s_2;

	bool end_;
};

bool VagDecoder::Decode()
{
	int predict_nr = GetByte();
	int shift_factor = predict_nr & 0xf;
	predict_nr >>= 4;
	int flags = GetByte();
	if (flags == 7)
	{
		end_ = true;
		return false;
	}
	for (int i = 0; i < 28; i += 2) 
	{
		int d = GetByte();
		int s = (d & 0xf) << 12;
		if (s & 0x8000)
			s |= 0xffff0000;
		samples[i] = (double)(s >> shift_factor);
		s = (d & 0xf0) << 8;
		if (s & 0x8000)
			s |= 0xffff0000;
		samples[i + 1] = (double)(s >> shift_factor);
	}
	for (int i = 0; i < 28; i++)
	{
		samples[i] = samples[i] + s_1 * f[predict_nr][0] + s_2 * f[predict_nr][1];
		s_2 = s_1;
		s_1 = samples[i];
	}
	curSample = 0;
	return true;
}

// A SAS voice.
// TODO: Look into pre-decoding the VAG samples on SetVoice instead of decoding them on the fly.
// It's not very likely that games encode VAG dynamically.
struct Voice
{
	u32 vagAddr;
	u32 pcmAddr;
	int samplePos;
	int size;
	int loop;
	int freq;  //units?
	int volumeLeft;
	int volumeRight;
	int volumeLeftSend;	// volume to "Send" (audio-lingo) to the effects processing engine, like reverb
	int volumeRightSend;
	int attackRate;
	int decayRate;
	int sustainRate;
	int releaseRate;
	int attackType;
	int decayType;
	int sustainType;
	int sustainLevel;
	int releaseType;
	int pitch;
	int setPaused;
	int height;
	bool playing;

	VagDecoder vag;
};

class SasInstance
{
public:
	enum { NUM_VOICES = 32 };
	Voice voices[NUM_VOICES];
	WaveformEffect waveformEffect;
	int grainSize;
	int maxVoices;
	int sampleRate;
	int outputMode;
	int length;

	void mix(u32 outAddr);
};

// TODO - allow more than one, associating each with one Core pointer (passed in to all the functions)
// No known games use more than one instance of Sas though.
SasInstance sas;	

// TODO: Make deterministic, by adding staging buffers that we pump out on a fixed CoreTiming-scheduled interval.

void SasInstance::mix(u32 outAddr)
{
	s16 *out = (s16 *)Memory::GetPointer(outAddr);
	// Don't need to memset, done by the caller.
	for (int v = 0; v < NUM_VOICES; v++)	 // sas.maxVoices?
	{
		Voice &voice = sas.voices[v];

		if (voice.playing && voice.vagAddr != 0)
		{
			for (int i = 0; i < sas.grainSize; i++)
			{
				int sample = voice.vag.GetSample();
				voice.samplePos++;
				if (voice.samplePos >= voice.size || voice.vag.End())
				{
					voice.playing = false;
					break;
				}
				int l = sample;
				int r = sample; //* (voice.volumeLeft >> 16), r = sample * (voice.volumeRight >> 16);

				// TODO: should mix into a temporary 32-bit buffer and then clip down
				out[i * 2] += l;
				out[i * 2 + 1] += r;
			}
		}
	}
}

u32 sceSasInit(u32 core, u32 grainSize, u32 maxVoices, u32 outputMode, u32 sampleRate)
{
	DEBUG_LOG(HLE,"0=sceSasInit()");
	memset(&sas, 0, sizeof(sas));
	sas.grainSize = grainSize;
	sas.maxVoices = maxVoices;
	sas.sampleRate = sampleRate;
	sas.outputMode = outputMode;
	for (int i = 0; i < 32; i++) {
		sas.voices[i].playing = false;
	}
	return 0;
}

u32 sceSasGetEndFlag(u32 core)
{
	u32 endFlag = 0;
	for (int i = 0; i < sas.maxVoices; i++) {
		if (!sas.voices[i].playing)
			endFlag |= 1 << i;
	}
	DEBUG_LOG(HLE,"%08x=sceSasGetEndFlag()", endFlag);
	return endFlag;
}

// Runs the mixer
void _sceSasCore(u32 core, u32 outAddr)
{
	DEBUG_LOG(HLE,"0=sceSasCore(, %08x)	(grain: %i samples)", outAddr, sas.grainSize);
	Memory::Memset(outAddr, 0, sas.grainSize * 2 * 2);
	sas.mix(outAddr);
	RETURN(0);
}

// Another way of running the mixer, what was the difference again?
void _sceSasCoreWithMix(u32 core, u32 outAddr)
{
	DEBUG_LOG(HLE,"0=sceSasCoreWithMix(, %08x)", outAddr);
	sas.mix(outAddr);
	RETURN(0);
}

void sceSasSetVoice(u32 core, int voiceNum, u32 vagAddr, int size, int loop)
{
	DEBUG_LOG(HLE,"0=sceSasSetVoice(core=%08x, voicenum=%i, vag=%08x, size=%i, loop=%i)", 
		core, voiceNum, vagAddr, size, loop);

	//Real VAG header is 0x30 bytes behind the vagAddr
	Voice &v = sas.voices[voiceNum];
	v.vagAddr = vagAddr;
	v.size = size;
	v.loop = loop;
	v.playing = false;
	RETURN(0);
}

u32 sceSasSetPause(u32 core, int voicebit, int pause)
{
	DEBUG_LOG(HLE,"0=sceSasSetPause(core=%08x, voicebit=%08x, pause=%i)", core, voicebit, pause);
	for (int i = 0; voicebit != 0; i++, voicebit >>= 1)
	{
		if (i < SasInstance::NUM_VOICES && i >= 0)
		{
			if ((voicebit & 1) != 0)
				sas.voices[i].setPaused=pause;
		}
		// TODO: Correct error code?  Mimana crashes otherwise.
		else
			return -1;
	}

	return 0;
}

void sceSasSetVolume(u32 core, int voiceNum, int l, int r, int el, int er)
{
	DEBUG_LOG(HLE,"0=sceSasSetVolume(core=%08x, voiceNum=%i, l=%i, r=%i, el=%i, er=%i", core, voiceNum, l, r, el, er);
	Voice &v = sas.voices[voiceNum];
	v.volumeLeft = l;
	v.volumeRight = r;
	RETURN(0);
}

void sceSasSetPitch(u32 core, int voiceNum, int pitch)
{
	Voice &v = sas.voices[voiceNum];
	v.pitch = pitch;
	DEBUG_LOG(HLE,"0=sceSasSetPitch(core=%08x, voiceNum=%i, pitch=%i)", core, voiceNum, pitch);
	RETURN(0);
}

void sceSasSetKeyOn(u32 core, int voiceNum)
{
	DEBUG_LOG(HLE,"0=sceSasSetKeyOn(core=%08x, voiceNum=%i)", core, voiceNum);
	Voice &v = sas.voices[voiceNum];
	v.vag.Start(Memory::GetPointer(v.vagAddr));
	v.playing = true;
	RETURN(0);
}

// TODO: We really need ADSR work: 
// sceSasSetKeyOff can be used to start sounds, that just sound during the Release phase!
void sceSasSetKeyOff(u32 core, int voiceNum)
{
	DEBUG_LOG(HLE,"0=sceSasSetKeyOff(core=%08x, voiceNum=%i)", core, voiceNum);
	Voice &v = sas.voices[voiceNum];
	v.playing = false;	// not right! Should directly enter Release envelope stage instead!
	RETURN(0);
}

u32 sceSasSetNoise(u32 core, int voiceNum, int freq)
{
	DEBUG_LOG(HLE,"0=sceSasSetNoise(core=%08x, voiceNum=%i, freq=%i)", core, voiceNum, freq);
	Voice &v = sas.voices[voiceNum];
	v.freq = freq;
	return 0;
}

u32 sceSasSetSL(u32 core, int voiceNum, int level)
{
	DEBUG_LOG(HLE,"0=sceSasSetSL(core=%08x, voiceNum=%i, level=%i)", core, voiceNum, level);
	Voice &v = sas.voices[voiceNum];
	v.sustainLevel = level;
	return 0;
}

u32 sceSasSetADSR(u32 core, int voiceNum,int flag ,int a, int d, int s, int r)
{
	DEBUG_LOG(HLE,"0=sceSasSetADSR(core=%08x, voicenum=%i, flag=%i, a=%08x, d=%08x, s=%08x, r=%08x)",core, voiceNum, flag, a,d,s,r)
	Voice &v = sas.voices[voiceNum];
	if ((flag & 0x1) != 0) v.attackRate  = a;
	if ((flag & 0x2) != 0) v.decayRate   = d;
	if ((flag & 0x4) != 0) v.sustainRate = s;
	if ((flag & 0x8) != 0) v.releaseRate = r;
	return 0;
}

u32 sceSasSetADSRMode(u32 core, int voiceNum,int flag ,int a, int d, int s, int r)
{
	DEBUG_LOG(HLE,"0=sceSasSetADSRMode(core=%08x, voicenum=%i, flag=%i, a=%08x, d=%08x, s=%08x, r=%08x)",core, voiceNum, flag, a,d,s,r)
	Voice &v = sas.voices[voiceNum];
	if ((flag & 0x1) != 0) v.attackType  = a;
	if ((flag & 0x2) != 0) v.decayType   = d;
	if ((flag & 0x4) != 0) v.sustainType = s;
	if ((flag & 0x8) != 0) v.releaseType = r;
	return 0 ;
}

// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/sceSasCore.java

int simpleRate(int n) {
	n &= 0x7F;
	if (n == 0x7F) {
		return 0;
	}
	int rate = ((7 - (n & 0x3)) << 26) >> (n >> 2);
	if (rate == 0) {
		return 1;
	}
	return rate;
}

int attackRate(int bitfield1) {
	return simpleRate(bitfield1 >> 8);
}

int attackType(int bitfield1) {
	return (bitfield1 & 0x8000) == 0 ? PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE : PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT;
}

int decayRate(int bitfield1) {
	return 0x80000000 >> ((bitfield1 >> 4) & 0x000F);
}

int sustainRate(int bitfield2) {
	return simpleRate(bitfield2 >> 6);
}

int sustainType(int bitfield2) {
	switch (bitfield2 >> 13) {
		case 0: return PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE;
		case 2: return PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE;
		case 4: return PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT;
		case 6: return PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE;
	}
	ERROR_LOG(HLE,"sasSetSimpleADSR,ERROR_SAS_INVALID_ADSR_CURVE_MODE");
	return 0;
}

int releaseType(int bitfield2) {
	return (bitfield2 & 0x0020) == 0 ? PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE : PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE;
}

int releaseRate(int bitfield2) {
	int n = bitfield2 & 0x001F;
	if (n == 31) {
		return 0;
	}
	if (releaseType(bitfield2) == PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE) {
		return (0x40000000 >> (n + 2));
	}
	return (0x8000000 >> n);
}

int sustainLevel(int bitfield1) {
	return ((bitfield1 & 0x000F) + 1) << 26;
}

u32 sceSasSetSimpleADSR(u32 core, u32 voiceNum, u32 ADSREnv1, u32 ADSREnv2)
{
	DEBUG_LOG(HLE,"0=sasSetSimpleADSR(%08x, %i, %08x, %08x)", core, voiceNum, ADSREnv1, ADSREnv2);
	ADSREnv1 &= 0xFFFF;
	ADSREnv2 &= 0xFFFF;
	Voice &v 	= sas.voices[voiceNum];
	v.attackRate 	= attackRate(ADSREnv1);
	v.attackType 	= attackType(ADSREnv1);
	v.decayRate 	= decayRate(ADSREnv1);
	v.decayType 	= PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE;
	v.sustainRate 	= sustainRate(ADSREnv2);
	v.sustainType 	= sustainType(ADSREnv2);
	v.releaseRate 	= releaseRate(ADSREnv2);
	v.releaseType 	= releaseType(ADSREnv2);
	v.sustainLevel 	= sustainLevel(ADSREnv1);
	return 0;
}

u32 sceSasGetEnvelopeHeight(u32 core, u32 voiceNum)
{
	// Spam reduction
	if (voiceNum == 17)
	{
		DEBUG_LOG(HLE,"UNIMPL 0=sceSasGetEnvelopeHeight(core=%08x, voicenum=%i)", core, voiceNum);
	}
	Voice &v = sas.voices[voiceNum];

	return v.playing ? 0x3fffffff : 0;
}

void sceSasRevType(u32 core, int type)
{
	DEBUG_LOG(HLE,"0=sceSasRevType(core=%08x, type=%i)", core, type);

	sas.waveformEffect.type=type;
	RETURN(0);
}

void sceSasRevParam(u32 core, int delay, int feedback)
{
	DEBUG_LOG(HLE,"0=sceSasRevParam(core=%08x, delay=%i, feedback=%i)", core, delay, feedback);

	sas.waveformEffect.delay = delay;
	sas.waveformEffect.feedback = feedback;
	RETURN(0);
}

u32 sceSasGetPauseFlag(u32 core)
{
	u32 PauseFlag = 0;
	for (int i = 0; i < sas.maxVoices; i++) {
		if (!sas.voices[i].playing)
			PauseFlag |= 1 << i;
	}
	DEBUG_LOG(HLE,"%08x=sceSasGetPauseFlag()", PauseFlag);
	return PauseFlag;
}

void sceSasRevEVOL(u32 core, int lv, int rv)
{
	DEBUG_LOG(HLE,"0=sceSasRevEVOL(core=%08x, leftVolume=%i, rightVolume=%i)", core, lv, rv);

	sas.waveformEffect.leftVol = lv;
	sas.waveformEffect.rightVol = rv;
	RETURN(0);
}

void sceSasRevVON(u32 core, int dry, int wet)
{
	DEBUG_LOG(HLE,"0=sceSasRevVON(core=%08x, dry=%i, wet=%i)", core, dry, wet);

	sas.waveformEffect.isDryOn = (dry > 0);
	sas.waveformEffect.isWetOn = (wet > 0);
	RETURN(0);
}

u32 sceSasGetGrain(u32 core)
{
	DEBUG_LOG(HLE,"0=sceSasGetGrain(core=%08x)", core);
	return sas.grainSize;

}

u32 sceSasSetGrain(u32 core, int grain)
{
	DEBUG_LOG(HLE,"0=sceSasSetGrain(core=%08x, grain=%i)", core, grain);
	sas.grainSize = grain;
	return 0;
}

u32 sceSasGetOutputMode(u32 core)
{
	DEBUG_LOG(HLE,"0=sceSasGetOutputMode(core=%08x)", core);
	return sas.outputMode;
}

u32 sceSasSetOutputMode(u32 core, u32 outputMode)
{
	DEBUG_LOG(HLE,"0=sceSasSetOutputMode(core=%08x, outputMode=%i)", core, outputMode);
	sas.outputMode = outputMode;
	return 0;
}

void sceSasSetVoicePCM(u32 core, int voiceNum, u32 pcmAddr, int size, int loop)
{
	DEBUG_LOG(HLE,"0=sceSasSetVoicePCM(core=%08x, voicenum=%i, pcmAddr=%08x, size=%i, loop=%i)",core, voiceNum, pcmAddr, size, loop);
	Voice &v = sas.voices[voiceNum];
	v.pcmAddr = pcmAddr;
	v.size = size;
	v.loop = loop;
	v.playing = true;
	RETURN(0);
}

u32 sceSasGetAllEnvelopeHeights(u32 core, u32 heightsAddr)
{
	DEBUG_LOG(HLE,"0=sceSasGetAllEnvelopeHeights(core=%08x, heightsAddr=%i)", core, heightsAddr);
	if (Memory::IsValidAddress(heightsAddr)) {
		for (int i = 0; i < sas.length; i++) {
			int voiceHeight = sas.voices[i].height;
			Memory::Write_U32(voiceHeight, heightsAddr + i * 4);
		}
	}
	return 0;
}

const HLEFunction sceSasCore[] =
{
	{0x42778a9f, WrapU_UUUUU<sceSasInit>, "__sceSasInit"}, // (SceUID * sasCore, int grain, int maxVoices, int outputMode, int sampleRate)
	{0xa3589d81, WrapV_UU<_sceSasCore>, "__sceSasCore"},
	{0x50a14dfc, WrapV_UU<_sceSasCoreWithMix>, "__sceSasCoreWithMix"},	// Process and mix into buffer (int sasCore, int sasInOut, int leftVolume, int rightVolume)
	{0x68a46b95, WrapU_U<sceSasGetEndFlag>, "__sceSasGetEndFlag"},	// int sasCore
	{0x440ca7d8, WrapV_UIIIII<sceSasSetVolume>, "__sceSasSetVolume"},
	{0xad84d37f, WrapV_UII<sceSasSetPitch>, "__sceSasSetPitch"},
	{0x99944089, WrapV_UIUII<sceSasSetVoice>, "__sceSasSetVoice"},	// (int sasCore, int voice, int vagAddr, int size, int loopmode)
	{0xb7660a23, WrapU_UII<sceSasSetNoise>, "__sceSasSetNoise"},
	{0x019b25eb, WrapU_UIIIIII<sceSasSetADSR>, "__sceSasSetADSR"},
	{0x9ec3676a, WrapU_UIIIIII<sceSasSetADSRMode>, "__sceSasSetADSRmode"},
	{0x5f9529f6, WrapU_UII<sceSasSetSL>, "__sceSasSetSL"},
	{0x74ae582a, WrapU_UU<sceSasGetEnvelopeHeight>, "__sceSasGetEnvelopeHeight"},	
	{0xcbcd4f79, WrapU_UUUU<sceSasSetSimpleADSR>, "__sceSasSetSimpleADSR"},
	{0xa0cf2fa4, WrapV_UI<sceSasSetKeyOff>, "__sceSasSetKeyOff"},
	{0x76f01aca, WrapV_UI<sceSasSetKeyOn>, "__sceSasSetKeyOn"},	// (int sasCore, int voice)
	{0xf983b186, WrapV_UII<sceSasRevVON>, "__sceSasRevVON"},	// int sasCore, int dry, int wet
	{0xd5a229c9, WrapV_UII<sceSasRevEVOL>, "__sceSasRevEVOL"},	// (int sasCore, int leftVol, int rightVol)	// effect volume
	{0x33d4ab37, WrapV_UI<sceSasRevType>, "__sceSasRevType"},	 // (int sasCore, int type)
	{0x267a6dd2, WrapV_UII<sceSasRevParam>, "__sceSasRevParam"},	// (int sasCore, int delay, int feedback)
	{0x2c8e6ab3, WrapU_U<sceSasGetPauseFlag>, "__sceSasGetPauseFlag"}, // int sasCore
	{0x787d04d5, WrapU_UII<sceSasSetPause>, "__sceSasSetPause"},
	{0xa232cbe6, 0, "__sceSasSetTriangularWave"},		// (int sasCore, int voice, int unknown)
	{0xd5ebbbcd, 0, "__sceSasSetSteepWave"},	 // (int sasCore, int voice, int unknown)		// square wave?
	{0xBD11B7C2, WrapU_U<sceSasGetGrain>, "__sceSasGetGrain"},
	{0xd1e0a01e, WrapU_UI<sceSasSetGrain>, "__sceSasSetGrain"},
	{0xe175ef66, WrapU_U<sceSasGetOutputMode>, "__sceSasGetOutputmode"},
	{0xe855bf76, WrapU_UU<sceSasSetOutputMode>, "__sceSasSetOutputmode"},
	{0x07f58c24, WrapU_UU<sceSasGetAllEnvelopeHeights>, "__sceSasGetAllEnvelopeHeights"},	// (int sasCore, int heightAddr)	32-bit heights, 0-0x40000000
	{0xE1CD9561, WrapV_UIUII<sceSasSetVoicePCM>, "__sceSasSetVoicePCM"},
};

void Register_sceSasCore()
{
	RegisterModule("sceSasCore", ARRAY_SIZE(sceSasCore), sceSasCore);
}

