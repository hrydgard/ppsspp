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
	delete[] tempBuf_;
}

WASAPIContext::AudioFormat WASAPIContext::Classify(const WAVEFORMATEX *format) {
	if (format->wFormatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 2) {
		return AudioFormat::S16;
	} else if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE *)format;
		if (ex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
			return AudioFormat::Float;
		}
	} else {
		WARN_LOG(Log::Audio, "Unhandled output format!");
	}
	return AudioFormat::Unhandled;
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

		ComPtr<IPropertyStore> props;
		device->OpenPropertyStore(STGM_READ, &props);

		PROPVARIANT nameProp;
		PropVariantInit(&nameProp);
		props->GetValue(PKEY_Device_FriendlyName, &nameProp);

		LPWSTR id_str = 0;
		if (SUCCEEDED(device->GetId(&id_str))) {
			AudioDeviceDesc desc;
			desc.name = ConvertWStringToUTF8(nameProp.pwszVal);
			desc.uniqueId = ConvertWStringToUTF8(id_str);
			output->push_back(desc);
			CoTaskMemFree(id_str);
		}

		PropVariantClear(&nameProp);
	}
}

bool WASAPIContext::InitOutputDevice(std::string_view uniqueId, LatencyMode latencyMode, bool *revertedToDefault) {
	Stop();

	*revertedToDefault = false;

	ComPtr<IMMDevice> device;
	if (uniqueId.empty()) {
		// Use the default device.
		if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
			return false;
		}
	} else {
		// Use whatever device.
		std::wstring wId = ConvertUTF8ToWString(uniqueId);
		if (FAILED(enumerator_->GetDevice(wId.c_str(), &device))) {
			// Fallback to default device
			INFO_LOG(Log::Audio, "Falling back to default device...\n");
			*revertedToDefault = true;
			if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
				return false;
			}
		}
	}

	deviceId_ = uniqueId;

	HRESULT hr = E_FAIL;
	// Try IAudioClient3 first if not in "safe" mode. It's probably safe anyway, but still, let's use the legacy client as a safe fallback option.
	if (false && latencyMode != LatencyMode::Safe) {
		hr = device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, (void**)&audioClient3_);
	}

	// Get rid of any old tempBuf_.
	delete[] tempBuf_;
	tempBuf_ = nullptr;

	if (SUCCEEDED(hr)) {
		audioClient3_->GetMixFormat(&format_);
		// We only use AudioClient3 if we got the format we wanted (stereo float).
		if (format_->nChannels != 2 || Classify(format_) != AudioFormat::Float) {
			// Let's fall back to the old path. The docs seem to be wrong, if you try to create an
			// AudioClient3 with low latency audio with AUTOCONVERTPCM, you get the error 0x88890021.
			audioClient3_.Reset();
			// Fall through to AudioClient creation below.
		} else {
			audioClient3_->GetSharedModeEnginePeriod(format_, &defaultPeriodFrames, &fundamentalPeriodFrames, &minPeriodFrames, &maxPeriodFrames);

			INFO_LOG(Log::Audio, "default: %d fundamental: %d min: %d max: %d\n", (int)defaultPeriodFrames, (int)fundamentalPeriodFrames, (int)minPeriodFrames, (int)maxPeriodFrames);
			INFO_LOG(Log::Audio, "initializing with %d frame period at %d Hz, meaning %0.1fms\n", (int)minPeriodFrames, (int)format_->nSamplesPerSec, FramesToMs(minPeriodFrames, format_->nSamplesPerSec));

			audioEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			HRESULT result = audioClient3_->InitializeSharedAudioStream(
				AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
				minPeriodFrames,
				format_,
				nullptr
			);
			if (FAILED(result)) {
				WARN_LOG(Log::Audio, "Error initializing AudioClient3 shared audio stream: %08lx", result);
				audioClient3_.Reset();
				return false;
			}
			actualPeriodFrames_ = minPeriodFrames;

			audioClient3_->GetBufferSize(&reportedBufferSize_);
			audioClient3_->SetEventHandle(audioEvent_);
			audioClient3_->GetService(IID_PPV_ARGS(&renderClient_));
		}
	}

	if (!audioClient3_) {
		// Fallback to IAudioClient (older OS)
		device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient_);

		audioClient_->GetMixFormat(&format_);

		// If there are too many channels, try asking for a 2-channel output format.
		DWORD extraStreamFlags = 0;
		const AudioFormat fmt = Classify(format_);

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
					format_ = (WAVEFORMATEX *)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
					memcpy(format_, &stereo, sizeof(WAVEFORMATEX) + stereo.Format.cbSize);
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
			}
		} else {
			// Some other format.
			WARN_LOG(Log::Audio, "Format not float, applying conversion.");
			createBuffer = true;
		}

		// Get engine period info
		REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
		audioClient_->GetDevicePeriod(&defaultPeriod, &minPeriod);

		audioEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		const REFERENCE_TIME duration = minPeriod;
		HRESULT hr = audioClient_->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_EVENTCALLBACK | extraStreamFlags,
			duration,  // This is a minimum, the result might be larger. We use GetBufferSize to check.
			0,  // ref duration, always 0 in shared mode.
			format_,
			nullptr
		);

		if (FAILED(hr)) {
			WARN_LOG(Log::Audio, "ERROR: Failed to initialize audio with all attempted buffer sizes\n");
			audioClient_.Reset();
			return false;
		}

		audioClient_->GetBufferSize(&reportedBufferSize_);
		actualPeriodFrames_ = reportedBufferSize_;  // we don't have a better estimate.
		audioClient_->SetEventHandle(audioEvent_);
		audioClient_->GetService(IID_PPV_ARGS(&renderClient_));

		if (createBuffer) {
			tempBuf_ = new float[reportedBufferSize_ * 2];
		}
	}

	latencyMode_ = latencyMode;

	Start();

	return true;
}

void WASAPIContext::Start() {
	running_ = true;
	audioThread_ = std::thread([this]() { AudioLoop(); });
}

void WASAPIContext::Stop() {
	running_ = false;
	if (audioClient_) audioClient_->Stop();
	if (audioEvent_) SetEvent(audioEvent_);
	if (audioThread_.joinable()) audioThread_.join();

	renderClient_.Reset();
	audioClient_.Reset();
	if (audioEvent_) {
		CloseHandle(audioEvent_);
		audioEvent_ = nullptr;
	}
	if (format_) {
		CoTaskMemFree(format_);
		format_ = nullptr;
	}
}

void WASAPIContext::FrameUpdate(bool allowAutoChange) {
	if (deviceId_.empty() && defaultDeviceChanged_ && allowAutoChange) {
		defaultDeviceChanged_ = false;
		Stop();
		Start();
	}
}

void WASAPIContext::AudioLoop() {
	SetCurrentThreadName("WASAPIAudioLoop");

	DWORD taskID = 0;
	HANDLE mmcssHandle = nullptr;
	if (latencyMode_ == LatencyMode::Aggressive) {
		mmcssHandle = AvSetMmThreadCharacteristics(L"Pro Audio", &taskID);
	}

	UINT32 available;
	if (audioClient3_) {
		audioClient3_->Start();
		audioClient3_->GetBufferSize(&available);
	} else {
		audioClient_->Start();
		audioClient_->GetBufferSize(&available);
	}

	const AudioFormat format = Classify(format_);
	const int nChannels = format_->nChannels;

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

		const UINT32 framesToWrite = available - padding;
		BYTE* buffer = nullptr;
		if (framesToWrite > 0 && SUCCEEDED(renderClient_->GetBuffer(framesToWrite, &buffer))) {
			if (!tempBuf_) {
				// Mix directly to the output buffer, avoiding a copy.
				if (buffer) {
					callback_(reinterpret_cast<float *>(buffer), framesToWrite, format_->nSamplesPerSec, userdata_);
				}
			} else {
				// We decided previously that we need conversion, so mix to our temp buffer...
				callback_(tempBuf_, framesToWrite, format_->nSamplesPerSec, userdata_);
				// .. and convert according to format (we support multi-channel float and s16)
				if (format == AudioFormat::S16 && buffer) {
					// Need to convert.
					s16 *dest = reinterpret_cast<s16 *>(buffer);
					for (UINT32 i = 0; i < framesToWrite; i++) {
						if (nChannels == 1) {
							// Maybe some bluetooth speakers? Mixdown.
							float sum = 0.5f * (tempBuf_[i * 2] + tempBuf_[i * 2 + 1]);
							dest[i] = ClampFloatToS16(sum);
						} else {
							dest[i * nChannels] = ClampFloatToS16(tempBuf_[i * 2]);
							dest[i * nChannels + 1] = ClampFloatToS16(tempBuf_[i * 2 + 1]);
							// Zero other channels.
							for (int j = 2; j < nChannels; j++) {
								dest[i * nChannels + j] = 0;
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
						} else {
							dest[i * nChannels] = tempBuf_[i * 2];
							dest[i * nChannels + 1] = tempBuf_[i * 2 + 1];
							// Zero other channels.
							for (int j = 2; j < nChannels; j++) {
								dest[i * nChannels + j] = 0;
							}
						}
					}
				}
			}

			renderClient_->ReleaseBuffer(framesToWrite, 0);
		}

		// In the old mode, we just estimate the "actualPeriodFrames_" from the framesToWrite.
		if (audioClient_ && framesToWrite < actualPeriodFrames_) {
			actualPeriodFrames_ = framesToWrite;
		}
	}

	if (audioClient3_) {
		audioClient3_->Stop();
	} else {
		audioClient_->Stop();
	}

	if (mmcssHandle) {
		AvRevertMmThreadCharacteristics(mmcssHandle);
	}
}

void WASAPIContext::DescribeOutputFormat(char *buffer, size_t bufferSize) const {
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
