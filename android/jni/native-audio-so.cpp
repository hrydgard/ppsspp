// Minimal audio streaming using OpenSL.
//
// Loosely based on the Android NDK sample code.

#include <assert.h>
#include <string.h>
#include <unistd.h>

// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "../base/logging.h"
#include "native-audio-so.h"

// This is kinda ugly, but for simplicity I've left these as globals just like in the sample,
// as there's not really any use case for this where we have multiple audio devices yet.

// engine interfaces
static SLObjectItf engineObject;
static SLEngineItf engineEngine;
static SLObjectItf outputMixObject;

// buffer queue player interfaces
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
static SLMuteSoloItf bqPlayerMuteSolo;
static SLVolumeItf bqPlayerVolume;

// Double buffering.
static short *buffer[2];
static int curBuffer = 0;
static int framesPerBuffer;
int sampleRate;

static AndroidAudioCallback audioCallback;

// This callback handler is called every time a buffer finishes playing.
// The documentation available is very unclear about how to best manage buffers.
// I've chosen to this approach: Instantly enqueue a buffer that was rendered to the last time,
// and then render the next. Hopefully it's okay to spend time in this callback after having enqueued. 
static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
	if (bq != bqPlayerBufferQueue) {
		ELOG("Wrong bq!");
		return;
	}

	int renderedFrames = audioCallback(buffer[curBuffer], framesPerBuffer);

	int sizeInBytes = framesPerBuffer * 2 * sizeof(short);
	int byteCount = (framesPerBuffer - renderedFrames) * 4;
	if (byteCount > 0) {
		memset(buffer[curBuffer] + renderedFrames * 2, 0, byteCount);
	}
	SLresult result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buffer[curBuffer], sizeInBytes);

	// Comment from sample code:
	// the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
	// which for this code example would indicate a programming error
	if (result != SL_RESULT_SUCCESS) {
		ELOG("OpenSL ES: Failed to enqueue! %i %i", renderedFrames, sizeInBytes);
	}

	curBuffer ^= 1;	// Switch buffer
}

// create the engine and output mix objects
bool OpenSLWrap_Init(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate) {
	audioCallback = cb;
	framesPerBuffer = _FramesPerBuffer;
	if (framesPerBuffer == 0)
		framesPerBuffer = 256;
	if (framesPerBuffer < 32)
		framesPerBuffer = 32;
	if (framesPerBuffer > 4096)
		framesPerBuffer = 4096;

	sampleRate = _SampleRate;
	if (sampleRate != 44100 && sampleRate != 48000) {
		ELOG("Invalid sample rate %i - choosing 44100", sampleRate);
		sampleRate = 44100;
	}

	SLresult result;
	// create engine
	result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
	if (result != SL_RESULT_SUCCESS) {
		ELOG("OpenSL ES: Failed to create the engine: %d", (int)result);
		return false;
	}
	result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
	assert(SL_RESULT_SUCCESS == result);
	result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
	assert(SL_RESULT_SUCCESS == result);
	result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, 0, 0);
	assert(SL_RESULT_SUCCESS == result);
	result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
	assert(SL_RESULT_SUCCESS == result);

	SLuint32 sr = SL_SAMPLINGRATE_44_1;
	if (sampleRate == 48000) {
		sr = SL_SAMPLINGRATE_48;
	} // Don't allow any other sample rates.

	SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
	SLDataFormat_PCM format_pcm = {
		SL_DATAFORMAT_PCM,
		2,
		sr,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_PCMSAMPLEFORMAT_FIXED_16,
		SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
		SL_BYTEORDER_LITTLEENDIAN
	};

	SLDataSource audioSrc = {&loc_bufq, &format_pcm};

	// configure audio sink
	SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
	SLDataSink audioSnk = {&loc_outmix, NULL};

	// create audio player
	const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
	const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
	result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk, 2, ids, req);
	if (result != SL_RESULT_SUCCESS) {
		ELOG("OpenSL ES: CreateAudioPlayer failed: %d", (int)result);
		// Should really tear everything down here. Sigh.
		return false;
	}

	result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
	assert(SL_RESULT_SUCCESS == result);
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
	assert(SL_RESULT_SUCCESS == result);
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
		&bqPlayerBufferQueue);
	assert(SL_RESULT_SUCCESS == result);
	result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, NULL);
	assert(SL_RESULT_SUCCESS == result);
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
	assert(SL_RESULT_SUCCESS == result);
	result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
	assert(SL_RESULT_SUCCESS == result);

	// Render and enqueue a first buffer. (or should we just play the buffer empty?)
	buffer[0] = new short[framesPerBuffer * 2];
	buffer[1] = new short[framesPerBuffer * 2];

	curBuffer = 0;
	audioCallback(buffer[curBuffer], framesPerBuffer);

	result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buffer[curBuffer], sizeof(buffer[curBuffer]));
	if (SL_RESULT_SUCCESS != result) {
		return false;
	}
	curBuffer ^= 1;
	return true;
}

// shut down the native audio system
void OpenSLWrap_Shutdown() {
	if (bqPlayerPlay) {
		ILOG("OpenSLWrap_Shutdown - stopping playback");
		SLresult result;
		result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
		if (SL_RESULT_SUCCESS != result) {
			ELOG("SetPlayState failed");
		}
	}

	ILOG("OpenSLWrap_Shutdown - deleting player object");

	if (bqPlayerObject) {
		(*bqPlayerObject)->Destroy(bqPlayerObject);
		bqPlayerObject = nullptr;
		bqPlayerPlay = nullptr;
		bqPlayerBufferQueue = nullptr;
		bqPlayerMuteSolo = nullptr;
		bqPlayerVolume = nullptr;
	}

	ILOG("OpenSLWrap_Shutdown - deleting mix object");

	if (outputMixObject) {
		(*outputMixObject)->Destroy(outputMixObject);
		outputMixObject = nullptr;
	}

	ILOG("OpenSLWrap_Shutdown - deleting engine object");

	if (engineObject) {
		(*engineObject)->Destroy(engineObject);
		engineObject = nullptr;
		engineEngine = nullptr;
	}
	delete [] buffer[0];
	delete [] buffer[1];
	buffer[0] = nullptr;
	buffer[1] = nullptr;
	ILOG("OpenSLWrap_Shutdown - finished");
}	

