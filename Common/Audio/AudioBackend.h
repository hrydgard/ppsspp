#pragma once

#include <string>
#include <string_view>
#include <vector>

// Always 2 channels, 16-bit audio.
typedef int (*StreamCallback)(short *buffer, int numSamples, int rate, void *userdata);

// Note that the backend may override the passed in sample rate. The actual sample rate
// should be returned by SampleRate though.
class AudioBackend {
public:
	virtual ~AudioBackend() {}
	virtual bool Init(StreamCallback _callback, void *userdata = nullptr) = 0;
	virtual int SampleRate() const = 0;
	virtual int PeriodFrames() const = 0;
	virtual void FrameUpdate() {}
};
