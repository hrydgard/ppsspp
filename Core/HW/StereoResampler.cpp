// Copyright (c) 2015- PPSSPP Project and Dolphin Project.

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

// Adapted from Dolphin.

// 16 bit Stereo

// These must be powers of 2.
#define MAX_BUFSIZE_DEFAULT (4096) // 2*64ms - had to double it for nVidia Shield which has huge buffers
#define MAX_BUFSIZE_EXTRA   (8192)

#define TARGET_BUFSIZE_MARGIN 512

#define TARGET_BUFSIZE_DEFAULT 1680 // 40 ms
#define TARGET_BUFSIZE_EXTRA 3360 // 80 ms

#define MAX_FREQ_SHIFT  600.0f  // how far off can we be from 44100 Hz
#define CONTROL_FACTOR  0.2f // in freq_shift per fifo size offset
#define CONTROL_AVG     32.0f

#include "ppsspp_config.h"
#include <cstring>
#include <atomic>

#include "Common/Common.h"
#include "Common/System/System.h"
#include "Common/Math/math_util.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HW/StereoResampler.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/Util/AudioFormat.h"  // for clamp_u8
#include "Core/System.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

StereoResampler::StereoResampler()
		: m_maxBufsize(MAX_BUFSIZE_DEFAULT)
	  , m_targetBufsize(TARGET_BUFSIZE_DEFAULT) {
	// Need to have space for the worst case in case it changes.
	m_buffer = new int16_t[MAX_BUFSIZE_EXTRA * 2]();

	// Some Android devices are v-synced to non-60Hz framerates. We simply timestretch audio to fit.
	// TODO: should only do this if auto frameskip is off?
	float refresh = System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE);

	// If framerate is "close"...
	if (refresh != 60.0f && refresh > 50.0f && refresh < 70.0f) {
		int input_sample_rate = (int)(44100 * (refresh / 60.0f));
		INFO_LOG(Log::Audio, "StereoResampler: Adjusting target sample rate to %dHz", input_sample_rate);
		m_input_sample_rate = input_sample_rate;
	}

	UpdateBufferSize();
}

StereoResampler::~StereoResampler() {
	delete[] m_buffer;
	m_buffer = nullptr;
}

void StereoResampler::UpdateBufferSize() {
	if (g_Config.bExtraAudioBuffering) {
		m_maxBufsize = MAX_BUFSIZE_EXTRA;
		m_targetBufsize = TARGET_BUFSIZE_EXTRA;
	} else {
		m_maxBufsize = MAX_BUFSIZE_DEFAULT;
		m_targetBufsize = TARGET_BUFSIZE_DEFAULT;

		int systemBufsize = System_GetPropertyInt(SYSPROP_AUDIO_FRAMES_PER_BUFFER);
		if (systemBufsize > 0 && m_targetBufsize < systemBufsize + TARGET_BUFSIZE_MARGIN) {
			m_targetBufsize = std::min(4096, systemBufsize + TARGET_BUFSIZE_MARGIN);
			if (m_targetBufsize * 2 > MAX_BUFSIZE_DEFAULT)
				m_maxBufsize = MAX_BUFSIZE_EXTRA;
		}
	}
}

template<bool useShift>
inline void ClampBufferToS16(s16 *out, const s32 *in, size_t size, s8 volShift) {
#ifdef _M_SSE
	// Size will always be 16-byte aligned as the hwBlockSize is.
	while (size >= 8) {
		__m128i in1 = _mm_loadu_si128((__m128i *)in);
		__m128i in2 = _mm_loadu_si128((__m128i *)(in + 4));
		__m128i packed = _mm_packs_epi32(in1, in2);
		if (useShift) {
			packed = _mm_srai_epi16(packed, volShift);
		}
		_mm_storeu_si128((__m128i *)out, packed);
		out += 8;
		in += 8;
		size -= 8;
	}
#elif PPSSPP_ARCH(ARM_NEON)
	// Dynamic shifts can only be left, but it's signed - negate to shift right.
	int16x4_t signedVolShift = vdup_n_s16(-volShift);
	while (size >= 8) {
		int32x4_t in1 = vld1q_s32(in);
		int32x4_t in2 = vld1q_s32(in + 4);
		int16x4_t packed1 = vqmovn_s32(in1);
		int16x4_t packed2 = vqmovn_s32(in2);
		if (useShift) {
			packed1 = vshl_s16(packed1, signedVolShift);
			packed2 = vshl_s16(packed2, signedVolShift);
		}
		vst1_s16(out, packed1);
		vst1_s16(out + 4, packed2);
		out += 8;
		in += 8;
		size -= 8;
	}
#endif
	// This does the remainder if SIMD was used, otherwise it does it all.
	for (size_t i = 0; i < size; i++) {
		out[i] = clamp_s16(useShift ? (in[i] >> volShift) : in[i]);
	}
}

inline void ClampBufferToS16WithVolume(s16 *out, const s32 *in, size_t size) {
	int volume = g_Config.iGlobalVolume;
	if (PSP_CoreParameter().fpsLimit != FPSLimit::NORMAL || PSP_CoreParameter().fastForward) {
		if (g_Config.iAltSpeedVolume != -1) {
			volume = g_Config.iAltSpeedVolume;
		}
	}

	if (volume >= VOLUME_FULL) {
		ClampBufferToS16<false>(out, in, size, 0);
	} else if (volume <= VOLUME_OFF) {
		memset(out, 0, size * sizeof(s16));
	} else {
		ClampBufferToS16<true>(out, in, size, VOLUME_FULL - (s8)volume);
	}
}

void StereoResampler::Clear() {
	memset(m_buffer, 0, m_maxBufsize * 2 * sizeof(int16_t));
}

inline int16_t MixSingleSample(int16_t s1, int16_t s2, uint16_t frac) {
	int32_t value = s1 + (((s2 - s1) * frac) >> 16);
	if (value < -32767)
		return -32767;
	else if (value > 32767)
		return 32767;
	else
		return (int16_t)value;
}

// Executed from sound stream thread, pulling sound out of the buffer.
unsigned int StereoResampler::Mix(short* samples, unsigned int numSamples, bool consider_framelimit, int sample_rate) {
	if (!samples)
		return 0;

	unsigned int currentSample;

	// Cache access in non-volatile variable
	// This is the only function changing the read value, so it's safe to
	// cache it locally although it's written here.
	// The writing pointer will be modified outside, but it will only increase,
	// so we will just ignore new written data while interpolating (until it wraps...).
	// Without this cache, the compiler wouldn't be allowed to optimize the
	// interpolation loop.
	u32 indexR = m_indexR.load();
	u32 indexW = m_indexW.load();

	const int INDEX_MASK = (m_maxBufsize * 2 - 1);

	// This is only for debug visualization, not used for anything.
	lastBufSize_ = ((indexW - indexR) & INDEX_MASK) / 2;

	// Drift prevention mechanism.
	float numLeft = (float)(((indexW - indexR) & INDEX_MASK) / 2);
	// If we had to discard samples the last frame due to underrun,
	// apply an adjustment here. Otherwise we'll overestimate how many
	// samples we need.
	numLeft -= droppedSamples_;
	droppedSamples_ = 0;

	// m_numLeftI here becomes a lowpass filtered version of numLeft.
	m_numLeftI = (numLeft + m_numLeftI * (CONTROL_AVG - 1.0f)) / CONTROL_AVG;

	// Here we try to keep the buffer size around m_lowwatermark (which is
	// really now more like desired_buffer_size) by adjusting the speed.
	// Note that the speed of adjustment here does not take the buffer size into
	// account. Since this is called once per "output frame", the frame size
	// will affect how fast this algorithm reacts, which can't be a good thing.
	float offset = (m_numLeftI - (float)m_targetBufsize) * CONTROL_FACTOR;
	if (offset > MAX_FREQ_SHIFT) offset = MAX_FREQ_SHIFT;
	if (offset < -MAX_FREQ_SHIFT) offset = -MAX_FREQ_SHIFT;

	output_sample_rate_ = (float)(m_input_sample_rate + offset);
	const u32 ratio = (u32)(65536.0 * output_sample_rate_ / (double)sample_rate);
	ratio_ = ratio;
	// TODO: consider a higher-quality resampling algorithm.
	// TODO: Add a fast path for 1:1.
	u32 frac = m_frac;
	for (currentSample = 0; currentSample < numSamples * 2; currentSample += 2) {
		if (((indexW - indexR) & INDEX_MASK) <= 2) {
			// Ran out!
			// int missing = numSamples * 2 - currentSample;
			// ILOG("Resampler underrun: %d (numSamples: %d, currentSample: %d)", missing, numSamples, currentSample / 2);
			underrunCount_++;
			break;
		}
		u32 indexR2 = indexR + 2; //next sample
		s16 l1 = m_buffer[indexR & INDEX_MASK]; //current
		s16 r1 = m_buffer[(indexR + 1) & INDEX_MASK]; //current
		s16 l2 = m_buffer[indexR2 & INDEX_MASK]; //next
		s16 r2 = m_buffer[(indexR2 + 1) & INDEX_MASK]; //next
		samples[currentSample] = MixSingleSample(l1, l2, (u16)frac);
		samples[currentSample + 1] = MixSingleSample(r1, r2, (u16)frac);
		frac += ratio;
		indexR += 2 * (frac >> 16);
		frac &= 0xffff;
	}
	m_frac = frac;

	// Let's not count the underrun padding here.
	outputSampleCount_ += currentSample / 2;

	// Padding with the last value to reduce clicking
	short s[2];
	s[0] = clamp_s16(m_buffer[(indexR - 1) & INDEX_MASK]);
	s[1] = clamp_s16(m_buffer[(indexR - 2) & INDEX_MASK]);
	for (; currentSample < numSamples * 2; currentSample += 2) {
		samples[currentSample] = s[0];
		samples[currentSample + 1] = s[1];
	}

	// Flush cached variable
	m_indexR.store(indexR);

	// TODO: What should we actually return here?
	return currentSample / 2;
}

// Executes on the emulator thread, pushing sound into the buffer.
void StereoResampler::PushSamples(const s32 *samples, unsigned int numSamples) {
	inputSampleCount_ += numSamples;

	UpdateBufferSize();
	const int INDEX_MASK = (m_maxBufsize * 2 - 1);
	// Cache access in non-volatile variable
	// indexR isn't allowed to cache in the audio throttling loop as it
	// needs to get updates to not deadlock.
	u32 indexW = m_indexW.load();

	u32 cap = m_maxBufsize * 2;
	// If fast-forwarding, no need to fill up the entire buffer, just screws up timing after releasing the fast-forward button.
	if (PSP_CoreParameter().fastForward) {
		cap = m_targetBufsize * 2;
	}

	// Check if we have enough free space
	// indexW == m_indexR results in empty buffer, so indexR must always be smaller than indexW
	if (numSamples * 2 + ((indexW - m_indexR.load()) & INDEX_MASK) >= cap) {
		if (!PSP_CoreParameter().fastForward) {
			overrunCount_++;
		}
		// TODO: "Timestretch" by doing a windowed overlap with existing buffer content?
		return;
	}

	// Check if we need to roll over to the start of the buffer during the copy.
	unsigned int indexW_left_samples = m_maxBufsize * 2 - (indexW & INDEX_MASK);
	if (numSamples * 2 > indexW_left_samples) {
		ClampBufferToS16WithVolume(&m_buffer[indexW & INDEX_MASK], samples, indexW_left_samples);
		ClampBufferToS16WithVolume(&m_buffer[0], samples + indexW_left_samples, numSamples * 2 - indexW_left_samples);
	} else {
		ClampBufferToS16WithVolume(&m_buffer[indexW & INDEX_MASK], samples, numSamples * 2);
	}

	m_indexW += numSamples * 2;
	lastPushSize_ = numSamples;
}

void StereoResampler::GetAudioDebugStats(char *buf, size_t bufSize) {
	double elapsed = time_now_d() - startTime_;

	double effective_input_sample_rate = (double)inputSampleCount_ / elapsed;
	double effective_output_sample_rate = (double)outputSampleCount_ / elapsed;
	snprintf(buf, bufSize,
		"Audio buffer: %d/%d (target: %d)\n"
		"Filtered: %0.2f\n"
		"Underruns: %d\n"
		"Overruns: %d\n"
		"Sample rate: %d (input: %d)\n"
		"Effective input sample rate: %0.2f\n"
		"Effective output sample rate: %0.2f\n"
		"Push size: %d\n"
		"Ratio: %0.6f\n",
		lastBufSize_,
		m_maxBufsize,
		m_targetBufsize,
		m_numLeftI,
		underrunCountTotal_,
		overrunCountTotal_,
		(int)output_sample_rate_,
		m_input_sample_rate,
		effective_input_sample_rate,
		effective_output_sample_rate,
		lastPushSize_,
		(float)ratio_ / 65536.0f);
	underrunCountTotal_ += underrunCount_;
	overrunCountTotal_ += overrunCount_;
	underrunCount_ = 0;
	overrunCount_ = 0;

	// Use this to remove the bias from the startup.
	// if (elapsed > 3.0) {
		//ResetStatCounters();
	// }
}

void StereoResampler::ResetStatCounters() {
	underrunCount_ = 0;
	overrunCount_ = 0;
	underrunCountTotal_ = 0;
	overrunCountTotal_ = 0;
	inputSampleCount_ = 0;
	outputSampleCount_ = 0;
	startTime_ = time_now_d();
}
