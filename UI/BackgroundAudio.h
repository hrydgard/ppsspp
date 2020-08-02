#pragma once

#include <string>

class AT3PlusReader;

class BackgroundAudio {
public:
	void Clear(bool hard);
	void SetGame(const std::string &path);
	void Update();
	int Play();
private:
	std::mutex g_bgMutex;
	std::string bgGamePath;
	int playbackOffset = 0;
	AT3PlusReader *at3Reader;
	double gameLastChanged = 0.0;
	double lastPlaybackTime = 0.0;
	int buffer[44100];
	bool fadingOut = true;
	float volume = 0.0f;
	float delta = -0.0001f;
};

extern BackgroundAudio g_BackgroundAudio;
