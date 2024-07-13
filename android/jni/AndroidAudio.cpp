#include "Common/Log.h"

#include "android/jni/AndroidAudio.h"
#include "android/jni/OpenSLContext.h"

std::string g_error;
std::mutex g_errorMutex;

AudioContext::AudioContext(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate)
	: audioCallback(cb), framesPerBuffer(_FramesPerBuffer), sampleRate(_SampleRate) {
	if (framesPerBuffer == 0)
		framesPerBuffer = 256;
	if (framesPerBuffer < 32)
		framesPerBuffer = 32;
	if (framesPerBuffer > 4096)
		framesPerBuffer = 4096;

	sampleRate = _SampleRate;
	g_error = "";
}

void AudioContext::SetErrorString(const std::string &error) {
	std::unique_lock<std::mutex> lock(g_errorMutex);
	g_error = error;
}

struct AndroidAudioState {
	AudioContext *ctx = nullptr;
	AndroidAudioCallback callback = nullptr;
	// output
	int frames_per_buffer = 0;
	int sample_rate = 0;
	// input
	int input_enable = 0;
	int input_sample_rate = 0;
};

AndroidAudioState *AndroidAudio_Init(AndroidAudioCallback callback, int optimalFramesPerBuffer, int optimalSampleRate) {
	AndroidAudioState *state = new AndroidAudioState();
	state->callback = callback;
	state->frames_per_buffer = optimalFramesPerBuffer ? optimalFramesPerBuffer : 256;
	state->sample_rate = optimalSampleRate ? optimalSampleRate : 44100;
	return state;
}

bool AndroidAudio_Recording_SetSampleRate(AndroidAudioState *state, int sampleRate) {
	if (!state) {
		ERROR_LOG(Log::Audio, "AndroidAudioState not initialized, cannot set recording sample rate");
		return false;
	}
	state->input_sample_rate = sampleRate;
	INFO_LOG(Log::Audio, "AndroidAudio_Recording_SetSampleRate=%d", sampleRate);
	return true;
}

bool AndroidAudio_Recording_Start(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(Log::Audio, "AndroidAudioState not initialized, cannot start recording!");
		return false;
	}
	state->input_enable = 1;
	if (!state->ctx) {
		ERROR_LOG(Log::Audio, "OpenSLContext not initialized, cannot start recording!");
		return false;
	}
	state->ctx->AudioRecord_Start(state->input_sample_rate);
	INFO_LOG(Log::Audio, "AndroidAudio_Recording_Start");
	return true;
}

bool AndroidAudio_Recording_Stop(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(Log::Audio, "AndroidAudioState not initialized, cannot stop recording!");
		return false;
	}
	if (!state->ctx) {
		ERROR_LOG(Log::Audio, "OpenSLContext not initialized, cannot stop recording!");
		return false;
	}
	state->input_enable = 0;
	state->input_sample_rate = 0;
	state->ctx->AudioRecord_Stop();
	INFO_LOG(Log::Audio, "AndroidAudio_Recording_Stop");
	return true;
}

bool AndroidAudio_Recording_State(AndroidAudioState *state) {
	if (!state) {
		return false;
	}
	return state->input_enable;
}

bool AndroidAudio_Resume(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(Log::Audio, "Audio was shutdown, cannot resume!");
		return false;
	}
	if (!state->ctx) {
		INFO_LOG(Log::Audio, "Calling OpenSLWrap_Init_T...");
		state->ctx = new OpenSLContext(state->callback, state->frames_per_buffer, state->sample_rate);
		INFO_LOG(Log::Audio, "Returned from OpenSLWrap_Init_T");
		bool init_retval = state->ctx->Init();
		if (!init_retval) {
			delete state->ctx;
			state->ctx = nullptr;
		}
		if (state->input_enable) {
			state->ctx->AudioRecord_Start(state->input_sample_rate);
		}
		return init_retval;
	}
	return false;
}

bool AndroidAudio_Pause(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(Log::Audio, "Audio was shutdown, cannot pause!");
		return false;
	}
	if (state->ctx) {
		INFO_LOG(Log::Audio, "Calling OpenSLWrap_Shutdown_T...");
		delete state->ctx;
		state->ctx = nullptr;
		INFO_LOG(Log::Audio, "Returned from OpenSLWrap_Shutdown_T ...");
		return true;
	}
	return false;
}

bool AndroidAudio_Shutdown(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(Log::Audio, "Audio already shutdown!");
		return false;
	}
	if (state->ctx) {
		ERROR_LOG(Log::Audio, "Should not shut down when playing! Something is wrong!");
		return false;
	}
	delete state;
	INFO_LOG(Log::Audio, "OpenSLWrap completely unloaded.");
	return true;
}

const std::string AndroidAudio_GetErrorString(AndroidAudioState *state) {
	if (!state) {
		return "No state";
	}
	std::unique_lock<std::mutex> lock(g_errorMutex);
	return g_error;
}
