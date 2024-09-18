#include "SimpleAudioDec.h"
#include "Common/LogReporting.h"
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
	Atrac3Audio(PSPAudioType audioType, int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize)
		: audioType_(audioType), channels_(channels) {
		blockAlign_ = (int)blockAlign;
		if (audioType == PSP_CODEC_AT3PLUS) {
			at3pCtx_ = atrac3p_alloc(channels, &blockAlign_);
			if (at3pCtx_) {
				codecOpen_ = true;
			} else {
				ERROR_LOG(Log::ME, "Failed to open atrac3+ context! (channels=%d blockAlign=%d ed=%d)", channels, (int)blockAlign, (int)extraDataSize);
			}
		} else if (audioType_ == PSP_CODEC_AT3) {
			at3Ctx_ = atrac3_alloc(channels, &blockAlign_, extraData, (int)extraDataSize);
			if (at3Ctx_) {
				codecOpen_ = true;
			} else {
				ERROR_LOG(Log::ME, "Failed to open atrac3 context! !channels=%d blockAlign=%d ed=%d)", channels, (int)blockAlign, (int)extraDataSize);
			}
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

	bool Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) override {
		if (!codecOpen_) {
			WARN_LOG_N_TIMES(codecNotOpen, 5, Log::ME, "Atrac3Audio:Decode: Codec not open, not decoding");
			if (outSamples)
				*outSamples = 0;
			if (inbytesConsumed)
				*inbytesConsumed = 0;
			return false;
		}
		if (inbytes != blockAlign_ && blockAlign_ != 0) {
			WARN_LOG(Log::ME, "Atrac3Audio::Decode: Unexpected block align %d (expected %d). %s", inbytes, blockAlign_, at3pCtx_ ? "Atrac3+" : "Atrac3");
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
			if (outSamples) {
				*outSamples = 0;
			}
			return false;
		}
		if (inbytesConsumed) {
			*inbytesConsumed = result;
		}
		if (outSamples) {
			// Allow capping the output samples by setting *outSamples to non-zero.
			if (*outSamples != 0) {
				nb_samples = std::min(*outSamples, nb_samples);
			}
			*outSamples = nb_samples;
		}
		if (nb_samples > 0) {
			if (outSamples) {
				*outSamples = nb_samples;
			}
			if (outbuf) {
				_dbg_assert_(outputChannels == 1 || outputChannels == 2);
				const float *left = buffers_[0];
				if (outputChannels == 2) {
					// Stereo output, standard.
					const float *right = channels_ == 2 ? buffers_[1] : buffers_[0];
					for (int i = 0; i < nb_samples; i++) {
						outbuf[i * 2] = clamp16(left[i]);
						outbuf[i * 2 + 1] = clamp16(right[i]);
					}
				} else {
					// Mono output, just take the left channel.
					for (int i = 0; i < nb_samples; i++) {
						outbuf[i] = clamp16(left[i]);
					}
				}
			}
		}
		return true;
	}

	void SetChannels(int channels) override {
		// Hmm. ignore for now.
	}

	PSPAudioType GetAudioType() const override { return audioType_; }

private:
	ATRAC3PContext *at3pCtx_ = nullptr;
	ATRAC3Context *at3Ctx_ = nullptr;

	int channels_ = 0;
	int blockAlign_ = 0;

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
