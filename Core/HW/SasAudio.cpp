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
#include "profiler/profiler.h"

#include "Globals.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/sceAtrac.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "SasAudio.h"

#include <algorithm>

// #define AUDIO_TO_FILE

static const u8 f[16][2] = {
	{   0,   0 },
	{  60,   0 },
	{ 115,  52 },
	{  98,  55 },
	{ 122,  60 },
	// TODO: The below values could use more testing, but match initial tests.
	// Not sure if they are used by games, found by tests.
	{   0,   0 },
	{   0,   0 },
	{  52,   0 },
	{  55,   2 },
	{  60, 125 },
	{   0,   0 },
	{   0,  91 },
	{   0,   0 },
	{   2, 216 },
	{ 125,   6 },
	{   0, 151 },
};

void VagDecoder::Start(u32 data, u32 vagSize, bool loopEnabled) {
	loopEnabled_ = loopEnabled;
	loopAtNextBlock_ = false;
	loopStartBlock_ = -1;
	numBlocks_ = vagSize / 16;
	end_ = false;
	data_ = data;
	read_ = data;
	curSample = 28;
	curBlock_ = -1;
	s_1 = 0;	// per block?
	s_2 = 0;
}

void VagDecoder::DecodeBlock(u8 *&read_pointer) {
	u8 *readp = read_pointer;
	int predict_nr = *readp++;
	int shift_factor = predict_nr & 0xf;
	predict_nr >>= 4;
	int flags = *readp++;
	if (flags == 7) {
		VERBOSE_LOG(SASMIX, "VAG ending block at %d", curBlock_);
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

	// Keep state in locals to avoid bouncing to memory.
	int s1 = s_1;
	int s2 = s_2;

	int coef1 = f[predict_nr][0];
	int coef2 = -f[predict_nr][1];

	// TODO: Unroll once more and interleave the unpacking with the decoding more?
	for (int i = 0; i < 28; i += 2) {
		u8 d = *readp++;
		int sample1 = (short)((d & 0xf) << 12) >> shift_factor;
		int sample2 = (short)((d & 0xf0) << 8) >> shift_factor;
		s2 = clamp_s16(sample1 + ((s1 * coef1 + s2 * coef2) >> 6));
		s1 = clamp_s16(sample2 + ((s2 * coef1 + s1 * coef2) >> 6));
		samples[i] = s2;
		samples[i + 1] = s1;
	}

	s_1 = s1;
	s_2 = s2;
	curSample = 0;
	curBlock_++;
	if (curBlock_ == numBlocks_) {
		end_ = true;
	}

	read_pointer = readp;
}

void VagDecoder::GetSamples(s16 *outSamples, int numSamples) {
	if (end_) {
		memset(outSamples, 0, numSamples * sizeof(s16));
		return;
	}
	if (!Memory::IsValidAddress(read_)) {
		WARN_LOG(SASMIX, "Bad VAG samples address?");
		return;
	}
	u8 *readp = Memory::GetPointerUnchecked(read_);
	u8 *origp = readp;

	for (int i = 0; i < numSamples; i++) {
		if (curSample == 28) {
			if (loopAtNextBlock_) {
				VERBOSE_LOG(SASMIX, "Looping VAG from block %d/%d to %d", curBlock_, numBlocks_, loopStartBlock_);
				// data_ starts at curBlock = -1.
				read_ = data_ + 16 * loopStartBlock_ + 16;
				readp = Memory::GetPointerUnchecked(read_);
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
		outSamples[i] = samples[curSample++];
	}

	if (readp > origp) {
		read_ += readp - origp;
	}
}

void VagDecoder::DoState(PointerWrap &p) {
	auto s = p.Section("VagDecoder", 1);
	if (!s)
		return;

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
		sampleQueue = new BufferQueue();
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
		_AtracDecodeData(atracID, (u8*)buf, 0, &numSamples, &finish, &remains);
		if (numSamples > 0)
			sampleQueue->push((u8*)buf, numSamples * sizeof(s16));
		else
			finish = 1;
	}
	sampleQueue->pop_front((u8*)outbuf, wantedbytes);
	return finish;
}

int SasAtrac3::addStreamData(u32 bufPtr, u32 addbytes) {
	if (atracID > 0) {
		_AtracAddStreamData(atracID, bufPtr, addbytes);
	}
	return 0;
}

void SasAtrac3::DoState(PointerWrap &p) {
	auto s = p.Section("SasAtrac3", 1);
	if (!s)
		return;

	p.Do(contextAddr);
	p.Do(atracID);
	if (p.mode == p.MODE_READ && atracID >= 0 && !sampleQueue) {
		sampleQueue = new BufferQueue();
	}
}

// http://code.google.com/p/jpcsp/source/browse/trunk/src/jpcsp/HLE/modules150/sceSasCore.java

static int simpleRate(int n) {
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

static int exponentRate(int n) {
	n &= 0x7F;
	if (n == 0x7F) {
		return 0;
	}
	int rate = ((7 - (n & 0x3)) << 24) >> (n >> 2);
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
	int n = (bitfield1 >> 4) & 0x000F;
	if (n == 0)
		return 0x7FFFFFFF;
	return 0x80000000 >> n;
}

static int getSustainType(int bitfield2) {
	return (bitfield2 >> 14) & 3;
}

static int getSustainRate(int bitfield2) {
	if (getSustainType(bitfield2) == PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE) {
		return exponentRate(bitfield2 >> 6);
	} else {
		return simpleRate(bitfield2 >> 6);
	}
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
		if (n == 30) {
			return 0x40000000;
		} else if (n == 29) {
			return 1;
		}
		return 0x10000000 >> n;
	}
	if (n == 0)
		return 0x7FFFFFFF;
	return 0x80000000 >> n;
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

	if (attackRate < 0 || decayRate < 0 || sustainRate < 0 || releaseRate < 0) {
		ERROR_LOG_REPORT(SCESAS, "Simple ADSR resulted in invalid rates: %04x, %04x", ADSREnv1, ADSREnv2);
	}
}

SasInstance::SasInstance()
	: maxVoices(PSP_SAS_VOICES_MAX),
		sampleRate(44100),
		outputMode(PSP_SAS_OUTPUTMODE_MIXED),
		mixBuffer(0),
		sendBuffer(0),
		resampleBuffer(0),
		grainSize(0) {
#ifdef AUDIO_TO_FILE
	audioDump = fopen("D:\\audio.raw", "wb");
#endif
	memset(&waveformEffect, 0, sizeof(waveformEffect));
	waveformEffect.type = PSP_SAS_EFFECT_TYPE_OFF;
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
	// so that we can do bicubic resampling if necessary.  Plus 1 for smoothness hackery.
	resampleBuffer = new s16[grainSize * 4 + 3];
}

void SasVoice::ReadSamples(s16 *output, int numSamples) {
	// Read N samples into the resample buffer. Could do either PCM or VAG here.
	switch (type) {
	case VOICETYPE_VAG:
		vag.GetSamples(output, numSamples);
		break;
	case VOICETYPE_PCM:
		{
			int needed = numSamples;
			s16 *out = output;
			while (needed > 0) {
				u32 size = std::min(pcmSize - pcmIndex, needed);
				if (!on) {
					pcmIndex = 0;
					break;
				}
				Memory::Memcpy(out, pcmAddr + pcmIndex * sizeof(s16), size * sizeof(s16));
				pcmIndex += size;
				needed -= size;
				out += size;
				if (pcmIndex >= pcmSize) {
					if (!loop) {
						// All out, quit.  We'll end in HaveSamplesEnded().
						break;
					}
					pcmIndex = pcmLoopPos;
				}
			}
			if (needed > 0) {
				memset(out, 0, needed * sizeof(s16));
			}
		}
		break;
	case VOICETYPE_ATRAC3:
		{
			int ret = atrac3.getNextSamples(output, numSamples);
			if (ret) {
				// Hit atrac3 voice end
				playing = false;
				on = false;  // ??
				envelope.End();
			}
		}
		break;
	default:
		{
			memset(output, 0, numSamples * sizeof(s16));
		}
		break;
	}
}

bool SasVoice::HaveSamplesEnded() const {
	switch (type) {
	case VOICETYPE_VAG:
		return vag.End();

	case VOICETYPE_PCM:
		return pcmIndex >= pcmSize;

	case VOICETYPE_ATRAC3:
		// TODO: Is it here, or before the samples are processed?
		return false;

	default:
		return false;
	}
}

void SasInstance::MixVoice(SasVoice &voice) {
	switch (voice.type) {
	case VOICETYPE_VAG:
		if (voice.type == VOICETYPE_VAG && !voice.vagAddr)
			break;
		// else fallthrough! Don't change the check above.
	case VOICETYPE_PCM:
		if (voice.type == VOICETYPE_PCM && !voice.pcmAddr)
			break;
		// else fallthrough! Don't change the check above.
	default:
		// Load resample history (so we can use a wide filter)
		resampleBuffer[0] = voice.resampleHist[0];
		resampleBuffer[1] = voice.resampleHist[1];

		// Figure out number of samples to read.
		// Actually this is not entirely correct - we need to get one extra sample, and store it
		// for the next time around. A little complicated...
		// But for now, see Smoothness HACKERY below :P
		u32 numSamples = ((u32)voice.sampleFrac + (u32)grainSize * (u32)voice.pitch) >> PSP_SAS_PITCH_BASE_SHIFT;
		if ((int)numSamples > grainSize * 4) {
			ERROR_LOG(SASMIX, "numSamples too large, clamping: %i vs %i", numSamples, grainSize * 4);
			numSamples = grainSize * 4;
		}

		// This feels a bit hacky.  The first 32 samples after a keyon are 0s.
		const bool ignorePitch = voice.type == VOICETYPE_PCM && voice.pitch > PSP_SAS_PITCH_BASE;
		if (voice.envelope.NeedsKeyOn()) {
			int delay = ignorePitch ? 32 : (32 * (u32)voice.pitch) >> PSP_SAS_PITCH_BASE_SHIFT;
			// VAG seems to have an extra sample delay (not shared by PCM.)
			if (voice.type == VOICETYPE_VAG)
				++delay;
			voice.ReadSamples(resampleBuffer + 2 + delay, numSamples - delay);
		} else {
			voice.ReadSamples(resampleBuffer + 2, numSamples);
		}

		// Smoothness HACKERY
		resampleBuffer[2 + numSamples] = resampleBuffer[2 + numSamples - 1];

		// Save resample history
		voice.resampleHist[0] = resampleBuffer[2 + numSamples - 2];
		voice.resampleHist[1] = resampleBuffer[2 + numSamples - 1];

		// Resample to the correct pitch, writing exactly "grainSize" samples.
		// This is a HORRIBLE resampler by the way.
		// TODO: Special case no-resample case (and 2x and 0.5x) for speed, it's not uncommon

		u32 sampleFrac = voice.sampleFrac;
		// We need to shift by 12 anyway, so combine that with the volume shift.
		for (int i = 0; i < grainSize; i++) {
			// For now: nearest neighbour, not even using the resample history at all.
			int sample = resampleBuffer[sampleFrac / PSP_SAS_PITCH_BASE + 2];
			sampleFrac += voice.pitch;

			// The maximum envelope height (PSP_SAS_ENVELOPE_HEIGHT_MAX) is (1 << 30) - 1.
			// Reduce it to 14 bits, by shifting off 15.  Round up by adding (1 << 14) first.
			int envelopeValue = voice.envelope.GetHeight();
			voice.envelope.Step();
			envelopeValue = (envelopeValue + (1 << 14)) >> 15;

			// We just scale by the envelope before we scale by volumes.
			// Again, we round up by adding (1 << 14) first (*after* multiplying.)
			sample = ((sample * envelopeValue) + (1 << 14)) >> 15;

			// We mix into this 32-bit temp buffer and clip in a second loop
			// Ideally, the shift right should be there too but for now I'm concerned about
			// not overflowing.
			mixBuffer[i * 2] += (sample * voice.volumeLeft ) >> 12;
			mixBuffer[i * 2 + 1] += (sample * voice.volumeRight) >> 12;
			sendBuffer[i * 2] += sample * voice.effectLeft >> 12;
			sendBuffer[i * 2 + 1] += sample * voice.effectRight >> 12;
		}

		voice.sampleFrac = sampleFrac;
		// Let's hope grainSize is a power of 2.
		//voice.sampleFrac &= grainSize * PSP_SAS_PITCH_BASE - 1;
		voice.sampleFrac -= numSamples * PSP_SAS_PITCH_BASE;

		if (voice.HaveSamplesEnded())
			voice.envelope.End();
		if (voice.envelope.HasEnded())
		{
			// NOTICE_LOG(SCESAS, "Hit end of envelope");
			voice.playing = false;
			voice.on = false;
		}
	}
}

void SasInstance::Mix(u32 outAddr, u32 inAddr, int leftVol, int rightVol) {
	PROFILE_THIS_SCOPE("mixer");

	int voicesPlayingCount = 0;

	for (int v = 0; v < PSP_SAS_VOICES_MAX; v++) {
		SasVoice &voice = voices[v];
		if (!voice.playing || voice.paused)
			continue;
		voicesPlayingCount++;
		MixVoice(voice);
	}

	// Okay, apply effects processing to the Send buffer.
	// TODO: Is this only done in PSP_SAS_OUTPUTMODE_MIXED?
	//if (waveformEffect.type != PSP_SAS_EFFECT_TYPE_OFF)
	//	ApplyReverb();

	// Then mix the send buffer in with the rest.

	// Alright, all voices mixed. Let's convert and clip, and at the same time, wipe mixBuffer for next time. Could also dither.
	s16 *outp = (s16 *)Memory::GetPointer(outAddr);
	const s16 *inp = inAddr ? (s16*)Memory::GetPointer(inAddr) : 0;
	if (outputMode == PSP_SAS_OUTPUTMODE_MIXED) {
		// TODO: Mix send when it has proper values, probably based on dry/wet?
		if (inp) {
			for (int i = 0; i < grainSize * 2; i += 2) {
				int sampleL = mixBuffer[i + 0] + ((*inp++) * leftVol >> 12);
				int sampleR = mixBuffer[i + 1] + ((*inp++) * rightVol >> 12);
				*outp++ = clamp_s16(sampleL);
				*outp++ = clamp_s16(sampleR);
			}
		} else {
			for (int i = 0; i < grainSize * 2; i += 2) {
				*outp++ = clamp_s16(mixBuffer[i + 0]);
				*outp++ = clamp_s16(mixBuffer[i + 1]);
			}
		}
	} else {
		s16 *outpL = outp + grainSize * 0;
		s16 *outpR = outp + grainSize * 1;
		s16 *outpSendL = outp + grainSize * 2;
		s16 *outpSendR = outp + grainSize * 3;
		WARN_LOG_REPORT_ONCE(sasraw, SCESAS, "sceSasCore: raw outputMode");
		for (int i = 0; i < grainSize * 2; i += 2) {
			*outpL++ = clamp_s16(mixBuffer[i + 0]);
			*outpR++ = clamp_s16(mixBuffer[i + 1]);
			*outpSendL++ = clamp_s16(sendBuffer[i + 0]);
			*outpSendR++ = clamp_s16(sendBuffer[i + 1]);
		}
	}
	memset(mixBuffer, 0, grainSize * sizeof(int) * 2);
	memset(sendBuffer, 0, grainSize * sizeof(int) * 2);

#ifdef AUDIO_TO_FILE
	fwrite(Memory::GetPointer(outAddr), 1, grainSize * 2 * 2, audioDump);
#endif
}

void SasInstance::ApplyReverb() {
	// for (int i = 0; i < grainSize * 2; i += 2) {
		// modify sendBuffer
	// }
}

void SasInstance::DoState(PointerWrap &p) {
	auto s = p.Section("SasInstance", 1);
	if (!s)
		return;

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
		p.DoArray(resampleBuffer, grainSize * 4 + 3);
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
			ERROR_LOG(SASMIX, "Invalid VAG address %08x", vagAddr);
			return;
		}
		break;
	default:
		break;
	}
	playing = true;
	on = true;
	paused = false;
	sampleFrac = 0;
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
	auto s = p.Section("SasVoice", 1, 3);
	if (!s)
		return;

	p.Do(playing);
	p.Do(paused);
	p.Do(on);

	p.Do(type);

	p.Do(vagAddr);
	p.Do(vagSize);
	p.Do(pcmAddr);
	p.Do(pcmSize);
	p.Do(pcmIndex);
	if (s >= 2) {
		p.Do(pcmLoopPos);
	} else {
		pcmLoopPos = 0;
	}
	p.Do(sampleRate);

	p.Do(sampleFrac);
	p.Do(pitch);
	p.Do(loop);
	if (s < 2 && type == VOICETYPE_PCM) {
		// We set loop incorrectly before, and always looped.
		// Let's keep always looping, since it's usually right.
		loop = true;
	}

	p.Do(noiseFreq);

	p.Do(volumeLeft);
	p.Do(volumeRight);
	if (s < 3) {
		// There were extra variables here that were for the same purpose.
		p.Do(effectLeft);
		p.Do(effectRight);
	}
	p.Do(effectLeft);
	p.Do(effectRight);
	p.DoArray(resampleHist, ARRAY_SIZE(resampleHist));

	envelope.DoState(p);
	vag.DoState(p);
	atrac3.DoState(p);
}

ADSREnvelope::ADSREnvelope()
	: attackRate(0),
		decayRate(0),
		sustainRate(0),
		releaseRate(0),
		attackType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE),
		decayType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE),
		sustainType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE),
		sustainLevel(0),
		releaseType(PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE),
		state_(STATE_OFF),
		height_(0) {
}

void ADSREnvelope::WalkCurve(int type, int rate) {
	s64 expDelta;
	switch (type) {
	case PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE:
		height_ += rate;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE:
		height_ -= rate;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_LINEAR_BENT:
		if (height_ <= (s64)PSP_SAS_ENVELOPE_HEIGHT_MAX * 3 / 4) {
			height_ += rate;
		} else {
			height_ += rate / 4;
		}
		break;

	case PSP_SAS_ADSR_CURVE_MODE_EXPONENT_DECREASE:
		expDelta = height_ - PSP_SAS_ENVELOPE_HEIGHT_MAX;
		// Flipping the sign so that we can shift in the top bits.
		expDelta += (-expDelta * rate) >> 32;
		height_ = expDelta + PSP_SAS_ENVELOPE_HEIGHT_MAX - (rate + 3UL) / 4UL;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_EXPONENT_INCREASE:
		expDelta = height_ - PSP_SAS_ENVELOPE_HEIGHT_MAX;
		// Flipping the sign so that we can shift in the top bits.
		expDelta += (-expDelta * rate) >> 32;
		height_ = expDelta + 0x4000 + PSP_SAS_ENVELOPE_HEIGHT_MAX;
		break;

	case PSP_SAS_ADSR_CURVE_MODE_DIRECT:
		height_ = rate;  // Simple :)
		break;
	}
}

void ADSREnvelope::SetState(ADSRState state) {
	if (height_ > PSP_SAS_ENVELOPE_HEIGHT_MAX) {
		height_ = PSP_SAS_ENVELOPE_HEIGHT_MAX;
	}
	// TODO: Also check for height_ < 0 and set to 0?
	state_ = state;
}

inline void ADSREnvelope::Step() {
	switch (state_) {
	case STATE_ATTACK:
		WalkCurve(attackType, attackRate);
		if (height_ >= PSP_SAS_ENVELOPE_HEIGHT_MAX || height_ < 0)
			SetState(STATE_DECAY);
		break;
	case STATE_DECAY:
		WalkCurve(decayType, decayRate);
		if (height_ < sustainLevel)
			SetState(STATE_SUSTAIN);
		break;
	case STATE_SUSTAIN:
		WalkCurve(sustainType, sustainRate);
		if (height_ <= 0) {
			height_ = 0;
			SetState(STATE_RELEASE);
		}
		break;
	case STATE_RELEASE:
		WalkCurve(releaseType, releaseRate);
		if (height_ <= 0) {
			height_ = 0;
			SetState(STATE_OFF);
		}
		break;
	case STATE_OFF:
		// Do nothing
		break;

	case STATE_KEYON:
		height_ = 0;
		SetState(STATE_KEYON_STEP);
		break;
	case STATE_KEYON_STEP:
		// This entire state is pretty much a hack to reproduce PSP behavior.
		// The STATE_KEYON state is a real state, but not sure how it switches.
		// It takes 32 steps at 0 for keyon to "kick in", 31 should shift to 0 anyway.
		height_++;
		if (height_ >= 31) {
			height_ = 0;
			SetState(STATE_ATTACK);
		}
		break;
	}
}

void ADSREnvelope::KeyOn() {
	SetState(STATE_KEYON);
}

void ADSREnvelope::KeyOff() {
	SetState(STATE_RELEASE);
}

void ADSREnvelope::End() {
	SetState(STATE_OFF);
	height_ = 0;
}

void ADSREnvelope::DoState(PointerWrap &p) {
	auto s = p.Section("ADSREnvelope", 1, 2);
	if (!s) {
		return;
	}

	p.Do(attackRate);
	p.Do(decayRate);
	p.Do(sustainRate);
	p.Do(releaseRate);
	p.Do(attackType);
	p.Do(decayType);
	p.Do(sustainType);
	p.Do(sustainLevel);
	p.Do(releaseType);
	if (s < 2) {
		p.Do(state_);
		if (state_ == 4) {
			state_ = STATE_OFF;
		}
		int stepsLegacy;
		p.Do(stepsLegacy);
	} else {
		p.Do(state_);
	}
	p.Do(height_);
}
