#include "base/logging.h"
#include "android/native_audio.h"
#include "android/native-audio-so.h"

struct AudioState {
	void *so;
	AndroidAudioCallback callback;
	bool playing;
	int frames_per_buffer;
	int sample_rate;
};

static AudioState *state = 0;

bool AndroidAudio_Init(AndroidAudioCallback callback, std::string libraryDir, int optimalFramesPerBuffer, int optimalSampleRate) {
	if (state != 0) {
		ELOG("Audio state already exists");
		return false;
	}

	state = new AudioState();
	state->callback = callback;
	state->playing = false;
	state->frames_per_buffer = optimalFramesPerBuffer ? optimalFramesPerBuffer : 256;
	state->sample_rate = optimalSampleRate ? optimalSampleRate : 44100;

	return true;
}

bool AndroidAudio_Resume() {
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

bool AndroidAudio_Pause() {
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

void AndroidAudio_Shutdown() {
	if (!state) {
		ELOG("Audio already shutdown!");
		return;
	}
	if (state->playing) {
		ELOG("Should not shut down when playing! Something is wrong!");
	}
	delete state;
	state = 0;
	ILOG("OpenSLWrap completely unloaded.");
}
