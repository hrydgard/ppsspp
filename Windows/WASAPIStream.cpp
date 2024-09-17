#include "stdafx.h"

#include "WindowsAudio.h"
#include "WASAPIStream.h"
#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Core/Config.h"
#include "Core/Util/AudioFormat.h"
#include "Common/Data/Encoding/Utf8.h"

#include "Common/Thread/ThreadUtil.h"

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

class CMMNotificationClient final : public IMMNotificationClient {
public:
	CMMNotificationClient() {
	}

	virtual ~CMMNotificationClient() {
		CoTaskMemFree(currentDevice_);
		currentDevice_ = nullptr;
		SAFE_RELEASE(_pEnumerator)
	}

	void SetCurrentDevice(IMMDevice *device) {
		std::lock_guard<std::mutex> guard(lock_);

		CoTaskMemFree(currentDevice_);
		currentDevice_ = nullptr;
		if (!device || FAILED(device->GetId(&currentDevice_))) {
			currentDevice_ = nullptr;
		}

		if (currentDevice_) {
			INFO_LOG(Log::sceAudio, "Switching to WASAPI audio device: '%s'", GetDeviceName(currentDevice_).c_str());
		}

		deviceChanged_ = false;
	}

	bool HasDefaultDeviceChanged() const {
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
			*ppvInterface = nullptr;
			return E_NOINTERFACE;
		}
		return S_OK;
	}

	// Callback methods for device-event notifications.

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) override {
		std::lock_guard<std::mutex> guard(lock_);

		if (flow != eRender || role != eConsole) {
			// Not relevant to us.
			return S_OK;
		}

		// pwstrDeviceId can be null. We consider that a new device, I think?
		bool same = currentDevice_ == pwstrDeviceId;
		if (!same && currentDevice_ && pwstrDeviceId) {
			same = !wcscmp(currentDevice_, pwstrDeviceId);
		}
		if (same) {
			// Already the current device, nothing to do.
			return S_OK;
		}

		deviceChanged_ = true;
		INFO_LOG(Log::sceAudio, "New default eRender/eConsole WASAPI audio device detected: '%s'", GetDeviceName(pwstrDeviceId).c_str());
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override {
		// Ignore.
		return S_OK;
	};

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override {
		// Ignore.
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override {
		// Ignore.
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override {
		INFO_LOG(Log::sceAudio, "Changed audio device property "
			"{%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x}#%d",
			(uint32_t)key.fmtid.Data1, key.fmtid.Data2, key.fmtid.Data3,
			key.fmtid.Data4[0], key.fmtid.Data4[1],
			key.fmtid.Data4[2], key.fmtid.Data4[3],
			key.fmtid.Data4[4], key.fmtid.Data4[5],
			key.fmtid.Data4[6], key.fmtid.Data4[7],
			(int)key.pid);
		return S_OK;
	}

	std::string GetDeviceName(LPCWSTR pwstrId)
	{
		HRESULT hr = S_OK;
		IMMDevice *pDevice = NULL;
		IPropertyStore *pProps = NULL;
		PROPVARIANT varString;
		PropVariantInit(&varString);

		if (_pEnumerator == NULL)
		{
			// Get enumerator for audio endpoint devices.
			hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
				NULL, CLSCTX_INPROC_SERVER,
				__uuidof(IMMDeviceEnumerator),
				(void**)&_pEnumerator);
		}
		if (hr == S_OK && _pEnumerator) {
			hr = _pEnumerator->GetDevice(pwstrId, &pDevice);
		}
		if (hr == S_OK && pDevice) {
			hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
		}
		if (hr == S_OK && pProps) {
			// Get the endpoint device's friendly-name property.
			hr = pProps->GetValue(PKEY_Device_FriendlyName, &varString);
		}

		std::string name = ConvertWStringToUTF8((hr == S_OK) ? varString.pwszVal : L"null device");

		PropVariantClear(&varString);

		SAFE_RELEASE(pProps)
		SAFE_RELEASE(pDevice)
		return name;
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

WASAPIAudioBackend::WASAPIAudioBackend() : threadData_(0) {
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
	if (!hThread_)
		return false;
	SetThreadPriority(hThread_, THREAD_PRIORITY_ABOVE_NORMAL);
	return true;
}

// This to be run only on the thread.
class WASAPIAudioThread {
public:
	WASAPIAudioThread(std::atomic<int> &threadData, int &sampleRate, StreamCallback &callback)
		: threadData_(threadData), sampleRate_(sampleRate), callback_(callback) {
	}
	~WASAPIAudioThread();

	void Run();

private:
	bool ActivateDefaultDevice();
	bool InitAudioDevice();
	void ShutdownAudioDevice();
	bool DetectFormat();
	bool ValidateFormat(const WAVEFORMATEXTENSIBLE *fmt);
	bool PrepareFormat();

	std::atomic<int> &threadData_;
	int &sampleRate_;
	StreamCallback &callback_;

	IMMDeviceEnumerator *deviceEnumerator_ = nullptr;
	IMMDevice *device_ = nullptr;
	IAudioClient *audioInterface_ = nullptr;
	CMMNotificationClient *notificationClient_ = nullptr;
	WAVEFORMATEXTENSIBLE *deviceFormat_ = nullptr;
	IAudioRenderClient *renderClient_ = nullptr;
	int16_t *shortBuf_ = nullptr;

	enum class Format {
		UNKNOWN = 0,
		IEEE_FLOAT = 1,
		PCM16 = 2,
	};

	uint32_t numBufferFrames = 0;
	Format format_ = Format::UNKNOWN;
	REFERENCE_TIME actualDuration_{};
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
	_assert_(device_ == nullptr);
	HRESULT hresult = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
	if (FAILED(hresult) || device_ == nullptr)
		return false;

	_assert_(audioInterface_ == nullptr);
	hresult = device_->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void **)&audioInterface_);
	if (FAILED(hresult) || audioInterface_ == nullptr)
		return false;

	return true;
}

bool WASAPIAudioThread::InitAudioDevice() {
	REFERENCE_TIME hnsBufferDuration = REFTIMES_PER_SEC;
	_assert_(deviceFormat_ == nullptr);
	HRESULT hresult = audioInterface_->GetMixFormat((WAVEFORMATEX **)&deviceFormat_);
	if (FAILED(hresult) || !deviceFormat_)
		return false;

	if (!DetectFormat()) {
		// Format unsupported - let's not even try to initialize.
		return false;
	}

	hresult = audioInterface_->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsBufferDuration, 0, &deviceFormat_->Format, nullptr);
	if (FAILED(hresult))
		return false;
	_assert_(renderClient_ == nullptr);
	hresult = audioInterface_->GetService(IID_IAudioRenderClient, (void **)&renderClient_);
	if (FAILED(hresult) || !renderClient_)
		return false;

	numBufferFrames = 0;
	hresult = audioInterface_->GetBufferSize(&numBufferFrames);
	if (FAILED(hresult) || numBufferFrames == 0)
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
	if (deviceFormat_ && !ValidateFormat(deviceFormat_)) {
		// Last chance, let's try to ask for one we support instead.
		WAVEFORMATEXTENSIBLE fmt{};
		fmt.Format.cbSize = sizeof(fmt);
		fmt.Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		fmt.Format.nChannels = 2;
		fmt.Format.nSamplesPerSec = 44100;
		if (deviceFormat_->Format.nSamplesPerSec >= 22050 && deviceFormat_->Format.nSamplesPerSec <= 192000)
			fmt.Format.nSamplesPerSec = deviceFormat_->Format.nSamplesPerSec;
		fmt.Format.nBlockAlign = 2 * sizeof(float);
		fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * fmt.Format.nBlockAlign;
		fmt.Format.wBitsPerSample = sizeof(float) * 8;
		fmt.Samples.wReserved = 0;
		fmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
		fmt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

		WAVEFORMATEXTENSIBLE *closest = nullptr;
		HRESULT hr = audioInterface_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &fmt.Format, (WAVEFORMATEX **)&closest);
		if (hr == S_OK) {
			// Okay, great.  Let's just use ours.
			CoTaskMemFree(closest);
			CoTaskMemFree(deviceFormat_);
			deviceFormat_ = (WAVEFORMATEXTENSIBLE *)CoTaskMemAlloc(sizeof(fmt));
			if (deviceFormat_)
				memcpy(deviceFormat_, &fmt, sizeof(fmt));

			// In case something above gets out of date.
			return ValidateFormat(deviceFormat_);
		} else if (hr == S_FALSE && closest != nullptr) {
			// This means check closest.  We'll allow it only if it's less specific on channels.
			if (ValidateFormat(closest)) {
				CoTaskMemFree(deviceFormat_);
				deviceFormat_ = closest;
			} else {
				wchar_t guid[256]{};
				StringFromGUID2(closest->SubFormat, guid, 256);
				ERROR_LOG_REPORT_ONCE(badfallbackclosest, Log::sceAudio, "WASAPI fallback and closest unsupported (fmt=%04x/%s)", closest->Format.wFormatTag, guid);
				CoTaskMemFree(closest);
				return false;
			}
		} else {
			CoTaskMemFree(closest);
			if (hr != AUDCLNT_E_DEVICE_INVALIDATED && hr != AUDCLNT_E_SERVICE_NOT_RUNNING)
				ERROR_LOG_REPORT_ONCE(badfallback, Log::sceAudio, "WASAPI fallback format was unsupported (%08x)", hr);
			return false;
		}
	}

	return true;
}

bool WASAPIAudioThread::ValidateFormat(const WAVEFORMATEXTENSIBLE *fmt) {
	// Don't know if PCM16 ever shows up here, the documentation only talks about float... but let's blindly
	// try to support it :P
	format_ = Format::UNKNOWN;
	if (!fmt)
		return false;

	if (fmt->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		if (!memcmp(&fmt->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(fmt->SubFormat))) {
			if (fmt->Format.nChannels >= 1)
				format_ = Format::IEEE_FLOAT;
		} else {
			wchar_t guid[256]{};
			StringFromGUID2(fmt->SubFormat, guid, 256);
			ERROR_LOG_REPORT_ONCE(unexpectedformat, Log::sceAudio, "Got unexpected WASAPI 0xFFFE stream format (%S), expected float!", guid);
			if (fmt->Format.wBitsPerSample == 16 && fmt->Format.nChannels == 2) {
				format_ = Format::PCM16;
			}
		}
	} else if (fmt->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
		if (fmt->Format.nChannels >= 1)
			format_ = Format::IEEE_FLOAT;
	} else {
		ERROR_LOG_REPORT_ONCE(unexpectedformat2, Log::sceAudio, "Got unexpected non-extensible WASAPI stream format, expected extensible float!");
		if (fmt->Format.wBitsPerSample == 16 && fmt->Format.nChannels == 2) {
			format_ = Format::PCM16;
		}
	}

	return format_ != Format::UNKNOWN;
}

bool WASAPIAudioThread::PrepareFormat() {
	delete [] shortBuf_;
	shortBuf_ = nullptr;

	BYTE *pData = nullptr;
	HRESULT hresult = renderClient_->GetBuffer(numBufferFrames, &pData);
	if (FAILED(hresult) || !pData)
		return false;

	const int numSamples = numBufferFrames * deviceFormat_->Format.nChannels;
	if (format_ == Format::IEEE_FLOAT) {
		memset(pData, 0, sizeof(float) * numSamples);
		// This buffer is always stereo - PPSSPP writes to it.
		shortBuf_ = new short[numBufferFrames * 2];
	} else if (format_ == Format::PCM16) {
		memset(pData, 0, sizeof(short) * numSamples);
	}

	hresult = renderClient_->ReleaseBuffer(numBufferFrames, 0);
	if (FAILED(hresult))
		return false;

	actualDuration_ = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * numBufferFrames / deviceFormat_->Format.nSamplesPerSec);
	return true;
}

void WASAPIAudioThread::Run() {
	// Adapted from http://msdn.microsoft.com/en-us/library/windows/desktop/dd316756(v=vs.85).aspx

	_assert_(deviceEnumerator_ == nullptr);
	HRESULT hresult = CoCreateInstance(CLSID_MMDeviceEnumerator,
		nullptr, /* Object is not created as the part of the aggregate */
		CLSCTX_ALL, IID_IMMDeviceEnumerator, (void **)&deviceEnumerator_);

	if (FAILED(hresult) || deviceEnumerator_ == nullptr)
		return;

	if (!ActivateDefaultDevice()) {
		ERROR_LOG(Log::sceAudio, "WASAPI: Could not activate default device");
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
		ERROR_LOG(Log::sceAudio, "WASAPI: Could not init audio device");
		return;
	}
	if (!PrepareFormat()) {
		ERROR_LOG(Log::sceAudio, "WASAPI: Could not find a suitable audio output format");
		return;
	}

	hresult = audioInterface_->Start();
	if (FAILED(hresult)) {
		ERROR_LOG(Log::sceAudio, "WASAPI: Failed to start audio stream");
		return;
	}

	DWORD flags = 0;
	while (flags != AUDCLNT_BUFFERFLAGS_SILENT) {
		Sleep((DWORD)(actualDuration_ / REFTIMES_PER_MILLISEC / 2));

		uint32_t pNumPaddingFrames = 0;
		hresult = audioInterface_->GetCurrentPadding(&pNumPaddingFrames);
		if (FAILED(hresult)) {
			// What to do?
			pNumPaddingFrames = 0;
		}
		uint32_t pNumAvFrames = numBufferFrames - pNumPaddingFrames;

		BYTE *pData = nullptr;
		hresult = renderClient_->GetBuffer(pNumAvFrames, &pData);
		if (FAILED(hresult) || pData == nullptr) {
			// What to do?
		} else if (pNumAvFrames) {
			int chans = deviceFormat_->Format.nChannels;
			switch (format_) {
			case Format::IEEE_FLOAT:
				callback_(shortBuf_, pNumAvFrames, sampleRate_);
				if (chans == 1) {
					float *ptr = (float *)pData;
					memset(ptr, 0, pNumAvFrames * chans * sizeof(float));
					for (uint32_t i = 0; i < pNumAvFrames; i++) {
						ptr[i * chans + 0] = 0.5f * ((float)shortBuf_[i * 2] + (float)shortBuf_[i * 2 + 1]) * (1.0f / 32768.0f);
					}
				} else if (chans == 2) {
					ConvertS16ToF32((float *)pData, shortBuf_, pNumAvFrames * chans);
				} else if (chans > 2) {
					float *ptr = (float *)pData;
					memset(ptr, 0, pNumAvFrames * chans * sizeof(float));
					for (uint32_t i = 0; i < pNumAvFrames; i++) {
						ptr[i * chans + 0] = (float)shortBuf_[i * 2] * (1.0f / 32768.0f);
						ptr[i * chans + 1] = (float)shortBuf_[i * 2 + 1] * (1.0f / 32768.0f);
					}
				}
				break;
			case Format::PCM16:
				callback_((short *)pData, pNumAvFrames, sampleRate_);
				break;
			}
		}

		if (threadData_ != 0) {
			flags = AUDCLNT_BUFFERFLAGS_SILENT;
		}

		if (!FAILED(hresult) && pData) {
			hresult = renderClient_->ReleaseBuffer(pNumAvFrames, flags);
			if (FAILED(hresult)) {
				// Not much to do here either...
			}
		}

		// Check if we should use a new device.
		if (notificationClient_ && notificationClient_->HasDefaultDeviceChanged() && g_Config.bAutoAudioDevice) {
			hresult = audioInterface_->Stop();
			ShutdownAudioDevice();

			if (!ActivateDefaultDevice()) {
				ERROR_LOG(Log::sceAudio, "WASAPI: Could not activate default device");
				// TODO: Return to the old device here?
				return;
			}
			notificationClient_->SetCurrentDevice(device_);
			if (!InitAudioDevice()) {
				ERROR_LOG(Log::sceAudio, "WASAPI: Could not init audio device");
				return;
			}
			if (!PrepareFormat()) {
				ERROR_LOG(Log::sceAudio, "WASAPI: Could not find a suitable audio output format");
				return;
			}

			hresult = audioInterface_->Start();
			if (FAILED(hresult)) {
				ERROR_LOG(Log::sceAudio, "WASAPI: Failed to start audio stream");
				return;
			}
		}
	}

	// Wait for last data in buffer to play before stopping.
	Sleep((DWORD)(actualDuration_ / REFTIMES_PER_MILLISEC / 2));

	hresult = audioInterface_->Stop();
	if (FAILED(hresult)) {
		ERROR_LOG(Log::sceAudio, "WASAPI: Failed to stop audio stream");
	}
}

int WASAPIAudioBackend::RunThread() {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	_dbg_assert_(SUCCEEDED(hr));
	SetCurrentThreadName("WASAPI_audio");

	if (threadData_ == 0) {
		// This will free everything once it's done.
		WASAPIAudioThread renderer(threadData_, sampleRate_, callback_);
		renderer.Run();
	}

	threadData_ = 2;
	CoUninitialize();
	return 0;
}
