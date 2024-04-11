#include "SimpleAudioDec.h"

#include "ext/at3_standalone/at3_decoders.h"
#include "ext/at3_standalone/avcodec.h"

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
	Atrac3Audio(PSPAudioType audioType, int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize) : audioType_(audioType) {
		if (audioType == PSP_CODEC_AT3PLUS) {
			at3pCtx_ = atrac3p_alloc(blockAlign, channels);
			codecOpen_ = true;
		} else {
			ctx_ = avcodec_alloc_context3(&ff_atrac3_decoder);
		}
		if (audioType_ == PSP_CODEC_AT3) {
			_dbg_assert_(ctx_);
			_dbg_assert_(!codecOpen_);
			ctx_->extradata = (uint8_t *)av_mallocz(extraDataSize);
			ctx_->extradata_size = (int)extraDataSize;
			ctx_->block_align = (int)blockAlign;
			codecOpen_ = false;
			if (extraData != nullptr) {
				memcpy(ctx_->extradata, extraData, extraDataSize);
			}
		}
		for (int i = 0; i < 2; i++) {
			buffers_[i] = new float[4096];
		}
	}
	~Atrac3Audio() {
		if (ctx_) {
			avcodec_close(ctx_);
			av_freep(&ctx_->extradata);
			av_freep(&ctx_);
		}
		if (at3pCtx_) {
			atrac3p_free(at3pCtx_);
		}
		for (int i = 0; i < 2; i++) {
			delete[] buffers_[i];
		}
	}

	bool Decode(const uint8_t *inbuf, int inbytes, uint8_t *outbuf, int *outbytes) override {
		if (!codecOpen_) {
			int retval;
			if (audioType_ == PSP_CODEC_AT3PLUS) {
				_dbg_assert_(false);
			} else {
				ctx_->block_align = inbytes;
				ctx_->channels = 2;
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
		int nb_samples = 0;
		if (audioType_ == PSP_CODEC_AT3PLUS) {
			result = atrac3p_decode_frame(at3pCtx_, buffers_, &nb_samples, &got_frame, inbuf, inbytes);
		} else {
			if (inbytes != ctx_->block_align) {
				WARN_LOG(ME, "Atrac3Audio::Decode: Unexpected block align %d (expected %d)", inbytes, ctx_->block_align);
			}
			result = atrac3_decode_frame(ctx_, buffers_, &nb_samples, &got_frame, inbuf, inbytes);
		}
		if (result < 0) {
			*outbytes = 0;
			return false;
		}
		srcPos_ = result;
		outSamples_ = nb_samples;
		if (nb_samples > 0) {
			*outbytes = nb_samples * 2 * 2;

			// Convert frame to outbuf.
			for (int channel = 0; channel < 2; channel++) {
				int16_t *output = (int16_t *)outbuf;
				for (int i = 0; i < nb_samples; i++) {
					output[i * 2] = clamp16(buffers_[0][i]);
					output[i * 2 + 1] = clamp16(buffers_[1][i]);
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

	PSPAudioType GetAudioType() const override { return audioType_; }

private:
	ATRAC3PContext *at3pCtx_ = nullptr;
	AVCodecContext* ctx_ = nullptr;

	int outSamples_ = 0;
	int srcPos_ = 0;
	float *buffers_[2]{};

	bool codecOpen_ = false;

	PSPAudioType audioType_;
};

AudioDecoder *CreateAtrac3Audio(int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize) {
	return new Atrac3Audio(PSP_CODEC_AT3, channels, blockAlign, extraData, extraDataSize);
}
AudioDecoder *CreateAtrac3PlusAudio(int channels, size_t blockAlign) {
	return new Atrac3Audio(PSP_CODEC_AT3PLUS, channels, blockAlign, nullptr, 0);
}
