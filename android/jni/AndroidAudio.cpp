#include "Common/Log.h"

#include "android/jni/AndroidAudio.h"
#include "android/jni/OpenSLContext.h"

AudioContext::AudioContext(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate)
	: audioCallback(cb), framesPerBuffer(_FramesPerBuffer), sampleRate(_SampleRate) {
	if (framesPerBuffer == 0)
		framesPerBuffer = 256;
	if (framesPerBuffer < 32)
		framesPerBuffer = 32;
	if (framesPerBuffer > 4096)
		framesPerBuffer = 4096;

	sampleRate = _SampleRate;
}

struct AndroidAudioState {
	AudioContext *ctx = nullptr;
	AndroidAudioCallback callback = nullptr;
	int frames_per_buffer = 0;
	int sample_rate = 0;
};

AndroidAudioState *AndroidAudio_Init(AndroidAudioCallback callback, int optimalFramesPerBuffer, int optimalSampleRate) {
	AndroidAudioState *state = new AndroidAudioState();
	state->callback = callback;
	state->frames_per_buffer = optimalFramesPerBuffer ? optimalFramesPerBuffer : 256;
	state->sample_rate = optimalSampleRate ? optimalSampleRate : 44100;
	return state;
}

bool AndroidAudio_Resume(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(AUDIO, "Audio was shutdown, cannot resume!");
		return false;
	}
	if (!state->ctx) {
		INFO_LOG(AUDIO, "Calling OpenSLWrap_Init_T...");
		state->ctx = new OpenSLContext(state->callback, state->frames_per_buffer, state->sample_rate);
		INFO_LOG(AUDIO, "Returned from OpenSLWrap_Init_T");
		bool init_retval = state->ctx->Init();
		if (!init_retval) {
			delete state->ctx;
			state->ctx = nullptr;
		}
		return init_retval;
	}
	return false;
}

bool AndroidAudio_Pause(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(AUDIO, "Audio was shutdown, cannot pause!");
		return false;
	}
	if (state->ctx) {
		INFO_LOG(AUDIO, "Calling OpenSLWrap_Shutdown_T...");
		delete state->ctx;
		state->ctx = nullptr;
		INFO_LOG(AUDIO, "Returned from OpenSLWrap_Shutdown_T ...");
		return true;
	}
	return false;
}

bool AndroidAudio_Shutdown(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(AUDIO, "Audio already shutdown!");
		return false;
	}
	if (state->ctx) {
		ERROR_LOG(AUDIO, "Should not shut down when playing! Something is wrong!");
		return false;
	}
	delete state;
	INFO_LOG(AUDIO, "OpenSLWrap completely unloaded.");
	return true;
}
