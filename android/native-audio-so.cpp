// Minimal audio streaming using OpenSL.
//
// Loosely based on the Android NDK sample code.
// Hardcoded to 44.1kHz stereo 16-bit audio, because as far as I'm concerned,
// that's the only format that makes any sense.

#include <assert.h>
#include <string.h>

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

#define BUFFER_SIZE 512
#define BUFFER_SIZE_IN_SAMPLES (BUFFER_SIZE / 2)

// Double buffering.
static short buffer[2][BUFFER_SIZE];
static int curBuffer = 0;

static AndroidAudioCallback audioCallback;

// This callback handler is called every time a buffer finishes playing.
// The documentation available is very unclear about how to best manage buffers.
// I've chosen to this approach: Instantly enqueue a buffer that was rendered to the last time,
// and then render the next. Hopefully it's okay to spend time in this callback after having enqueued. 
static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
  assert(bq == bqPlayerBufferQueue);
  assert(NULL == context);

  short *nextBuffer = buffer[curBuffer];
  int nextSize = sizeof(buffer[0]);

  SLresult result;
  result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, nextBuffer, nextSize);

  // Comment from sample code:
  // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
  // which for this code example would indicate a programming error
  assert(SL_RESULT_SUCCESS == result);

  curBuffer ^= 1;  // Switch buffer
  // Render to the fresh buffer
  audioCallback(buffer[curBuffer], BUFFER_SIZE_IN_SAMPLES);
}

// create the engine and output mix objects
extern "C" bool OpenSLWrap_Init(AndroidAudioCallback cb) {
  audioCallback = cb;

  SLresult result;
  // create engine
  result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
  assert(SL_RESULT_SUCCESS == result);
  result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
  assert(SL_RESULT_SUCCESS == result);
  result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
  assert(SL_RESULT_SUCCESS == result);
  result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, 0, 0);
  assert(SL_RESULT_SUCCESS == result);
  result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
  assert(SL_RESULT_SUCCESS == result);

  SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
  SLDataFormat_PCM format_pcm = {
    SL_DATAFORMAT_PCM,
    2,
    SL_SAMPLINGRATE_44_1,
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
  assert(SL_RESULT_SUCCESS == result);

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
  curBuffer = 0;
  audioCallback(buffer[curBuffer], BUFFER_SIZE_IN_SAMPLES);

  result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue, buffer[curBuffer], sizeof(buffer[curBuffer]));
  if (SL_RESULT_SUCCESS != result) {
    return false;
  }
  curBuffer ^= 1;
  return true;
}

// shut down the native audio system
extern "C" void OpenSLWrap_Shutdown() {
  if (bqPlayerObject != NULL) {
    (*bqPlayerObject)->Destroy(bqPlayerObject);
    bqPlayerObject = NULL;
    bqPlayerPlay = NULL;
    bqPlayerBufferQueue = NULL;
    bqPlayerMuteSolo = NULL;
    bqPlayerVolume = NULL;
  }
  if (outputMixObject != NULL) {
    (*outputMixObject)->Destroy(outputMixObject);
    outputMixObject = NULL;
  }
  if (engineObject != NULL) {
    (*engineObject)->Destroy(engineObject);
    engineObject = NULL;
    engineEngine = NULL;
  }
}
