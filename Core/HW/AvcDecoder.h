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

#pragma once

#include <deque>
#include <map>
#include <vector>

#include "Common/CommonTypes.h"

struct AVCodecContext;
struct AVCodecParserContext;
struct AVFrame;

// An access unit's timing SEI, as far as ffmpeg's parser reports it.
struct AvcTiming {
	bool picTiming = false;        // a picture timing SEI came with it
	bool bufferingPeriod = false;  // and a buffering period SEI
	bool keyFrame = false;         // a picture decoding can start from
	int cpbRemovalDelay = 0;
	int dpbOutputDelay = 0;
	// H.264 pic_struct (0 a frame, 1/2 a field, 3-8 a frame shown as that many fields).
	int picStruct = 0;
};

// One decoded picture: three 8-bit 4:2:0 planes (Y, Cb, Cr). Strides are in bytes and are not
// necessarily the width.
struct AvcPicture {
	const u8 *planes[3]{};
	int strides[3]{};
	int width = 0;
	int height = 0;
	AvcTiming timing;
	// For a picture coded as two fields (each its own access unit), the second one's timing.
	// timing is then the first field's.
	bool fieldPair = false;
	AvcTiming secondField;
};

// An H.264 decoder for sceVideocodec, which is a low level interface to the PSP Media Engine.
// PPSSPP's MediaEngine can't serve that - it is built around sceMpeg's own model, with the
// PSMF demuxer, the ringbuffer and its own frame pacing wrapped around the decoder, so this
// is a second path in addition to that. Implementing the old path around this might happen later.
//
// There are two ways in. Decode is "hand it one access unit, get a frame back when ffmpeg has
// one". DecodePictures hands pictures back when the PSP's decoder would - see AvcDecoder.cpp.
class AvcDecoder {
public:
	AvcDecoder();
	~AvcDecoder();

	// Not copyable.
	AvcDecoder(const AvcDecoder &) = delete;
	AvcDecoder &operator=(const AvcDecoder &) = delete;

	// Decodes one access unit. Returns true when a frame came out - H.264 reorders, so a frame
	// is not produced for every unit fed in, and that is not an error.
	bool Decode(const u8 *data, int size);

	// Decodes one access unit and returns how many pictures the PSP's decoder hands back for it,
	// which Pictures() then holds - none, one, or several. -1 for a stream the PSP's decoder
	// refuses.
	int DecodePictures(const u8 *data, int size);
	// The end of the stream: hands back everything the decoder still holds, as stopping the PSP's
	// decoder does.
	int StopPictures();
	// The pictures of the last DecodePictures or StopPictures call, in output order. Valid until the
	// next call.
	const std::vector<AvcPicture> &Pictures() const { return pictures_; }

	// Throws away any decoder state, for a seek or a new stream.
	void Flush();

	// The timing of the last access unit DecodePictures was given.
	const AvcTiming &LastTiming() const { return lastTiming_; }
	// The frame rate from the stream's parameter sets, false until they've been seen.
	bool FrameRate(int *num, int *den) const;

	bool HasFrame() const { return haveFrame_; }
	int Width() const { return width_; }
	int Height() const { return height_; }

	// Only meaningful while HasFrame(). Planes are Y, Cb, Cr; strides are in bytes and are not
	// necessarily the width.
	const u8 *Plane(int index) const;
	int Stride(int index) const;

	// True if we were built without ffmpeg, in which case nothing decodes.
	static bool IsAvailable();

private:
	// Feeds one packet to ffmpeg (null to drain at the end), and queues every frame that comes out
	// under its access unit's index.
	void FeedFfmpeg(const u8 *data, int size, int index);
	// Checks a frame ffmpeg returned, and says whether it can be used.
	bool AcceptFrame(AVFrame *frame);
	// Hands back the frame at the front of the queue.
	void Release();
	void ClearPictures();

	AVCodecContext *codecCtx_ = nullptr;
	AVFrame *frame_ = nullptr;
	// The access unit, copied so we can pad it - ffmpeg reads a little past the end of a packet.
	// Kept between calls so the usual case doesn't allocate.
	std::vector<u8> packetBuf_;
	bool haveFrame_ = false;
	// For reporting runs of concealed-error frames rather than one line each - see Decode.
	bool wasConcealing_ = false;
	int concealedFrames_ = 0;
	int frameCount_ = 0;
	int width_ = 0;
	int height_ = 0;

	// DecodePictures. The parser reads the picture timing SEI; it gets a context of its own so it
	// can't change the decoder's.
	AVCodecParserContext *parser_ = nullptr;
	AVCodecContext *parserCtx_ = nullptr;
	struct Decoded {
		int index;
		AVFrame *frame;
	};
	// What ffmpeg has decoded and not been handed back yet, in output order.
	std::deque<Decoded> decoded_;
	// When each access unit's picture is due, in clock ticks, for streams with picture timing.
	std::map<int, s64> outputTicks_;
	// Each access unit's timing, while its picture hasn't come out yet.
	std::map<int, AvcTiming> timings_;
	AvcTiming lastTiming_;
	s64 bufferingTick_ = 0;
	// The last access unit's removal tick, and how far apart the last two were (2 ticks a frame).
	s64 lastRemovalTick_ = 0;
	s64 removalStep_ = 2;
	bool firstTiming_ = true;
	int nextIndex_ = 0;
	int released_ = 0;
	std::vector<AVFrame *> outFrames_;
	std::vector<AvcPicture> pictures_;
};
