// Copyright (c) 2026- PPSSPP Project.

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

#include "ppsspp_config.h"

#include "Common/Log.h"
#include "Core/HW/AvcDecoder.h"
#include "Core/HW/MediaEngine.h"

#ifdef USE_FFMPEG
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
}
#include "Core/FFMPEGCompat.h"
#endif  // USE_FFMPEG

AvcDecoder::AvcDecoder() {
#ifdef USE_FFMPEG
	AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec) {
		ERROR_LOG(Log::ME, "AvcDecoder: no H.264 decoder in this ffmpeg build");
		return;
	}
	codecCtx_ = avcodec_alloc_context3(codec);
	if (!codecCtx_) {
		ERROR_LOG(Log::ME, "AvcDecoder: couldn't allocate a codec context");
		return;
	}

	// Route ffmpeg's own diagnostics into our log - it's the only way we can detect damaged bitstreams.
	InitFFmpeg();

	// The PSP hands us whole access units, so no parser is needed and no truncation is expected.
	codecCtx_->flags = 0;
	codecCtx_->flags2 = 0;
	if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
		ERROR_LOG(Log::ME, "AvcDecoder: couldn't open the H.264 decoder");
		avcodec_free_context(&codecCtx_);
		codecCtx_ = nullptr;
		return;
	}
	frame_ = av_frame_alloc();
	if (!frame_) {
		avcodec_free_context(&codecCtx_);
		codecCtx_ = nullptr;
	}
#endif
}

AvcDecoder::~AvcDecoder() {
#ifdef USE_FFMPEG
	if (frame_) {
		av_frame_free(&frame_);
	}
	if (codecCtx_) {
		avcodec_free_context(&codecCtx_);
	}
#endif
}

bool AvcDecoder::IsAvailable() {
#ifdef USE_FFMPEG
	return true;
#else
	return false;
#endif
}

void AvcDecoder::Flush() {
	haveFrame_ = false;
#ifdef USE_FFMPEG
	if (codecCtx_) {
		avcodec_flush_buffers(codecCtx_);
	}
	if (frame_) {
		av_frame_unref(frame_);
	}
#endif
}

bool AvcDecoder::Decode(const u8 *data, int size) {
	haveFrame_ = false;
#ifdef USE_FFMPEG
	if (!codecCtx_ || !frame_ || !data || size <= 0) {
		return false;
	}

	// ffmpeg reads a little past the end of a packet, so it gets a padded copy rather than the
	// caller's buffer. A plain stack AVPacket, like the rest of the emulator uses - the heap
	// packet API isn't in the ffmpeg headers every target builds against.
	packetBuf_.resize((size_t)size + AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(packetBuf_.data(), data, size);
	memset(packetBuf_.data() + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

	AVPacket packet;
	av_init_packet(&packet);
	packet.data = packetBuf_.data();
	packet.size = size;

	av_frame_unref(frame_);

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
	int err = avcodec_send_packet(codecCtx_, &packet);
	if (err == AVERROR(EAGAIN)) {
		// The decoder has frames waiting and won't take more input until one is taken out. It did
		// not consume the packet, so drain a frame and hand it the same packet again rather than
		// dropping the access unit.
		err = avcodec_receive_frame(codecCtx_, frame_);
		if (err < 0) {
			WARN_LOG(Log::ME, "AvcDecoder: send_packet wanted a frame out but none came (%d)", err);
			return false;
		}
		const int resend = avcodec_send_packet(codecCtx_, &packet);
		if (resend < 0) {
			WARN_LOG(Log::ME, "AvcDecoder: send_packet failed on retry (%d)", resend);
			return false;
		}
		// A frame is already in hand from the drain above - fall through to the format check.
	} else {
		if (err < 0) {
			WARN_LOG(Log::ME, "AvcDecoder: send_packet failed (%d)", err);
			return false;
		}

		err = avcodec_receive_frame(codecCtx_, frame_);
		if (err == AVERROR(EAGAIN)) {
			// Normal: H.264 reorders, so the decoder wants more input before it gives a frame back.
			return false;
		}
		if (err < 0) {
			WARN_LOG(Log::ME, "AvcDecoder: receive_frame failed (%d)", err);
			return false;
		}
	}
#else
	int gotFrame = 0;
	int err = avcodec_decode_video2(codecCtx_, frame_, &gotFrame, &packet);
	if (err < 0) {
		WARN_LOG(Log::ME, "AvcDecoder: decode_video2 failed (%d)", err);
		return false;
	}
	if (!gotFrame) {
		// Normal: H.264 reorders, so the decoder wants more input before it gives a frame back.
		return false;
	}
#endif

	// Check for decode errors. Concealing means covering up missing data, and to us, that is a bad error
	// as we're normally streaming from disk.
	if (frame_->decode_error_flags || (frame_->flags & AV_FRAME_FLAG_CORRUPT)) {
		concealedFrames_++;
		if (!wasConcealing_) {
			WARN_LOG(Log::ME, "AvcDecoder: frame %d decoded from a damaged bitstream (flags %08x%s) - "
				"the stream is missing data, not the decoder", frameCount_, frame_->decode_error_flags,
				(frame_->flags & AV_FRAME_FLAG_CORRUPT) ? ", corrupt" : "");
			wasConcealing_ = true;
		}
	} else if (wasConcealing_) {
		WARN_LOG(Log::ME, "AvcDecoder: clean again at frame %d, %d frames were concealed",
			frameCount_, concealedFrames_);
		wasConcealing_ = false;
	}
	frameCount_++;

	// Everything downstream indexes the three planes as 8-bit 4:2:0, which is all the PSP's
	// encoder produced. Anything else would be read as if it were, so say so and drop the frame
	// rather than converting garbage.
	if (frame_->format != AV_PIX_FMT_YUV420P && frame_->format != AV_PIX_FMT_YUVJ420P) {
		WARN_LOG(Log::ME, "AvcDecoder: unexpected pixel format %d, ignoring the frame", frame_->format);
		return false;
	}
	width_ = frame_->width;
	height_ = frame_->height;
	haveFrame_ = width_ > 0 && height_ > 0;
	return haveFrame_;
#else
	return false;
#endif
}

const u8 *AvcDecoder::Plane(int index) const {
#ifdef USE_FFMPEG
	if (!haveFrame_ || !frame_ || index < 0 || index >= 3) {
		return nullptr;
	}
	return frame_->data[index];
#else
	return nullptr;
#endif
}

int AvcDecoder::Stride(int index) const {
#ifdef USE_FFMPEG
	if (!haveFrame_ || !frame_ || index < 0 || index >= 3) {
		return 0;
	}
	return frame_->linesize[index];
#else
	return 0;
#endif
}
