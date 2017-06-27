#include "WindowsAudio.h"
#include "WASAPIStream.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "Core/Util/AudioFormat.h"

#include "thread/threadutil.h"

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

// TODO: Make these adjustable. This is from the example in MSDN.
// 200 times/sec = 5ms, pretty good :) Wonder if all computers can handle it though.
#define REFTIMES_PER_SEC  (10000000/200)
#define REFTIMES_PER_MILLISEC  (REFTIMES_PER_SEC / 1000)

WASAPIAudioBackend::WASAPIAudioBackend() : hThread_(nullptr), sampleRate_(0), callback_(nullptr), threadData_(0) {
}

WASAPIAudioBackend::~WASAPIAudioBackend() {
	if (threadData_ == 0) {
		threadData_ = 1;
	}

	if (hThread_) {
		WaitForSingleObject(hThread_, 1000);
		CloseHandle(hThread_);
		hThread_ = nullptr;
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
