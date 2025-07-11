#pragma once

#include "Audio/AudioBackend.h"
#include <atomic>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <string>
#include <string_view>
#include <wrl/client.h>

class WASAPIContext : public AudioBackend {
public:
	WASAPIContext();
	~WASAPIContext();

	void SetRenderCallback(RenderCallback callback, void *userdata) override {
		callback_ = callback;
		userdata_ = userdata;
	}

	void EnumerateDevices(std::vector<AudioDeviceDesc> *outputDevices, bool captureDevices = false) override;

	bool InitOutputDevice(std::string_view uniqueId, LatencyMode latencyMode, bool *revertedToDefault) override;

	void FrameUpdate(bool allowAutoChange) override;

	int PeriodFrames() const override { return actualPeriodFrames_; }  // NOTE: This may have the wrong value (too large) until audio has started playing.
	int BufferSize() const override { return reportedBufferSize_; }
	int SampleRate() const override { return format_->nSamplesPerSec; }

	// Implements device change notifications
	class DeviceNotificationClient : public IMMNotificationClient {
	public:
		DeviceNotificationClient(WASAPIContext *engine) : engine_(engine) {}
		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
			if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
				*ppv = static_cast<IMMNotificationClient*>(this);
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}

		HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
			if (flow == eRender && role == eConsole) {
				// PostMessage(hwnd, WM_APP + 1, 0, 0);
				engine_->defaultDeviceChanged_ = true;
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
	private:
		WASAPIContext *engine_;
	};

	void DescribeOutputFormat(char *buffer, size_t bufferSize) const override;

private:
	void Start();
	void Stop();

	void AudioLoop();

	enum class AudioFormat {
		Float,
		S16,
		Unhandled,
	};
	static AudioFormat Classify(const WAVEFORMATEX *format);

	// Only one of these can be non-null at a time. Check audioClient3 to determine if it's being used.
	Microsoft::WRL::ComPtr<IAudioClient3> audioClient3_;
	Microsoft::WRL::ComPtr<IAudioClient> audioClient_;

	Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient_;
	WAVEFORMATEX *format_ = nullptr;
	HANDLE audioEvent_ = nullptr;
	std::thread audioThread_;
	UINT32 defaultPeriodFrames = 0, fundamentalPeriodFrames = 0, minPeriodFrames = 0, maxPeriodFrames = 0;
	std::atomic<bool> running_ = true;
	UINT32 actualPeriodFrames_ = 0;  // may not be the requested.
	UINT32 reportedBufferSize_ = 0;
	Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
	DeviceNotificationClient notificationClient_;
	RenderCallback callback_{};
	void *userdata_ = nullptr;
	LatencyMode latencyMode_ = LatencyMode::Aggressive;
	std::string deviceId_;
	std::atomic<bool> defaultDeviceChanged_{};

	float *tempBuf_ = nullptr;
};
