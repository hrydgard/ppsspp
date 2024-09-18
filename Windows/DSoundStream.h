#pragma once

// This should only be included from WindowsAudio.cpp and DSoundStream.cpp.

#include "WindowsAudio.h"
#include <mmreg.h>

struct IDirectSound8;
struct IDirectSoundBuffer;

class DSoundAudioBackend : public WindowsAudioBackend {
public:
	~DSoundAudioBackend();

	bool Init(HWND window, StreamCallback callback, int sampleRate) override;  // If fails, can safely delete the object
	int GetSampleRate() const override { return sampleRate_; }

private:
	int RunThread();
	static unsigned int WINAPI soundThread(void *param);
	bool CreateBuffer();
	bool WriteDataToBuffer(DWORD offset, // Our own write cursor.
		char* soundData, // Start of our data.
		DWORD soundBytes); // Size of block to copy.

	CRITICAL_SECTION soundCriticalSection;
	HWND window_ = nullptr;
	HANDLE hThread_ = nullptr;

	StreamCallback callback_;

	IDirectSound8 *ds_ = nullptr;
	IDirectSoundBuffer *dsBuffer_ = nullptr;

	int bufferSize_ = 0; // bytes
	int totalRenderedBytes_ = 0;
	int sampleRate_ = 0;

	volatile int threadData_ = 0;

	enum {
		BUFSIZE = 0x4000,
		MAXWAIT = 20,   //ms
	};

	int currentPos_ = 0;
	int lastPos_ = 0;
	short realtimeBuffer_[BUFSIZE * 2];
};
