#include <dlfcn.h>
#include <errno.h>

#include "base/logging.h"
#include "android/native_audio.h"
#include "android/native-audio-so.h"

static void *so;
static AndroidAudioCallback s_cb;
static OpenSLWrap_Init_T init_func;
static OpenSLWrap_Shutdown_T shutdown_func;
static bool playing;

bool AndroidAudio_Init(AndroidAudioCallback cb, std::string libraryDir) {
	s_cb = cb;
	playing = false;
	ILOG("Loading native audio library...");
	std::string so_filename = libraryDir + "/libnative_audio.so";
	so = dlopen(so_filename.c_str(), RTLD_LAZY);
	if (!so) {
		ELOG("Failed to find native audio library: %i: %s ", errno, dlerror());
		return false;
	}
	init_func = (OpenSLWrap_Init_T)dlsym(so, "OpenSLWrap_Init");
	shutdown_func = (OpenSLWrap_Shutdown_T)dlsym(so, "OpenSLWrap_Shutdown");
	return true;
}

bool AndroidAudio_Resume() {
	if (!playing) {
		ILOG("Calling OpenSLWrap_Init_T...");
		bool init_retval = init_func(s_cb);
		ILOG("Returned from OpenSLWrap_Init_T");
		playing = true;
		return init_retval;
	}
	return false;
}

bool AndroidAudio_Pause() {
	if (playing) {
		ILOG("Calling OpenSLWrap_Shutdown_T...");
		shutdown_func();
		playing = false;
		return true;
	}
	return false;
}

void AndroidAudio_Shutdown() {
	if (playing) {
		ELOG("Should not shut down when playing! Something is wrong!");
	}
	ILOG("Returned from OpenSLWrap_Shutdown_T");
	dlclose(so);
	so = 0;
	ILOG("OpenSLWrap completely unloaded.");
}
