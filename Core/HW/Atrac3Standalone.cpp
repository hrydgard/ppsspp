#include <algorithm>
#include <cstring>

#include "SimpleAudioDec.h"
#include "Common/LogOnce.h"

#include <formats/ratrac3.h>
#include <formats/ratrac3p.h>

// ATRAC3 and ATRAC3plus on libretro-common's ratrac3 and ratrac3p.
// Test case for ATRAC3: Mega Man Maverick Hunter X, PSP menu sound
class Atrac3Audio : public AudioDecoder {
public:
	Atrac3Audio(PSPAudioType audioType, int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize)
		: audioType_(audioType), channels_(channels), blockAlign_((int)blockAlign) {
		if (audioType_ == PSP_CODEC_AT3) {
			// The WAVE extra data's coding mode: 1 is joint stereo.
			jointStereo_ = extraData && extraDataSize >= 8 && (extraData[6] | (extraData[7] << 8)) == 1;
			at3_ = ratrac3_new(channels_, blockAlign_, jointStereo_);
			if (at3_) {
				codecOpen_ = true;
			} else {
				ERROR_LOG(Log::ME, "Failed to open atrac3 context! channels=%d blockAlign=%d joint=%d", channels, (int)blockAlign, (int)jointStereo_);
				codecFailed_ = true;
			}
		}
	}
	~Atrac3Audio() override {
		if (at3_)
			ratrac3_free(at3_);
		if (at3p_)
			ratrac3p_free(at3p_);
	}

	bool IsOK() const override {
		return codecOpen_;
	}

	void FlushBuffers() override {
		if (at3_)
			ratrac3_reset(at3_);
		if (at3p_)
			ratrac3p_reset(at3p_);
	}

	bool Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) override {
		// A non-zero *outSamples caps the samples written.
		const int cap = outSamples ? *outSamples : 0;
		if (outSamples)
			*outSamples = 0;
		if (inbytesConsumed)
			*inbytesConsumed = 0;
		if (!inbuf || inbytes <= 0)
			return false;

		if (audioType_ == PSP_CODEC_AT3PLUS && (!at3p_ || inbytes != blockAlign_)) {
			// Opened at the first decode, so the SetChannels call from MediaEngine::getAudioSamples
			// can take effect; again if the frames change size, which the decoder is made for.
			if (codecFailed_ && inbytes == blockAlign_)
				return false;
			if (at3p_) {
				WARN_LOG(Log::ME, "Atrac3Audio::Decode: Atrac3+ frame of %d bytes, expected %d", inbytes, blockAlign_);
				ratrac3p_free(at3p_);
			}
			blockAlign_ = inbytes;
			at3p_ = ratrac3p_new(channels_, blockAlign_);
			codecOpen_ = at3p_ != nullptr;
			codecFailed_ = !codecOpen_;
			if (!at3p_) {
				ERROR_LOG(Log::ME, "Failed to open atrac3+ context! (channels=%d blockAlign=%d)", channels_, blockAlign_);
				return false;
			}
		}
		if (!codecOpen_) {
			WARN_LOG_N_TIMES(codecNotOpen, 5, Log::ME, "Atrac3Audio:Decode: Codec not open, not decoding");
			return false;
		}
		if (audioType_ == PSP_CODEC_AT3 && inbytes < blockAlign_) {
			WARN_LOG(Log::ME, "Atrac3Audio::Decode: Atrac3 frame of %d bytes, expected %d", inbytes, blockAlign_);
			return false;
		}

		const int n = audioType_ == PSP_CODEC_AT3PLUS
			? ratrac3p_decode_s16(at3p_, inbuf, inbytes, pcm_)
			: ratrac3_decode_s16(at3_, inbuf, blockAlign_, pcm_);
		if (n < 0) {
			// Passed over rather than offered again: a bad frame (seen in some unofficial game
			// mods) would otherwise be decoded over and over.
			if (inbytesConsumed)
				*inbytesConsumed = inbytes;
			return false;
		}
		if (inbytesConsumed)
			*inbytesConsumed = audioType_ == PSP_CODEC_AT3PLUS ? inbytes : blockAlign_;

		const int samples = cap != 0 ? std::min(cap, n) : n;
		if (outSamples)
			*outSamples = samples;
		if (outbuf) {
			_dbg_assert_(outputChannels == 1 || outputChannels == 2);
			const int ch = channels_;
			if (ch == 2 && outputChannels == 2) {
				memcpy(outbuf, pcm_, samples * 2 * sizeof(int16_t));
			} else if (outputChannels == 2) {
				// Mono doubled; more than two channels, the first two.
				const int right = ch > 1 ? 1 : 0;
				for (int i = 0; i < samples; i++) {
					outbuf[i * 2] = pcm_[i * ch];
					outbuf[i * 2 + 1] = pcm_[i * ch + right];
				}
			} else {
				// Mono output, the left channel.
				for (int i = 0; i < samples; i++)
					outbuf[i] = pcm_[i * ch];
			}
		}
		return true;
	}

	void SetChannels(int channels) override {
		_dbg_assert_(audioType_ == PSPAudioType::PSP_CODEC_AT3PLUS);
		if (audioType_ == PSPAudioType::PSP_CODEC_AT3PLUS) {
			if (!at3p_) {
				// Codec shouldn't be open yet.
				_dbg_assert_(!codecOpen_);
				channels_ = 1;
			} else {
				// This will come every packet, just ignore it.
			}
		}
	}

	PSPAudioType GetAudioType() const override { return audioType_; }

private:
	PSPAudioType audioType_;
	int channels_ = 0;
	int blockAlign_ = 0;
	bool jointStereo_ = false;

	ratrac3_t *at3_ = nullptr;
	ratrac3p_t *at3p_ = nullptr;

	bool codecOpen_ = false;
	bool codecFailed_ = false;

	// A frame of either, interleaved, at the most channels either decodes.
	int16_t pcm_[RATRAC3P_SAMPLES * RATRAC3P_MAX_CHANNELS];
};

AudioDecoder *CreateAtrac3Audio(int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize) {
	return new Atrac3Audio(PSP_CODEC_AT3, channels, blockAlign, extraData, extraDataSize);
}
AudioDecoder *CreateAtrac3PlusAudio(int channels, size_t blockAlign) {
	return new Atrac3Audio(PSP_CODEC_AT3PLUS, channels, blockAlign, nullptr, 0);
}
