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

#include "base/basictypes.h"
#include "../Globals.h"
#include "../MemMap.h"
#include "SasAudio.h"

static const double f[5][2] = 
{ { 0.0, 0.0 },
{	 60.0 / 64.0,	0.0 },
{	115.0 / 64.0, -52.0 / 64.0 },
{	 98.0 / 64.0, -55.0 / 64.0 },
{	122.0 / 64.0, -60.0 / 64.0 } };

void VagDecoder::Start(u8 *data)
{
	data_ = data;
	curSample = 28;
	s_1 = 0.0;	// per block?
	s_2 = 0.0;
}

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

static int getAttackRate(int bitfield1) {
	return simpleRate(bitfield1 >> 8);
}

static int getAttackType(int bitfield1) {
	return (bitfield1 & 0x8000) == 0 ? PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE : PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT;
}

static int getDecayRate(int bitfield1) {
	return 0x80000000 >> ((bitfield1 >> 4) & 0x000F);
}

static int getSustainRate(int bitfield2) {
	return simpleRate(bitfield2 >> 6);
}

static int getSustainType(int bitfield2) {
	switch (bitfield2 >> 13) {
	case 0: return PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE;
	case 2: return PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE;
	case 4: return PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT;
	case 6: return PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE;
	}
	ERROR_LOG(HLE,"sasSetSimpleADSR,ERROR_SAS_INVALID_ADSR_CURVE_MODE");
	return 0;
}

static int getReleaseType(int bitfield2) {
	return (bitfield2 & 0x0020) == 0 ? PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE : PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE;
}

static int getReleaseRate(int bitfield2) {
	int n = bitfield2 & 0x001F;
	if (n == 31) {
		return 0;
	}
	if (getReleaseType(bitfield2) == PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE) {
		return (0x40000000 >> (n + 2));
	}
	return (0x8000000 >> n);
}

static int getSustainLevel(int bitfield1) {
	return ((bitfield1 & 0x000F) + 1) << 26;
}

void ADSREnvelope::SetSimpleEnvelope(u32 ADSREnv1, u32 ADSREnv2)
{
	attackRate 	= getAttackRate(ADSREnv1);
	attackType 	= getAttackType(ADSREnv1);
	decayRate 	= getDecayRate(ADSREnv1);
	decayType 	= PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE;
	sustainRate 	= getSustainRate(ADSREnv2);
	sustainType 	= getSustainType(ADSREnv2);
	releaseRate 	= getReleaseRate(ADSREnv2);
	releaseType 	= getReleaseType(ADSREnv2);
	sustainLevel 	= getSustainLevel(ADSREnv1);
}

void SasInstance::Mix(u32 outAddr)
{
	for (int v = 0; v < PSP_SAS_VOICES_MAX; v++) {
		SasVoice &voice = voices[v];
		if (!voice.playing)
			continue;

		if (voice.vagAddr != 0) {
			for (int i = 0; i < grainSize; i++) {
				int sample = voice.vag.GetSample();
				voice.samplePos++;
				if (voice.samplePos >= voice.vagSize || voice.vag.End()) {
					if (voice.loop) {
						voice.vag.Loop();
					} else {
						voice.playing = false;
					}
					break;
				}
				int l = sample * voice.volumeLeft >> 15;
				int r = sample * voice.volumeRight >> 15;

				// We mix into this 32-bit temp buffer and clip in a second loop
				mixBuffer[i * 2] += l;
				mixBuffer[i * 2 + 1] += r;
			}
		}
		else if (voice.pcmAddr != 0) {
			// PCM mixing should be easy, can probably share code with VAG if we predecode those
		}
		else if (voice.noiseFreq != 0) {
			// Generate noise?
		}
	}

	// Alright, all voices mixed. Let's convert and clip, and at the same time, wipe mixBuffer for next time. Could also dither.
	for (int i = 0; i < grainSize * 2; i++) {
		s16 *out = (s16 *)Memory::GetPointer(outAddr);
		int sample = mixBuffer[i];
		mixBuffer[i] = 0;
		if (sample > 32767) out[i] = 32767;
		else if (sample < -32768) out[i] = -32768;
		else out[i] = sample;
	}
}