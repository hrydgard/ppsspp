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

#pragma once

#include <string>

#include "base/mutex.h"

#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"

// 16 bit Stereo

#define MAX_SAMPLES     (2*(1024 * 2)) // 2*64ms - had to double it for nVidia Shield which has huge buffers
#define INDEX_MASK      (MAX_SAMPLES * 2 - 1)

#define LOW_WATERMARK   1280 // 40 ms
#define MAX_FREQ_SHIFT  200  // per 32000 Hz
#define CONTROL_FACTOR  0.2f // in freq_shift per fifo size offset
#define CONTROL_AVG     32

class StereoResampler {

public:
	StereoResampler()
		: m_dma_mixer(this, 44100)
	{
	}

	virtual ~StereoResampler() {}

	// Called from audio threads
	virtual unsigned int Mix(short* samples, unsigned int numSamples, bool consider_framelimit, int sampleRate);

	// Called from main thread
	// This clamps the samples to 16-bit before starting to work on them.
	virtual void PushSamples(const s32* samples, unsigned int num_samples);

	void Clear() {
		m_dma_mixer.Clear();
	}

	void DoState(PointerWrap &p);

protected:
	class MixerFifo {
	public:
		MixerFifo(StereoResampler *mixer, unsigned sample_rate)
			: m_mixer(mixer)
			, m_input_sample_rate(sample_rate)
			, m_indexW(0)
			, m_indexR(0)
			, m_numLeftI(0.0f)
			, m_frac(0)
		{
			memset(m_buffer, 0, sizeof(m_buffer));
		}
		void PushSamples(const s32* samples, unsigned int num_samples);
		unsigned int Mix(short* samples, unsigned int numSamples, bool consider_framelimit, int sample_rate);
		void SetInputSampleRate(unsigned int rate);
		void Clear();

	private:
		StereoResampler *m_mixer;
		unsigned m_input_sample_rate;
		short m_buffer[MAX_SAMPLES * 2];
		volatile u32 m_indexW;
		volatile u32 m_indexR;
		float m_numLeftI;
		u32 m_frac;
	};

	MixerFifo m_dma_mixer;
};
