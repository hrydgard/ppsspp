#pragma once

#include "Common/CommonWindows.h"

typedef int (*StreamCallback)(short *buffer, int numSamples, int bits, int rate, int channels);

// Note that the backend may override the passed in sample rate. The actual sample rate
// should be returned by GetSampleRate though.
class WindowsAudioBackend {
public:
	WindowsAudioBackend() {}
	virtual ~WindowsAudioBackend() {}
	virtual bool Init(HWND window, StreamCallback _callback, int sampleRate) = 0;
	virtual void Update() {}  // Doesn't have to do anything
	virtual int GetSampleRate() = 0;
};

enum AudioBackendType {
	AUDIO_BACKEND_DSOUND,
	AUDIO_BACKEND_WASAPI,   // TODO
	AUDIO_BACKEND_AUTO
};

// Factory
WindowsAudioBackend *CreateAudioBackend(AudioBackendType type);
