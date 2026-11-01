// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <windows.h>
// EndpointFormFactor has an unscoped Microphone, which clashes with our namespace.
#define Microphone EndpointFormFactor_Microphone
#include <mmdeviceapi.h>
#undef Microphone
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <wrl/client.h>

#include <thread>

#include "Common/Audio/SampleRing.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Config.h"
#include "Core/HLE/sceUsbMic.h"
#include "Windows/WASAPIMicrophone.h"

using Microsoft::WRL::ComPtr;

// From the capture thread to the CPU thread. 0.7 s at 44.1 kHz.
static SampleRing<32768> g_micRing;

// Only touched on the CPU thread.
static std::thread g_micThread;

static HANDLE g_stopEvent;    // Manual reset, the thread exits.
static HANDLE g_reopenEvent;  // Auto reset, the thread reopens the device.

// How long the shared-mode buffer is, in 100 ns units.
static constexpr REFERENCE_TIME MIC_BUFFER_DURATION = 1000000;  // 100 ms.

static std::string GetFriendlyName(IMMDevice *device) {
	std::string name;
	ComPtr<IPropertyStore> props;
	if (FAILED(device->OpenPropertyStore(STGM_READ, &props)) || !props) {
		return name;
	}
	PROPVARIANT nameProp;
	PropVariantInit(&nameProp);
	if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &nameProp)) && nameProp.vt == VT_LPWSTR && nameProp.pwszVal) {
		name = ConvertWStringToUTF8(nameProp.pwszVal);
	}
	PropVariantClear(&nameProp);
	return name;
}

// The configured device if it's there, otherwise the default one.
static ComPtr<IMMDevice> FindDevice(IMMDeviceEnumerator *enumerator) {
	ComPtr<IMMDevice> device;
	const std::string wanted = g_Config.sMicDevice;
	if (!wanted.empty()) {
		ComPtr<IMMDeviceCollection> collection;
		UINT count = 0;
		if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection)) && collection) {
			collection->GetCount(&count);
		}
		for (UINT i = 0; i < count; i++) {
			ComPtr<IMMDevice> candidate;
			if (SUCCEEDED(collection->Item(i, &candidate)) && candidate && GetFriendlyName(candidate.Get()) == wanted) {
				return candidate;
			}
		}
		WARN_LOG(Log::Audio, "Microphone '%s' not found, using the default one", wanted.c_str());
	}
	enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
	return device;
}

enum class CaptureResult {
	STOPPED,
	REOPEN,
	FAILED,
};

// Captures until we're stopped, or asked to reopen, or the device goes away.
static CaptureResult RunCapture(IMMDeviceEnumerator *enumerator, int sampleRate, HANDLE audioEvent, bool logErrors) {
	ComPtr<IMMDevice> device = FindDevice(enumerator);
	if (!device) {
		if (logErrors) {
			WARN_LOG(Log::Audio, "No microphone, the game will get silence");
		}
		return CaptureResult::FAILED;
	}

	ComPtr<IAudioClient> client;
	HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client);
	if (SUCCEEDED(hr)) {
		// Mono 16-bit at the rate the game asked for. WASAPI converts from the mix format.
		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 1;
		format.nSamplesPerSec = sampleRate;
		format.wBitsPerSample = 16;
		format.nBlockAlign = 2;
		format.nAvgBytesPerSec = sampleRate * 2;
		const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
		hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, MIC_BUFFER_DURATION, 0, &format, nullptr);
	}
	if (SUCCEEDED(hr)) {
		hr = client->SetEventHandle(audioEvent);
	}
	ComPtr<IAudioCaptureClient> capture;
	if (SUCCEEDED(hr)) {
		hr = client->GetService(IID_PPV_ARGS(&capture));
	}
	if (SUCCEEDED(hr)) {
		hr = client->Start();
	}
	const std::string name = GetFriendlyName(device.Get());
	if (FAILED(hr)) {
		if (logErrors) {
			ERROR_LOG(Log::Audio, "Could not start microphone '%s' (HRESULT: %08lx)", name.c_str(), hr);
		}
		return CaptureResult::FAILED;
	}
	INFO_LOG(Log::Audio, "Microphone '%s' started at %d Hz", name.c_str(), sampleRate);

	const HANDLE events[3] = { g_stopEvent, g_reopenEvent, audioEvent };
	CaptureResult captureResult = CaptureResult::FAILED;
	while (true) {
		const DWORD result = WaitForMultipleObjects(3, events, FALSE, INFINITE);
		if (result == WAIT_OBJECT_0) {
			captureResult = CaptureResult::STOPPED;
			break;
		} else if (result == WAIT_OBJECT_0 + 1) {
			captureResult = CaptureResult::REOPEN;
			break;
		} else if (result != WAIT_OBJECT_0 + 2) {
			ERROR_LOG(Log::Audio, "Microphone wait failed: %08lx", GetLastError());
			break;
		}

		UINT32 packetFrames = 0;
		while (SUCCEEDED(hr = capture->GetNextPacketSize(&packetFrames)) && packetFrames != 0) {
			BYTE *data = nullptr;
			UINT32 frames = 0;
			DWORD bufferFlags = 0;
			hr = capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr);
			if (FAILED(hr)) {
				break;
			}
			g_micRing.Push((bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) ? nullptr : (const int16_t *)data, frames);
			capture->ReleaseBuffer(frames);
		}
		if (FAILED(hr)) {
			// Most likely AUDCLNT_E_DEVICE_INVALIDATED, the device was unplugged or disabled.
			WARN_LOG(Log::Audio, "Lost microphone '%s' (HRESULT: %08lx)", name.c_str(), hr);
			break;
		}
	}

	client->Stop();
	return captureResult;
}

static void CaptureThread(int sampleRate) {
	SetCurrentThreadName("Microphone");
	const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	HANDLE audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	ComPtr<IMMDeviceEnumerator> enumerator;
	CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
	if (audioEvent && enumerator) {
		bool logErrors = true;
		while (true) {
			const CaptureResult result = RunCapture(enumerator.Get(), sampleRate, audioEvent, logErrors);
			if (result == CaptureResult::STOPPED) {
				break;
			}
			logErrors = true;
			if (result == CaptureResult::FAILED) {
				// Check again for a device every so often, until we're stopped. No need to log each time.
				const HANDLE events[2] = { g_stopEvent, g_reopenEvent };
				const DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, 1000);
				if (waitResult == WAIT_OBJECT_0) {
					break;
				}
				logErrors = waitResult == WAIT_OBJECT_0 + 1;
			}
		}
	} else {
		ERROR_LOG(Log::Audio, "Could not set up the microphone");
	}

	enumerator.Reset();
	if (audioEvent) {
		CloseHandle(audioEvent);
	}
	if (SUCCEEDED(coHr)) {
		CoUninitialize();
	}
}

void WASAPIMicrophoneStart(int sampleRate) {
	WASAPIMicrophoneStop();

	if (!g_stopEvent) {
		g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		g_reopenEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}
	ResetEvent(g_stopEvent);
	ResetEvent(g_reopenEvent);

	// Whatever is left over is from before, maybe at another rate. We're the consumer, so we can drop it.
	g_micRing.Clear();

	g_micThread = std::thread(&CaptureThread, sampleRate);
}

void WASAPIMicrophoneStop() {
	if (!g_micThread.joinable()) {
		return;
	}
	SetEvent(g_stopEvent);
	g_micThread.join();
	INFO_LOG(Log::Audio, "Microphone stopped");
}

void WASAPIMicrophonePoll() {
	g_micRing.Drain([](const int16_t *samples, uint32_t count) {
		Microphone::addAudioData((u8 *)samples, count * sizeof(int16_t));
	});
}

void WASAPIMicrophoneDeviceChanged() {
	if (g_micThread.joinable()) {
		SetEvent(g_reopenEvent);
	}
}

std::vector<std::string> WASAPIMicrophoneGetDeviceList() {
	std::vector<std::string> list;
	// Can be called from any thread.
	const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	ComPtr<IMMDeviceEnumerator> enumerator;
	ComPtr<IMMDeviceCollection> collection;
	UINT count = 0;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
		SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection)) && collection) {
		collection->GetCount(&count);
	}
	for (UINT i = 0; i < count; i++) {
		ComPtr<IMMDevice> device;
		if (SUCCEEDED(collection->Item(i, &device)) && device) {
			std::string name = GetFriendlyName(device.Get());
			if (!name.empty()) {
				list.push_back(name);
			}
		}
	}
	collection.Reset();
	enumerator.Reset();
	if (SUCCEEDED(coHr)) {
		CoUninitialize();
	}
	return list;
}
