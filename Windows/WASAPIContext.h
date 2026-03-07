#pragma once

#include "Audio/AudioBackend.h"
#include <atomic>
#include <mutex>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <string>
#include <string_view>
#include <memory>
#include <wrl/client.h>

class WASAPIContext : public AudioBackend {
public:
	WASAPIContext();
	~WASAPIContext();

	// This is only called on init.
	void SetRenderCallback(RenderCallback callback, void *userdata) override {
		callback_ = callback;
		userdata_ = userdata;
	}

	void EnumerateDevices(std::vector<AudioDeviceDesc> *outputDevices, bool captureDevices = false) override;

	bool InitOutputDevice(std::string_view uniqueId, LatencyMode latencyMode, bool *revertedToDefault) override;

	void FrameUpdate(bool allowAutoChange) override;

	int PeriodFrames() const override { return actualPeriodFrames_; }  // NOTE: This may have the wrong value (too large) until audio has started playing.
	int BufferSize() const override { return reportedBufferSize_; }
	int SampleRate() const override { return curSamplesPerSec_; }

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

		HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR device) override;
		HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR device) override;
		HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR device) override;
		HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR device, DWORD state) override;
		HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR device, const PROPERTYKEY key) override;
	private:
		WASAPIContext *engine_;
	};

	void DescribeOutputFormat(char *buffer, size_t bufferSize) const override;

	std::string GetCurrentDeviceName() const override {
		std::lock_guard<std::mutex> guard(deviceLock_);
		return curDeviceId_;
	}

private:
	void Start();
	void Stop();

	void AudioLoop();

	enum class AudioFormat {
		Float,
		PCM16,
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
	int curSamplesPerSec_ = 0;
	UINT32 defaultPeriodFrames_ = 0;
	UINT32 fundamentalPeriodFrames_ = 0;
	UINT32 minPeriodFrames_ = 0;
	UINT32 maxPeriodFrames_ = 0;
	std::atomic<bool> running_ = true;
	UINT32 actualPeriodFrames_ = 0;  // may not be the requested.
	UINT32 reportedBufferSize_ = 0;
	Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
	DeviceNotificationClient notificationClient_;
	RenderCallback callback_{};
	void *userdata_ = nullptr;
	LatencyMode latencyMode_ = LatencyMode::Aggressive;

	mutable std::mutex deviceLock_;
	std::string curDeviceId_;
	std::string newDeviceId_;
	bool defaultDeviceChanged_ = false;

	std::unique_ptr<float[]> tempBuf_;
};
