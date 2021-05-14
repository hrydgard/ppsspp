#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Common/File/Path.h"
#include "Common/UI/Root.h"

class AT3PlusReader;

class BackgroundAudio {
public:
	BackgroundAudio();
	~BackgroundAudio();

	void SetGame(const Path &path);
	void Update();
	int Play();

	void LoadSamples();
	void PlaySFX(UI::UISound sfx);

private:
	void Clear(bool hard);

	enum {
		BUFSIZE = 44100,
	};

	std::mutex mutex_;
	Path bgGamePath_;
	std::atomic<bool> sndLoadPending_;
	int playbackOffset_ = 0;
	AT3PlusReader *at3Reader_;
	double gameLastChanged_ = 0.0;
	double lastPlaybackTime_ = 0.0;
	int *buffer = nullptr;
	bool fadingOut_ = true;
	float volume_ = 0.0f;
	float delta_ = -0.0001f;

	struct PlayInstance {
		UI::UISound sound;
		int offset;
		int volume; // 0..255
		bool done;
	};

	struct Sample {
		// data must be new-ed.
		Sample(int16_t *data, int length) : data_(data), length_(length) {}
		~Sample() {
			delete[] data_;
		}
		int16_t *data_;
		int length_;  // stereo samples.
	};

	static Sample *LoadSample(const std::string &path);

	std::vector<PlayInstance> plays_;
	std::vector<std::unique_ptr<Sample>> samples_;
};

extern BackgroundAudio g_BackgroundAudio;
