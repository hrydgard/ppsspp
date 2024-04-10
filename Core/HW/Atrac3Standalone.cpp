#pragma once

#include "SimpleAudioDec.h"

#include "ext/at3_standalone/at3_decoders.h"
extern "C" {
#include "ext/at3_standalone/avcodec.h"
}

// Uses our standalone AT3/AT3+ decoder derived from FFMPEG
class Atrac3Audio : public AudioDecoder {
public:
	Atrac3Audio(PSPAudioType audioType) {
		ctx_ = avcodec_alloc_context3(&ff_atrac3p_decoder);
		atrac3p_decode_init(ctx_);
		frame_ = av_frame_alloc();
	}
	~Atrac3Audio() {
		atrac3p_decode_close(ctx_);
		av_frame_free(&frame_);
	}

	bool Decode(const uint8_t *inbuf, int inbytes, uint8_t *outbuf, int *outbytes) override {
		int got_frame = 0;
		int samples = atrac3p_decode_frame(ctx_, frame_, &got_frame, inbuf, inbytes);
		return true;
	}

	bool IsOK() const override { return true; }
	int GetOutSamples() const override {
		return outSamples_;
	}
	int GetSourcePos() const override {
		return srcPos_;
	}

	void SetChannels(int channels) override {
		// Hmm. ignore for now.
	}

	PSPAudioType GetAudioType() const override { return audioType_; }

private:
	AVCodecContext* ctx_ = nullptr;
	AVFrame *frame_ = nullptr;

	int outSamples_ = 0;
	int srcPos_ = 0;

	PSPAudioType audioType_;
};

AudioDecoder *CreateAtrac3Audio(PSPAudioType audioType) {
	return new Atrac3Audio(audioType);
}
