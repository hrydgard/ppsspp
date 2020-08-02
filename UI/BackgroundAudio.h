#pragma once

#include <string>
#include <mutex>

class AT3PlusReader;

class BackgroundAudio {
public:
	BackgroundAudio();
	~BackgroundAudio();

	void Clear(bool hard);
	void SetGame(const std::string &path);
	void Update();
	int Play();
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
};

extern BackgroundAudio g_BackgroundAudio;
