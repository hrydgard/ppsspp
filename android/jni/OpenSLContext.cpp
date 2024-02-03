// Minimal audio streaming using OpenSL.
//
// Loosely based on the Android NDK sample code.

#include <cstring>
#include <unistd.h>

// for native audio
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "Common/Log.h"
#include "OpenSLContext.h"
#include "Core/HLE/sceUsbMic.h"

void OpenSLContext::bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
	OpenSLContext *ctx = (OpenSLContext *)context;
	SLresult result;

	SLuint32 recordsState;
	result = (*ctx->recorderRecord)->GetRecordState(ctx->recorderRecord, &recordsState);
	if (!CheckResultStatic(result, "GetRecordState error: %d"))
		return;

	Microphone::addAudioData((uint8_t*) ctx->recordBuffer[ctx->activeRecordBuffer], ctx->recordBufferSize);

	if (recordsState == SL_RECORDSTATE_RECORDING) {
		result = (*ctx->recorderBufferQueue)->Enqueue(ctx->recorderBufferQueue, ctx->recordBuffer[ctx->activeRecordBuffer], ctx->recordBufferSize);
		CheckResultStatic(result, "Enqueue error");
	}

	ctx->activeRecordBuffer += 1; // Switch buffer
	if (ctx->activeRecordBuffer == NUM_BUFFERS)
		ctx->activeRecordBuffer = 0;
}

// This callback handler is called every time a buffer finishes playing.
// The documentation available is very unclear about how to best manage buffers.
// I've chosen to this approach: Instantly enqueue a buffer that was rendered to the last time,
// and then render the next. Hopefully it's okay to spend time in this callback after having enqueued. 
void OpenSLContext::bqPlayerCallbackWrap(SLAndroidSimpleBufferQueueItf bq, void *context) {
	OpenSLContext *ctx = (OpenSLContext *)context;
	ctx->BqPlayerCallback(bq);
}

void OpenSLContext::BqPlayerCallback(SLAndroidSimpleBufferQueueItf bq) {
	if (bq != bqPlayerBufferQueue) {
		ERROR_LOG(AUDIO, "OpenSL: Wrong bq!");
		return;
	}

	int renderedFrames = audioCallback(buffer[curBuffer], framesPerBuffer, SampleRate());

	int sizeInBytes = framesPerBuffer * 2 * sizeof(short);
	int byteCount = (framesPerBuffer - renderedFrames) * 4;
	if (byteCount > 0) {
		memset(buffer[curBuffer] + renderedFrames * 2, 0, byteCount);
	}
	SLresult result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buffer[curBuffer], sizeInBytes);
	
	// TODO: get rid of this snprintf too
	CheckResult(result, "Failed to enqueue");
	// Comment from sample code:
	// the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
	// which for this code example would indicate a programming error
	if (result != SL_RESULT_SUCCESS) {
		ERROR_LOG(AUDIO, "OpenSL: Failed to enqueue! %i %i", renderedFrames, sizeInBytes);
	}

	curBuffer += 1; // Switch buffer
	if (curBuffer == NUM_BUFFERS)
		curBuffer = 0;
}

// create the engine and output mix objects
OpenSLContext::OpenSLContext(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate)
	: AudioContext(cb, _FramesPerBuffer, _SampleRate) {}

bool OpenSLContext::Init() {
	SLresult result;
	// create engine
	result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
	if (!CheckResult(result, "slCreateEngine")) {
		engineObject = nullptr;
		return false;
	}

	result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
	if (!CheckResult(result, "engine->Realize"))
		return false;

	result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
	if (!CheckResult(result, "engine->GetInterface(ENGINE)"))
		return false;

	result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, 0, 0);
	if (!CheckResult(result, "engine->CreateOutputMix(ENGINE)")) {
		(*engineObject)->Destroy(engineObject);
		engineEngine = nullptr;
		engineObject = nullptr;
		return false;
	}

	result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
	if (!CheckResult(result, "outputMix->Realize"))
		return false;

	// The constants, such as SL_SAMPLINGRATE_44_1, are just 44100000.
	SLuint32 sr = (SLuint32)sampleRate * 1000;

	SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, NUM_BUFFERS};
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
	result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject, &audioSrc, &audioSnk,
			sizeof(ids)/sizeof(ids[0]), ids, req);
	if (result != SL_RESULT_SUCCESS) {
		ERROR_LOG(AUDIO, "OpenSL: CreateAudioPlayer failed: %d", (int)result);
		(*outputMixObject)->Destroy(outputMixObject);
		outputMixObject = nullptr;

		// Should really tear everything down here. Sigh.
		(*engineObject)->Destroy(engineObject);
		engineEngine = nullptr;
		engineObject = nullptr;
		return false;
	}

	result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
	if (!CheckResult(result, "player->Realize"))
		return false;  // TODO: Release stuff!
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
	if (!CheckResult(result, "player->GetInterface(PLAY)"))
		return false;  // TODO: Release stuff!
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE, &bqPlayerBufferQueue);
	if (!CheckResult(result, "player->GetInterface(BUFFER_QUEUE)"))
		return false;  // TODO: Release stuff!
	result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, &bqPlayerCallbackWrap, this);
	if (!CheckResult(result, "playerbq->RegisterCallback()"))
		return false;  // TODO: Release stuff!
	result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
	if (!CheckResult(result, "playerbq->GetInterface()"))
		return false;  // TODO: Release stuff!
	result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
	if (!CheckResult(result, "playerbq->SetPlayState(PLAYING)"))
		return false;  // TODO: Release stuff!

	// Allocate and enqueue N empty buffers.
	for (int i = 0; i < NUM_BUFFERS; i++) {
		buffer[i] = new short[framesPerBuffer * 2]{};
	}

	int sizeInBytes = framesPerBuffer * 2 * sizeof(short);
	for (int i = 0; i < NUM_BUFFERS; i++) {
		result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buffer[i], sizeInBytes);
		if (SL_RESULT_SUCCESS != result) {
			return false;
		}
	}

	curBuffer = 0;
	return true;
}

bool OpenSLContext::AudioRecord_Start(int sampleRate) {
	SLresult result;

	if (!engineEngine) {
		SetErrorString("AudioRecord_Start: No engine");
		return false;
	}

	// configure audio source
	SLDataLocator_IODevice loc_dev = {SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT, SL_DEFAULTDEVICEID_AUDIOINPUT, NULL};
	SLDataSource audioSrc = {&loc_dev, NULL};

	// configure audio sink
	SLDataLocator_AndroidSimpleBufferQueue loc_bq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
	SLDataFormat_PCM format_pcm = {
			SL_DATAFORMAT_PCM,
			1,
			(SLuint32) sampleRate * 1000, // The constants such as SL_SAMPLINGRATE_44_1 are 44100000
			SL_PCMSAMPLEFORMAT_FIXED_16,
			SL_PCMSAMPLEFORMAT_FIXED_16,
			SL_SPEAKER_FRONT_CENTER,
			SL_BYTEORDER_LITTLEENDIAN
	};
	SLDataSink audioSnk = {&loc_bq, &format_pcm};

	// create audio recorder
	const SLInterfaceID id[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
	const SLboolean req[1] = {SL_BOOLEAN_TRUE};
	result = (*engineEngine)->CreateAudioRecorder(engineEngine, &recorderObject, &audioSrc, &audioSnk,
			sizeof(id)/sizeof(id[0]), id, req);
	if (!CheckResult(result, "CreateAudioRecorder failed"))
		return false;

	// realize the audio recorder
	result = (*recorderObject)->Realize(recorderObject, SL_BOOLEAN_FALSE);
	if (!CheckResult(result, "recorderObject->Realize failed"))
		return false;


	// get the record interface
	result = (*recorderObject)->GetInterface(recorderObject, SL_IID_RECORD, &recorderRecord);
	if (!CheckResult(result, "GetInterface(recorderObject) failed"))
		return false;

	// get the buffer queue interface
	result = (*recorderObject)->GetInterface(recorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, (void*) &recorderBufferQueue);
	if (!CheckResult(result, "GetInterface(queue interface) failed"))
		return false;

	// register callback on the buffer queue
	result = (*recorderBufferQueue)->RegisterCallback(recorderBufferQueue, &bqRecorderCallback, this);
	if (!CheckResult(result, "RegisterCallback failed"))
		return false;

	recordBufferSize = (44100 * 20 / 1000 * 2);
	for (int i = 0; i < NUM_BUFFERS; i++) {
		recordBuffer[i] = new short[recordBufferSize];
	}
	for (int i = 0; i < NUM_BUFFERS; i++) {
		result = (*recorderBufferQueue)->Enqueue(recorderBufferQueue, recordBuffer[i], recordBufferSize);
		if (!CheckResult(result, "Enqueue failed"))
			return false;
	}

	result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_RECORDING);
	return CheckResult(result, "SetRecordState(recording) failed");
}

bool OpenSLContext::AudioRecord_Stop() {
	if (recorderRecord != nullptr) {
		SLresult result = (*recorderRecord)->SetRecordState(recorderRecord, SL_RECORDSTATE_STOPPED);
		CheckResult(result, "SetRecordState(stopped) failed");
	}
	if (recorderObject != nullptr) {
		(*recorderObject)->Destroy(recorderObject);
		recorderObject = nullptr;
		recorderRecord = nullptr;
		recorderBufferQueue = nullptr;
	}
	if (recordBuffer[0] != nullptr) {
		delete [] recordBuffer[0];
		delete [] recordBuffer[1];
		recordBuffer[0] = nullptr;
		recordBuffer[1] = nullptr;
	}
	return true;
}

// shut down the native audio system
OpenSLContext::~OpenSLContext() {
	if (bqPlayerPlay) {
		INFO_LOG(AUDIO, "OpenSL: Shutdown - stopping playback");
		SLresult result;
		result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
		CheckResult(result, "SetPlayState(stopped) failed");
	}

	INFO_LOG(AUDIO, "OpenSL: Shutdown - deleting player object");

	if (bqPlayerObject) {
		(*bqPlayerObject)->Destroy(bqPlayerObject);
		bqPlayerObject = nullptr;
		bqPlayerPlay = nullptr;
		bqPlayerBufferQueue = nullptr;
		bqPlayerVolume = nullptr;
	}

	INFO_LOG(AUDIO, "OpenSL: Shutdown - deleting mix object");

	if (outputMixObject) {
		(*outputMixObject)->Destroy(outputMixObject);
		outputMixObject = nullptr;
	}
	AudioRecord_Stop();

	INFO_LOG(AUDIO, "OpenSL: Shutdown - deleting engine object");

	if (engineObject) {
		(*engineObject)->Destroy(engineObject);
		engineObject = nullptr;
		engineEngine = nullptr;
	}

	for (int i = 0; i < NUM_BUFFERS; i++) {
		delete[] buffer[i];
		buffer[i] = nullptr;
	}
	INFO_LOG(AUDIO, "OpenSL: Shutdown - finished");
}

bool OpenSLContext::CheckResult(SLresult result, const char *str) {
	if (result != SL_RESULT_SUCCESS) {
		ERROR_LOG(AUDIO, "OpenSL failure (%s): %d", str, result);
		SetErrorString(str);
		return false;
	} else {
		return true;
	}
}

bool OpenSLContext::CheckResultStatic(SLresult result, const char *str) {
	if (result != SL_RESULT_SUCCESS) {
		ERROR_LOG(AUDIO, "OpenSL failure (%s): %d", str, result);
		return false;
	} else {
		return true;
	}
}
