#pragma once

#include <string>
#include <mutex>
#include <vector>

class AT3PlusReader;

enum class MenuSFX {
	SELECT = 0,
	BACK = 1,
	CONFIRM = 2,
	COUNT,
};

class BackgroundAudio {
public:
	BackgroundAudio();
	~BackgroundAudio();

	void Clear(bool hard);
	void SetGame(const std::string &path);
	void Update();
	int Play();

	void LoadSamples();
	void PlaySFX(MenuSFX sfx);

private:
	enum {
		BUFSIZE = 44100,
	};

	std::mutex g_bgMutex;
	std::string bgGamePath;
	int playbackOffset = 0;
	AT3PlusReader *at3Reader;
	double gameLastChanged = 0.0;
	double lastPlaybackTime = 0.0;
	int *buffer = nullptr;
	bool fadingOut = true;
	float volume = 0.0f;
	float delta = -0.0001f;

	struct PlayInstance {
		MenuSFX sound;
		int offset;
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
