#pragma once

// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstddef>
#include <memory>
#include <vector>

#include "Common/CommonTypes.h"

namespace AudioCore {

	class TimeStretcher final {
	public:
		TimeStretcher();
		~TimeStretcher();

		/**
		* Set sample rate for the samples that Process returns.
		* @param sample_rate The sample rate.
		*/
		void SetOutputSampleRate(unsigned int sample_rate);

		/**
		* Add samples to be processed.
		* @param sample_buffer Buffer of samples in interleaved stereo PCM16 format.
		* @param num_sample Number of samples.
		*/
		void AddSamples(const s16* sample_buffer, size_t num_samples);

		/**
		* Flush audio remaining in internal buffers.
		*/
		void Flush();

		/**
		* Does audio stretching and produces the time-stretched samples.
		* Timer calculations use sample_delay to determine how much of a margin we have.
		* @param sample_delay How many samples are buffered downstream of this module and haven't been played yet.
		* @return Samples to play in interleaved stereo PCM16 format.
		*/
		std::vector<s16> Process(size_t sample_delay, double now);

		void ResetRatio(float ratio = 1.0f);

		int GetSamplesQueued();

		double GetCurrentRatio();
		double GetCurrentRate();

	private:
		struct Impl;
		Impl *impl;

		/// INTERNAL: Clamp ratio within limits.
		static double ClampRatio(double ratio);
		/// INTERNAL: ratio = wallclock time / emulated time
		double CalculateCurrentRatio(double now);
		/// INTERNAL: If we have too many or too few samples downstream, nudge ratio in the appropriate direction.
		double CorrectForUnderAndOverflow(double ratio, size_t sample_delay) const;
		/// INTERNAL: Gets the time-stretched samples from SoundTouch.
		std::vector<s16> GetSamples();
	};

} // namespace AudioCore