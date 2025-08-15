#pragma once

#include <string>
#include <string_view>
#include <vector>

// For absolute minimal latency, we do not use std::function.
// We always request mixing to 2 channels, then if needed we can expand here.
typedef void (*RenderCallback)(float *dest, int framesToWrite, int sampleRateHz, void *userdata);

enum class LatencyMode {
	Safe,
	Aggressive
};

struct AudioDeviceDesc {
	std::string name;      // User-friendly name
	std::string uniqueId;  // store-able ID for settings.
};

inline float FramesToMs(int frames, int sampleRate) {
	return 1000.0f * (float)frames / (float)sampleRate;
}

class AudioBackend {
public:
	virtual ~AudioBackend() {}
	virtual void EnumerateDevices(std::vector<AudioDeviceDesc> *outputDevices, bool captureDevices = false) = 0;
	virtual void SetRenderCallback(RenderCallback callback, void *userdata) = 0;
	virtual bool InitOutputDevice(std::string_view uniqueId, LatencyMode latencyMode, bool *revertedToDefault) = 0;
	virtual int SampleRate() const = 0;
	virtual int BufferSize() const = 0;
	virtual int PeriodFrames() const = 0;
	virtual void DescribeOutputFormat(char *buffer, size_t bufferSize) const { buffer[0] = '-'; buffer[1] = '\0'; }
	virtual void FrameUpdate(bool allowAutoChange) {}
};
