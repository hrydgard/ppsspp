#include "Common/Audio/SampleRing.h"
#include "Common/Log.h"
#include "Core/HLE/sceUsbMic.h"

#include "android/jni/AndroidAudio.h"
#include "android/jni/OboeContext.h"

std::string g_error;
std::string g_info;
std::mutex g_errorMutex;

// Pause and resume come from the Java UI thread, a restart from the settings screen.
static std::mutex g_ctxMutex;

// From the input stream's callback to the CPU thread. 0.7 s at 44.1 kHz.
static SampleRing<32768> g_micRing;

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
	g_info = "";
}

void AudioContext::SetErrorString(const std::string &error) {
	std::unique_lock<std::mutex> lock(g_errorMutex);
	g_error = error;
}

void AudioContext::SetInfoString(const std::string &info) {
	std::unique_lock<std::mutex> lock(g_errorMutex);
	g_info = info;
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
	std::lock_guard<std::mutex> guard(g_ctxMutex);
	state->input_enable = 1;
	if (!state->ctx) {
		ERROR_LOG(Log::Audio, "Audio context not initialized, cannot start recording!");
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
	std::lock_guard<std::mutex> guard(g_ctxMutex);
	// Clear these even while paused, or resume would start the mic again.
	state->input_enable = 0;
	state->input_sample_rate = 0;
	if (!state->ctx) {
		return true;
	}
	state->ctx->AudioRecord_Stop();
	INFO_LOG(Log::Audio, "AndroidAudio_Recording_Stop");
	return true;
}

void AndroidAudio_Recording_Push(const int16_t *samples, int count) {
	g_micRing.Push(samples, count);
}

void AndroidAudio_Recording_Poll() {
	g_micRing.Drain([](const int16_t *samples, uint32_t count) {
		Microphone::addAudioData((u8 *)samples, count * sizeof(int16_t));
	});
}

void AndroidAudio_Recording_Clear() {
	g_micRing.Clear();
}

static bool StartContext(AndroidAudioState *state) {
	state->ctx = new OboeContext(state->callback, state->frames_per_buffer, state->sample_rate);
	bool init_retval = state->ctx->Init();
	if (!init_retval) {
		delete state->ctx;
		state->ctx = nullptr;
	} else if (state->input_enable) {
		state->ctx->AudioRecord_Start(state->input_sample_rate);
	}
	return init_retval;
}

bool AndroidAudio_Resume(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(Log::Audio, "Audio was shutdown, cannot resume!");
		return false;
	}
	std::lock_guard<std::mutex> guard(g_ctxMutex);
	if (!state->ctx) {
		return StartContext(state);
	}
	return false;
}

bool AndroidAudio_Restart(AndroidAudioState *state) {
	if (!state) {
		return false;
	}
	std::lock_guard<std::mutex> guard(g_ctxMutex);
	// While paused, there's nothing to restart. Resume picks up the settings.
	if (!state->ctx) {
		return false;
	}
	delete state->ctx;
	state->ctx = nullptr;
	return StartContext(state);
}

bool AndroidAudio_Pause(AndroidAudioState *state) {
	if (!state) {
		ERROR_LOG(Log::Audio, "Audio was shutdown, cannot pause!");
		return false;
	}
	std::lock_guard<std::mutex> guard(g_ctxMutex);
	if (state->ctx) {
		delete state->ctx;
		state->ctx = nullptr;
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
	INFO_LOG(Log::Audio, "Android audio completely unloaded.");
	return true;
}

const std::string AndroidAudio_GetErrorString(AndroidAudioState *state) {
	if (!state) {
		return "No state";
	}
	std::unique_lock<std::mutex> lock(g_errorMutex);
	return g_error;
}

const std::string AndroidAudio_GetInfoString(AndroidAudioState *state) {
	if (!state) {
		return "";
	}
	std::unique_lock<std::mutex> lock(g_errorMutex);
	return g_info;
}
