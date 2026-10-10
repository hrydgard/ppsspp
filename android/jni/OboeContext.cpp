// Audio output and microphone input through Oboe, which uses AAudio where it's
// available (Android 8.1+) and falls back to OpenSL ES on older devices.

#include <mutex>

#include <oboe/Oboe.h>

#include "Common/Log.h"
#include "android/jni/OboeContext.h"

// Owns the streams. Held by shared_ptr since after a disconnect, Oboe calls the error
// callback on a thread of its own that can outlive the OboeContext.
class OboeContext::Streams : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback, public std::enable_shared_from_this<OboeContext::Streams> {
public:
	Streams(AndroidAudioCallback cb, int framesPerBuffer, int sampleRate)
		: audioCallback_(cb), framesPerBuffer_(framesPerBuffer), sampleRate_(sampleRate) {}

	bool StartOutput() {
		std::lock_guard<std::mutex> guard(mutex_);
		return OpenOutput();
	}

	bool StartInput(int sampleRate) {
		std::lock_guard<std::mutex> guard(mutex_);
		CloseStream(input_);
		// The PSP mic runs at 44100, 22050 or 11025 Hz. Oboe resamples from whatever the device has.
		inputSampleRate_ = sampleRate > 0 ? sampleRate : 44100;
		return OpenInput();
	}

	void StopInput() {
		std::lock_guard<std::mutex> guard(mutex_);
		CloseStream(input_);
		inputSampleRate_ = 0;
	}

	void Shutdown() {
		std::lock_guard<std::mutex> guard(mutex_);
		shutdown_ = true;
		CloseStream(output_);
		CloseStream(input_);
	}

	oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override {
		if (stream->getDirection() == oboe::Direction::Output) {
			audioCallback_((short *)audioData, numFrames, stream->getSampleRate(), nullptr);
		} else {
			// This is the audio thread, so just buffer it. The CPU thread picks it up.
			AndroidAudio_Recording_Push((const int16_t *)audioData, numFrames);
		}
		return oboe::DataCallbackResult::Continue;
	}

	// Oboe has already closed the stream by now. A disconnect (headphones unplugged, a Bluetooth
	// device going away) means the route changed, so open a new stream on the new default device.
	void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override {
		std::lock_guard<std::mutex> guard(mutex_);
		if (shutdown_) {
			return;
		}
		const bool isOutput = stream == output_.get();
		const bool isInput = stream == input_.get();
		if (!isOutput && !isInput) {
			return;
		}
		WARN_LOG(Log::Audio, "Oboe: %s stream closed with error: %s", isOutput ? "output" : "input", oboe::convertToText(error));
		if (isOutput) {
			output_.reset();
		} else {
			input_.reset();
		}
		if (error != oboe::Result::ErrorDisconnected) {
			OboeContext::SetErrorString(std::string("Stream error: ") + oboe::convertToText(error));
			return;
		}
		if (isOutput) {
			OpenOutput();
		} else if (inputSampleRate_) {
			OpenInput();
		}
	}

private:
	// The ones below are called with mutex_ held.
	bool OpenOutput() {
		oboe::AudioStreamBuilder builder;
		builder.setDirection(oboe::Direction::Output)
			->setPerformanceMode(oboe::PerformanceMode::LowLatency)
			->setSharingMode(oboe::SharingMode::Shared)
			->setUsage(oboe::Usage::Game)
			->setFormat(oboe::AudioFormat::I16)
			->setFormatConversionAllowed(true)
			->setChannelCount(oboe::ChannelCount::Stereo)
			->setChannelConversionAllowed(true)
			->setSampleRate(sampleRate_)
			->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
			->setFramesPerDataCallback(framesPerBuffer_)
			->setDataCallback(this)
			->setErrorCallback(shared_from_this());
		oboe::Result result = builder.openStream(output_);
		if (result != oboe::Result::OK) {
			output_.reset();
			return Fail("Failed to open output stream", result);
		}
		// Double buffering, the same latency as the old OpenSL path.
		output_->setBufferSizeInFrames(output_->getFramesPerBurst() * 2);
		result = output_->requestStart();
		if (result != oboe::Result::OK) {
			CloseStream(output_);
			return Fail("Failed to start output stream", result);
		}
		INFO_LOG(Log::Audio, "Oboe: Output stream open: %s, %d Hz, %d frames per burst, buffer %d frames, %s, %s",
			oboe::convertToText(output_->getAudioApi()),
			output_->getSampleRate(), output_->getFramesPerBurst(), output_->getBufferSizeInFrames(),
			oboe::convertToText(output_->getPerformanceMode()), oboe::convertToText(output_->getSharingMode()));
		return true;
	}

	bool OpenInput() {
		oboe::AudioStreamBuilder builder;
		builder.setDirection(oboe::Direction::Input)
			->setSharingMode(oboe::SharingMode::Shared)
			->setFormat(oboe::AudioFormat::I16)
			->setFormatConversionAllowed(true)
			->setChannelCount(oboe::ChannelCount::Mono)
			->setChannelConversionAllowed(true)
			->setSampleRate(inputSampleRate_)
			->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
			->setDataCallback(this)
			->setErrorCallback(shared_from_this());
		oboe::Result result = builder.openStream(input_);
		if (result != oboe::Result::OK) {
			input_.reset();
			return Fail("Failed to open input stream", result);
		}
		result = input_->requestStart();
		if (result != oboe::Result::OK) {
			CloseStream(input_);
			return Fail("Failed to start input stream", result);
		}
		INFO_LOG(Log::Audio, "Oboe: Input stream open: %d Hz", input_->getSampleRate());
		return true;
	}

	static void CloseStream(std::shared_ptr<oboe::AudioStream> &stream) {
		if (stream) {
			stream->stop();
			stream->close();
			stream.reset();
		}
	}

	static bool Fail(const char *what, oboe::Result result) {
		ERROR_LOG(Log::Audio, "Oboe: %s: %s", what, oboe::convertToText(result));
		OboeContext::SetErrorString(std::string(what) + ": " + oboe::convertToText(result));
		return false;
	}

	AndroidAudioCallback audioCallback_;
	int framesPerBuffer_;
	int sampleRate_;

	std::mutex mutex_;
	bool shutdown_ = false;
	std::shared_ptr<oboe::AudioStream> output_;
	std::shared_ptr<oboe::AudioStream> input_;
	int inputSampleRate_ = 0;
};

OboeContext::OboeContext(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate)
	: AudioContext(cb, _FramesPerBuffer, _SampleRate) {
	// Only used by the OpenSL ES fallback, which can't query the device's native values itself.
	oboe::DefaultStreamValues::SampleRate = sampleRate;
	oboe::DefaultStreamValues::FramesPerBurst = framesPerBuffer;
	streams_ = std::make_shared<Streams>(cb, framesPerBuffer, sampleRate);
}

bool OboeContext::Init() {
	return streams_->StartOutput();
}

bool OboeContext::AudioRecord_Start(int sampleRate) {
	return streams_->StartInput(sampleRate);
}

bool OboeContext::AudioRecord_Stop() {
	streams_->StopInput();
	return true;
}

OboeContext::~OboeContext() {
	streams_->Shutdown();
	INFO_LOG(Log::Audio, "Oboe: Shutdown - finished");
}
