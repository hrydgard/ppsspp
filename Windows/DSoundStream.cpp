#include "Common/CommonWindows.h"
#include <MMReg.h>
#include <process.h>

#ifdef __MINGW32__
#define __null
#endif
#include <dsound.h>
#ifdef __MINGW32__
#undef __null
#endif

#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"
#include "Common/OSVersion.h"
#include "Core/ConfigValues.h"
#include "Core/Util/AudioFormat.h"
#include "Windows/W32Util/Misc.h"

#include "DSoundStream.h"

inline int RoundDown128(int x) {
	return x & (~127);
}

unsigned int WINAPI DSoundAudioBackend::soundThread(void *param) {
	DSoundAudioBackend *dsound = (DSoundAudioBackend *)param;
	return dsound->RunThread();
}

bool DSoundAudioBackend::WriteDataToBuffer(DWORD offset, // Our own write cursor.
										   char* soundData, // Start of our data.
										   DWORD soundBytes) { // Size of block to copy.
	void *ptr1, *ptr2;
	DWORD numBytes1, numBytes2;
	// Obtain memory address of write block. This will be in two parts if the block wraps around.
	HRESULT hr = dsBuffer_->Lock(offset, soundBytes, &ptr1, &numBytes1, &ptr2, &numBytes2, 0);
	// If the buffer was lost, restore and retry lock.
	/*
	if (DSERR_BUFFERLOST == hr) {
	dsBuffer->Restore();
	hr=dsBuffer->Lock(dwOffset, dwSoundBytes, &ptr1, &numBytes1, &ptr2, &numBytes2, 0);
	} */
	if (FAILED(hr)) {
		return false;
	}

	memcpy(ptr1, soundData, numBytes1);
	if (ptr2)
		memcpy(ptr2, soundData + numBytes1, numBytes2);
	// Release the data back to DirectSound.
	dsBuffer_->Unlock(ptr1, numBytes1, ptr2, numBytes2);
	return true;
}

bool DSoundAudioBackend::CreateBuffer() {
	PCMWAVEFORMAT pcmwf;
	DSBUFFERDESC dsbdesc;

	memset(&pcmwf, 0, sizeof(PCMWAVEFORMAT));
	memset(&dsbdesc, 0, sizeof(DSBUFFERDESC));

	bufferSize_ = BUFSIZE;

	pcmwf.wf.wFormatTag = WAVE_FORMAT_PCM;
	pcmwf.wf.nChannels = 2;
	pcmwf.wf.nSamplesPerSec = sampleRate_;
	pcmwf.wf.nBlockAlign = 4;
	pcmwf.wf.nAvgBytesPerSec = pcmwf.wf.nSamplesPerSec * pcmwf.wf.nBlockAlign;
	pcmwf.wBitsPerSample = 16;

	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS; // //DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY; 
	dsbdesc.dwBufferBytes = bufferSize_;  //FIX32(pcmwf.wf.nAvgBytesPerSec);   //change to set buffer size
	dsbdesc.lpwfxFormat = (WAVEFORMATEX *)&pcmwf;

	if (SUCCEEDED(ds_->CreateSoundBuffer(&dsbdesc, &dsBuffer_, NULL))) {
		dsBuffer_->SetCurrentPosition(0);
		return true;
	} else {
		dsBuffer_ = NULL;
		return false;
	}
}

int DSoundAudioBackend::RunThread() {
	if (FAILED(DirectSoundCreate8(0, &ds_, 0))) {
		ds_ = NULL;
		threadData_ = 2;
		return 1;
	}

	ds_->SetCooperativeLevel(window_, DSSCL_PRIORITY);
	if (!CreateBuffer()) {
		ds_->Release();
		ds_ = NULL;
		threadData_ = 2;
		return 1;
	}

	InitializeCriticalSection(&soundCriticalSection);

	DWORD num1;
	int16_t *p1;

	dsBuffer_->Lock(0, bufferSize_, (void **)&p1, &num1, 0, 0, 0);

	memset(p1, 0, num1);
	dsBuffer_->Unlock(p1, num1, 0, 0);
	totalRenderedBytes_ = -bufferSize_;

	SetCurrentThreadName("DSound");
	currentPos_ = 0;
	lastPos_ = 0;

	dsBuffer_->Play(0, 0, DSBPLAY_LOOPING);

	auto ModBufferSize = [&](int x) { return (x + bufferSize_) % bufferSize_; };

	while (!threadData_) {
		EnterCriticalSection(&soundCriticalSection);

		dsBuffer_->GetCurrentPosition((DWORD *)&currentPos_, 0);
		int numBytesToRender = RoundDown128(ModBufferSize(currentPos_ - lastPos_)); 

		if (numBytesToRender >= 256) {
			int numBytesRendered = 4 * (*callback_)(realtimeBuffer_, numBytesToRender >> 2, 44100);
			//We need to copy the full buffer, regardless of what the mixer claims to have filled
			//If we don't do this then the sound will loop if the sound stops and the mixer writes only zeroes
			numBytesRendered = numBytesToRender;
			WriteDataToBuffer(lastPos_, (char *) realtimeBuffer_, numBytesRendered);

			currentPos_ = ModBufferSize(lastPos_ + numBytesRendered);
			totalRenderedBytes_ += numBytesRendered;

			lastPos_ = currentPos_;
		}

		LeaveCriticalSection(&soundCriticalSection);
		Sleep(5);
	}
	dsBuffer_->Stop();

	dsBuffer_->Release();
	ds_->Release();

	threadData_ = 2;
	return 0;
}

DSoundAudioBackend::~DSoundAudioBackend() {
	if (!ds_)
		return;

	if (!dsBuffer_)
		return;

	EnterCriticalSection(&soundCriticalSection);

	if (threadData_ == 0) {
		threadData_ = 1;
	}

	if (hThread_ != NULL) {
		WaitForSingleObject(hThread_, 1000);
		CloseHandle(hThread_);
		hThread_ = NULL;
	}

	LeaveCriticalSection(&soundCriticalSection);
	DeleteCriticalSection(&soundCriticalSection);
}

bool DSoundAudioBackend::Init(HWND window, StreamCallback _callback, int sampleRate) {
	window_ = window;
	callback_ = _callback;
	sampleRate_ = sampleRate;
	threadData_ = 0;
	hThread_ = (HANDLE)_beginthreadex(0, 0, soundThread, (void *)this, 0, 0);
	if (!hThread_)
		return false;
	SetThreadPriority(hThread_, THREAD_PRIORITY_ABOVE_NORMAL);
	return true;
}
