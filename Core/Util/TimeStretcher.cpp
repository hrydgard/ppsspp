// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <vector>

#include <ext/soundtouch/include/SoundTouch.h>

#include "Core/Util/TimeStretcher.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/MathUtil.h"

namespace AudioCore {

	constexpr double MIN_RATIO = 0.3;
	constexpr double MAX_RATIO = 100.0;

	constexpr double MIN_DELAY_TIME = 0.05; // Units: seconds
	constexpr double MAX_DELAY_TIME = 0.25; // Units: seconds
	constexpr size_t DROP_FRAMES_SAMPLE_DELAY = 16000; // Units: samples

	constexpr double SMOOTHING_FACTOR = 0.007;
	constexpr int native_sample_rate = 44100;

	struct TimeStretcher::Impl {
		soundtouch::SoundTouch soundtouch;

		double frame_timer = 0.0;
		size_t samples_queued = 0;

		double smoothed_ratio = 1.0;

		double rate = 1.0;
		double sample_rate = static_cast<double>(native_sample_rate);
		double smoothed_rate = 1.0;
	};

	void TimeStretcher::ResetRatio(float ratio) {
		impl->smoothed_ratio = ratio;
	}

	double TimeStretcher::GetCurrentRatio() {
		return impl->smoothed_ratio;
	}

	double TimeStretcher::GetCurrentRate() {
		return impl->smoothed_rate;
	}

	std::vector<s16> TimeStretcher::Process(size_t samples_in_queue, double now) {
		// This is a very simple algorithm without any fancy control theory. It works and is stable.

		double ratio = CalculateCurrentRatio(now);
		ratio = CorrectForUnderAndOverflow(ratio, samples_in_queue);
		impl->smoothed_ratio = (1.0 - SMOOTHING_FACTOR) * impl->smoothed_ratio + SMOOTHING_FACTOR * ratio;
		impl->smoothed_ratio = ClampRatio(impl->smoothed_ratio);

		// If ratio is very close to 1.0, don't bother stretching, just resample instead.
		ratio = impl->smoothed_ratio;
		double rate = impl->rate;
		if (ratio > 0.97 && ratio < 1.03) {
			ratio = 1.0;
			rate /= ratio;
		}

		// SoundTouch's tempo definition the inverse of our ratio definition.
		impl->soundtouch.setTempo(1.0 / ratio);
		impl->soundtouch.setRate(rate);

		std::vector<s16> samples = GetSamples();
		if (samples_in_queue >= DROP_FRAMES_SAMPLE_DELAY) {
			samples.clear();
			// LOG_DEBUG(Audio, "Dropping frames!");
		}
		return samples;
	}

	TimeStretcher::TimeStretcher() : impl(new Impl) {
		impl->soundtouch.setTempo(1.0);
		impl->soundtouch.setPitch(1.0);
		impl->soundtouch.setRate(1.0);
		impl->soundtouch.setChannels(2);
		impl->soundtouch.setSampleRate(native_sample_rate);
	}

	TimeStretcher::~TimeStretcher() {
		impl->soundtouch.clear();
	}

	void TimeStretcher::SetOutputSampleRate(unsigned int sample_rate) {
		impl->sample_rate = static_cast<double>(sample_rate);
		impl->rate = 1.0 / (impl->sample_rate / static_cast<double>(native_sample_rate));
		impl->soundtouch.setRate(impl->rate);
	}

	void TimeStretcher::AddSamples(const s16* buffer, size_t num_samples) {
		impl->soundtouch.putSamples(buffer, static_cast<uint>(num_samples));
		impl->samples_queued += num_samples;
	}

	int TimeStretcher::GetSamplesQueued() {
		return impl->samples_queued;
	}

	void TimeStretcher::Flush() {
		impl->soundtouch.flush();
	}

	double TimeStretcher::ClampRatio(double ratio) {
		return MathUtil::Clamp<double>(ratio, MIN_RATIO, MAX_RATIO);
	}

	double TimeStretcher::CalculateCurrentRatio(double now) {
		const double duration = now - impl->frame_timer;

		const double expected_time = static_cast<double>(impl->samples_queued) / static_cast<double>(native_sample_rate);
		const double actual_time = duration;

		double ratio;
		if (expected_time != 0) {
			ratio = ClampRatio(actual_time / expected_time);
		} else {
			ratio = 1.0;
		}

		impl->frame_timer = now;
		impl->samples_queued = 0;

		return ratio;
	}

	double TimeStretcher::CorrectForUnderAndOverflow(double ratio, size_t sample_delay) const {
		const size_t min_sample_delay = static_cast<size_t>(MIN_DELAY_TIME * impl->sample_rate);
		const size_t max_sample_delay = static_cast<size_t>(MAX_DELAY_TIME * impl->sample_rate);

		if (sample_delay < min_sample_delay) {
			// Make the ratio bigger.
			ratio = ratio > 1.0 ? ratio * ratio : sqrt(ratio);
		} else if (sample_delay > max_sample_delay) {
			// Make the ratio smaller.
			ratio = ratio > 1.0 ? sqrt(ratio) : ratio * ratio;
		}

		return ClampRatio(ratio);
	}

	std::vector<short> TimeStretcher::GetSamples() {
		uint available = impl->soundtouch.numSamples();
		std::vector<s16> output(static_cast<size_t>(available) * 2);
		impl->soundtouch.receiveSamples(output.data(), available);
		return output;
	}

}