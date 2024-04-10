#pragma once

#include "SimpleAudioDec.h"

#include "ext/at3_standalone/at3_decoders.h"
extern "C" {
#include "ext/at3_standalone/avcodec.h"
}

inline int16_t clamp16(float f) {
	if (f >= 1.0f)
		return 32767;
	else if (f <= -1.0f)
		return -32767;
	else
		return (int)(f * 32767);
}

// Uses our standalone AT3/AT3+ decoder derived from FFMPEG
// Test case for ATRAC3: Mega Man Maverick Hunter X, PSP menu sound
class Atrac3Audio : public AudioDecoder {
public:
	Atrac3Audio(PSPAudioType audioType) : audioType_(audioType) {
		if (audioType == PSP_CODEC_AT3PLUS) {
			ctx_ = avcodec_alloc_context3(&ff_atrac3p_decoder);
		} else {
			ctx_ = avcodec_alloc_context3(&ff_atrac3_decoder);
		}
		frame_ = av_frame_alloc();
	}
	~Atrac3Audio() {
		avcodec_close(ctx_);
		av_frame_free(&frame_);
		av_freep(&ctx_->extradata);
		av_freep(&ctx_->subtitle_header);
		av_freep(&ctx_);
	}

	bool Decode(const uint8_t *inbuf, int inbytes, uint8_t *outbuf, int *outbytes) override {
		if (!codecOpen_) {
			ctx_->block_align = inbytes;
			ctx_->channels = 2;
			ctx_->channel_layout = ctx_->channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
			ctx_->sample_rate = 44100;
			int retval;
			if (audioType_ == PSP_CODEC_AT3PLUS) {
				retval = avcodec_open2(ctx_, &ff_atrac3p_decoder, nullptr);
			} else {
				retval = avcodec_open2(ctx_, &ff_atrac3_decoder, nullptr);
			}
			_dbg_assert_(retval >= 0);
			if (retval < 0) {
				return false;
			}
			codecOpen_ = true;
		}

		// We just call the decode function directly without going through the whole packet machinery.
		int got_frame = 0;
		int result;
		if (audioType_ == PSP_CODEC_AT3PLUS) {
			result = atrac3p_decode_frame(ctx_, frame_, &got_frame, inbuf, inbytes);
		} else {
			result = atrac3_decode_frame(ctx_, frame_, &got_frame, inbuf, inbytes);
		}
		if (result < 0) {
			*outbytes = 0;
			return false;
		}
		srcPos_ = result;
		outSamples_ = frame_->nb_samples;
		if (frame_->nb_samples > 0) {
			*outbytes = frame_->nb_samples * 2 * 2;

			// Convert frame to outbuf.
			for (int channel = 0; channel < 2; channel++) {
				float **pointers = (float **)frame_->data;
				int16_t *output = (int16_t *)outbuf;
				for (int i = 0; i < frame_->nb_samples; i++) {
					output[i * 2] = clamp16(pointers[0][i]);
					output[i * 2 + 1] = clamp16(pointers[1][i]);
				}
			}
		} else {
			*outbytes = 0;
		}
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

	void SetExtraData(const uint8_t *data, int size, int wav_bytes_per_packet) {
		// if (audioType_ == PSP_CODEC_AT3PLUS) {
			_dbg_assert_(ctx_);
			_dbg_assert_(!codecOpen_);
			ctx_->extradata = (uint8_t *)av_mallocz(size);
			ctx_->extradata_size = size;
			ctx_->block_align = wav_bytes_per_packet;
			codecOpen_ = false;
			if (data != nullptr) {
				memcpy(ctx_->extradata, data, size);
			}
		//}
	}

	PSPAudioType GetAudioType() const override { return audioType_; }

private:
	AVCodecContext* ctx_ = nullptr;
	AVFrame *frame_ = nullptr;

	int outSamples_ = 0;
	int srcPos_ = 0;

	bool codecOpen_ = false;

	PSPAudioType audioType_;
};

AudioDecoder *CreateAtrac3Audio(PSPAudioType audioType) {
	return new Atrac3Audio(audioType);
}
