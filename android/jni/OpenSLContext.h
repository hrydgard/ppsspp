#pragma once

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "AndroidAudio.h"

class OpenSLContext : public AudioContext {
public:
	OpenSLContext(AndroidAudioCallback cb, int framesPerBuffer, int sampleRate);

	bool Init() override;
	bool AudioRecord_Start(int sampleRate) override;
	bool AudioRecord_Stop() override;

	~OpenSLContext();

private:
	bool CheckResult(SLresult result, const char *str);
	static bool CheckResultStatic(SLresult result, const char *str);

	// Should be no reason to need more than two buffers, but make it clear in the code.
	enum {
		NUM_BUFFERS = 2,
	};

	// engine interfaces
	SLObjectItf engineObject = nullptr;
	SLEngineItf engineEngine = nullptr;
	SLObjectItf outputMixObject = nullptr;

	// audio recorder interfaces
	SLObjectItf recorderObject = nullptr;
	SLRecordItf recorderRecord = nullptr;
	SLAndroidSimpleBufferQueueItf recorderBufferQueue = nullptr;

	int recordBufferSize = 0;
	short *recordBuffer[NUM_BUFFERS]{};
	int activeRecordBuffer = 0;

	static void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

	// buffer queue player interfaces
	SLObjectItf bqPlayerObject = nullptr;
	SLPlayItf bqPlayerPlay = nullptr;
	SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue = nullptr;
	SLVolumeItf bqPlayerVolume = nullptr;

	// Double buffering.
	short *buffer[NUM_BUFFERS]{};
	int curBuffer = 0;

	static void bqPlayerCallbackWrap(SLAndroidSimpleBufferQueueItf bq, void *context);
	void BqPlayerCallback(SLAndroidSimpleBufferQueueItf bq);
};
