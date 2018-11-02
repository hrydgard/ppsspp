#pragma once

// This should only be included from WindowsAudio.cpp and DSoundStream.cpp.

#include "WindowsAudio.h"
#include <mmreg.h>
#include <dsound.h>

class DSoundAudioBackend : public WindowsAudioBackend {
public:
	DSoundAudioBackend();
	~DSoundAudioBackend() override;

	bool Init(HWND window, StreamCallback callback, int sampleRate) override;  // If fails, can safely delete the object
	void Update() override;
	int GetSampleRate() override { return sampleRate_; }

private:
	inline int ModBufferSize(int x) { return (x + bufferSize_) % bufferSize_; }
	int RunThread();
	static unsigned int WINAPI soundThread(void *param);
	bool CreateBuffer();
	bool WriteDataToBuffer(DWORD offset, // Our own write cursor.
		char* soundData, // Start of our data.
		DWORD soundBytes); // Size of block to copy.

	CRITICAL_SECTION soundCriticalSection;
	HWND window_;
	HANDLE soundSyncEvent_ = nullptr;
	HANDLE hThread_ = nullptr;

	StreamCallback callback_;

	IDirectSound8 *ds_ = nullptr;
	IDirectSoundBuffer *dsBuffer_ = nullptr;

	int bufferSize_; // bytes
	int totalRenderedBytes_;
	int sampleRate_;

	volatile int threadData_;

	enum {
		BUFSIZE = 0x4000,
		MAXWAIT = 20,   //ms
	};

	int currentPos_;
	int lastPos_;
	short realtimeBuffer_[BUFSIZE * 2];
};
