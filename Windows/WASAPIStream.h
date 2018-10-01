#pragma once

#include "WindowsAudio.h"

// This should only be included from WindowsAudio.cpp and WASAPIStream.cpp.

class WASAPIAudioBackend : public WindowsAudioBackend {
public:
	WASAPIAudioBackend();
	~WASAPIAudioBackend() override;

	bool Init(HWND window, StreamCallback callback, int sampleRate) override;  // If fails, can safely delete the object
	void Update() override {}
	int GetSampleRate() override { return sampleRate_; }

private:
	int RunThread();
	static unsigned int WINAPI soundThread(void *param);

	HANDLE hThread_ = nullptr;
	StreamCallback callback_ = nullptr;
	int sampleRate_ = 0;
	volatile int threadData_ = 0;
};
