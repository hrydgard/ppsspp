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

struct rh264_video;

// A plain "hand it one access unit, get a frame back" H.264 decoder.
//
// This exists for sceVideocodec, which is a low level interface to the PSP Media Engine.
// PPSSPP's MediaEngine can't serve that - it is built around sceMpeg's own model, with the
// PSMF demuxer, the ringbuffer and its own frame pacing wrapped around the decoder, so this
// is a second path in addition to that. Implementing the old path around this might happen later.
//
// Output is 8-bit YUV 4:2:0 planes, cropped to the picture shown; the caller converts.
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

	// At the end of a stream: the next of the frames held back for reordering, if any.
	bool Drain();

	// Throws away any decoder state, for a seek or a new stream.
	void Flush();

	bool HasFrame() const { return haveFrame_; }

	// What the last Decode or Drain did: a frame out (1), nothing yet (0), the unit refused (-1),
	// or a frame out that could not be used (-2).
	int LastResult() const { return lastResult_; }
	int Width() const { return width_; }
	int Height() const { return height_; }

	// Only meaningful while HasFrame(). Planes are Y, Cb, Cr; strides are in bytes and are not
	// necessarily the width.
	const u8 *Plane(int index) const;
	int Stride(int index) const;

	// Whether anything decodes at all.
	static bool IsAvailable();

private:
	// The frame the last Decode or Drain gave, from the decoder.
	bool TakeFrame();

	rh264_video *dec_ = nullptr;
	const u8 *planes_[3]{};
	int strides_[3]{};
	bool haveFrame_ = false;
	int lastResult_ = 0;
	// For reporting runs of refused access units rather than one line each - see Decode.
	bool wasFailing_ = false;
	int failedUnits_ = 0;
	int frameCount_ = 0;
	int width_ = 0;
	int height_ = 0;
};
