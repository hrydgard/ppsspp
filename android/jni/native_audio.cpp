#include "base/logging.h"
#include "android/jni/native_audio.h"
#include "android/jni/native-audio-so.h"

struct AndroidAudioState {
	void *so;
	AndroidAudioCallback callback;
	bool playing;
	int frames_per_buffer;
	int sample_rate;
};

AndroidAudioState *AndroidAudio_Init(AndroidAudioCallback callback, std::string libraryDir, int optimalFramesPerBuffer, int optimalSampleRate) {
	AndroidAudioState *state = new AndroidAudioState();
	state->callback = callback;
	state->playing = false;
	state->frames_per_buffer = optimalFramesPerBuffer ? optimalFramesPerBuffer : 256;
	state->sample_rate = optimalSampleRate ? optimalSampleRate : 44100;
	return state;
}

bool AndroidAudio_Resume(AndroidAudioState *state) {
	if (!state) {
		ELOG("Audio was shutdown, cannot resume!");
		return false;
	}
	if (!state->playing) {
		ILOG("Calling OpenSLWrap_Init_T...");
		bool init_retval = OpenSLWrap_Init(state->callback, state->frames_per_buffer, state->sample_rate);
		ILOG("Returned from OpenSLWrap_Init_T");
		state->playing = true;
		return init_retval;
	}
	return false;
}

bool AndroidAudio_Pause(AndroidAudioState *state) {
	if (!state) {
		ELOG("Audio was shutdown, cannot pause!");
		return false;
	}
	if (state->playing) {
		ILOG("Calling OpenSLWrap_Shutdown_T...");
		OpenSLWrap_Shutdown();
		ILOG("Returned from OpenSLWrap_Shutdown_T ...");
		state->playing = false;
		return true;
	}
	return false;
}

bool AndroidAudio_Shutdown(AndroidAudioState *state) {
	if (!state) {
		ELOG("Audio already shutdown!");
		return false;
	}
	if (state->playing) {
		ELOG("Should not shut down when playing! Something is wrong!");
		return false;
	}
	delete state;
	ILOG("OpenSLWrap completely unloaded.");
	return true;
}
