#include "WindowsAudio.h"
#include "WASAPIStream.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "Core/Util/AudioFormat.h"

#include "thread/threadutil.h"

#include <mutex>
#include <Objbase.h>
#include <Mmreg.h>
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>
#include "Functiondiscoverykeys_devpkey.h"

// Includes some code from https://msdn.microsoft.com/en-us/library/dd370810%28VS.85%29.aspx?f=255&MSPPError=-2147217396

#pragma comment(lib, "ole32.lib")

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

// Adapted from a MSDN sample.

#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

class CMMNotificationClient : public IMMNotificationClient {
public:
	CMMNotificationClient() {
	}

	~CMMNotificationClient() {
		if (currentDevice_)
			CoTaskMemFree(currentDevice_);
		currentDevice_ = nullptr;
		SAFE_RELEASE(_pEnumerator)
	}

	void SetCurrentDevice(IMMDevice *device) {
		std::lock_guard<std::mutex> guard(lock_);

		if (currentDevice_)
			CoTaskMemFree(currentDevice_);
		device->GetId(&currentDevice_);
		deviceChanged_ = false;
	}

	bool HasDeviceChanged() {
		return deviceChanged_;
	}

	// IUnknown methods -- AddRef, Release, and QueryInterface
	ULONG STDMETHODCALLTYPE AddRef() override {
		return InterlockedIncrement(&_cRef);
	}

	ULONG STDMETHODCALLTYPE Release() override {
		ULONG ulRef = InterlockedDecrement(&_cRef);
		if (0 == ulRef) {
			delete this;
		}
		return ulRef;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface) override {
		if (IID_IUnknown == riid) {
			AddRef();
			*ppvInterface = (IUnknown*)this;
		} else if (__uuidof(IMMNotificationClient) == riid) {
			AddRef();
			*ppvInterface = (IMMNotificationClient*)this;
		} else {
			*ppvInterface = NULL;
			return E_NOINTERFACE;
		}
		return S_OK;
	}

	// Callback methods for device-event notifications.

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) {
		std::lock_guard<std::mutex> guard(lock_);

		if (flow != eRender || role != eMultimedia) {
			// Not relevant to us.
			return S_OK;
		}

		if (!wcscmp(currentDevice_, pwstrDeviceId)) {
			// Already the current device, nothing to do.
			return S_OK;
		}

		deviceChanged_ = true;
		INFO_LOG(SCEAUDIO, "New default WASAPI audio device detected");
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) {
		// Ignore.
		return S_OK;
	};

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
		// Ignore.
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
		// Ignore.
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
		INFO_LOG(SCEAUDIO, "Changed audio device property "
			"{%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x}#%d",
			key.fmtid.Data1, key.fmtid.Data2, key.fmtid.Data3,
			key.fmtid.Data4[0], key.fmtid.Data4[1],
			key.fmtid.Data4[2], key.fmtid.Data4[3],
			key.fmtid.Data4[4], key.fmtid.Data4[5],
			key.fmtid.Data4[6], key.fmtid.Data4[7],
			key.pid);
		return S_OK;
	}

private:
	std::mutex lock_;
	LONG _cRef = 1;
	IMMDeviceEnumerator *_pEnumerator = nullptr;
	wchar_t *currentDevice_ = nullptr;
	bool deviceChanged_ = false;
};

// TODO: Make these adjustable. This is from the example in MSDN.
// 200 times/sec = 5ms, pretty good :) Wonder if all computers can handle it though.
#define REFTIMES_PER_SEC  (10000000/200)
#define REFTIMES_PER_MILLISEC  (REFTIMES_PER_SEC / 1000)

WASAPIAudioBackend::WASAPIAudioBackend() {
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

// This to be run only on the thread.
class WASAPIAudioThread {
public:
	WASAPIAudioThread(volatile int &threadData, int &sampleRate, StreamCallback &callback)
		: threadData_(threadData), sampleRate_(sampleRate), callback_(callback) {
	}
	~WASAPIAudioThread();

	void Run();

private:
	bool ActivateDefaultDevice();
	bool InitAudioDevice();
	void ShutdownAudioDevice();
	bool DetectFormat();

	volatile int &threadData_;
	int &sampleRate_;
	StreamCallback &callback_;

	IMMDeviceEnumerator *deviceEnumerator_ = nullptr;
	IMMDevice *device_ = nullptr;
	IAudioClient *audioInterface_ = nullptr;
	CMMNotificationClient *notificationClient_ = nullptr;
	WAVEFORMATEXTENSIBLE *deviceFormat_ = nullptr;
	IAudioRenderClient *renderClient_ = nullptr;
	short *shortBuf_ = nullptr;

	enum class Format {
		UNKNOWN = 0,
		IEEE_FLOAT = 1,
		PCM16 = 2,
	};

	uint32_t numBufferFrames = 0;
	Format format_ = Format::UNKNOWN;
	REFERENCE_TIME actualDuration_;
};

WASAPIAudioThread::~WASAPIAudioThread() {
	delete [] shortBuf_;
	shortBuf_ = nullptr;
	ShutdownAudioDevice();
	if (notificationClient_ && deviceEnumerator_)
		deviceEnumerator_->UnregisterEndpointNotificationCallback(notificationClient_);
	delete notificationClient_;
	notificationClient_ = nullptr;
	SAFE_RELEASE(deviceEnumerator_);
}

bool WASAPIAudioThread::ActivateDefaultDevice() {
	HRESULT hresult = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device_);
	if (FAILED(hresult))
		return false;

	hresult = device_->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&audioInterface_);
	if (FAILED(hresult))
		return false;

	return true;
}

bool WASAPIAudioThread::InitAudioDevice() {
	REFERENCE_TIME hnsBufferDuration = REFTIMES_PER_SEC;
	HRESULT hresult = audioInterface_->GetMixFormat((WAVEFORMATEX **)&deviceFormat_);
	hresult = audioInterface_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsBufferDuration, 0, &deviceFormat_->Format, nullptr);
	hresult = audioInterface_->GetService(IID_IAudioRenderClient, (void **)&renderClient_);
	if (FAILED(hresult))
		return false;

	hresult = audioInterface_->GetBufferSize(&numBufferFrames);
	if (FAILED(hresult))
		return false;

	sampleRate_ = deviceFormat_->Format.nSamplesPerSec;

	return true;
}

void WASAPIAudioThread::ShutdownAudioDevice() {
	SAFE_RELEASE(renderClient_);
	CoTaskMemFree(deviceFormat_);
	deviceFormat_ = nullptr;
	SAFE_RELEASE(audioInterface_);
	SAFE_RELEASE(device_);
}

bool WASAPIAudioThread::DetectFormat() {
	// Don't know if PCM16 ever shows up here, the documentation only talks about float... but let's blindly
	// try to support it :P

	if (deviceFormat_->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		if (!memcmp(&deviceFormat_->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(deviceFormat_->SubFormat))) {
			format_ = Format::IEEE_FLOAT;
		} else {
			ERROR_LOG_REPORT_ONCE(unexpectedformat, SCEAUDIO, "Got unexpected WASAPI 0xFFFE stream format, expected float!");
			if (deviceFormat_->Format.wBitsPerSample == 16 && deviceFormat_->Format.nChannels == 2) {
				format_ = Format::PCM16;
			}
		}
	} else if (deviceFormat_->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
		format_ = Format::IEEE_FLOAT;
	} else {
		ERROR_LOG_REPORT_ONCE(unexpectedformat2, SCEAUDIO, "Got unexpected non-extensible WASAPI stream format, expected extensible float!");
		if (deviceFormat_->Format.wBitsPerSample == 16 && deviceFormat_->Format.nChannels == 2) {
			format_ = Format::PCM16;
		}
	}

	delete [] shortBuf_;
	shortBuf_ = nullptr;

	BYTE *pData;
	HRESULT hresult = renderClient_->GetBuffer(numBufferFrames, &pData);
	int numSamples = numBufferFrames * deviceFormat_->Format.nChannels;
	if (format_ == Format::IEEE_FLOAT) {
		memset(pData, 0, sizeof(float) * numSamples);
		shortBuf_ = new short[numBufferFrames * deviceFormat_->Format.nChannels];
	} else if (format_ == Format::PCM16) {
		memset(pData, 0, sizeof(short) * numSamples);
	}

	hresult = renderClient_->ReleaseBuffer(numBufferFrames, 0);
	actualDuration_ = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * numBufferFrames / deviceFormat_->Format.nSamplesPerSec);

	return true;
}

void WASAPIAudioThread::Run() {
	// Adapted from http://msdn.microsoft.com/en-us/library/windows/desktop/dd316756(v=vs.85).aspx

	HRESULT hresult;
	hresult = CoCreateInstance(CLSID_MMDeviceEnumerator,
		nullptr, /* Object is not created as the part of the aggregate */
		CLSCTX_ALL, IID_IMMDeviceEnumerator, (void **)&deviceEnumerator_);
	if (FAILED(hresult))
		return;

	if (!ActivateDefaultDevice()) {
		ERROR_LOG(SCEAUDIO, "WASAPI: Could not activate default device");
		return;
	}

	notificationClient_ = new CMMNotificationClient();
	notificationClient_->SetCurrentDevice(device_);
	hresult = deviceEnumerator_->RegisterEndpointNotificationCallback(notificationClient_);
	if (FAILED(hresult)) {
		// Let's just keep going, but release the client since it doesn't work.
		delete notificationClient_;
		notificationClient_ = nullptr;
	}

	if (!InitAudioDevice()) {
		ERROR_LOG(SCEAUDIO, "WASAPI: Could not init audio device");
		return;
	}
	if (!DetectFormat()) {
		ERROR_LOG(SCEAUDIO, "WASAPI: Could not find a suitable audio output format");
		return;
	}

	hresult = audioInterface_->Start();

	DWORD flags = 0;
	while (flags != AUDCLNT_BUFFERFLAGS_SILENT) {
		Sleep((DWORD)(actualDuration_ / REFTIMES_PER_MILLISEC / 2));

		uint32_t pNumPaddingFrames;
		hresult = audioInterface_->GetCurrentPadding(&pNumPaddingFrames);
		if (FAILED(hresult)) {
			// What to do?
			pNumPaddingFrames = 0;
		}
		uint32_t pNumAvFrames = numBufferFrames - pNumPaddingFrames;

		BYTE *pData;
		hresult = renderClient_->GetBuffer(pNumAvFrames, &pData);
		if (FAILED(hresult)) {
			// What to do?
		} else if (pNumAvFrames) {
			switch (format_) {
			case Format::IEEE_FLOAT:
				callback_(shortBuf_, pNumAvFrames, 16, sampleRate_, 2);
				if (deviceFormat_->Format.nChannels == 2) {
					ConvertS16ToF32((float *)pData, shortBuf_, pNumAvFrames * deviceFormat_->Format.nChannels);
				} else {
					float *ptr = (float *)pData;
					int chans = deviceFormat_->Format.nChannels;
					memset(ptr, 0, pNumAvFrames * chans * sizeof(float));
					for (UINT32 i = 0; i < pNumAvFrames; i++) {
						ptr[i * chans + 0] = (float)shortBuf_[i * 2] * (1.0f / 32768.0f);
						ptr[i * chans + 1] = (float)shortBuf_[i * 2 + 1] * (1.0f / 32768.0f);
					}
				}
				break;
			case Format::PCM16:
				callback_((short *)pData, pNumAvFrames, 16, sampleRate_, 2);
				break;
			}
		}

		if (threadData_ != 0) {
			flags = AUDCLNT_BUFFERFLAGS_SILENT;
		}

		hresult = renderClient_->ReleaseBuffer(pNumAvFrames, flags);
		if (FAILED(hresult)) {
			// Not much to do here either...
		}

		// Check if we should use a new device.
		if (notificationClient_ && notificationClient_->HasDeviceChanged()) {
			hresult = audioInterface_->Stop();
			ShutdownAudioDevice();

			if (!ActivateDefaultDevice()) {
				ERROR_LOG(SCEAUDIO, "WASAPI: Could not activate default device");
				return;
			}
			notificationClient_->SetCurrentDevice(device_);
			if (!InitAudioDevice()) {
				ERROR_LOG(SCEAUDIO, "WASAPI: Could not init audio device");
				return;
			}
			if (!DetectFormat()) {
				ERROR_LOG(SCEAUDIO, "WASAPI: Could not find a suitable audio output format");
				return;
			}

			hresult = audioInterface_->Start();
		}
	}

	// Wait for last data in buffer to play before stopping.
	Sleep((DWORD)(actualDuration_ / REFTIMES_PER_MILLISEC / 2));

	hresult = audioInterface_->Stop();
}

int WASAPIAudioBackend::RunThread() {
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	setCurrentThreadName("WASAPI_audio");

	if (threadData_ == 0) {
		// This will free everything once it's done.
		WASAPIAudioThread renderer(threadData_, sampleRate_, callback_);
		renderer.Run();
	}

	threadData_ = 2;
	CoUninitialize();
	return 0;
}
