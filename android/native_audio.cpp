#include <dlfcn.h>
#include <errno.h>

#include "base/logging.h"
#include "android/native_audio.h"
#include "android/native-audio-so.h"

struct AudioState {
	void *so;
	AndroidAudioCallback s_cb;
	OpenSLWrap_Init_T init_func;
	OpenSLWrap_Shutdown_T shutdown_func;
	bool playing;
	int frames_per_buffer;
	int sample_rate;
};

static AudioState *state = 0;

bool AndroidAudio_Init(AndroidAudioCallback cb, std::string libraryDir, int optimalFramesPerBuffer, int optimalSampleRate) {
	if (state != 0) {
		ELOG("Audio state already exists");
		return false;
	}

	ILOG("Loading native audio library...");
	std::string so_filename = libraryDir + "/libnative_audio.so";
	void *the_so = dlopen(so_filename.c_str(), RTLD_LAZY);
	if (!the_so) {
		ELOG("Failed to find native audio library: %i: %s ", errno, dlerror());
		return false;
	}

	state = new AudioState();
	state->so = the_so;
	state->s_cb = cb;
	state->playing = false;
	state->init_func = (OpenSLWrap_Init_T)dlsym(state->so, "OpenSLWrap_Init");
	state->shutdown_func = (OpenSLWrap_Shutdown_T)dlsym(state->so, "OpenSLWrap_Shutdown");
	state->frames_per_buffer = optimalFramesPerBuffer ? optimalFramesPerBuffer : 256;
	state->sample_rate = optimalSampleRate ? optimalSampleRate : 44100;

	ILOG("OpenSLWrap init_func: %p   shutdown_func: %p", (void *)state->init_func, (void *)state->shutdown_func);

	return true;
}

bool AndroidAudio_Resume() {
	if (!state) {
		ELOG("Audio was shutdown, cannot resume!");
		return false;
	}
	if (!state->playing) {
		ILOG("Calling OpenSLWrap_Init_T...");
		bool init_retval = state->init_func(state->s_cb, state->frames_per_buffer, state->sample_rate);
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
		state->shutdown_func();
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
	ILOG("dlclose");
	dlclose(state->so);
	ILOG("Returned from dlclose");
	state->so = 0;
	delete state;
	state = 0;
	ILOG("OpenSLWrap completely unloaded.");
}
