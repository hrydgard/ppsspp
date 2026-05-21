#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <avrt.h>
#include <comdef.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string_view>
#include <wrl/client.h>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadUtil.h"
#include "WASAPIContext.h"

using Microsoft::WRL::ComPtr;

// We must have one of these already...
static inline s16 ClampFloatToS16(float f) {
	f *= 32768.0f;
	if (f >= 32767) {
		return 32767;
	} else if (f < -32767) {
		return -32767;
	} else {
		return (s16)(s32)f;
	}
}

void BuildStereoFloatFormat(const WAVEFORMATEXTENSIBLE *original, WAVEFORMATEXTENSIBLE *output) {
	// Zero‑init all fields first.
	ZeroMemory(output, sizeof(WAVEFORMATEXTENSIBLE));

	// Fill the WAVEFORMATEX base part.
	output->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	output->Format.nChannels = 2;
	output->Format.nSamplesPerSec = original->Format.nSamplesPerSec;
	output->Format.wBitsPerSample = 32;                                 // 32‑bit float
	output->Format.nBlockAlign = output->Format.nChannels *
		output->Format.wBitsPerSample / 8;
	output->Format.nAvgBytesPerSec = output->Format.nSamplesPerSec *
		output->Format.nBlockAlign;
	output->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

	// Fill the extensible fields.
	output->Samples.wValidBitsPerSample = 32;
	output->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
	output->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

WASAPIContext::WASAPIContext() : notificationClient_(this) {
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
	if (FAILED(hr)) {
		// Bad!
		enumerator_ = nullptr;
		return;
	}
	enumerator_->RegisterEndpointNotificationCallback(&notificationClient_);
}

WASAPIContext::~WASAPIContext() {
	if (!enumerator_) {
		// Nothing can have been happening.
		return;
	}
	Stop();
	enumerator_->UnregisterEndpointNotificationCallback(&notificationClient_);
}

WASAPIContext::AudioFormat WASAPIContext::Classify(const WAVEFORMATEX *format) {
	if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE *)format;
		if (ex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
			if (format->nChannels >= 1)
				return AudioFormat::Float;
		} else {
			wchar_t guid[256]{};
			StringFromGUID2(ex->SubFormat, guid, 256);
			ERROR_LOG(Log::Audio, "Got unexpected WASAPI 0xFFFE stream format (%S), expected float!", guid);
			if (ex->Format.wBitsPerSample == 16 && format->nChannels >= 1) {
				INFO_LOG(Log::Audio, "Got a PCM16 audio output (%d channels)", format->nChannels);
				return AudioFormat::PCM16;
			}
		}
	} else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format->nChannels >= 1) {
		return AudioFormat::Float;
	} else if (format->wFormatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16 && format->nChannels >= 1) {
		INFO_LOG(Log::Audio, "Got a PCM16 audio output", format->nChannels);
		return AudioFormat::PCM16;
	} else {
		WARN_LOG(Log::Audio, "Unhandled output format!");
	}
	return AudioFormat::Unhandled;
}

bool GetDeviceDesc(IMMDevice *device, AudioDeviceDesc *desc) {
	ComPtr<IPropertyStore> props;
	device->OpenPropertyStore(STGM_READ, &props);
	PROPVARIANT nameProp;
	PropVariantInit(&nameProp);
	props->GetValue(PKEY_Device_FriendlyName, &nameProp);
	LPWSTR id_str = 0;
	bool success = false;
	if (SUCCEEDED(device->GetId(&id_str))) {
		desc->name = ConvertWStringToUTF8(nameProp.pwszVal);
		desc->uniqueId = ConvertWStringToUTF8(id_str);
		CoTaskMemFree(id_str);
		success = true;
	}
	PropVariantClear(&nameProp);
	return success;
}

void WASAPIContext::EnumerateDevices(std::vector<AudioDeviceDesc> *output, bool captureDevices) {
	ComPtr<IMMDeviceCollection> collection;
	enumerator_->EnumAudioEndpoints(captureDevices ? eCapture : eRender, DEVICE_STATE_ACTIVE, &collection);

	if (!collection) {
		ERROR_LOG(Log::Audio, "Failed to enumerate devices");
		return;
	}

	UINT count = 0;
	collection->GetCount(&count);

	for (UINT i = 0; i < count; ++i) {
		ComPtr<IMMDevice> device;
		collection->Item(i, &device);

		AudioDeviceDesc desc{};
		if (GetDeviceDesc(device.Get(), &desc)) {
			output->push_back(desc);
		}
	}
}

// Also logs.
void WASAPIContext::SetErrorString(std::string_view str, HRESULT hr) {
	std::string temp = StringFromFormat("%s (HRESULT: %08lx)", str.data(), hr);
	ERROR_LOG(Log::Audio, "%s", temp.c_str());
	std::lock_guard<std::mutex> guard(errorLock_);
	errorString_ = temp;
}

void WASAPIContext::ClearErrorString() {
	std::lock_guard<std::mutex> guard(errorLock_);
	errorString_.clear();
}

bool WASAPIContext::TryInitAudioClient3(IMMDevice *device, LatencyMode latencyMode) {
	HRESULT hr = E_FAIL;
	// Try IAudioClient3 first if not in "safe" mode. It's probably safe anyway, but still, let's use the legacy client as a safe fallback option.
	if (latencyMode != LatencyMode::Safe) {
		hr = device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, (void**)&audioClient3_);
	} else {
		// Proceed to AudioClient.
		INFO_LOG(Log::Audio, "LatencyMode::Safe is set, skipping AudioClient3 and going directly to AudioClient");
		return false;
	}

	if (!SUCCEEDED(hr)) {
		audioClient3_.Reset();
		return false;
	}

	hr = audioClient3_->GetMixFormat(&format_);
	if (FAILED(hr)) {
		audioClient3_.Reset();
		SetErrorString("AudioClient3 GetMixFormat failed", hr);
		return false;
	}
	curSamplesPerSec_ = format_->nSamplesPerSec;

	// AudioClient3 requires an exact format match because it doesn't support AUTOCONVERTPCM.
	// Our callback always produces stereo float (see RenderCallback in AudioBackend.h),
	// so we can only use AudioClient3 when the device's native format is stereo float.
	// For other formats, we fall back to AudioClient which supports conversion via AUTOCONVERTPCM
	// or manual conversion through tempBuf_.
	if (format_->nChannels != 2 || Classify(format_) != AudioFormat::Float) {
		INFO_LOG(Log::Audio, "AudioClient3: Got %d channels or non-float format, falling back to AudioClient", format_->nChannels);
		audioClient3_.Reset();
		// Free the format before falling through - AudioClient will allocate a new one
		CoTaskMemFree(format_);
		format_ = nullptr;
		return false;
	} else {
		hr = audioClient3_->GetSharedModeEnginePeriod(format_, &defaultPeriodFrames_, &fundamentalPeriodFrames_, &minPeriodFrames_, &maxPeriodFrames_);
		if (FAILED(hr)) {
			audioClient3_.Reset();
			CoTaskMemFree(format_);
			format_ = nullptr;
			SetErrorString("AudioClient3 GetSharedModeEnginePeriod failed", hr);
			return false;
		}

		INFO_LOG(Log::Audio, "AudioClient3: default: %d fundamental: %d min: %d max: %d\n", (int)defaultPeriodFrames_, (int)fundamentalPeriodFrames_, (int)minPeriodFrames_, (int)maxPeriodFrames_);
		INFO_LOG(Log::Audio, "initializing with %d frame period at %d Hz, meaning %0.1fms\n", (int)minPeriodFrames_, (int)format_->nSamplesPerSec, FramesToMs(minPeriodFrames_, format_->nSamplesPerSec));

		hr = audioClient3_->InitializeSharedAudioStream(
			AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
			minPeriodFrames_,
			format_,
			nullptr
		);
		if (FAILED(hr)) {
			WARN_LOG(Log::Audio, "Error initializing AudioClient3 shared audio stream: %08lx", hr);
			audioClient3_.Reset();
			CoTaskMemFree(format_);
			format_ = nullptr;
			SetErrorString("AudioClient3 init failed", hr);
			return false;
		}
		actualPeriodFrames_ = minPeriodFrames_;

		UINT32 bufSize = 0;
		hr = audioClient3_->GetBufferSize(&bufSize);
		reportedBufferSize_.store(bufSize);
		if (FAILED(hr)) {
			audioClient3_.Reset();
			CoTaskMemFree(format_);
			format_ = nullptr;
			SetErrorString("AudioClient3 GetBufferSize failed", hr);
			return false;
		}

		hr = audioClient3_->SetEventHandle(audioEvent_);
		if (FAILED(hr)) {
			audioClient3_.Reset();
			CoTaskMemFree(format_);
			format_ = nullptr;
			SetErrorString("AudioClient3 SetEventHandle failed", hr);
			return false;
		}

		hr = audioClient3_->GetService(IID_PPV_ARGS(&renderClient_));
		if (FAILED(hr)) {
			audioClient3_.Reset();
			CoTaskMemFree(format_);
			format_ = nullptr;
			SetErrorString("AudioClient3 GetService failed", hr);
			return false;
		}
	}
	return true;
}

bool WASAPIContext::TryInitAudioClient(IMMDevice *device, LatencyMode latencyMode) {
	// Fallback to IAudioClient (older OS)
	HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient_);
	if (FAILED(hr)) {
		SetErrorString("Failed to activate audio device", hr);
		return false;
	}

	hr = audioClient_->GetMixFormat(&format_);
	if (FAILED(hr)) {
		audioClient_.Reset();
		SetErrorString("AudioClient GetMixFormat failed", hr);
		return false;
	}

	// If there are too many channels, try asking for a 2-channel output format.
	DWORD extraStreamFlags = 0;
	const AudioFormat fmt = Classify(format_);

	curSamplesPerSec_ = format_->nSamplesPerSec;

	bool createBuffer = false;
	if (fmt == AudioFormat::Float) {
		if (format_->nChannels != 2) {
			INFO_LOG(Log::Audio, "Got %d channels, asking for stereo instead", format_->nChannels);
			WAVEFORMATEXTENSIBLE stereo;
			BuildStereoFloatFormat((const WAVEFORMATEXTENSIBLE *)format_, &stereo);

			WAVEFORMATEX *closestMatch = nullptr;
			const HRESULT result = audioClient_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, (const WAVEFORMATEX *)&stereo, &closestMatch);
			if (result == S_OK) {
				// We got the format! Use it and set as current.
				_dbg_assert_(!closestMatch);
				WAVEFORMATEX *newFormat = (WAVEFORMATEX *)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
				_dbg_assert_(newFormat);
				memcpy(newFormat, &stereo, sizeof(WAVEFORMATEX) + stereo.Format.cbSize);
				CoTaskMemFree(format_);
				format_ = newFormat;
				extraStreamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
				INFO_LOG(Log::Audio, "Successfully asked for two channels");
			} else if (result == S_FALSE) {
				// We got another format. Meh, let's just use what we got.
				if (closestMatch) {
					WARN_LOG(Log::Audio, "Didn't get the format we wanted, but got: %lu ch=%d", closestMatch->nSamplesPerSec, closestMatch->nChannels);
					CoTaskMemFree(closestMatch);
				} else {
					WARN_LOG(Log::Audio, "Failed to fall back to two channels. Using workarounds.");
				}
				createBuffer = true;
			} else {
				WARN_LOG(Log::Audio, "Got other error %08lx", result);
				_dbg_assert_(!closestMatch);
			}
		} else {
			// All good, nothing to convert.
			_dbg_assert_(format_);
		}
	} else {
		// Some other format.
		WARN_LOG(Log::Audio, "Format not float, applying conversion.");
		createBuffer = true;
	}

	// Get engine period info
	REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
	audioClient_->GetDevicePeriod(&defaultPeriod, &minPeriod);

	const REFERENCE_TIME duration = minPeriod;
	hr = audioClient_->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK | extraStreamFlags,
		duration,  // This is a minimum, the result might be larger. We use GetBufferSize to check.
		0,  // ref duration, always 0 in shared mode.
		format_,
		nullptr
	);

	if (FAILED(hr)) {
		audioClient_.Reset();
		CoTaskMemFree(format_);
		format_ = nullptr;
		SetErrorString("AudioClient init failed", hr);
		return false;
	}

	UINT32 bufSize = 0;
	hr = audioClient_->GetBufferSize(&bufSize);
	reportedBufferSize_.store(bufSize);
	if (FAILED(hr)) {
		audioClient_.Reset();
		CoTaskMemFree(format_);
		format_ = nullptr;
		SetErrorString("AudioClient GetBufferSize failed", hr);
		return false;
	}
	actualPeriodFrames_.store(reportedBufferSize_.load());  // we don't have a better estimate.

	hr = audioClient_->SetEventHandle(audioEvent_);
	if (FAILED(hr)) {
		audioClient_.Reset();
		CoTaskMemFree(format_);
		format_ = nullptr;
		SetErrorString("AudioClient SetEventHandle failed", hr);
		return false;
	}

	hr = audioClient_->GetService(IID_PPV_ARGS(&renderClient_));
	if (FAILED(hr)) {
		audioClient_.Reset();
		CoTaskMemFree(format_);
		format_ = nullptr;
		SetErrorString("AudioClient GetService failed", hr);
		return false;
	}

	if (createBuffer) {
		tempBuf_ = std::make_unique<float[]>(reportedBufferSize_.load() * 2);
	}
	return true;
}

bool WASAPIContext::InitOutputDevice(std::string_view uniqueId, LatencyMode latencyMode, bool *revertedToDefault) {
	Stop();

	*revertedToDefault = false;

	ComPtr<IMMDevice> device;
	if (uniqueId.empty()) {
		// Use the default device.
		HRESULT hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		if (FAILED(hr)) {
			SetErrorString("Failed to get the default endpoint", hr);
			return false;
		}
	} else {
		// Use whatever device.
		std::wstring wId = ConvertUTF8ToWString(uniqueId);
		HRESULT hr = enumerator_->GetDevice(wId.c_str(), &device);
		if (FAILED(hr)) {
			// Fallback to default device
			INFO_LOG(Log::Audio, "Falling back to default device...\n");
			*revertedToDefault = true;
			hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device);
			if (FAILED(hr)) {
				SetErrorString("Failed to fallback", hr);
				return false;
			}
		}
	}

	AudioDeviceDesc desc{};
	GetDeviceDesc(device.Get(), &desc);
	INFO_LOG(Log::Audio, "Activating audio device: %s : %s", desc.name.c_str(), desc.uniqueId.c_str());

	{
		std::lock_guard<std::mutex> guard(deviceLock_);
		curDeviceId_ = desc.uniqueId;
		curDeviceName_ = desc.name;
	}

	// Get rid of any old tempBuf_.
	tempBuf_.reset();

	// This is used by both paths.
	audioEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (!TryInitAudioClient3(device.Get(), latencyMode)) {
		if (!TryInitAudioClient(device.Get(), latencyMode)) {
			// Failed both client types.
			CloseHandle(audioEvent_);
			audioEvent_ = nullptr;
			return false;
		}
	}

	latencyMode_ = latencyMode;

	_dbg_assert_(audioClient_ || audioClient3_);

	Start();

	return true;
}

void WASAPIContext::Start() {
	if (audioThread_.joinable()) {
		_dbg_assert_(false);
		ERROR_LOG(Log::Audio, "Audio thread already running!");
		return;
	}

	running_ = true;
	audioThread_ = std::thread([this]() { AudioLoop(); });
}

void WASAPIContext::Stop() {
	running_ = false;
	if (audioEvent_) SetEvent(audioEvent_);
	// Stop is actually called on the audioclient in the thread, while exiting.
	if (audioThread_.joinable()) audioThread_.join();

	renderClient_.Reset();
	audioClient_.Reset();
	audioClient3_.Reset();
	if (audioEvent_) {
		CloseHandle(audioEvent_);
		audioEvent_ = nullptr;
	}
	if (format_) {
		CoTaskMemFree(format_);
		format_ = nullptr;
	}
	{
		std::lock_guard<std::mutex> guard(deviceLock_);
		curDeviceId_.clear();
		curDeviceName_.clear();
	}
}

void WASAPIContext::FrameUpdate(bool allowAutoChange) {
	std::string deviceIdToInit;
	{
		std::lock_guard<std::mutex> guard(deviceLock_);
		if (!defaultDeviceChanged_) {
			return;
		}

		if (allowAutoChange) {
			// Check if there actually was a change, we ignore false positives.
			{
				if (newDeviceId_ == curDeviceId_) {
					// False positive, ignore.
					defaultDeviceChanged_ = false;
					return;
				}
				deviceIdToInit = newDeviceId_;
				newDeviceId_.clear();
			}
			defaultDeviceChanged_ = false;
		}
	}

	bool reverted;
	InitOutputDevice(deviceIdToInit, latencyMode_, &reverted);
}

void WASAPIContext::AudioLoop() {
	SetCurrentThreadName("WASAPIAudioLoop");

	DWORD taskID = 0;
	HANDLE mmcssHandle = nullptr;
	if (latencyMode_ == LatencyMode::Aggressive) {
		mmcssHandle = AvSetMmThreadCharacteristics(L"Pro Audio", &taskID);
	}

	UINT32 available;
	HRESULT hr;
	if (audioClient3_) {
		hr = audioClient3_->Start();
		if (FAILED(hr)) {
			SetErrorString("AudioClient3::Start failed", hr);
			return;
		}
		hr = audioClient3_->GetBufferSize(&available);
		if (FAILED(hr)) {
			SetErrorString("AudioClient3::GetBufferSize failed", hr);
			audioClient3_->Stop();
			return;
		}
		// Check if buffer grew beyond what we allocated tempBuf_ for
		if (tempBuf_ && available > reportedBufferSize_.load()) {
			INFO_LOG(Log::Audio, "Buffer size grew from %d to %d, reallocating tempBuf_", reportedBufferSize_.load(), available);
			tempBuf_ = std::make_unique<float[]>(available * 2);
			reportedBufferSize_.store(available);
		}
	} else if (audioClient_) {
		hr = audioClient_->Start();
		if (FAILED(hr)) {
			SetErrorString("AudioClient::Start failed", hr);
			return;
		}
		hr = audioClient_->GetBufferSize(&available);
		if (FAILED(hr)) {
			SetErrorString("AudioClient::GetBufferSize failed", hr);
			audioClient_->Stop();
			return;
		}
		// Check if buffer grew beyond what we allocated tempBuf_ for
		if (tempBuf_ && available > reportedBufferSize_.load()) {
			INFO_LOG(Log::Audio, "Buffer size grew from %d to %d, reallocating tempBuf_", reportedBufferSize_.load(), available);
			tempBuf_ = std::make_unique<float[]>(available * 2);
			reportedBufferSize_.store(available);
		}
	} else {
		// No audio client, nothing to do.
		SetErrorString("No audio client in AudioLoop", 0);
		return;
	}

	if (!format_) {
		ERROR_LOG(Log::Audio, "Can't start audio - no format");
		return;
	}

	const AudioFormat format = Classify(format_);
	const int nChannels = format_->nChannels;

	ClearErrorString();

	while (running_) {
		const DWORD waitResult = WaitForSingleObject(audioEvent_, INFINITE);
		if (waitResult != WAIT_OBJECT_0) {
			// Something bad happened.
			break;
		}

		UINT32 padding = 0;
		if (audioClient3_) {
			audioClient3_->GetCurrentPadding(&padding);
		} else {
			audioClient_->GetCurrentPadding(&padding);
		}

		UINT32 framesToWrite = available - padding;

		// Safety: clamp framesToWrite to tempBuf_ capacity if using conversion path
		const UINT32 bufCapacity = reportedBufferSize_.load();
		if (tempBuf_ && framesToWrite > bufCapacity) {
			WARN_LOG(Log::Audio, "framesToWrite (%d) exceeds buffer capacity (%d), clamping", framesToWrite, bufCapacity);
			framesToWrite = bufCapacity;
		}

		BYTE* buffer = nullptr;
		if (framesToWrite > 0 && SUCCEEDED(renderClient_->GetBuffer(framesToWrite, &buffer))) {
			if (!tempBuf_) {
				// Mix directly to the output buffer, avoiding a copy.
				if (buffer) {
					callback_(reinterpret_cast<float *>(buffer), framesToWrite, format_->nSamplesPerSec, userdata_);
				}
			} else {
				// We decided previously that we need conversion, so mix to our temp buffer...
				callback_(tempBuf_.get(), framesToWrite, format_->nSamplesPerSec, userdata_);
				// .. and convert according to format (we support multi-channel float and s16)
				if (format == AudioFormat::PCM16 && buffer) {
					// Need to convert.
					s16 *dest = reinterpret_cast<s16 *>(buffer);
					for (UINT32 i = 0; i < framesToWrite; i++) {
						if (nChannels == 1) {
							// Maybe some bluetooth speakers? Mixdown.
							float sum = 0.5f * (tempBuf_[i * 2] + tempBuf_[i * 2 + 1]);
							dest[i] = ClampFloatToS16(sum);
						} else if (nChannels == 2) {
							// Stereo output
							dest[i * 2] = ClampFloatToS16(tempBuf_[i * 2]);
							dest[i * 2 + 1] = ClampFloatToS16(tempBuf_[i * 2 + 1]);
						} else {
							// Multi-channel output (e.g., 5.1, 7.1)
							// Copy stereo to front L/R channels
							dest[i * nChannels] = ClampFloatToS16(tempBuf_[i * 2]);      // Front Left
							dest[i * nChannels + 1] = ClampFloatToS16(tempBuf_[i * 2 + 1]);  // Front Right

							// For 5.1/7.1 systems, also send audio to rear channels
							if (nChannels >= 4) {
								// Rear/Surround Left and Right at reduced volume
								dest[i * nChannels + 2] = ClampFloatToS16(tempBuf_[i * 2] * 0.7f);
								dest[i * nChannels + 3] = ClampFloatToS16(tempBuf_[i * 2 + 1] * 0.7f);
							}
							// Center and LFE (if present)
							for (int j = 4; j < nChannels; j++) {
								if (j == 4 && nChannels >= 6) {
									// Center channel - mix of L+R at reduced volume
									dest[i * nChannels + j] = ClampFloatToS16((tempBuf_[i * 2] + tempBuf_[i * 2 + 1]) * 0.5f * 0.7f);
								} else if (j == 5 && nChannels >= 6) {
									// LFE channel - bass from L+R at reduced volume
									dest[i * nChannels + j] = ClampFloatToS16((tempBuf_[i * 2] + tempBuf_[i * 2 + 1]) * 0.5f * 0.5f);
								} else {
									// Any extra channels get zeroed
									dest[i * nChannels + j] = 0;
								}
							}
						}
					}
				} else if (format == AudioFormat::Float && buffer) {
					// We have a non-2 number of channels (since we're in the tempBuf_ 'if'), so we contract/expand.
					float *dest = reinterpret_cast<float *>(buffer);
					for (UINT32 i = 0; i < framesToWrite; i++) {
						if (nChannels == 1) {
							// Maybe some bluetooth speakers? Mixdown.
							dest[i] = 0.5f * (tempBuf_[i * 2] + tempBuf_[i * 2 + 1]);
						} else if (nChannels == 2) {
							// Stereo output
							dest[i * 2] = tempBuf_[i * 2];
							dest[i * 2 + 1] = tempBuf_[i * 2 + 1];
						} else {
							// Multi-channel output (e.g., 5.1, 7.1)
							// Copy stereo to front L/R channels
							dest[i * nChannels] = tempBuf_[i * 2];      // Front Left
							dest[i * nChannels + 1] = tempBuf_[i * 2 + 1];  // Front Right

							// For 5.1/7.1 systems, also send audio to rear channels
							// This prevents the "half silent" buffer issue that can cause crackling
							if (nChannels >= 4) {
								// Rear/Surround Left and Right at reduced volume
								dest[i * nChannels + 2] = tempBuf_[i * 2] * 0.7f;      // Rear/Side Left
								dest[i * nChannels + 3] = tempBuf_[i * 2 + 1] * 0.7f;  // Rear/Side Right
							}
							// Center and LFE (if present)
							for (int j = 4; j < nChannels; j++) {
								if (j == 4 && nChannels >= 6) {
									// Center channel - mix of L+R at reduced volume
									dest[i * nChannels + j] = (tempBuf_[i * 2] + tempBuf_[i * 2 + 1]) * 0.5f * 0.7f;
								} else if (j == 5 && nChannels >= 6) {
									// LFE channel - bass from L+R at reduced volume
									dest[i * nChannels + j] = (tempBuf_[i * 2] + tempBuf_[i * 2 + 1]) * 0.5f * 0.5f;
								} else {
									// Any extra channels get zeroed
									dest[i * nChannels + j] = 0;
								}
							}
						}
					}
				}
			}

			renderClient_->ReleaseBuffer(framesToWrite, 0);

			// In the old mode, we just estimate the "actualPeriodFrames_" from the framesToWrite.
			if (audioClient_ && framesToWrite < actualPeriodFrames_.load()) {
				actualPeriodFrames_.store(framesToWrite);
			}
		}
	}

	if (audioClient3_) {
		audioClient3_->Stop();
	}
	if (audioClient_) {
		audioClient_->Stop();
	}

	if (mmcssHandle) {
		AvRevertMmThreadCharacteristics(mmcssHandle);
	}
}

void WASAPIContext::DescribeOutputFormat(char *buffer, size_t bufferSize) const {
	if (!format_) {
		snprintf(buffer, bufferSize, "No format");
		return;
	}
	const int numChannels = format_->nChannels;
	const int sampleBits = format_->wBitsPerSample;
	const int sampleRateHz = format_->nSamplesPerSec;
	const char *fmt = "N/A";
	if (format_->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE *)format_;
		if (ex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
			fmt = "float";
		} else {
			fmt = "PCM";
		}
	} else {
		fmt = "PCM";  // probably
	}
	snprintf(buffer, bufferSize, "%d Hz %s %d-bit, %d ch%s", sampleRateHz, fmt, sampleBits, numChannels, audioClient3_ ? " (ac3)" : " (ac)");
}


HRESULT STDMETHODCALLTYPE WASAPIContext::DeviceNotificationClient::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR device) {
	if (flow != eRender) {
		INFO_LOG(Log::Audio, "Default WASAPI audio recording device changed! Currently ignoring.");
		return S_OK;
	}
	INFO_LOG(Log::Audio, "Default device changed to %s! role=%d", ConvertWStringToUTF8(device).c_str(), role);
	if (role == eConsole) {
		// PostMessage(hwnd, WM_APP + 1, 0, 0);
		std::lock_guard<std::mutex> guard(engine_->deviceLock_);
		engine_->defaultDeviceChanged_ = true;
		engine_->newDeviceId_ = ConvertWStringToUTF8(device);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPIContext::DeviceNotificationClient::OnDeviceAdded(LPCWSTR device) {
	INFO_LOG(Log::Audio, "Audio device added! device=%s", ConvertWStringToUTF8(device).c_str());
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPIContext::DeviceNotificationClient::OnDeviceRemoved(LPCWSTR device) {
	INFO_LOG(Log::Audio, "Audio device removed! device=%s", ConvertWStringToUTF8(device).c_str());
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPIContext::DeviceNotificationClient::OnDeviceStateChanged(LPCWSTR device, DWORD state) {
	INFO_LOG(Log::Audio, "Audio device state changed! device=%s state=%08x", ConvertWStringToUTF8(device).c_str(), state);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPIContext::DeviceNotificationClient::OnPropertyValueChanged(LPCWSTR device, const PROPERTYKEY key) {
	return S_OK;
}
