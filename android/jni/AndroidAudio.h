#pragma once

#include <string>
#include <mutex>

typedef int (*AndroidAudioCallback)(short *buffer, int num_samples);

class AudioContext {
public:
	AudioContext(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate);
	virtual bool Init() { return false; }
	virtual bool AudioRecord_Start(int sampleRate) { return false; };
	virtual bool AudioRecord_Stop() { return false; };
	const std::string &GetErrorString() {
		std::unique_lock<std::mutex> lock(errorMutex_);
		return error_;
	}

	virtual ~AudioContext() {}

protected:
	void SetErrorString(const std::string &error) {
		std::unique_lock<std::mutex> lock(errorMutex_);
		error_ = error;
	}
	AndroidAudioCallback audioCallback;

	int framesPerBuffer;
	int sampleRate;
	std::string error_ = "";
	std::mutex errorMutex_;
};

struct AndroidAudioState;

// TODO: Get rid of this unnecessary wrapper layer from the old .so days

// It's okay for optimalFramesPerBuffer and optimalSampleRate to be 0. Defaults will be used.
AndroidAudioState *AndroidAudio_Init(AndroidAudioCallback cb, int optimalFramesPerBuffer, int optimalSampleRate);
bool AndroidAudio_Recording_SetSampleRate(AndroidAudioState *state, int sampleRate);
bool AndroidAudio_Recording_Start(AndroidAudioState *state);
bool AndroidAudio_Recording_Stop(AndroidAudioState *state);
bool AndroidAudio_Recording_State(AndroidAudioState *state);
bool AndroidAudio_Pause(AndroidAudioState *state);
bool AndroidAudio_Resume(AndroidAudioState *state);
bool AndroidAudio_Shutdown(AndroidAudioState *state);
const std::string AndroidAudio_GetErrorString(AndroidAudioState *state);
