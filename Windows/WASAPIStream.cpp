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
	CMMNotificationClient() :
		_cRef(1),
		_pEnumerator(NULL) {
	}

	~CMMNotificationClient() {
		SAFE_RELEASE(_pEnumerator)
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
		PrintDeviceName(pwstrDeviceId);
		const char *pszFlow = "?????";
		const char *pszRole = "?????";
		switch (flow) {
		case eRender:
			pszFlow = "eRender";
			break;
		case eCapture:
			pszFlow = "eCapture";
			break;
		}
		switch (role) {
		case eConsole:
			pszRole = "eConsole";
			break;
		case eMultimedia:
			pszRole = "eMultimedia";
			break;
		case eCommunications:
			pszRole = "eCommunications";
			break;
		}
		INFO_LOG(SCEAUDIO, "  -->New default device: flow = %s, role = %s\n", pszFlow, pszRole);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) {
		PrintDeviceName(pwstrDeviceId);
		INFO_LOG(SCEAUDIO, "  -->Added device\n");
		return S_OK;
	};

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
		PrintDeviceName(pwstrDeviceId);
		INFO_LOG(SCEAUDIO, "  -->Removed device\n");
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
		const char *pszState = "?????";
		PrintDeviceName(pwstrDeviceId);
		switch (dwNewState) {
		case DEVICE_STATE_ACTIVE:
			pszState = "ACTIVE";
			break;
		case DEVICE_STATE_DISABLED:
			pszState = "DISABLED";
			break;
		case DEVICE_STATE_NOTPRESENT:
			pszState = "NOTPRESENT";
			break;
		case DEVICE_STATE_UNPLUGGED:
			pszState = "UNPLUGGED";
			break;
		}
		INFO_LOG(SCEAUDIO, "  -->New device state is DEVICE_STATE_%s (0x%8.8x)\n", pszState, dwNewState);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
		PrintDeviceName(pwstrDeviceId);
		INFO_LOG(SCEAUDIO, "  -->Changed device property "
			"{%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x}#%d\n",
			key.fmtid.Data1, key.fmtid.Data2, key.fmtid.Data3,
			key.fmtid.Data4[0], key.fmtid.Data4[1],
			key.fmtid.Data4[2], key.fmtid.Data4[3],
			key.fmtid.Data4[4], key.fmtid.Data4[5],
			key.fmtid.Data4[6], key.fmtid.Data4[7],
			key.pid);
		return S_OK;
	}

private:
	LONG _cRef;
	IMMDeviceEnumerator *_pEnumerator;

	// Private function to print device-friendly name
	HRESULT PrintDeviceName(LPCWSTR  pwstrId);
};

// Given an endpoint ID string, print the friendly device name.
HRESULT CMMNotificationClient::PrintDeviceName(LPCWSTR pwstrId) {
	HRESULT hr = S_OK;
	IMMDevice *pDevice = NULL;
	IPropertyStore *pProps = NULL;
	PROPVARIANT varString;

	CoInitialize(NULL);
	PropVariantInit(&varString);

	if (_pEnumerator == NULL) {
		// Get enumerator for audio endpoint devices.
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
			NULL, CLSCTX_INPROC_SERVER,
			__uuidof(IMMDeviceEnumerator),
			(void**)&_pEnumerator);
	}
	if (hr == S_OK) {
		hr = _pEnumerator->GetDevice(pwstrId, &pDevice);
	}
	if (hr == S_OK) {
		hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
	}
	if (hr == S_OK) {
		// Get the endpoint device's friendly-name property.
		hr = pProps->GetValue(PKEY_Device_FriendlyName, &varString);
	}
	printf("----------------------\nDevice name: \"%S\"\n"
		"  Endpoint ID string: \"%S\"\n",
		(hr == S_OK) ? varString.pwszVal : L"null device",
		(pwstrId != NULL) ? pwstrId : L"null ID");

	PropVariantClear(&varString);

	SAFE_RELEASE(pProps)
		SAFE_RELEASE(pDevice)
		CoUninitialize();
	return hr;
}



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

	notificationClient_ = new CMMNotificationClient();
	hresult = pDeviceEnumerator->RegisterEndpointNotificationCallback(notificationClient_);
	if (FAILED(hresult)) {
		pDevice->Release();
		pDeviceEnumerator->Release();
		delete notificationClient_;
		notificationClient_ = nullptr;
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
	pDeviceEnumerator->UnregisterEndpointNotificationCallback(notificationClient_);
	delete notificationClient_;
	pDeviceEnumerator->Release();
	pDevice->Release();
	pAudioInterface->Release();
	pAudioRenderClient->Release();

bail:
	threadData_ = 2;
	CoUninitialize();
	return 0;
}
