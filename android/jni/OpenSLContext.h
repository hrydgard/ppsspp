#pragma once

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "AndroidAudio.h"

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
	SLVolumeItf bqPlayerVolume = nullptr;

	// Should be no reason to need more than two buffers, but make it clear in the code.
	enum {
		NUM_BUFFERS = 2,
	};

	// Double buffering.
	short *buffer[NUM_BUFFERS]{};
	int curBuffer = 0;

	static void bqPlayerCallbackWrap(SLAndroidSimpleBufferQueueItf bq, void *context);
	void BqPlayerCallback(SLAndroidSimpleBufferQueueItf bq);
};
