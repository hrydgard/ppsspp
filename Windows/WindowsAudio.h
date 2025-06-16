#pragma once

#include "Common/CommonWindows.h"
#include "Core/ConfigValues.h"

// Always 2 channels, 16-bit audio.
typedef int (*StreamCallback)(short *buffer, int numSamples, int rate, void *userdata);

// Note that the backend may override the passed in sample rate. The actual sample rate
// should be returned by GetSampleRate though.
class WindowsAudioBackend {
public:
	virtual ~WindowsAudioBackend() {}
	virtual bool Init(StreamCallback _callback, void *userdata = nullptr) = 0;
	virtual int GetSampleRate() const = 0;
	virtual int PeriodFrames() const = 0;
	virtual void FrameUpdate() {}
};

// Factory
WindowsAudioBackend *CreateAudioBackend(AudioBackendType type);
