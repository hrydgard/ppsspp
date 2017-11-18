#pragma once

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

typedef int (*AndroidAudioCallback)(short *buffer, int num_samples);

class AudioContext {
public:
	AudioContext(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate);
	virtual bool Init() { return false; }
	virtual ~AudioContext() {}

protected:
	AndroidAudioCallback audioCallback;

	int framesPerBuffer;
	int sampleRate;
};

class OpenSLContext : public AudioContext {
public:
	OpenSLContext(AndroidAudioCallback cb, int framesPerBuffer, int sampleRate);

	bool Init() override;
	~OpenSLContext();

private:
	// engine interfaces
	SLObjectItf engineObject = nullptr;
	SLEngineItf engineEngine = nullptr;
	SLObjectItf outputMixObject = nullptr;

	// buffer queue player interfaces
	SLObjectItf bqPlayerObject = nullptr;
	SLPlayItf bqPlayerPlay = nullptr;
	SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue = nullptr;
	SLMuteSoloItf bqPlayerMuteSolo = nullptr;
	SLVolumeItf bqPlayerVolume = nullptr;

	// Double buffering.
	short *buffer[2]{};
	int curBuffer = 0;

	static void bqPlayerCallbackWrap(SLAndroidSimpleBufferQueueItf bq, void *context);
	void BqPlayerCallback(SLAndroidSimpleBufferQueueItf bq);
};