// Copyright (c) 2013- PPSSPP Project.

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

#include <algorithm>
#include <cmath>

#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"
#include "Core/HW/Atrac3Standalone.h"

#include "ext/minimp3/minimp3.h"

#ifdef USE_FFMPEG

extern "C" {
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/samplefmt.h"
#include "libavcodec/avcodec.h"
#include "libavutil/version.h"

#include "Core/FFMPEGCompat.h"
}

#else

extern "C" {
	struct AVCodec;
	struct AVCodecContext;
	struct SwrContext;
	struct AVFrame;
}

#endif  // USE_FFMPEG

// AAC decoder candidates:
// * https://github.com/mstorsjo/fdk-aac/tree/master

// h.264 decoder candidates:
// * https://github.com/meerkat-cv/h264_decoder
// * https://github.com/shengbinmeng/ffmpeg-h264-dec

// minimp3-based decoder.
class MiniMp3Audio : public AudioDecoder {
public:
	MiniMp3Audio() {
		mp3dec_init(&mp3_);
	}
	~MiniMp3Audio() {}

	bool Decode(const uint8_t* inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) override {
		_dbg_assert_(outputChannels == 2);

		mp3dec_frame_info_t info{};
		int samplesWritten = mp3dec_decode_frame(&mp3_, inbuf, inbytes, (mp3d_sample_t *)outbuf, &info);
		*inbytesConsumed = info.frame_bytes;
		*outSamples = samplesWritten;
		return true;
	}

	bool IsOK() const override { return true; }
	void SetChannels(int channels) override {
		// Hmm. ignore for now.
	}

	PSPAudioType GetAudioType() const override { return PSP_CODEC_MP3; }

private:
	// We use the lowest-level API.
	mp3dec_t mp3_{};
};

// FFMPEG-based decoder. TODO: Replace with individual codecs.
// Based on http://ffmpeg.org/doxygen/trunk/doc_2examples_2decoding_encoding_8c-example.html#_a13
class FFmpegAudioDecoder : public AudioDecoder {
public:
	FFmpegAudioDecoder(PSPAudioType audioType, int sampleRateHz = 44100, int channels = 2);
	~FFmpegAudioDecoder();

	bool Decode(const uint8_t* inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) override;
	bool IsOK() const override {
#ifdef USE_FFMPEG
		return codec_ != 0;
#else
		return 0;
#endif
	}

	void SetChannels(int channels) override;

	// These two are only here because of save states.
	PSPAudioType GetAudioType() const override { return audioType; }

private:
	bool OpenCodec(int block_align);

	PSPAudioType audioType;
	int sample_rate_;
	int channels_;

	AVFrame *frame_ = nullptr;
	AVCodec *codec_ = nullptr;
	AVCodecContext  *codecCtx_ = nullptr;
	SwrContext      *swrCtx_ = nullptr;

	bool codecOpen_ = false;
};

AudioDecoder *CreateAudioDecoder(PSPAudioType audioType, int sampleRateHz, int channels, size_t blockAlign, const uint8_t *extraData, size_t extraDataSize) {
	switch (audioType) {
	case PSP_CODEC_MP3:
		return new MiniMp3Audio();
	case PSP_CODEC_AT3:
		return CreateAtrac3Audio(channels, blockAlign, extraData, extraDataSize);
	case PSP_CODEC_AT3PLUS:
		return CreateAtrac3PlusAudio(channels, blockAlign);
	default:
		// Only AAC falls back to FFMPEG now.
		return new FFmpegAudioDecoder(audioType, sampleRateHz, channels);
	}
}

static int GetAudioCodecID(int audioType) {
#ifdef USE_FFMPEG
	switch (audioType) {
	case PSP_CODEC_AAC:
		return AV_CODEC_ID_AAC;
	case PSP_CODEC_AT3:
		return AV_CODEC_ID_ATRAC3;
	case PSP_CODEC_AT3PLUS:
		return AV_CODEC_ID_ATRAC3P;
	case PSP_CODEC_MP3:
		return AV_CODEC_ID_MP3;
	default:
		return AV_CODEC_ID_NONE;
	}
#else
	return 0;
#endif // USE_FFMPEG
}

FFmpegAudioDecoder::FFmpegAudioDecoder(PSPAudioType audioType, int sampleRateHz, int channels)
	: audioType(audioType), sample_rate_(sampleRateHz), channels_(channels) {

#ifdef USE_FFMPEG
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 18, 100)
	avcodec_register_all();
#endif
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 12, 100)
	av_register_all();
#endif
	InitFFmpeg();

	frame_ = av_frame_alloc();

	// Get AUDIO Codec ctx
	int audioCodecId = GetAudioCodecID(audioType);
	if (!audioCodecId) {
		ERROR_LOG(Log::ME, "This version of FFMPEG does not support Audio codec type: %08x. Update your submodule.", audioType);
		return;
	}
	// Find decoder
	codec_ = avcodec_find_decoder((AVCodecID)audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(Log::ME, "This version of FFMPEG does not support AV_CODEC_ctx for audio (%s). Update your submodule.", GetCodecName(audioType));
		return;
	}
	// Allocate codec context
	codecCtx_ = avcodec_alloc_context3(codec_);
	if (!codecCtx_) {
		ERROR_LOG(Log::ME, "Failed to allocate a codec context");
		return;
	}
	codecCtx_->channels = channels_;
	codecCtx_->channel_layout = channels_ == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
	codecCtx_->sample_rate = sample_rate_;
	codecOpen_ = false;
#endif  // USE_FFMPEG
}

bool FFmpegAudioDecoder::OpenCodec(int block_align) {
#ifdef USE_FFMPEG
	// Some versions of FFmpeg require this set.  May be set in SetExtraData(), but optional.
	// When decoding, we decode by packet, so we know the size.
	if (codecCtx_->block_align == 0) {
		codecCtx_->block_align = block_align;
	}

	AVDictionary *opts = 0;
	int retval = avcodec_open2(codecCtx_, codec_, &opts);
	if (retval < 0) {
		ERROR_LOG(Log::ME, "Failed to open codec: retval = %i", retval);
	}
	av_dict_free(&opts);
	codecOpen_ = true;
	return retval >= 0;
#else
	return false;
#endif  // USE_FFMPEG
}

void FFmpegAudioDecoder::SetChannels(int channels) {
	if (channels_ == channels) {
		// Do nothing, already set.
		return;
	}
#ifdef USE_FFMPEG

	if (codecOpen_) {
		ERROR_LOG(Log::ME, "Codec already open, cannot change channels");
	} else {
		channels_ = channels;
		codecCtx_->channels = channels_;
		codecCtx_->channel_layout = channels_ == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
	}
#endif
}

FFmpegAudioDecoder::~FFmpegAudioDecoder() {
#ifdef USE_FFMPEG
	swr_free(&swrCtx_);
	av_frame_free(&frame_);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 52, 0)
	avcodec_free_context(&codecCtx_);
#else
	// Future versions may add other things to free, but avcodec_free_context didn't exist yet here.
	avcodec_close(codecCtx_);
	av_freep(&codecCtx_->extradata);
	av_freep(&codecCtx_->subtitle_header);
	av_freep(&codecCtx_);
#endif
	codec_ = 0;
#endif  // USE_FFMPEG
}

// Decodes a single input frame.
bool FFmpegAudioDecoder::Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) {
#ifdef USE_FFMPEG
	if (!codecOpen_) {
		OpenCodec(inbytes);
	}

	AVPacket packet;
	av_init_packet(&packet);
	packet.data = (uint8_t *)(inbuf);
	packet.size = inbytes;

	int got_frame = 0;
	av_frame_unref(frame_);

	if (outSamples) {
		*outSamples = 0;
	}
	if (inbytesConsumed) {
		*inbytesConsumed = 0;
	}
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
	if (inbytes != 0) {
		int err = avcodec_send_packet(codecCtx_, &packet);
		if (err < 0) {
			ERROR_LOG(Log::ME, "Error sending audio frame to decoder (%d bytes): %d (%08x)", inbytes, err, err);
			return false;
		}
	}
	int err = avcodec_receive_frame(codecCtx_, frame_);
	int len = 0;
	if (err >= 0) {
		len = frame_->pkt_size;
		got_frame = 1;
	} else if (err != AVERROR(EAGAIN)) {
		len = err;
	}
#else
	int len = avcodec_decode_audio4(codecCtx_, frame_, &got_frame, &packet);
#endif
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
	av_packet_unref(&packet);
#else
	av_free_packet(&packet);
#endif

	if (len < 0) {
		ERROR_LOG(Log::ME, "Error decoding Audio frame (%i bytes): %i (%08x)", inbytes, len, len);
		return false;
	}
	
	// get bytes consumed in source
	*inbytesConsumed = len;

	if (got_frame) {
		// Initializing the sample rate convert. We will use it to convert float output into int.
		_dbg_assert_(outputChannels == 2);
		int64_t wanted_channel_layout = AV_CH_LAYOUT_STEREO; // we want stereo output layout
		int64_t dec_channel_layout = frame_->channel_layout; // decoded channel layout

		if (!swrCtx_) {
			swrCtx_ = swr_alloc_set_opts(
				swrCtx_,
				wanted_channel_layout,
				AV_SAMPLE_FMT_S16,
				codecCtx_->sample_rate,
				dec_channel_layout,
				codecCtx_->sample_fmt,
				codecCtx_->sample_rate,
				0,
				NULL);

			if (!swrCtx_ || swr_init(swrCtx_) < 0) {
				ERROR_LOG(Log::ME, "swr_init: Failed to initialize the resampling context");
				avcodec_close(codecCtx_);
				codec_ = 0;
				return false;
			}
		}

		// convert audio to AV_SAMPLE_FMT_S16
		int swrRet = 0;
		if (outbuf != nullptr) {
			swrRet = swr_convert(swrCtx_, (uint8_t **)&outbuf, frame_->nb_samples, (const u8 **)frame_->extended_data, frame_->nb_samples);
		}
		if (swrRet < 0) {
			ERROR_LOG(Log::ME, "swr_convert: Error while converting: %d", swrRet);
			return false;
		}
		// output stereo samples per frame
		*outSamples = swrRet;

		// Save outbuf into pcm audio, you can uncomment this line to save and check the decoded audio into pcm file.
		// SaveAudio("dump.pcm", outbuf, *outbytes);
	}
	return true;
#else
	// Zero bytes output. No need to memset.
	*outbytes = 0;
	return true;
#endif  // USE_FFMPEG
}

void AudioClose(AudioDecoder **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}

void AudioClose(FFmpegAudioDecoder **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}

static const char *const codecNames[4] = {
	"AT3+", "AT3", "MP3", "AAC",
};

const char *GetCodecName(int codec) {
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return codecNames[codec - PSP_CODEC_AT3PLUS];
	} else {
		return "(unk)";
	}
};

bool IsValidCodec(PSPAudioType codec){
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return true;
	}
	return false;
}


// sceAu module starts from here

AuCtx::AuCtx() {
}

AuCtx::~AuCtx() {
	if (decoder) {
		AudioClose(&decoder);
		decoder = nullptr;
	}
}

size_t AuCtx::FindNextMp3Sync() {
	for (size_t i = 0; i < sourcebuff.size() - 2; ++i) {
		if ((sourcebuff[i] & 0xFF) == 0xFF && (sourcebuff[i + 1] & 0xC0) == 0xC0) {
			return i;
		}
	}
	return 0;
}

// return output pcm size, <0 error
u32 AuCtx::AuDecode(u32 pcmAddr) {
	u32 outptr = PCMBuf + nextOutputHalf * PCMBufSize / 2;
	auto outbuf = Memory::GetPointerWriteRange(outptr, PCMBufSize / 2);
	int outpcmbufsize = 0;

	if (pcmAddr)
		Memory::Write_U32(outptr, pcmAddr);

	// Decode a single frame in sourcebuff and output into PCMBuf.
	if (!sourcebuff.empty()) {
		// FFmpeg doesn't seem to search for a sync for us, so let's do that.
		int nextSync = 0;
		if (decoder->GetAudioType() == PSP_CODEC_MP3) {
			nextSync = (int)FindNextMp3Sync();
		}
		int inbytesConsumed = 0;
		int outSamples = 0;
		decoder->Decode(&sourcebuff[nextSync], (int)sourcebuff.size() - nextSync, &inbytesConsumed, 2, (int16_t *)outbuf, &outSamples);
		outpcmbufsize = outSamples * 2 * sizeof(int16_t);

		if (outpcmbufsize == 0) {
			// Nothing was output, hopefully we're at the end of the stream.
			AuBufAvailable = 0;
			sourcebuff.clear();
		} else {
			// Update our total decoded samples, but don't count stereo.
			SumDecodedSamples += outSamples;
			// get consumed source length
			int srcPos = inbytesConsumed + nextSync;
			// remove the consumed source
			if (srcPos > 0)
				sourcebuff.erase(sourcebuff.begin(), sourcebuff.begin() + srcPos);
			// reduce the available Aubuff size
			// (the available buff size is now used to know if we can read again from file and how many to read)
			AuBufAvailable -= srcPos;
		}
	}

	bool end = readPos - AuBufAvailable >= (int64_t)endPos;
	if (end && LoopNum != 0) {
		// When looping, start the sum back off at zero and reset readPos to the start.
		SumDecodedSamples = 0;
		readPos = startPos;
		if (LoopNum > 0)
			LoopNum--;
	}

	if (outpcmbufsize == 0 && !end) {
		// If we didn't decode anything, we fill this half of the buffer with zeros.
		outpcmbufsize = PCMBufSize / 2;
		if (outbuf != nullptr)
			memset(outbuf, 0, outpcmbufsize);
	} else if ((u32)outpcmbufsize < PCMBufSize) {
		// TODO: Not sure it actually zeros this out.
		if (outbuf != nullptr)
			memset(outbuf + outpcmbufsize, 0, PCMBufSize / 2 - outpcmbufsize);
	}

	if (outpcmbufsize != 0)
		NotifyMemInfo(MemBlockFlags::WRITE, outptr, outpcmbufsize, "AuDecode");

	nextOutputHalf ^= 1;
	return outpcmbufsize;
}

// return 1 to read more data stream, 0 don't read
int AuCtx::AuCheckStreamDataNeeded() {
	// If we would ask for bytes, then some are needed.
	if (AuStreamBytesNeeded() > 0) {
		return 1;
	}
	return 0;
}

int AuCtx::AuStreamBytesNeeded() {
	if (decoder->GetAudioType() == PSP_CODEC_MP3) {
		// The endPos and readPos are not considered, except when you've read to the end.
		if (readPos >= endPos)
			return 0;
		// Account for the workarea.
		int offset = AuStreamWorkareaSize();
		return (int)AuBufSize - AuBufAvailable - offset;
	}

	// TODO: Untested.  Maybe similar to MP3.
	return std::min((int)AuBufSize - AuBufAvailable, (int)endPos - readPos);
}

int AuCtx::AuStreamWorkareaSize() {
	// Note that this is 31 bytes more than the max layer 3 frame size.
	if (decoder->GetAudioType() == PSP_CODEC_MP3)
		return 0x05c0;
	return 0;
}

// check how many bytes we have read from source file
u32 AuCtx::AuNotifyAddStreamData(int size) {
	int offset = AuStreamWorkareaSize();

	if (askedReadSize != 0) {
		// Old save state, numbers already adjusted.
		int diffsize = size - askedReadSize;
		// Notify the real read size
		if (diffsize != 0) {
			readPos += diffsize;
			AuBufAvailable += diffsize;
		}
		askedReadSize = 0;
	} else {
		readPos += size;
		AuBufAvailable += size;
	}

	if (Memory::IsValidRange(AuBuf, size)) {
		sourcebuff.resize(sourcebuff.size() + size);
		Memory::MemcpyUnchecked(&sourcebuff[sourcebuff.size() - size], AuBuf + offset, size);
	}

	return 0;
}

// read from stream position srcPos of size bytes into buff
// buff, size and srcPos are all pointers
u32 AuCtx::AuGetInfoToAddStreamData(u32 bufPtr, u32 sizePtr, u32 srcPosPtr) {
	int readsize = AuStreamBytesNeeded();
	int offset = AuStreamWorkareaSize();

	// we can recharge AuBuf from its beginning
	if (readsize != 0) {
		if (Memory::IsValidAddress(bufPtr))
			Memory::WriteUnchecked_U32(AuBuf + offset, bufPtr);
		if (Memory::IsValidAddress(sizePtr))
			Memory::WriteUnchecked_U32(readsize, sizePtr);
		if (Memory::IsValidAddress(srcPosPtr))
			Memory::WriteUnchecked_U32(readPos, srcPosPtr);
	} else {
		if (Memory::IsValidAddress(bufPtr))
			Memory::WriteUnchecked_U32(0, bufPtr);
		if (Memory::IsValidAddress(sizePtr))
			Memory::WriteUnchecked_U32(0, sizePtr);
		if (Memory::IsValidAddress(srcPosPtr))
			Memory::WriteUnchecked_U32(0, srcPosPtr);
	}

	// Just for old save states.
	askedReadSize = 0;
	return 0;
}

u32 AuCtx::AuResetPlayPositionByFrame(int frame) {
	// Note: this doesn't correctly handle padding or slot size, but the PSP doesn't either.
	uint32_t bytesPerSecond = (MaxOutputSample / 8) * BitRate * 1000;
	readPos = startPos + (frame * bytesPerSecond) / SamplingRate;
	// Not sure why, but it seems to consistently seek 1 before, maybe in case it's off slightly.
	if (frame != 0)
		readPos -= 1;
	SumDecodedSamples = frame * MaxOutputSample;
	AuBufAvailable = 0;
	sourcebuff.clear();
	return 0;
}

u32 AuCtx::AuResetPlayPosition() {
	readPos = startPos;
	SumDecodedSamples = 0;
	AuBufAvailable = 0;
	sourcebuff.clear();
	return 0;
}

void AuCtx::DoState(PointerWrap &p) {
	auto s = p.Section("AuContext", 0, 2);
	if (!s)
		return;

	Do(p, startPos);
	Do(p, endPos);
	Do(p, AuBuf);
	Do(p, AuBufSize);
	Do(p, PCMBuf);
	Do(p, PCMBufSize);
	Do(p, freq);
	Do(p, SumDecodedSamples);
	Do(p, LoopNum);
	Do(p, Channels);
	Do(p, MaxOutputSample);
	Do(p, readPos);
	int audioType = decoder ? (int)decoder->GetAudioType() : 0;
	Do(p, audioType);
	Do(p, BitRate);
	Do(p, SamplingRate);
	Do(p, askedReadSize);
	int dummy = 0;
	Do(p, dummy);
	Do(p, FrameNum);

	if (s < 2) {
		AuBufAvailable = 0;
		Version = 3;
	} else {
		Do(p, Version);
		Do(p, AuBufAvailable);
		Do(p, sourcebuff);
		Do(p, nextOutputHalf);
	}

	if (p.mode == p.MODE_READ) {
		decoder = CreateAudioDecoder((PSPAudioType)audioType);
	}
}
