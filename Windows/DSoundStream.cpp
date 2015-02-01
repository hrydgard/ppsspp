#include "Common/CommonWindows.h"
#include <dsound.h>

#include "native/thread/threadutil.h"
#include "Core/Reporting.h"
#include "Core/Util/AudioFormat.h"
#include "Windows/W32Util/Misc.h"

#include "dsoundstream.h"	

// WASAPI begin
#include <Objbase.h>
#include <Mmreg.h>
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>

#pragma comment(lib, "ole32.lib")

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
// WASAPI end

#define BUFSIZE 0x4000
#define MAXWAIT 20   //ms

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
	HANDLE soundSyncEvent_ = NULL;
	HANDLE hThread_ = NULL;

	StreamCallback callback_;

	IDirectSound8 *ds_ = NULL;
	IDirectSoundBuffer *dsBuffer_ = NULL;

	int bufferSize_; // bytes
	int totalRenderedBytes_;
	int sampleRate_;

	volatile int threadData_;

	int currentPos_;
	int lastPos_;
	short realtimeBuffer_[BUFSIZE * 2];
};

// TODO: Get rid of this
static DSoundAudioBackend *g_dsound;

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
	if (SUCCEEDED(hr)) { 
		memcpy(ptr1, soundData, numBytes1);
		if (ptr2)
			memcpy(ptr2, soundData+numBytes1, numBytes2);
		// Release the data back to DirectSound.
		dsBuffer_->Unlock(ptr1, numBytes1, ptr2, numBytes2);
		return true;
	}
	return false;
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

	soundSyncEvent_ = CreateEvent(0, false, false, 0);
	InitializeCriticalSection(&soundCriticalSection);

	DWORD num1;
	short *p1;

	dsBuffer_->Lock(0, bufferSize_, (void **)&p1, &num1, 0, 0, 0);

	memset(p1, 0, num1);
	dsBuffer_->Unlock(p1, num1, 0, 0);
	totalRenderedBytes_ = -bufferSize_;

	setCurrentThreadName("DSound");
	currentPos_ = 0;
	lastPos_ = 0;

	dsBuffer_->Play(0,0,DSBPLAY_LOOPING);

	while (!threadData_) {
		EnterCriticalSection(&soundCriticalSection);

		dsBuffer_->GetCurrentPosition((DWORD *)&currentPos_, 0);
		int numBytesToRender = RoundDown128(ModBufferSize(currentPos_ - lastPos_)); 

		if (numBytesToRender >= 256) {
			int numBytesRendered = 4 * (*callback_)(realtimeBuffer_, numBytesToRender >> 2, 16, 44100, 2);
			//We need to copy the full buffer, regardless of what the mixer claims to have filled
			//If we don't do this then the sound will loop if the sound stops and the mixer writes only zeroes
			numBytesRendered = numBytesToRender;
			WriteDataToBuffer(lastPos_, (char *) realtimeBuffer_, numBytesRendered);

			currentPos_ = ModBufferSize(lastPos_ + numBytesRendered);
			totalRenderedBytes_ += numBytesRendered;

			lastPos_ = currentPos_;
		}

		LeaveCriticalSection(&soundCriticalSection);
		WaitForSingleObject(soundSyncEvent_, MAXWAIT);
	}
	dsBuffer_->Stop();

	dsBuffer_->Release();
	ds_->Release();

	threadData_ = 2;
	return 0;
}

DSoundAudioBackend::DSoundAudioBackend() : threadData_(0), ds_(nullptr) {
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

	if (soundSyncEvent_ != NULL) {
		CloseHandle(soundSyncEvent_);
	}
	soundSyncEvent_ = NULL;
	LeaveCriticalSection(&soundCriticalSection);
	DeleteCriticalSection(&soundCriticalSection);
}

bool DSoundAudioBackend::Init(HWND window, StreamCallback _callback, int sampleRate) {
	window_ = window;
	callback_ = _callback;
	sampleRate_ = sampleRate;
	threadData_ = 0;
	hThread_ = (HANDLE)_beginthreadex(0, 0, soundThread, (void *)this, 0, 0);
	SetThreadPriority(hThread_, THREAD_PRIORITY_ABOVE_NORMAL);
	return true;
}

void DSoundAudioBackend::Update() {
	if (soundSyncEvent_ != NULL)
		SetEvent(soundSyncEvent_);
}

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

	HANDLE hThread_;

	StreamCallback callback_;
	int sampleRate_;

	volatile int threadData_;
};

// TODO: Make these adjustable. This is from the example in MSDN.
// 200 times/sec = 5ms, pretty good :) Wonder if all computers can handle it though.
#define REFTIMES_PER_SEC  (10000000/200)
#define REFTIMES_PER_MILLISEC  (REFTIMES_PER_SEC / 1000)

WASAPIAudioBackend::WASAPIAudioBackend() : hThread_(NULL), sampleRate_(0), callback_(nullptr), threadData_(0) {
}

WASAPIAudioBackend::~WASAPIAudioBackend() {
	if (threadData_ == 0) {
		threadData_ = 1;
	}

	if (hThread_ != NULL) {
		WaitForSingleObject(hThread_, 1000);
		CloseHandle(hThread_);
		hThread_ = NULL;
	}

	if (threadData_ == 2) {
		// blah.
	}
}

unsigned int WINAPI WASAPIAudioBackend::soundThread(void *param) {
	WASAPIAudioBackend *backend = (WASAPIAudioBackend *)param;
	return backend->RunThread();
}

bool WASAPIAudioBackend::Init(HWND window, StreamCallback callback, int sampleRate) {
	threadData_ = 0;
	callback_ = callback;
	sampleRate_ = sampleRate;
	hThread_ = (HANDLE)_beginthreadex(0, 0, soundThread, (void *)this, 0, 0);
	SetThreadPriority(hThread_, THREAD_PRIORITY_ABOVE_NORMAL);
	return true;
}

int WASAPIAudioBackend::RunThread() {
	// Adapted from http://msdn.microsoft.com/en-us/library/windows/desktop/dd316756(v=vs.85).aspx

	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	setCurrentThreadName("WASAPI_audio");

	IMMDeviceEnumerator *pDeviceEnumerator;
	IMMDevice *pDevice;
	IAudioClient *pAudioInterface;
	IAudioRenderClient *pAudioRenderClient;
	WAVEFORMATEXTENSIBLE *pDeviceFormat;
	DWORD flags = 0;
	REFERENCE_TIME hnsBufferDuration, hnsActualDuration;
	UINT32 pNumBufferFrames;
	UINT32 pNumPaddingFrames, pNumAvFrames;
	hnsBufferDuration = REFTIMES_PER_SEC;

	HRESULT hresult;
	hresult = CoCreateInstance(CLSID_MMDeviceEnumerator,
		NULL, /*Object is not created as the part of the aggregate */
		CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pDeviceEnumerator);
	if (FAILED(hresult)) goto bail;

	hresult = pDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
	if (FAILED(hresult)) {
		pDeviceEnumerator->Release();
		goto bail;
	}

	hresult = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioInterface);
	if (FAILED(hresult)) {
		pDevice->Release();
		pDeviceEnumerator->Release();
		goto bail;
	}

	hresult = pAudioInterface->GetMixFormat((WAVEFORMATEX**)&pDeviceFormat);
	hresult = pAudioInterface->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsBufferDuration, 0, &pDeviceFormat->Format, NULL);
	hresult = pAudioInterface->GetService(IID_IAudioRenderClient, (void**)&pAudioRenderClient);
	if (FAILED(hresult)) {
		pDevice->Release();
		pDeviceEnumerator->Release();
		pAudioInterface->Release();
		goto bail;
	}
	hresult = pAudioInterface->GetBufferSize(&pNumBufferFrames);
	if (FAILED(hresult)) {
		pDevice->Release();
		pDeviceEnumerator->Release();
		pAudioInterface->Release();
		goto bail;
	}

	sampleRate_ = pDeviceFormat->Format.nSamplesPerSec;

	enum {
		UNKNOWN_FORMAT = 0,
		IEEE_FLOAT = 1,
		PCM16 = 2,
	} format = UNKNOWN_FORMAT;

	// Don't know if PCM16 ever shows up here, the documentation only talks about float... but let's blindly
	// try to support it :P

	if (pDeviceFormat->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		if (!memcmp(&pDeviceFormat->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(pDeviceFormat->SubFormat))) {
			format = IEEE_FLOAT;
			// printf("float format\n");
		} else {
			ERROR_LOG_REPORT_ONCE(unexpectedformat, SCEAUDIO, "Got unexpected WASAPI 0xFFFE stream format, expected float!");
			if (pDeviceFormat->Format.wBitsPerSample == 16 && pDeviceFormat->Format.nChannels == 2) {
				format = PCM16;
			}
		}
	} else {
		ERROR_LOG_REPORT_ONCE(unexpectedformat2, SCEAUDIO, "Got unexpected non-extensible WASAPI stream format, expected extensible float!");
		if (pDeviceFormat->Format.wBitsPerSample == 16 && pDeviceFormat->Format.nChannels == 2) {
			format = PCM16;
		}
	}

	short *shortBuf = nullptr;

	BYTE *pData;
	hresult = pAudioRenderClient->GetBuffer(pNumBufferFrames, &pData);
	int numSamples = pNumBufferFrames * pDeviceFormat->Format.nChannels;
	if (format == IEEE_FLOAT) {
		memset(pData, 0, sizeof(float) * numSamples);
		shortBuf = new short[pNumBufferFrames * pDeviceFormat->Format.nChannels];
	} else if (format == PCM16) {
		memset(pData, 0, sizeof(short) * numSamples);
	}

	hresult = pAudioRenderClient->ReleaseBuffer(pNumBufferFrames, flags);
	hnsActualDuration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * pNumBufferFrames / pDeviceFormat->Format.nSamplesPerSec);

	hresult = pAudioInterface->Start();

	while (flags != AUDCLNT_BUFFERFLAGS_SILENT) {
		Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

		hresult = pAudioInterface->GetCurrentPadding(&pNumPaddingFrames);
		if (FAILED(hresult)) {
			// What to do?
			pNumPaddingFrames = 0;
		}
		pNumAvFrames = pNumBufferFrames - pNumPaddingFrames;

		hresult = pAudioRenderClient->GetBuffer(pNumAvFrames, &pData);
		if (FAILED(hresult)) {
			// What to do?
		} else if (pNumAvFrames) {
			switch (format) {
			case IEEE_FLOAT:
				callback_(shortBuf, pNumAvFrames, 16, sampleRate_, 2);
				if (pDeviceFormat->Format.nChannels == 2) {
					ConvertS16ToF32((float *)pData, shortBuf, pNumAvFrames * pDeviceFormat->Format.nChannels);
				} else {
					float *ptr = (float *)pData;
					int chans = pDeviceFormat->Format.nChannels;
					memset(ptr, 0, pNumAvFrames * chans * sizeof(float));
					for (UINT32 i = 0; i < pNumAvFrames; i++) {
						ptr[i * chans + 0] = (float)shortBuf[i * 2] * (1.0f / 32768.0f);
						ptr[i * chans + 1] = (float)shortBuf[i * 2 + 1] * (1.0f / 32768.0f);
					}
				}
				break;
			case PCM16:
				callback_((short *)pData, pNumAvFrames, 16, sampleRate_, 2);
				break;
			}
		}

		if (threadData_ != 0) {
			flags = AUDCLNT_BUFFERFLAGS_SILENT;
		}

		hresult = pAudioRenderClient->ReleaseBuffer(pNumAvFrames, flags);
		if (FAILED(hresult)) {
			// Not much to do here either...
		}
	}

	// Wait for last data in buffer to play before stopping.
	Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

	delete[] shortBuf;
	hresult = pAudioInterface->Stop();

	CoTaskMemFree(pDeviceFormat);
	pDeviceEnumerator->Release();
	pDevice->Release();
	pAudioInterface->Release();
	pAudioRenderClient->Release();

bail:
	threadData_ = 2;
	CoUninitialize();
	return 0;
}

WindowsAudioBackend *CreateAudioBackend(AudioBackendType type) {
	if (IsVistaOrHigher()) {
		switch (type) {
		case AUDIO_BACKEND_WASAPI:
		case AUDIO_BACKEND_AUTO:
			return new WASAPIAudioBackend();
		case AUDIO_BACKEND_DSOUND:
		default:
			return new DSoundAudioBackend();
		}
	} else {
		return new DSoundAudioBackend();
	}
}