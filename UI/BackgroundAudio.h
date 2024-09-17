#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Common/File/Path.h"
#include "Common/UI/Root.h"

class AT3PlusReader;

struct Sample {
	// data must be new[]-ed.
	Sample(int16_t *data, int channels, int length, int rateInHz) : channels_(channels), data_(data), length_(length), rateInHz_(rateInHz) {}
	~Sample() {
		delete[] data_;
	}
	int16_t *data_;
	int length_;  // stereo or mono samples.
	int rateInHz_;  // sampleRate
	int channels_;

	static Sample *Load(const std::string &path);
};

// Mixer for things played on top of everything.
class SoundEffectMixer {
public:
	void LoadSamples();

	void Mix(int16_t *buffer, int sz, int sampleRateHz);
	void Play(UI::UISound sfx, float volume);

	void UpdateSample(UI::UISound sound, Sample *sample);
	void LoadDefaultSample(UI::UISound sound);

	std::vector<std::unique_ptr<Sample>> samples_;

	struct PlayInstance {
		UI::UISound sound;
		int64_t offset;  // 32.32 fixed point
		int volume; // 0..255
		bool done;
	};

private:
	std::mutex mutex_;
	std::vector<PlayInstance> queue_;
	std::vector<PlayInstance> plays_;
};

class BackgroundAudio {
public:
	BackgroundAudio();
	~BackgroundAudio();

	void SetGame(const Path &path);
	void Update();
	bool Play();

	SoundEffectMixer &SFX() {
		return sfxMixer_;
	}

private:
	void Clear(bool hard);

	enum {
		// 0.5 ms buffer at 44.1 khz should be enough.
		BUFSIZE = 22050,
	};

	std::mutex mutex_;
	Path bgGamePath_;
	std::atomic<bool> sndLoadPending_;
	int playbackOffset_ = 0;
	AT3PlusReader *at3Reader_ = nullptr;
	double gameLastChanged_ = 0.0;
	double lastPlaybackTime_ = 0.0;
	int *buffer = nullptr;
	bool fadingOut_ = true;
	float volume_ = 0.0f;
	float delta_ = -0.0001f;
	SoundEffectMixer sfxMixer_;
};

extern BackgroundAudio g_BackgroundAudio;
