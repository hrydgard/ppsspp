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

#include <climits>
#include <cstring>

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

// When the PSP's decoder hands a picture back.
//
// Measured in pspautotests video/mp4/avchold and video/mpeg/decodeend, on streams that differ only
// in their headers. The decoder follows the output process of the H.264 standard (Annex C):
//
// - A stream with HRD parameters and picture timing SEI gets each picture output dpb_output_delay
//   after its access unit is removed from the coded picture buffer - one decode call per access
//   unit. Sony's encoder writes these: its 30fps movies, without B-frames, come out a call late.
// - Any other stream is "bumped" out of a picture buffer three frames deep, whatever the stream
//   says it needs: max_num_ref_frames, max_dec_frame_buffering, max_num_reorder_frames, size and
//   level make no difference. So a stream without B-frames comes out three calls late.
//
// sceMpegAvcDecodeStop then hands back what's left. Streams with more than three reference frames
// are refused (0x80628002).
//
// ffmpeg only provides the pixels, in display order: each access unit gets its index as the
// packet's timestamp, so every frame can be matched to its unit, and the picture timing comes from
// ffmpeg's H.264 parser. The one thing this doesn't follow is bumping with B-frames and no picture
// timing: there the PSP's buffer also holds reference pictures that were already output, so a
// call can complete two or three pictures at once (only the first lands in the frame buffer).
// Here those come out one a call. Sony's movies have picture timing, so that's other encoders'.

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
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 48, 101)
	// DecodePictures keeps frames past the next decode, which the old API only allows for
	// reference counted ones. The new one always hands those out.
	codecCtx_->refcounted_frames = 1;
#endif
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
		return;
	}

	parserCtx_ = avcodec_alloc_context3(nullptr);
	parser_ = av_parser_init(AV_CODEC_ID_H264);
	if (parser_) {
		// Each packet is a whole access unit, so there's no need to wait for the next to find
		// where this one ends.
		parser_->flags |= PARSER_FLAG_COMPLETE_FRAMES;
	} else {
		WARN_LOG(Log::ME, "AvcDecoder: no H.264 parser in this ffmpeg build, movies may come out early");
	}
#endif
}

AvcDecoder::~AvcDecoder() {
	ClearPictures();
#ifdef USE_FFMPEG
	for (Decoded &d : decoded_) {
		av_frame_free(&d.frame);
	}
	if (parser_) {
		av_parser_close(parser_);
	}
	if (parserCtx_) {
		avcodec_free_context(&parserCtx_);
	}
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

void AvcDecoder::ClearPictures() {
	pictures_.clear();
#ifdef USE_FFMPEG
	for (AVFrame *&frame : outFrames_) {
		av_frame_free(&frame);
	}
#endif
	outFrames_.clear();
}

void AvcDecoder::Flush() {
	haveFrame_ = false;
	ClearPictures();
#ifdef USE_FFMPEG
	if (codecCtx_) {
		avcodec_flush_buffers(codecCtx_);
	}
	if (frame_) {
		av_frame_unref(frame_);
	}
	for (Decoded &d : decoded_) {
		av_frame_free(&d.frame);
	}
	// The parser keeps the parameter sets and timing of the stream it saw, and has no reset.
	if (parser_) {
		av_parser_close(parser_);
		parser_ = av_parser_init(AV_CODEC_ID_H264);
		if (parser_) {
			parser_->flags |= PARSER_FLAG_COMPLETE_FRAMES;
		}
	}
#endif
	decoded_.clear();
	outputTicks_.clear();
	timings_.clear();
	lastTiming_ = AvcTiming();
	bufferingTick_ = 0;
	lastRemovalTick_ = 0;
	removalStep_ = 2;
	firstTiming_ = true;
	nextIndex_ = 0;
	released_ = 0;
}

bool AvcDecoder::AcceptFrame(AVFrame *frame) {
#ifdef USE_FFMPEG
	// Check for decode errors. Concealing means covering up missing data, and to us, that is a bad error
	// as we're normally streaming from disk.
	if (frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
		concealedFrames_++;
		if (!wasConcealing_) {
			WARN_LOG(Log::ME, "AvcDecoder: frame %d decoded from a damaged bitstream (flags %08x%s) - "
				"the stream is missing data, not the decoder", frameCount_, frame->decode_error_flags,
				(frame->flags & AV_FRAME_FLAG_CORRUPT) ? ", corrupt" : "");
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
	if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) {
		WARN_LOG(Log::ME, "AvcDecoder: unexpected pixel format %d, ignoring the frame", frame->format);
		return false;
	}
	return frame->width > 0 && frame->height > 0;
#else
	return false;
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

	if (!AcceptFrame(frame_)) {
		return false;
	}
	width_ = frame_->width;
	height_ = frame_->height;
	haveFrame_ = true;
	return true;
#else
	return false;
#endif
}

void AvcDecoder::FeedFfmpeg(const u8 *data, int size, int index) {
#ifdef USE_FFMPEG
	AVPacket packet;
	av_init_packet(&packet);
	if (data) {
		packetBuf_.resize((size_t)size + AV_INPUT_BUFFER_PADDING_SIZE);
		memcpy(packetBuf_.data(), data, size);
		memset(packetBuf_.data() + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
		packet.data = packetBuf_.data();
		packet.size = size;
		// Only a pts: with no dts to prefer, ffmpeg's best effort timestamp is this, whatever
		// order the frames come back in.
		packet.pts = index;
	} else {
		packet.data = nullptr;
		packet.size = 0;
	}
	packet.dts = AV_NOPTS_VALUE;

	auto queue = [&](AVFrame *frame) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(55, 58, 100)
		const s64 pts = frame->best_effort_timestamp;
#else
		const s64 pts = av_frame_get_best_effort_timestamp(frame);
#endif
		if (pts == AV_NOPTS_VALUE || !AcceptFrame(frame)) {
			av_frame_unref(frame);
			return;
		}
		AVFrame *copy = av_frame_alloc();
		av_frame_move_ref(copy, frame);
		decoded_.push_back({ (int)pts, copy });
	};

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
	int err = avcodec_send_packet(codecCtx_, data ? &packet : nullptr);
	while (avcodec_receive_frame(codecCtx_, frame_) >= 0) {
		queue(frame_);
	}
	if (err == AVERROR(EAGAIN)) {
		// Everything waiting was taken out just above, so there's room now.
		err = avcodec_send_packet(codecCtx_, &packet);
		while (avcodec_receive_frame(codecCtx_, frame_) >= 0) {
			queue(frame_);
		}
	}
	if (err < 0 && err != AVERROR_EOF && data) {
		WARN_LOG(Log::ME, "AvcDecoder: send_packet failed (%d)", err);
	}
#else
	for (int n = 0; n < 32; n++) {
		int gotFrame = 0;
		const int err = avcodec_decode_video2(codecCtx_, frame_, &gotFrame, &packet);
		if (err < 0 && data) {
			WARN_LOG(Log::ME, "AvcDecoder: decode_video2 failed (%d)", err);
		}
		if (gotFrame) {
			queue(frame_);
		}
		// Draining hands back one frame per call; decoding, one at most.
		if (data || !gotFrame) {
			break;
		}
	}
#endif
	if (!data) {
		avcodec_flush_buffers(codecCtx_);
	}
#endif
}

void AvcDecoder::Release() {
#ifdef USE_FFMPEG
	const Decoded d = decoded_.front();
	decoded_.pop_front();
	outputTicks_.erase(d.index);
	released_++;
	auto timing = timings_.find(d.index);
	outFrames_.push_back(d.frame);
	AvcPicture pic;
	for (int i = 0; i < 3; i++) {
		pic.planes[i] = d.frame->data[i];
		pic.strides[i] = d.frame->linesize[i];
	}
	pic.width = d.frame->width;
	pic.height = d.frame->height;
	if (timing != timings_.end()) {
		pic.timing = timing->second;
		timings_.erase(timing);
		// ffmpeg puts the two fields of a pair together into one frame, stamped with the first's
		// access unit. The second is the next unit in decode order, the opposite field.
		auto second = timings_.find(d.index + 1);
		const int ps = pic.timing.picStruct;
		if ((ps == 1 || ps == 2) && second != timings_.end() && second->second.picStruct == 3 - ps) {
			pic.fieldPair = true;
			pic.secondField = second->second;
			timings_.erase(second);
			outputTicks_.erase(d.index + 1);
		}
	}
	pictures_.push_back(pic);
#endif
}

#ifdef USE_FFMPEG
// The parser doesn't report pic_struct itself, only what it decides from it (h264_parser.c).
static int PicStructFromParser(const AVCodecParserContext *parser) {
	if (parser->picture_structure == AV_PICTURE_STRUCTURE_TOP_FIELD) {
		return 1;
	}
	if (parser->picture_structure == AV_PICTURE_STRUCTURE_BOTTOM_FIELD) {
		return 2;
	}
	switch (parser->repeat_pict) {
	case 1: return parser->field_order == AV_FIELD_TT ? 3 : (parser->field_order == AV_FIELD_BB ? 4 : 0);
	case 2: return parser->field_order == AV_FIELD_BB ? 6 : 5;
	case 3: return 7;
	case 5: return 8;
	default: return 0;
	}
}
#endif

bool AvcDecoder::FrameRate(int *num, int *den) const {
#ifdef USE_FFMPEG
	if (codecCtx_ && codecCtx_->framerate.num > 0 && codecCtx_->framerate.den > 0) {
		*num = codecCtx_->framerate.num;
		*den = codecCtx_->framerate.den;
		return true;
	}
#endif
	return false;
}

int AvcDecoder::DecodePictures(const u8 *data, int size) {
	ClearPictures();
#ifdef USE_FFMPEG
	if (!codecCtx_ || !data || size <= 0) {
		return 0;
	}
	const int index = nextIndex_++;

	// The picture timing SEI, in clock ticks from the first buffering period. The parser reports
	// cpb_removal_delay and dpb_output_delay here, and whether this unit starts a buffering period.
	bool timed = false;
	s64 removalTick = 0;
	lastTiming_ = AvcTiming();
	if (parser_) {
		u8 *out = nullptr;
		int outSize = 0;
		av_parser_parse2(parser_, parserCtx_, &out, &outSize, data, size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (parser_->dts_ref_dts_delta != INT_MIN && parser_->pts_dts_delta != INT_MIN) {
			timed = true;
			if (parser_->dts_sync_point > 0) {
				if (firstTiming_) {
					bufferingTick_ = 0;
				} else if (parser_->dts_ref_dts_delta > 0) {
					bufferingTick_ += parser_->dts_ref_dts_delta;
				} else {
					// A new stream fed in without a flush (Burnout Legends' intro movies) starts its
					// clock over. Carry on from the last access unit, or every picture after this
					// one waits until the new clock catches up with the old one.
					bufferingTick_ = lastRemovalTick_ + removalStep_;
				}
				removalTick = bufferingTick_;
			} else {
				removalTick = bufferingTick_ + parser_->dts_ref_dts_delta;
			}
			if (!firstTiming_ && removalTick > lastRemovalTick_) {
				removalStep_ = removalTick - lastRemovalTick_;
			}
			lastRemovalTick_ = removalTick;
			firstTiming_ = false;
			outputTicks_[index] = removalTick + parser_->pts_dts_delta;
			lastTiming_.picTiming = true;
			lastTiming_.bufferingPeriod = parser_->dts_sync_point > 0;
			lastTiming_.cpbRemovalDelay = parser_->dts_ref_dts_delta;
			lastTiming_.dpbOutputDelay = parser_->pts_dts_delta;
		}
		lastTiming_.picStruct = PicStructFromParser(parser_);
		lastTiming_.keyFrame = parser_->key_frame == 1;
		timings_[index] = lastTiming_;
	}

	FeedFfmpeg(data, size, index);
	if (codecCtx_->refs > 3) {
		// Known once ffmpeg has seen the parameter sets.
		for (Decoded &d : decoded_) {
			av_frame_free(&d.frame);
		}
		decoded_.clear();
		return -1;
	}

	if (timed) {
		// Everything whose time has come, in display order - including this picture, if its delay
		// is zero. A picture ffmpeg is still holding comes out late.
		while (!decoded_.empty()) {
			auto it = outputTicks_.find(decoded_.front().index);
			if (it != outputTicks_.end() && it->second > removalTick) {
				break;
			}
			Release();
		}
	} else {
		// The buffer holds three pictures.
		while (!decoded_.empty() && nextIndex_ - released_ > 3) {
			Release();
		}
	}
	return (int)pictures_.size();
#else
	return 0;
#endif
}

int AvcDecoder::StopPictures() {
	ClearPictures();
	if (!codecCtx_) {
		return 0;
	}
	FeedFfmpeg(nullptr, 0, -1);
	while (!decoded_.empty()) {
		Release();
	}
	return (int)pictures_.size();
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
