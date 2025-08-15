#include "pch.h"

#include <XAudio2.h>

#include <algorithm>
#include <cstdint>
#include <thread>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Audio/AudioBackend.h"
#include "XAudioSoundStream.h"

const size_t BUFSIZE = 32 * 1024;

class XAudioBackend : public AudioBackend {
public:
	XAudioBackend();
	~XAudioBackend() override;

	void EnumerateDevices(std::vector<AudioDeviceDesc> *outputDevices, bool captureDevices = false) {
		// Do nothing! Auto is the only option.
	}
	void SetRenderCallback(RenderCallback callback, void *userdata) override {
		callback_ = callback;
		userdata_ = userdata;
	}

	bool InitOutputDevice(std::string_view uniqueId, LatencyMode latencyMode, bool *reverted) override;
	int SampleRate() const override { return sampleRate_; }
	int PeriodFrames() const override { return samplesPerBuffer; }
	int BufferSize() const override { return samplesPerBuffer * bufferCount; }

private:
	void Start();
	void Stop();

	RenderCallback callback_ = nullptr;

	IXAudio2 *xaudioDevice = nullptr;
	IXAudio2MasteringVoice *masterVoice_ = nullptr;
	IXAudio2SourceVoice *sourceVoice_ = nullptr;

	void *userdata_ = nullptr;

	WAVEFORMATEX format_;

	int sampleRate_ = 44100;
	int periodFrames_ = 0;

	enum {
		samplesPerBuffer = 480, // 10 ms @ 48kHz. maybe we can tweak this using latency mode.
		bufferCount = 3,
		channels = 2,
	};
	float audioBuffer_[bufferCount][samplesPerBuffer * channels];
	int curBuffer_ = 0;

	uint32_t cursor_ = 0;

	std::thread thread_;
	std::atomic<bool> running_{};
};

// TODO: Get rid of this
static XAudioBackend *g_dsound;

XAudioBackend::XAudioBackend() {
	format_.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	format_.nChannels = channels;
	format_.nSamplesPerSec = 48000;
	format_.wBitsPerSample = 32;
	format_.nBlockAlign = format_.nChannels * format_.wBitsPerSample / 8;
	format_.nAvgBytesPerSec = format_.nSamplesPerSec * format_.nBlockAlign;
	format_.cbSize = 0;
}

XAudioBackend::~XAudioBackend() {
	Stop();
}

void XAudioBackend::Stop() {
	running_ = false;
	if (thread_.joinable()) {
		thread_.join();
	}

	if (xaudioDevice) {
		xaudioDevice->Release();
		xaudioDevice = nullptr;
		sourceVoice_ = nullptr;
	}
}

bool XAudioBackend::InitOutputDevice(std::string_view uniqueId, LatencyMode latencyMode, bool *reverted) {
	Stop();

	*reverted = false;
	if FAILED(XAudio2Create(&xaudioDevice, 0, XAUDIO2_DEFAULT_PROCESSOR)) {
		xaudioDevice = nullptr;
		return false;
	}

	XAUDIO2_DEBUG_CONFIGURATION dbgCfg;
	ZeroMemory(&dbgCfg, sizeof(dbgCfg));
	dbgCfg.TraceMask = XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_DETAIL;
	//dbgCfg.BreakMask = XAUDIO2_LOG_ERRORS;
	xaudioDevice->SetDebugConfiguration(&dbgCfg);

	if FAILED(xaudioDevice->CreateMasteringVoice(&masterVoice_, 2, sampleRate_, 0, 0, nullptr)) {
		xaudioDevice->Release();
		xaudioDevice = nullptr;
		return false;
	}

	if FAILED(xaudioDevice->CreateSourceVoice(&sourceVoice_, &format_, 0, 1.0, nullptr, nullptr, nullptr)) {
		xaudioDevice->Release();
		xaudioDevice = nullptr;
		return false;
	}

	sourceVoice_->SetFrequencyRatio(1.0);

	cursor_ = 0;

	if FAILED(sourceVoice_->Start(0, XAUDIO2_COMMIT_NOW)) {
		xaudioDevice->Release();
		xaudioDevice = nullptr;
		return false;
	}

	running_ = true;
	thread_ = std::thread([this]() {
		while (running_) {
			XAUDIO2_VOICE_STATE state = {};
			sourceVoice_->GetState(&state);
			if (state.BuffersQueued < bufferCount) {
				// Fill buffer with audio
				callback_(audioBuffer_[curBuffer_], samplesPerBuffer, format_.nSamplesPerSec, userdata_);

				XAUDIO2_BUFFER buf = {};
				buf.AudioBytes = samplesPerBuffer * 2 * sizeof(float);
				buf.pAudioData = reinterpret_cast<BYTE*>(audioBuffer_[curBuffer_]);
				buf.Flags = 0;
				sourceVoice_->SubmitSourceBuffer(&buf);
				curBuffer_ += 1;
				if (curBuffer_ >= bufferCount) {
					curBuffer_ = 0;
				}
			} else {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	});
	return true;
}

AudioBackend *System_CreateAudioBackend() {
	// Only one type available on UWP.
	return new XAudioBackend();
}
