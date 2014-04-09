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

#include "Core/HW/SimpleAudioDec.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"

#ifdef USE_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}

#endif  // USE_FFMPEG

bool SimpleAudio::GetAudioCodecID(int audioType){
#ifdef USE_FFMPEG
	
	switch (audioType)
	{
	case PSP_CODEC_AAC:
		audioCodecId = AV_CODEC_ID_AAC;
		break;
	case PSP_CODEC_AT3:
		audioCodecId = AV_CODEC_ID_ATRAC3;
		break;
	case PSP_CODEC_AT3PLUS:
		audioCodecId = AV_CODEC_ID_ATRAC3P;
		break;
	case PSP_CODEC_MP3:
		audioCodecId = AV_CODEC_ID_MP3;
		break;
	default:
		audioType = 0;
		break;
	}
	if (audioType != 0){
		return true;
	}
	return false;
#else
	return false;
#endif // USE_FFMPEG
}

SimpleAudio::SimpleAudio(int audioType)
: codec_(0), codecCtx_(0), swrCtx_(0), audioType(audioType){
#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
	InitFFmpeg();
	
	frame_ = av_frame_alloc();

	// Get Audio Codec ID
	if (!GetAudioCodecID(audioType)){
		ERROR_LOG(ME, "This version of FFMPEG does not support Audio codec type: %08x. Update your submodule.", audioType);
		return;
	}
	// Find decoder
	codec_ = avcodec_find_decoder(audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ID for audio (%s). Update your submodule.", GetCodecName(audioType));
		return;
	}
	// Allocate codec context
	codecCtx_ = avcodec_alloc_context3(codec_);
	if (!codecCtx_) {
		ERROR_LOG(ME, "Failed to allocate a codec context");
		return;
	}
	codecCtx_->channels = 2;
	codecCtx_->channel_layout = AV_CH_LAYOUT_STEREO;
	codecCtx_->sample_rate = 44100;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		return;
	}

	av_dict_free(&opts);
#endif  // USE_FFMPEG
}


SimpleAudio::SimpleAudio(u32 ctxPtr, int audioType)
: codec_(0), codecCtx_(0), swrCtx_(0), ctxPtr(ctxPtr), audioType(audioType){
#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
	InitFFmpeg();

	frame_ = av_frame_alloc();

	// Get Audio Codec ID
	if (!GetAudioCodecID(audioType)){
		ERROR_LOG(ME, "This version of FFMPEG does not support Audio codec type: %08x. Update your submodule.", audioType);
		return;
	}
	// Find decoder
	codec_ = avcodec_find_decoder(audioCodecId);
	if (!codec_) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ID for audio (%s). Update your submodule.",GetCodecName(audioType));
		return;
	}
	// Allocate codec context
	codecCtx_ = avcodec_alloc_context3(codec_);
	if (!codecCtx_) {
		ERROR_LOG(ME, "Failed to allocate a codec context");
		return;
	}
	codecCtx_->channels = 2;
	codecCtx_->channel_layout = AV_CH_LAYOUT_STEREO;
	codecCtx_->sample_rate = 44100;
	// Open codec
	AVDictionary *opts = 0;
	if (avcodec_open2(codecCtx_, codec_, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		return;
	}

	av_dict_free(&opts);
#endif  // USE_FFMPEG
}

SimpleAudio::~SimpleAudio() {
#ifdef USE_FFMPEG
	if (frame_)
		av_frame_free(&frame_);
	if (codecCtx_)
		avcodec_close(codecCtx_);
	codecCtx_ = 0;
	codec_ = 0;
	if (swrCtx_)
		swr_free(&swrCtx_);
#endif  // USE_FFMPEG
}

void SaveAudio(const char filename[], uint8_t *outbuf, int size){
	FILE * pf;
	pf = fopen(filename, "ab+");

	fwrite(outbuf, size, 1, pf);
	fclose(pf);
}

bool SimpleAudio::Decode(void* inbuf, int inbytes, uint8_t *outbuf, int *outbytes) {
#ifdef USE_FFMPEG
	AVPacket packet;
	av_init_packet(&packet);
	packet.data = static_cast<uint8_t *>(inbuf);
	packet.size = inbytes;

	int got_frame = 0;
	av_frame_unref(frame_);
	
	int len = avcodec_decode_audio4(codecCtx_, frame_, &got_frame, &packet);
	if (len < 0) {
		ERROR_LOG(ME, "Error decoding Audio frame");
		// TODO: cleanup
		return false;
	}
	if (got_frame) {
		// Initializing the sample rate convert. We will use it to convert float output into int.
		int64_t wanted_channel_layout = AV_CH_LAYOUT_STEREO; // we want stereo output layout
		int64_t dec_channel_layout = frame_->channel_layout; // decoded channel layout

		swrCtx_ = swr_alloc_set_opts(
			swrCtx_,
			wanted_channel_layout,
			AV_SAMPLE_FMT_S16,
			44100,
			dec_channel_layout,
			codecCtx_->sample_fmt,
			codecCtx_->sample_rate,
			0,
			NULL);

		if (!swrCtx_ || swr_init(swrCtx_) < 0) {
			ERROR_LOG(ME, "swr_init: Failed to initialize the resampling context");
			avcodec_close(codecCtx_);
			codec_ = 0;
			return false;
		}
		// convert audio to AV_SAMPLE_FMT_S16
		int swrRet = swr_convert(swrCtx_, &outbuf, frame_->nb_samples, (const u8 **)frame_->extended_data, frame_->nb_samples);
		if (swrRet < 0) {
			ERROR_LOG(ME, "swr_convert: Error while converting %d", swrRet);
			return false;
		}
		swr_free(&swrCtx_);
		// output samples per frame, we should *2 since we have two channels
		int outSamples = swrRet * 2;

		// each sample occupies 2 bytes
		*outbytes = outSamples * 2;
		// We always convert to stereo.
		__AdjustBGMVolume((s16 *)outbuf, frame_->nb_samples * 2);

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

void AudioClose(SimpleAudio **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}

bool isValidCodec(int codec){
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return true;
	}
	return false;
}

