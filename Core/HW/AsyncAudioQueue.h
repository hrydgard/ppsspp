#pragma once


struct AudioDebugStats {
	int buffered;
	int watermark;
	int bufsize;
	int underrunCount;
	int overrunCount;
	int instantSampleRate;
	int lastPushSize;
	float ratio;
	float duration;
};


class AsyncAudioQueue {
public:
	virtual ~AsyncAudioQueue() {}

	// Called from audio threads
	virtual unsigned int Mix(short* samples, unsigned int numSamples, bool consider_framelimit, int sampleRate) = 0;

	virtual void Clear() = 0;
	// Called from main thread
	// This clamps the samples to 16-bit before starting to work on them.
	virtual void PushSamples(const s32* samples, unsigned int num_samples) = 0;

	virtual void GetAudioDebugStats(AudioDebugStats *stats) = 0;
};
