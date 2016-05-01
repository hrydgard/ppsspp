#include <algorithm>
#include <numeric>
#include "base/basictypes.h"
#include "base/timeutil.h"
#include "StereoStretcher.h"

StereoStretcher::StereoStretcher() {
	stretch_.SetOutputSampleRate(48000);
	mixed_samples_ = 0;
	last_now_ = real_time_now();
}

unsigned int StereoStretcher::Mix(s16 *buffer, unsigned int numSamples, bool consider_framelimit, int sampleRate) {
	mutex_.lock();
	int remaining_size = numSamples * 2;
	while (remaining_size > 0 && !queue.empty()) {
		if (!queue.front().size()) {
			queue.pop_front();
		} else if (queue.front().size() <= remaining_size) {
			memcpy(buffer, queue.front().data(), queue.front().size() * sizeof(s16));
			buffer += queue.front().size();
			remaining_size -= (int)queue.front().size();
			queue.pop_front();
		} else {
			memcpy(buffer, queue.front().data(), remaining_size * sizeof(s16));
			buffer += remaining_size;
			queue.front().erase(queue.front().begin(), queue.front().begin() + remaining_size);
			remaining_size = 0;
		}
	}
	if (remaining_size > 0) {
		memset(buffer, 0, remaining_size * sizeof(s16));
	}
	mutex_.unlock();
	mixed_samples_ += numSamples;
	return numSamples - remaining_size * 2;
}

void StereoStretcher::PushSamples(const s32 * samples, unsigned int num_samples) {
	stats_.lastPushSize = num_samples;
	s16 buffer[16384];
	while (num_samples > 0) {
		int mono_count = std::min(num_samples * 2, (unsigned int)ARRAY_SIZE(buffer));
		for (int i = 0; i < mono_count; i++) {
			buffer[i] = (s16)samples[i];
		}
		stretch_.AddSamples(buffer, mono_count / 2);
		num_samples -= mono_count / 2;
	}
	
	// Only process when we have a substantial amount of samples.
	if (stretch_.GetSamplesQueued() < 512)
		return;

	stats_.watermark = stretch_.GetSamplesQueued();
	mutex_.lock();
	size_t total_size = std::accumulate(queue.begin(), queue.end(), 0, [](size_t sum, const auto& buffer) {
		return sum + buffer.size();
	});
	double now = real_time_now();
	stats_.duration = now - last_now_;
	last_now_ = now;
	std::vector<s16> audio = stretch_.Process(total_size, /*mixed_samples_ / 48000.0*/ real_time_now());
	if (audio.size() > 0) {
		queue.emplace_back(audio);
	}
	buffered_ = (int)total_size;
	stats_.ratio = stretch_.GetCurrentRatio();
	stats_.buffered = (int)total_size;
	mutex_.unlock();
}

void StereoStretcher::DoState(PointerWrap & p) {
}

void StereoStretcher::GetAudioDebugStats(AudioDebugStats * stats) {
	*stats = stats_;
}

void StereoStretcher::Clear() {
	stretch_.Flush();
}