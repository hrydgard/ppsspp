#pragma once

#include "Common/CommonWindows.h"
#include "Core/ConfigValues.h"

// Always 2 channels, 16-bit audio.
typedef int (*StreamCallback)(short *buffer, int numSamples, int rate);

// Note that the backend may override the passed in sample rate. The actual sample rate
// should be returned by GetSampleRate though.
class WindowsAudioBackend {
public:
	virtual ~WindowsAudioBackend() {}
	virtual bool Init(HWND window, StreamCallback _callback, int sampleRate) = 0;
	virtual int GetSampleRate() const = 0;
};

// Factory
WindowsAudioBackend *CreateAudioBackend(AudioBackendType type);
