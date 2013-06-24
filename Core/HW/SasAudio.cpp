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
#include "Core/HLE/sceAtrac.h"
#include "SasAudio.h"

// #define AUDIO_TO_FILE

static const s8 f[16][2] = {
	{   0,   0 },
	{  60,	 0 },
	{ 115, -52 },
	{  98, -55 },
	{ 122, -60 },

	// Padding to prevent overflow.
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
	{   0,   0 },
};

void VagDecoder::Start(u32 data, int vagSize, bool loopEnabled) {
	loopEnabled_ = loopEnabled;
	loopAtNextBlock_ = false;
	loopStartBlock_ = 0;
	numBlocks_ = vagSize / 16;
	end_ = false;
	data_ = data;
	read_ = data;
	curSample = 28;
	curBlock_ = -1;
	s_1 = 0;	// per block?
	s_2 = 0;
}

void VagDecoder::DecodeBlock(u8 *&readp) {
	int predict_nr = *readp++;
	int shift_factor = predict_nr & 0xf;
	predict_nr >>= 4;
	int flags = *readp++;
	if (flags == 7) {
		VERBOSE_LOG(SAS, "VAG ending block at %d", curBlock_);
		end_ = true;
		return;
	}
	else if (flags == 6) {
		loopStartBlock_ = curBlock_;
	}
	else if (flags == 3) {
		if (loopEnabled_) {
			loopAtNextBlock_ = true;
		}
	}
	for (int i = 0; i < 28; i += 2) {
		int d = *readp++;
		int s = (short)((d & 0xf) << 12);
		DecodeSample(i, s >> shift_factor, predict_nr);
		s = (short)((d & 0xf0) << 8);
		DecodeSample(i + 1, s >> shift_factor, predict_nr);
	}
	curSample = 0;
	curBlock_++;
	if (curBlock_ == numBlocks_) {
		end_ = true;
	}
}

inline void VagDecoder::DecodeSample(int i, int sample, int predict_nr) {
	samples[i] = (int) (sample + ((s_1 * f[predict_nr][0] + s_2 * f[predict_nr][1]) >> 6));
	s_2 = s_1;
	s_1 = samples[i];
}

void VagDecoder::GetSamples(s16 *outSamples, int numSamples) {
	if (end_) {
		memset(outSamples, 0, numSamples * sizeof(s16));
		return;
	}
	u8 *readp = Memory::GetPointer(read_);
	if (!readp)
	{
		WARN_LOG(SAS, "Bad VAG samples address?");
		return;
	}
	u8 *origp = readp;
	for (int i = 0; i < numSamples; i++) {
		if (curSample == 28) {
			if (loopAtNextBlock_) {
				VERBOSE_LOG(SAS, "Looping VAG from block %d/%d to %d", curBlock_, numBlocks_, loopStartBlock_);
				// data_ starts at curBlock = -1.
				read_ = data_ + 16 * loopStartBlock_ + 16;
				readp = Memory::GetPointer(read_);
				origp = readp;
				curBlock_ = loopStartBlock_;
				loopAtNextBlock_ = false;
			}
			DecodeBlock(readp);
			if (end_) {
				// Clear the rest of the buffer and return.
				memset(&outSamples[i], 0, (numSamples - i) * sizeof(s16));
				return;
			}
		}
		outSamples[i] = end_ ? 0 : samples[curSample++];
	}

	if (readp > origp) {
		read_ += readp - origp;
	}
}

void VagDecoder::DoState(PointerWrap &p)
{
	p.DoArray(samples, ARRAY_SIZE(samples));
	p.Do(curSample);

	p.Do(data_);
	p.Do(read_);
	p.Do(curBlock_);
	p.Do(loopStartBlock_);
	p.Do(numBlocks_);

	p.Do(s_1);
	p.Do(s_2);

	p.Do(loopEnabled_);
	p.Do(loopAtNextBlock_);
	p.Do(end_);
}

int SasAtrac3::setContext(u32 context) {
	contextAddr = context;
	atracID = _AtracGetIDByContext(context);
	if (!sampleQueue)
		sampleQueue = new Atrac3plus_Decoder::BufferQueue;
	sampleQueue->clear();
	return 0;
}

int SasAtrac3::getNextSamples(s16* outbuf, int wantedSamples) {
	if (atracID < 0)
		return -1;
	u32 finish = 0;
	int wantedbytes = wantedSamples * sizeof(s16);
	while (!finish && sampleQueue->getQueueSize() < wantedbytes) {
		u32 numSamples = 0;
		int remains = 0;
		static s16 buf[0x800];
		_AtracDecodeData(atracID, (u8*)buf, &numSamples, &finish, &remains);
		if (numSamples > 0)
			sampleQueue->push((u8*)buf, numSamples * sizeof(s16));
		else 
			finish = 1;
	}
	sampleQueue->pop_front((u8*)outbuf, wantedbytes);
	return finish;
}

int SasAtrac3::addStreamData(u8* buf, u32 addbytes) {
	if (atracID > 0) {
		_AtracAddStreamData(atracID, buf, addbytes);
	}
	return 0;
}

void SasAtrac3::DoState(PointerWrap &p) {
	p.Do(contextAddr);
	p.Do(atracID);
	if (p.mode == p.MODE_READ && atracID >= 0 && !sampleQueue) {
		sampleQueue = new Atrac3plus_Decoder::BufferQueue;
	}
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
	ERROR_LOG(SAS,"sasSetSimpleADSR,ERROR_SAS_INVALID_ADSR_CURVE_MODE");
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

void ADSREnvelope::SetSimpleEnvelope(u32 ADSREnv1, u32 ADSREnv2) {
	attackRate 		= getAttackRate(ADSREnv1);
	attackType 		= getAttackType(ADSREnv1);
	decayRate 		= getDecayRate(ADSREnv1);
	decayType 		= PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE;
	sustainRate 	= getSustainRate(ADSREnv2);
	sustainType 	= getSustainType(ADSREnv2);
	releaseRate 	= getReleaseRate(ADSREnv2);
	releaseType 	= getReleaseType(ADSREnv2);
	sustainLevel 	= getSustainLevel(ADSREnv1);
}

SasInstance::SasInstance()
	: maxVoices(PSP_SAS_VOICES_MAX),
		sampleRate(44100),
		outputMode(0),
		mixBuffer(0),
		sendBuffer(0),
		resampleBuffer(0),
		grainSize(0) {
#ifdef AUDIO_TO_FILE
	audioDump = fopen("D:\\audio.raw", "wb");
#endif
	memset(&waveformEffect, 0, sizeof(waveformEffect));
	waveformEffect.type = -1;
	waveformEffect.isDryOn = 1;
}

SasInstance::~SasInstance() {
	ClearGrainSize();
}

void SasInstance::ClearGrainSize() {
	if (mixBuffer)
		delete [] mixBuffer;
	if (sendBuffer)
		delete [] sendBuffer;
	if (resampleBuffer)
		delete [] resampleBuffer;
	mixBuffer = NULL;
	sendBuffer = NULL;
	resampleBuffer = NULL;
}

void SasInstance::SetGrainSize(int newGrainSize) {
	grainSize = newGrainSize;

	// If you change the sizes here, don't forget DoState().
	if (mixBuffer)
		delete [] mixBuffer;
	if (sendBuffer)
		delete [] sendBuffer;
	mixBuffer = new s32[grainSize * 2];
	sendBuffer = new s32[grainSize * 2];
	memset(mixBuffer, 0, sizeof(int) * grainSize * 2);
	memset(sendBuffer, 0, sizeof(int) * grainSize * 2);
	if (resampleBuffer)
		delete [] resampleBuffer;

	// 2 samples padding at the start, that's where we copy the two last samples from the channel
	// so that we can do bicubic resampling if necessary.
	resampleBuffer = new s16[grainSize * 4 + 2];
}

static inline s16 clamp_s16(int i) {
	if (i > 32767)
		return 32767;
	if (i < -32768)
		return -32768;
	return i;
}

void SasInstance::Mix(u32 outAddr, u32 inAddr, int leftVol, int rightVol) {
	int voicesPlayingCount = 0;
	for (int v = 0; v < PSP_SAS_VOICES_MAX; v++) {
		SasVoice &voice = voices[v];
		if (!voice.playing || voice.paused)
			continue;
		voicesPlayingCount++;

		// TODO: Special case no-resample case for speed

		switch (voice.type) {
		case VOICETYPE_VAG:
			if (voice.type == VOICETYPE_VAG && !voice.vagAddr)
				break;
		case VOICETYPE_PCM:
			if (voice.type == VOICETYPE_PCM && !voice.pcmAddr)
				break;
		default: 
			// Load resample history (so we can use a wide filter)
			resampleBuffer[0] = voice.resampleHist[0];
			resampleBuffer[1] = voice.resampleHist[1];

			// Figure out number of samples to read.
			// Actually this is not entirely correct - we need to get one extra sample, and store it
			// for the next time around. A little complicated...
			// But for now, see Smoothness HACKERY below :P
			u32 numSamples = (voice.sampleFrac + grainSize * voice.pitch) / PSP_SAS_PITCH_BASE;
			if ((int)numSamples > grainSize * 4) {
				ERROR_LOG(SAS, "numSamples too large, clamping: %i vs %i", numSamples, grainSize * 4);
				numSamples = grainSize * 4;
			}

			// Read N samples into the resample buffer. Could do either PCM or VAG here.
			switch (voice.type) {
			case VOICETYPE_VAG:
				{
					voice.vag.GetSamples(resampleBuffer + 2, numSamples);
					if (voice.vag.End()) {
						// NOTICE_LOG(SAS, "Hit end of VAG audio");
						voice.playing = false;
						voice.on = false;  // ??
					}
				}
				break;
			case VOICETYPE_PCM:
				{
					u32 size = std::min(voice.pcmSize * 2 - voice.pcmIndex, (int)(numSamples * sizeof(s16)));
					memset(resampleBuffer + 2, 0, numSamples * sizeof(s16));
					if (!voice.on) {
						voice.pcmIndex = 0;
						break;
					}
					Memory::Memcpy(resampleBuffer + 2, voice.pcmAddr + voice.pcmIndex, size);
					voice.pcmIndex += size;
					if (voice.pcmIndex >= voice.pcmSize * 2) {
						voice.pcmIndex = 0;
					}
				}
				break;
			case VOICETYPE_ATRAC3:
				{
					int ret = voice.atrac3.getNextSamples(resampleBuffer + 2, numSamples);
					if (ret) {
						// Hit atrac3 voice end
						voice.playing = false;
						voice.on = false;  // ??
					}
				}
				break;
			default:
				{
					memset(resampleBuffer + 2, 0, numSamples * sizeof(s16));
				}
				break;
			}
			// Smoothness HACKERY
			resampleBuffer[2 + numSamples] = resampleBuffer[2 + numSamples - 1];

			// Save resample history
			voice.resampleHist[0] = resampleBuffer[2 + numSamples - 2];
			voice.resampleHist[1] = resampleBuffer[2 + numSamples - 1];

			// Resample to the correct pitch, writing exactly "grainSize" samples.
			u32 sampleFrac = voice.sampleFrac;
			for (int i = 0; i < grainSize; i++) {
				// For now: nearest neighbour, not even using the resample history at all.
				int sample = resampleBuffer[sampleFrac / PSP_SAS_PITCH_BASE + 2];
				sampleFrac += voice.pitch;

				// The maximum envelope height (PSP_SAS_ENVELOPE_HEIGHT_MAX) is (1 << 30) - 1.
				// Reduce it to 14 bits, by shifting off 15.  Round up by adding (1 << 14) first.
				int envelopeValue = voice.envelope.GetHeight();
				envelopeValue = (envelopeValue + (1 << 14)) >> 15;

				// We just scale by the envelope before we scale by volumes.
				// Again, we round up by adding (1 << 14) first (*after* multiplying.)
				sample = ((sample * envelopeValue) + (1 << 14)) >> 15;

				// We mix into this 32-bit temp buffer and clip in a second loop
				// Ideally, the shift right should be there too but for now I'm concerned about
				// not overflowing.
				mixBuffer[i * 2] += sample * voice.volumeLeft >> 12;
				mixBuffer[i * 2 + 1] += sample * voice.volumeRight >> 12;
				sendBuffer[i * 2] += sample * voice.volumeLeftSend >> 12;
				sendBuffer[i * 2 + 1] += sample * voice.volumeRightSend >> 12;
				voice.envelope.Step();
			}
			voice.sampleFrac = sampleFrac;
			// Let's hope grainSize is a power of 2.
			//voice.sampleFrac &= grainSize * PSP_SAS_PITCH_BASE - 1;
			voice.sampleFrac -= numSamples * PSP_SAS_PITCH_BASE;

			if (voice.envelope.HasEnded())
			{
				// NOTICE_LOG(SAS, "Hit end of envelope");
				voice.playing = false;
			}
		}
	}

	//if (voicesPlayingCount)
	//	NOTICE_LOG(SAS, "Sas mixed %i voices", voicesPlayingCount);
	// Okay, apply effects processing to the Send buffer alone here.
	// Reverb, echo, what have you.
	// TODO

	// Alright, all voices mixed. Let's convert and clip, and at the same time, wipe mixBuffer for next time. Could also dither.
	s16 *outp = (s16 *)Memory::GetPointer(outAddr);
	const s16 *inp = inAddr ? (s16*)Memory::GetPointer(inAddr) : 0;
	if (outputMode == 0) {
		if (inp) {
			for (int i = 0; i < grainSize * 2; i += 2) {
				int sampleL = mixBuffer[i] + sendBuffer[i] + ((*inp++) * leftVol >> 12);
				int sampleR = mixBuffer[i + 1] + sendBuffer[i + 1] + ((*inp++) * rightVol >> 12);
				*outp++ = clamp_s16(sampleL);
				*outp++ = clamp_s16(sampleR);
			}
		} else {
			for (int i = 0; i < grainSize * 2; i += 2) {
				*outp++ = clamp_s16(mixBuffer[i] + sendBuffer[i]);
				*outp++ = clamp_s16(mixBuffer[i + 1] + sendBuffer[i + 1]);
			}
		}
	} else {
		for (int i = 0; i < grainSize * 2; i += 2) {
			int sampleL = mixBuffer[i] + sendBuffer[i];
			if (inp)
				sampleL += (*inp++) * leftVol >> 12;
			*outp++ = clamp_s16(sampleL);
		}
	}
	memset(mixBuffer, 0, grainSize * sizeof(int) * 2);
	memset(sendBuffer, 0, grainSize * sizeof(int) * 2);

#ifdef AUDIO_TO_FILE
	fwrite(Memory::GetPointer(outAddr), 1, grainSize * 2 * 2, audioDump);
#endif
}

void SasInstance::DoState(PointerWrap &p) {
	p.Do(grainSize);
	if (p.mode == p.MODE_READ) {
		if (grainSize > 0) {
			SetGrainSize(grainSize);
		} else {
			ClearGrainSize();
		}
	}

	p.Do(maxVoices);
	p.Do(sampleRate);
	p.Do(outputMode);

	// SetGrainSize() / ClearGrainSize() should've made our buffers match.
	if (mixBuffer != NULL && grainSize > 0) {
		p.DoArray(mixBuffer, grainSize * 2);
	}
	if (sendBuffer != NULL && grainSize > 0) {
		p.DoArray(sendBuffer, grainSize * 2);
	}
	if (resampleBuffer != NULL && grainSize > 0) {
		p.DoArray(resampleBuffer, grainSize * 4 + 2);
	}

	int n = PSP_SAS_VOICES_MAX;
	p.Do(n);
	if (n != PSP_SAS_VOICES_MAX)
	{
		ERROR_LOG(HLE, "Savestate failure: wrong number of SAS voices");
		return;
	}
	p.DoArray(voices, ARRAY_SIZE(voices));
	p.Do(waveformEffect);

	p.DoMarker("SasInstance");
}

void SasVoice::Reset() {
	resampleHist[0] = 0;
	resampleHist[1] = 0;
}

void SasVoice::KeyOn() {
	envelope.KeyOn();
	switch (type) {
	case VOICETYPE_VAG:
		if (Memory::IsValidAddress(vagAddr)) {
			vag.Start(vagAddr, vagSize, loop);
		} else {
			ERROR_LOG(SAS, "Invalid VAG address %08x", vagAddr);
			return;
		}
		break;
	default:
		break;
	}
	playing = true;
	on = true;
	paused = false;
}

void SasVoice::KeyOff() {
	on = false;
	envelope.KeyOff();
}

void SasVoice::ChangedParams(bool changedVag) {
	if (!playing && on) {
		playing = true;
		if (changedVag)
			vag.Start(vagAddr, vagSize, loop);
	}
	// TODO: restart VAG somehow
}

void SasVoice::DoState(PointerWrap &p)
{
	p.Do(playing);
	p.Do(paused);
	p.Do(on);

	p.Do(type);

	p.Do(vagAddr);
	p.Do(vagSize);
	p.Do(pcmAddr);
	p.Do(pcmSize);
	p.Do(pcmIndex);
	p.Do(sampleRate);

	p.Do(sampleFrac);
	p.Do(pitch);
	p.Do(loop);

	p.Do(noiseFreq);

	p.Do(volumeLeft);
	p.Do(volumeRight);
	p.Do(volumeLeftSend);
	p.Do(volumeRightSend);
	p.Do(effectLeft);
	p.Do(effectRight);
	p.DoArray(resampleHist, ARRAY_SIZE(resampleHist));

	p.DoMarker("SasVoice");

	envelope.DoState(p);
	vag.DoState(p);
	atrac3.DoState(p);
}

// This is horribly stolen from JPCSP.
// Need to find a real solution.
static const short expCurve[] = {
	0x0000, 0x0380, 0x06E4, 0x0A2D, 0x0D5B, 0x1072, 0x136F, 0x1653,
	0x1921, 0x1BD9, 0x1E7B, 0x2106, 0x237F, 0x25E4, 0x2835, 0x2A73,
	0x2CA0, 0x2EBB, 0x30C6, 0x32C0, 0x34AB, 0x3686, 0x3852, 0x3A10,
	0x3BC0, 0x3D63, 0x3EF7, 0x4081, 0x41FC, 0x436E, 0x44D3, 0x462B,
	0x477B, 0x48BF, 0x49FA, 0x4B2B, 0x4C51, 0x4D70, 0x4E84, 0x4F90,
	0x5095, 0x5191, 0x5284, 0x5370, 0x5455, 0x5534, 0x5609, 0x56D9,
	0x57A3, 0x5867, 0x5924, 0x59DB, 0x5A8C, 0x5B39, 0x5BE0, 0x5C81,
	0x5D1C, 0x5DB5, 0x5E48, 0x5ED5, 0x5F60, 0x5FE5, 0x6066, 0x60E2,
	0x615D, 0x61D2, 0x6244, 0x62B2, 0x631D, 0x6384, 0x63E8, 0x644A,
	0x64A8, 0x6503, 0x655B, 0x65B1, 0x6605, 0x6653, 0x66A2, 0x66ED,
	0x6737, 0x677D, 0x67C1, 0x6804, 0x6844, 0x6882, 0x68BF, 0x68F9,
	0x6932, 0x6969, 0x699D, 0x69D2, 0x6A03, 0x6A34, 0x6A63, 0x6A8F,
	0x6ABC, 0x6AE6, 0x6B0E, 0x6B37, 0x6B5D, 0x6B84, 0x6BA7, 0x6BCB,
	0x6BED, 0x6C0E, 0x6C2D, 0x6C4D, 0x6C6B, 0x6C88, 0x6CA4, 0x6CBF,
	0x6CD9, 0x6CF3, 0x6D0C, 0x6D24, 0x6D3B, 0x6D52, 0x6D68, 0x6D7D,
	0x6D91, 0x6DA6, 0x6DB9, 0x6DCA, 0x6DDE, 0x6DEF, 0x6DFF, 0x6E10,
	0x6E20, 0x6E30, 0x6E3E, 0x6E4C, 0x6E5A, 0x6E68, 0x6E76, 0x6E82,
	0x6E8E, 0x6E9B, 0x6EA5, 0x6EB1, 0x6EBC, 0x6EC6, 0x6ED1, 0x6EDB,
	0x6EE4, 0x6EED, 0x6EF6, 0x6EFE, 0x6F07, 0x6F10, 0x6F17, 0x6F20,
	0x6F27, 0x6F2E, 0x6F35, 0x6F3C, 0x6F43, 0x6F48, 0x6F4F, 0x6F54,
	0x6F5B, 0x6F60, 0x6F66, 0x6F6B, 0x6F70, 0x6F74, 0x6F79, 0x6F7E,
	0x6F82, 0x6F87, 0x6F8A, 0x6F90, 0x6F93, 0x6F97, 0x6F9A, 0x6F9E,
	0x6FA1, 0x6FA5, 0x6FA8, 0x6FAC, 0x6FAD, 0x6FB1, 0x6FB4, 0x6FB6,
	0x6FBA, 0x6FBB, 0x6FBF, 0x6FC1, 0x6FC4, 0x6FC6, 0x6FC8, 0x6FC9,
	0x6FCD, 0x6FCF, 0x6FD0, 0x6FD2, 0x6FD4, 0x6FD6, 0x6FD7, 0x6FD9,
	0x6FDB, 0x6FDD, 0x6FDE, 0x6FDE, 0x6FE0, 0x6FE2, 0x6FE4, 0x6FE5,
	0x6FE5, 0x6FE7, 0x6FE9, 0x6FE9, 0x6FEB, 0x6FEC, 0x6FEC, 0x6FEE,
	0x6FEE, 0x6FF0, 0x6FF0, 0x6FF2, 0x6FF2, 0x6FF3, 0x6FF3, 0x6FF5,
	0x6FF5, 0x6FF7, 0x6FF7, 0x6FF7, 0x6FF9, 0x6FF9, 0x6FF9, 0x6FFA,
	0x6FFA, 0x6FFA, 0x6FFC, 0x6FFC, 0x6FFC, 0x6FFE, 0x6FFE, 0x6FFE,
	0x7000
};

static int durationFromRate(int rate)
{
	if (rate == 0) {
		return PSP_SAS_ENVELOPE_FREQ_MAX;
	} else {
		// From experimental tests on a PSP:
		//   rate=0x7FFFFFFF => duration=0x10
		//   rate=0x3FFFFFFF => duration=0x22
		//   rate=0x1FFFFFFF => duration=0x44
		//   rate=0x0FFFFFFF => duration=0x81
		//   rate=0x07FFFFFF => duration=0xF1
		//   rate=0x03FFFFFF => duration=0x1B9
		//
		// The correct curve model is still unknown.
		// We use the following approximation:
		//   duration = 0x7FFFFFFF / rate * 0x10
		return PSP_SAS_ENVELOPE_FREQ_MAX / rate * 0x10;
	}
}

const short expCurveReference = 0x7000;

// This needs a rewrite / rethink. Doing all this per sample is insane.
static int getExpCurveAt(int index, int duration) {
	const short curveLength = sizeof(expCurve) / sizeof(short);

	if (duration == 0) {
		// Avoid division by zero, and thus undefined behaviour in conversion to int.
		return 0;
	}

	float curveIndex = (index * curveLength) / (float) duration;
	int curveIndex1 = (int) curveIndex;
	int curveIndex2 = curveIndex1 + 1;
	float curveIndexFraction = curveIndex - curveIndex1;

	if (curveIndex1 < 0) {
		return expCurve[0];
	} else if (curveIndex2 >= curveLength || curveIndex2 < 0) {
		return expCurve[curveLength - 1];
	}

	float sample = expCurve[curveIndex1] * (1.f - curveIndexFraction) + expCurve[curveIndex2] * curveIndexFraction;
	return (short)(sample);
}

ADSREnvelope::ADSREnvelope()
	: 	attackRate(0),
		attackType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE),
		decayRate(0),
		decayType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE),
		sustainRate(0),
		sustainType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE),
		releaseRate(0),
		releaseType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE),
		sustainLevel(0x100),
		state_(STATE_OFF),
		steps_(0),
		height_(0) {
}

void ADSREnvelope::WalkCurve(int rate, int type) {
	short expFactor;
	int duration;
	switch (type) {
	case PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE:
		height_ += rate;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE:
		height_ -= rate;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT:
		if (height_ < (s64)PSP_SAS_ENVELOPE_HEIGHT_MAX * 3 / 4) {
			height_ += rate;
		} else {
			height_ += rate / 4;
		}
		break;

	case PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE:
		// NOTICE_LOG(SAS, "UNIMPL EXP DECR");
		duration = durationFromRate(rate);
		expFactor = getExpCurveAt(steps_, duration);
		height_ = (s64)expFactor * PSP_SAS_ENVELOPE_HEIGHT_MAX / expCurveReference;
		height_ = PSP_SAS_ENVELOPE_HEIGHT_MAX - height_;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_EXPONENT_INCREASE:
		duration = durationFromRate(rate);
		expFactor = getExpCurveAt(steps_, duration);
		height_ = (s64)expFactor * PSP_SAS_ENVELOPE_HEIGHT_MAX / expCurveReference;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_DIRECT:
		height_ = rate;  // Simple :)
		break;
	}
}

void ADSREnvelope::SetState(ADSRState state) {
	steps_ = 0;
	state_ = state;
}

void ADSREnvelope::Step() {
	switch (state_) {
	case STATE_ATTACK:
		WalkCurve(attackRate, attackType);
		if (height_ > PSP_SAS_ENVELOPE_HEIGHT_MAX || height_ < 0)
			SetState(STATE_DECAY);
		break;
	case STATE_DECAY:
		WalkCurve(decayRate, decayType);
		if (height_ > PSP_SAS_ENVELOPE_HEIGHT_MAX || height_ < sustainLevel)
			SetState(STATE_SUSTAIN);
		break;
	case STATE_SUSTAIN:
		WalkCurve(sustainRate, sustainType);
		if (height_ <= 0) {
			height_ = 0;
			SetState(STATE_RELEASE);
		}
		break;
	case STATE_RELEASE:
		WalkCurve(releaseRate, releaseType);
		if (height_ <= 0) {
			height_ = 0;
			SetState(STATE_OFF);
		}
		break;
	case STATE_OFF:
		// Do nothing
		break;
	}
	steps_++;
}

void ADSREnvelope::KeyOn() {
	SetState(STATE_ATTACK);
	height_ = 0;
}

void ADSREnvelope::KeyOff() {
	SetState(STATE_RELEASE);
	height_ = sustainLevel;
}

void ADSREnvelope::DoState(PointerWrap &p) {
	p.Do(attackRate);
	p.Do(decayRate);
	p.Do(sustainRate);
	p.Do(releaseRate);
	p.Do(attackType);
	p.Do(decayType);
	p.Do(sustainType);
	p.Do(sustainLevel);
	p.Do(releaseType);
	p.Do(state_);
	p.Do(steps_);
	p.Do(height_);
	p.DoMarker("ADSREnvelope");
}
