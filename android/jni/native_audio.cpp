#include "base/logging.h"
#include "android/jni/native_audio.h"
#include "android/jni/native-audio-so.h"

struct AndroidAudioState {
	AudioContext *ctx = nullptr;
	AndroidAudioCallback callback = nullptr;
	int frames_per_buffer = 0;
	int sample_rate = 0;
};

AndroidAudioState *AndroidAudio_Init(AndroidAudioCallback callback, std::string libraryDir, int optimalFramesPerBuffer, int optimalSampleRate) {
	AndroidAudioState *state = new AndroidAudioState();
	state->callback = callback;
	state->frames_per_buffer = optimalFramesPerBuffer ? optimalFramesPerBuffer : 256;
	state->sample_rate = optimalSampleRate ? optimalSampleRate : 44100;
	return state;
}

bool AndroidAudio_Resume(AndroidAudioState *state) {
	if (!state) {
		ELOG("Audio was shutdown, cannot resume!");
		return false;
	}
	if (!state->ctx) {
		ILOG("Calling OpenSLWrap_Init_T...");
		state->ctx = new OpenSLContext(state->callback, state->frames_per_buffer, state->sample_rate);
		ILOG("Returned from OpenSLWrap_Init_T");
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
		ELOG("Audio was shutdown, cannot pause!");
		return false;
	}
	if (state->ctx) {
		ILOG("Calling OpenSLWrap_Shutdown_T...");
		delete state->ctx;
		state->ctx = nullptr;
		ILOG("Returned from OpenSLWrap_Shutdown_T ...");
		return true;
	}
	return false;
}

bool AndroidAudio_Shutdown(AndroidAudioState *state) {
	if (!state) {
		ELOG("Audio already shutdown!");
		return false;
	}
	if (state->ctx) {
		ELOG("Should not shut down when playing! Something is wrong!");
		return false;
	}
	delete state;
	ILOG("OpenSLWrap completely unloaded.");
	return true;
}
