#pragma once

#include "base/mutex.h"
#include "Common/ChunkFile.h"
#include "Core/HW/AsyncAudioQueue.h"
#include "Core/Util/TimeStretcher.h"

// Similar to StereoResample but uses Citra's TimeStretcher instead of resampling.
// This helps cover up even pretty long time gaps with no issues, except that it doesn't
// sound super great.


class StereoStretcher : public AsyncAudioQueue {
public:
	StereoStretcher();
	unsigned int Mix(short * samples, unsigned int numSamples, bool consider_framelimit, int sampleRate) override;
	void PushSamples(const s32 * samples, unsigned int num_samples) override;
	void DoState(PointerWrap & p) override;
	void GetAudioDebugStats(AudioDebugStats * stats) override;
	void Clear() override;

private:
	AudioCore::TimeStretcher stretch_;
	recursive_mutex mutex_;

	std::list<std::vector<s16>> queue;
	double last_now_;
	int buffered_;
	int64_t mixed_samples_;
	AudioDebugStats stats_;
	bool lastUnthrottle_;
};