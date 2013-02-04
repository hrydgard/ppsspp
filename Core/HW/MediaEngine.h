// Copyright (c) 2012- PPSSPP Project.

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



// Simulation of the hardware video/audio decoders.
// The idea is high level emulation where we simply use FFMPEG.
// TODO: Actually hook up to ffmpeg.

#pragma once

// An approximation of what the interface will look like. Similar to JPCSP's.

#include "../../Globals.h"
#include "../HLE/sceMpeg.h"
#include "ChunkFile.h"

class MediaEngine
{
public:
	MediaEngine() : fakeMode_(false), readLength_(0), fakeFrameCounter_(0) {}

	void setFakeMode(bool fake) {
		fakeMode_ = fake;
	}

	void init(u32 bufferAddr, u32 mpegStreamSize, u32 mpegOffset) {
		bufferAddr_ = bufferAddr;
		mpegStreamSize_ = mpegStreamSize;
		mpegOffset_ = mpegOffset;
	}
	void finish();

	void setVideoDim(int w, int h) { videoWidth_ = w; videoHeight_ = h; }
	void feedPacketData(u32 addr, int size);

	bool readVideoAu(SceMpegAu *au) {
		if (fakeMode_) {
			au->pts += videoTimestampStep;
		}
		return true;
	}
	bool readAudioAu(SceMpegAu *au) {
		if (fakeMode_) {

		}
		return true;
	}

	bool stepVideo() {
		if (fakeMode_)
			return true;
		return true;
	}

	void writeVideoImage(u32 bufferPtr, int frameWidth, int videoPixelMode);

	// WTF is this?
	int readLength() { return readLength_; }
	void setReadLength(int len) { readLength_ = len; }

	void DoState(PointerWrap &p) {
		p.Do(fakeMode_);
		p.Do(bufferAddr_);
		p.Do(mpegStreamSize_);
		p.Do(mpegOffset_);
		p.Do(readLength_);
		p.Do(videoWidth_);
		p.Do(videoHeight_);
		p.Do(fakeFrameCounter_);
		p.DoMarker("MediaEngine");
	}

private:
	bool fakeMode_;
	u32 bufferAddr_;
	u32 mpegStreamSize_;
	u32 mpegOffset_;
	int readLength_;
	int videoWidth_;
	int videoHeight_;
	int fakeFrameCounter_;
};
