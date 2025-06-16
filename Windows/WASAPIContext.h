#pragma once

#include <atomic>
#include "WindowsAudio.h"

// This should only be included from WindowsAudio.cpp and WASAPIStream.cpp.

class WASAPIAudioBackend : public AudioBackend {
public:
	WASAPIAudioBackend();
	~WASAPIAudioBackend();

	bool Init(StreamCallback callback, void *userdata) override;  // If fails, can safely delete the object
	int SampleRate() const override { return sampleRate_; }
	int PeriodFrames() const override { return periodFrames_; }  // amount of frames normally requested
	void FrameUpdate() override {}

private:
	int RunThread();
	static unsigned int WINAPI soundThread(void *param);

	HANDLE hThread_ = nullptr;
	StreamCallback callback_ = nullptr;
	int sampleRate_ = 0;
	int periodFrames_ = 0;
	void *userdata_ = 0;
	std::atomic<int> threadData_{};
};
