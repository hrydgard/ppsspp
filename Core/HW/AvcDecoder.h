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

#include <vector>

#include "Common/CommonTypes.h"

struct AVCodecContext;
struct AVFrame;

// A plain "hand it one access unit, get a frame back" H.264 decoder.
//
// This exists for sceVideocodec, which is the interface the Media Engine really exposes: the
// caller owns the buffers and hands over one access unit at a time. MediaEngine can't serve that
// - it is built around sceMpeg's own model, with the PSMF demuxer, the ringbuffer and its own
// frame pacing wrapped around the decoder - so this is deliberately a second, separate path
// rather than a refactor of code every game that plays video depends on. Merging the two is
// worth doing once sceVideocodec has proven itself.
//
// Output is whatever pixel format the decoder produced, normally YUV420P; the caller converts.
class AvcDecoder {
public:
	AvcDecoder();
	~AvcDecoder();

	AvcDecoder(const AvcDecoder &) = delete;
	AvcDecoder &operator=(const AvcDecoder &) = delete;

	// Decodes one access unit. Returns true when a frame came out - H.264 reorders, so a frame
	// is not produced for every unit fed in, and that is not an error.
	bool Decode(const u8 *data, int size);

	// Throws away any decoder state, for a seek or a new stream.
	void Flush();

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
	AVCodecContext *codecCtx_ = nullptr;
	AVFrame *frame_ = nullptr;
	// The access unit, copied so we can pad it - ffmpeg reads a little past the end of a packet.
	// Kept between calls so the usual case doesn't allocate.
	std::vector<u8> packetBuf_;
	bool haveFrame_ = false;
	int width_ = 0;
	int height_ = 0;
};
