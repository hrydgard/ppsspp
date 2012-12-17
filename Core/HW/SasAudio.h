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



// This is not really hardware, it's a software audio mixer running on the Media Engine.
// From the perspective of a PSP app though, it might as well be.

#pragma once

#include "../Globals.h"

enum {
	PSP_SAS_VOICES_MAX = 32,

	PSP_SAS_PITCH_MIN = 1,
	PSP_SAS_PITCH_BASE = 0x1000,
	PSP_SAS_PITCH_MAX = 0x4000,

	PSP_SAS_VOL_MAX = 0x1000,

	PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE = 0,
	PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE = 1,
	PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT = 2,
	PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE = 3,
	PSP_SAS_ADSR_CURVE_MODE_EXPONENT_INCREASE = 4,
	PSP_SAS_ADSR_CURVE_MODE_DIRECT = 5,

	PSP_SAS_ADSR_ATTACK = 1,
	PSP_SAS_ADSR_DECAY = 2,
	PSP_SAS_ADSR_SUSTAIN = 4,
	PSP_SAS_ADSR_RELEASE = 8,

	PSP_SAS_ENVELOPE_HEIGHT_MAX = 0x40000000,
	PSP_SAS_ENVELOPE_FREQ_MAX = 0x7FFFFFFF,
};

struct WaveformEffect
{
	int type;
	int delay;
	int feedback;
	int leftVol;
	int rightVol;
	int isDryOn;
	int isWetOn;
};

enum VoiceType {
	VOICETYPE_VAG = 0,  // default
	VOICETYPE_PCM = 1,
	VOICETYPE_NOISE = 2,
	VOICETYPE_ATRAC3 = 3,
	VOICETYPE_TRIWAVE = 4,  // are these used? there are functions for them (sceSetTriangularWave)
	VOICETYPE_PULSEWAVE = 5,
};

// VAG is a Sony ADPCM audio compression format, which goes all the way back to the PSX.
// It compresses 28 16-bit samples into a block of 16 bytes.
// TODO: Get rid of the doubles, making sure it does not impact sound quality.
// Doubles are pretty fast on Android devices these days though.
class VagDecoder
{
public:
	void Start(u8 *data, bool loopEnabled);

	void GetSamples(s16 *outSamples, int numSamples);

	bool DecodeBlock();
	bool End() const { return end_; }

	u8 GetByte() {
		return *read_++;
	}

private:
	double samples[28];
	int curSample;

	u8 *data_;
	u8 *read_;
	int curBlock_;
	int loopStartBlock_;

	// rolling state. start at 0, should probably reset to 0 on loops?
	double s_1;
	double s_2;

	bool loopEnabled_;
	bool loopAtNextBlock_;
	bool end_;
};

// Max height: 0x40000000 I think
class ADSREnvelope
{
public:
	void SetSimpleEnvelope(u32 ADSREnv1, u32 ADSREnv2);

	void WalkCurve(int rate, int type);

	void KeyOn();
	void KeyOff();

	void Step();

	int GetHeight() const {
		return height_ > PSP_SAS_ENVELOPE_HEIGHT_MAX ? PSP_SAS_ENVELOPE_HEIGHT_MAX : height_; 
	}
	bool HasEnded() const {
		return state_ == STATE_OFF;
	}
	int attackRate;
	int decayRate;
	int sustainRate;
	int releaseRate;
	int attackType;
	int decayType;
	int sustainType;
	int sustainLevel;
	int releaseType;

private:
	enum ADSRState {
		STATE_ATTACK,
		STATE_DECAY,
		STATE_SUSTAIN,
		STATE_RELEASE,
		STATE_OFF,
	};
	void SetState(ADSRState state);

	ADSRState state_;
	int steps_;
	s64 height_;  // s64 to avoid having to care about overflow when calculatimg. TODO: this should be fine as s32
};

// A SAS voice.
// TODO: Look into pre-decoding the VAG samples on SetVoice instead of decoding them on the fly.
// It's not very likely that games encode VAG dynamically.
struct SasVoice
{
	bool playing;
	bool paused;  // a voice can be playing AND paused. In that case, it won't play.
	bool on;   // key-on, key-off.

	VoiceType type;

	u32 vagAddr;
	int vagSize;
	u32 pcmAddr;
	int pcmSize;
	int sampleRate;

	int samplePos;
	int pitch;
	bool loop;

	int noiseFreq;

	int volumeLeft;
	int volumeRight;
	int volumeLeftSend;	// volume to "Send" (audio-lingo) to the effects processing engine, like reverb
	int volumeRightSend;

	void Reset();

	void KeyOn();
	void KeyOff();

	void ChangedParams();

	s16 resampleHist[2];

	ADSREnvelope envelope;

	VagDecoder vag;
};

class SasInstance
{
public:
	SasInstance();
	~SasInstance();

	void SetGrainSize(int newGrainSize);
	int GetGrainSize() const { return grainSize; }

	int maxVoices;
	int sampleRate;
	int outputMode;
	int length;

	int *mixBuffer;
	int *sendBuffer;
	s16 *resampleBuffer;

	void Mix(u32 outAddr);

	SasVoice voices[PSP_SAS_VOICES_MAX];
	WaveformEffect waveformEffect;

private:
	int grainSize;
};
