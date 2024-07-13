#include "pch.h"

#include <XAudio2.h>

#include <algorithm>
#include <cstdint>

#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "XAudioSoundStream.h"

#include <process.h>

const size_t BUFSIZE = 32 * 1024;

class XAudioBackend : public WindowsAudioBackend {
public:
	XAudioBackend();
	~XAudioBackend() override;

	bool Init(HWND window, StreamCallback callback, int sampleRate) override;  // If fails, can safely delete the object
	int GetSampleRate() const override { return sampleRate_; }

private:
	bool RunSound();
	bool CreateBuffer();
	void PollLoop();

	StreamCallback callback_ = nullptr;

	IXAudio2 *xaudioDevice = nullptr;
	IXAudio2MasteringVoice *xaudioMaster = nullptr;
	IXAudio2SourceVoice *xaudioVoice = nullptr;

	int sampleRate_ = 0;

	char realtimeBuffer_[BUFSIZE]{};
	uint32_t cursor_ = 0;

	HANDLE thread_ = 0;
	HANDLE exitEvent_ = 0;

	bool exit = false;
};

// TODO: Get rid of this
static XAudioBackend *g_dsound;

XAudioBackend::XAudioBackend() {
	exitEvent_ = CreateEvent(nullptr, true, true, L"");
}

inline int RoundDown128(int x) {
	return x & (~127);
}

bool XAudioBackend::CreateBuffer() {
	if FAILED(xaudioDevice->CreateMasteringVoice(&xaudioMaster, 2, sampleRate_, 0, 0, NULL))
		return false;

	WAVEFORMATEX waveFormat;
	waveFormat.cbSize = sizeof(waveFormat);
	waveFormat.nAvgBytesPerSec = sampleRate_ * 4;
	waveFormat.nBlockAlign = 4;
	waveFormat.nChannels = 2;
	waveFormat.nSamplesPerSec = sampleRate_;
	waveFormat.wBitsPerSample = 16;
	waveFormat.wFormatTag = WAVE_FORMAT_PCM;

	if FAILED(xaudioDevice->CreateSourceVoice(&xaudioVoice, &waveFormat, 0, 1.0, nullptr, nullptr, nullptr))
		return false;

	xaudioVoice->SetFrequencyRatio(1.0);
	return true;
}

bool XAudioBackend::RunSound() {
	if FAILED(XAudio2Create(&xaudioDevice, 0, XAUDIO2_DEFAULT_PROCESSOR)) {
		xaudioDevice = NULL;
		return false;
	}

	XAUDIO2_DEBUG_CONFIGURATION dbgCfg;
	ZeroMemory(&dbgCfg, sizeof(dbgCfg));
	dbgCfg.TraceMask = XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_DETAIL;
	//dbgCfg.BreakMask = XAUDIO2_LOG_ERRORS;
	xaudioDevice->SetDebugConfiguration(&dbgCfg);

	if (!CreateBuffer()) {
		xaudioDevice->Release();
		xaudioDevice = NULL;
		return false;
	}

	cursor_ = 0;

	if FAILED(xaudioVoice->Start(0, XAUDIO2_COMMIT_NOW)) {
		xaudioDevice->Release();
		xaudioDevice = NULL;
		return false;
	}

	thread_ = (HANDLE)_beginthreadex(0, 0, [](void* param)
	{
		SetCurrentThreadName("XAudio2");
		XAudioBackend *backend = (XAudioBackend *)param;
		backend->PollLoop();
		return 0U;
	}, (void *)this, 0, 0);
	SetThreadPriority(thread_, THREAD_PRIORITY_ABOVE_NORMAL);

	return true;
}

XAudioBackend::~XAudioBackend() {
	if (!xaudioDevice)
		return;

	if (!xaudioVoice)
		return;

	exit = true;
	WaitForSingleObject(exitEvent_, INFINITE);
	CloseHandle(exitEvent_);

	xaudioDevice->Release();
}

bool XAudioBackend::Init(HWND window, StreamCallback _callback, int sampleRate) {
	callback_ = _callback;
	sampleRate_ = sampleRate;
	return RunSound();
}

void XAudioBackend::PollLoop() {
	ResetEvent(exitEvent_);

	while (!exit) {
		XAUDIO2_VOICE_STATE state;
		xaudioVoice->GetState(&state);

		// TODO: Still plenty of tuning to do here.
		// 4 seems to work fine.
		if (state.BuffersQueued > 4) {
			Sleep(1);
			continue;
		}

		uint32_t bytesRequired = (sampleRate_ * 4) / 100;

		uint32_t bytesLeftInBuffer = BUFSIZE - cursor_;
		uint32_t readCount = std::min(bytesRequired, bytesLeftInBuffer);

		// realtimeBuffer_ is just used as a ring of scratch space to be submitted, since SubmitSourceBuffer doesn't
		// take ownership of the data. It needs to be big enough to fit the max number of buffers we check for
		// above, which it is, easily.

		int stereoSamplesRendered = (*callback_)((short*)&realtimeBuffer_[cursor_], readCount / 4, sampleRate_);
		int numBytesRendered = 2 * sizeof(short) * stereoSamplesRendered;

		XAUDIO2_BUFFER xaudioBuffer{};
		xaudioBuffer.pAudioData = (const BYTE*)&realtimeBuffer_[cursor_];
		xaudioBuffer.AudioBytes = numBytesRendered;

		if FAILED(xaudioVoice->SubmitSourceBuffer(&xaudioBuffer, NULL)) {
			WARN_LOG(Log::Audio, "XAudioBackend: Failed writing bytes");
		}
		cursor_ += numBytesRendered;
		if (cursor_ >= BUFSIZE) {
			cursor_ = 0;
			bytesLeftInBuffer = BUFSIZE;
		}
	}

	SetEvent(exitEvent_);
}

WindowsAudioBackend *CreateAudioBackend(AudioBackendType type) {
	// Only one type available on UWP.
	return new XAudioBackend();
}
