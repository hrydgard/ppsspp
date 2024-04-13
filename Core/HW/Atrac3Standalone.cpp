#include "SimpleAudioDec.h"

#include "ext/at3_standalone/at3_decoders.h"

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
		blockAlign_ = (int)blockAlign;
		if (audioType == PSP_CODEC_AT3PLUS) {
			at3pCtx_ = atrac3p_alloc(channels, &blockAlign_);
			if (at3pCtx_)
				codecOpen_ = true;
		} else if (audioType_ == PSP_CODEC_AT3) {
			at3Ctx_ = atrac3_alloc(channels, &blockAlign_, extraData, (int)extraDataSize);
			if (at3Ctx_)
				codecOpen_ = true;
		}
		for (int i = 0; i < 2; i++) {
			buffers_[i] = new float[4096];
		}
	}
	~Atrac3Audio() {
		if (at3Ctx_) {
			atrac3_free(at3Ctx_);
		}
		if (at3pCtx_) {
			atrac3p_free(at3pCtx_);
		}
		for (int i = 0; i < 2; i++) {
			delete[] buffers_[i];
		}
	}

	bool IsOK() const override {
		return codecOpen_;
	}

	void FlushBuffers() override {
		if (at3Ctx_) {
			atrac3_flush_buffers(at3Ctx_);
		}
		if (at3pCtx_) {
			atrac3p_flush_buffers(at3pCtx_);
		}
	}

	bool Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, uint8_t *outbuf, int *outbytes) override {
		if (!codecOpen_) {
			_dbg_assert_(false);
		}
		if (inbytes != blockAlign_ && blockAlign_ != 0) {
			WARN_LOG(ME, "Atrac3Audio::Decode: Unexpected block align %d (expected %d)", inbytes, blockAlign_);
		}
		blockAlign_ = inbytes;
		// We just call the decode function directly without going through the whole packet machinery.
		int result;
		int nb_samples = 0;
		if (audioType_ == PSP_CODEC_AT3PLUS) {
			result = atrac3p_decode_frame(at3pCtx_, buffers_, &nb_samples, inbuf, inbytes);
		} else {
			result = atrac3_decode_frame(at3Ctx_, buffers_, &nb_samples, inbuf, inbytes);
		}
		if (result < 0) {
			*outbytes = 0;
			return false;
		}
		if (inbytesConsumed) {
			*inbytesConsumed = result;
		}
		outSamples_ = nb_samples;
		if (nb_samples > 0) {
			if (outbytes) {
				*outbytes = nb_samples * 2 * 2;
			}
			if (outbuf) {
				// Convert frame to outbuf. TODO: Very SIMDable, though hardly hot.
				for (int channel = 0; channel < 2; channel++) {
					int16_t *output = (int16_t *)outbuf;
					for (int i = 0; i < nb_samples; i++) {
						output[i * 2] = clamp16(buffers_[0][i]);
						output[i * 2 + 1] = clamp16(buffers_[1][i]);
					}
				}
			}
		} else if (outbytes) {
			*outbytes = 0;
		}
		return true;
	}

	int GetOutSamples() const override {
		return outSamples_;
	}

	void SetChannels(int channels) override {
		// Hmm. ignore for now.
	}

	PSPAudioType GetAudioType() const override { return audioType_; }

private:
	ATRAC3PContext *at3pCtx_ = nullptr;
	ATRAC3Context *at3Ctx_ = nullptr;

	int blockAlign_ = 0;

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
