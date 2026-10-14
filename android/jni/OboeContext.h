#pragma once

#include <memory>

#include "AndroidAudio.h"

class OboeContext : public AudioContext {
public:
	OboeContext(AndroidAudioCallback cb, int framesPerBuffer, int sampleRate);
	~OboeContext();

	bool Init() override;
	bool AudioRecord_Start(int sampleRate) override;
	bool AudioRecord_Stop() override;

private:
	class Streams;
	std::shared_ptr<Streams> streams_;
};
