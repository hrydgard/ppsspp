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

#ifdef USE_FFMPEG

// Urgh! Why is this needed?
#ifdef ANDROID
#ifndef UINT64_C
#define UINT64_C(c) (c ## ULL)
#endif
#endif

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}
#endif // USE_FFMPEG

#include "Core/HW/SimpleAT3Dec.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"

#ifdef USE_FFMPEG

struct SimpleAT3 {
public:
	SimpleAT3();
	~SimpleAT3();

	bool Decode(void* inbuf, int inbytes, uint8_t *outbuf, int *outbytes);

	bool IsOK() const { return codec != 0; }

private:
	AVCodec *codec;
	AVCodecContext  *codecCtx;
	SwrContext      *swrCtx;

	u8 *data_;
	int dataSize_;
};

SimpleAT3::SimpleAT3()
	: codec(0),
		codecCtx(0),
		swrCtx(0),
		data_(0),
		dataSize_(0) {
	codec = avcodec_find_decoder(AV_CODEC_ID_ATRAC3P);
	if (!codec) {
		// Eh, we shouldn't even have managed to compile. But meh.
		ERROR_LOG(ME, "This version of FFMPEG does not support AV_CODEC_ID_ATRAC3P (Atrac3+). Update your submodule.");
		return;
	}

	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx) {
		ERROR_LOG(ME, "Failed to allocate a codec context");
		return;
	}

	codecCtx->channels = 2;
	codecCtx->channel_layout = AV_CH_LAYOUT_STEREO;

	AVDictionary *opts = 0;
	av_dict_set(&opts, "channels", "2", 0);
	av_dict_set(&opts, "sample_rate", "44100", 0);
	if (avcodec_open2(codecCtx, codec, &opts) < 0) {
		ERROR_LOG(ME, "Failed to open codec");
		return;
	}

	av_dict_free(&opts);

	// Initializing the sample rate convert. We only really use it to convert float output
	// into int.
	int wanted_channels = 2;
	int64_t wanted_channel_layout = av_get_default_channel_layout(wanted_channels);
	int64_t dec_channel_layout = av_get_default_channel_layout(2);

	swrCtx = swr_alloc_set_opts(
			swrCtx,
			wanted_channel_layout,
			AV_SAMPLE_FMT_S16,
			codecCtx->sample_rate,
			dec_channel_layout,
			codecCtx->sample_fmt,
			codecCtx->sample_rate,
			0,
			NULL);

	if (!swrCtx || swr_init(swrCtx) < 0) {
		ERROR_LOG(ME, "swr_init: Failed to initialize the resampling context");
		avcodec_close(codecCtx);
		codec = 0;
		return;
	}
}

SimpleAT3::~SimpleAT3() {
	if (codecCtx)
		avcodec_close(codecCtx);
	if (swrCtx)
		swr_free(&swrCtx);

	codecCtx = 0;
	codec = 0;
	if (data_) {
		delete [] data_;
		dataSize_ = 0;
	}
}

// Input is a single Atrac3+ packet.
bool SimpleAT3::Decode(void* inbuf, int inbytes, uint8_t *outbuf, int *outbytes) {
#ifdef USE_FFMPEG
	AVPacket packet = {0};
	av_init_packet(&packet);
	packet.data = static_cast<uint8_t *>(inbuf);
	packet.size = inbytes;

	AVFrame *decoded_frame = 0;

	*outbytes = 0;

	int got_frame = 0;
	if (!decoded_frame) {
		decoded_frame = avcodec_alloc_frame();
	} else {
		avcodec_get_frame_defaults(decoded_frame);
	}

	int len = avcodec_decode_audio4(codecCtx, decoded_frame, &got_frame, &packet);
	if (len < 0) {
		ERROR_LOG(ME, "Error decoding Atrac3+ frame");
		// TODO: cleanup
		return false;
	}

	if (got_frame) {
		int data_size = av_samples_get_buffer_size(
				NULL,
				codecCtx->channels,
				decoded_frame->nb_samples,
				codecCtx->sample_fmt, 1);

		int numSamples = decoded_frame->nb_samples;
		u8 *out = outbuf;
		int swrRet = swr_convert(swrCtx, &out, numSamples,
			(const u8 **)decoded_frame->extended_data, numSamples);

		if (swrRet < 0) {
			ERROR_LOG(ME, "swr_convert: Error while converting %d", swrRet);
			avcodec_free_frame(&decoded_frame);
			return false;
		}

		outbytes += data_size;
		outbuf += data_size;
	}

	if (decoded_frame)
		avcodec_free_frame(&decoded_frame);

	return true;
#else
	// Zero bytes output. No need to memset.
	*outbytes = 0;
	return true;
#endif  // USE_FFMPEG
}

#endif  // USE_FFMPEG

// "C" wrapper

SimpleAT3 *AT3Create() {
#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
	InitFFmpeg();

	SimpleAT3 *at3 = new SimpleAT3();
	if (!at3->IsOK()) {
		delete at3;
		return 0;
	}
	return at3;
#else
	return 0;
#endif  // USE_FFMPEG
}

bool AT3Decode(SimpleAT3 *ctx, void* inbuf, int inbytes, int *outbytes, uint8_t *outbuf) {
#ifdef USE_FFMPEG
	return ctx->Decode(inbuf, inbytes, outbuf, outbytes);
#else
	*outbytes = 0;
	return true;
#endif
}

void AT3Close(SimpleAT3 **ctx) {
#ifdef USE_FFMPEG
	delete *ctx;
	*ctx = 0;
#endif  // USE_FFMPEG
}
