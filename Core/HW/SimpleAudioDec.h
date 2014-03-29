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

#pragma once

#include <cmath>

#include "base/basictypes.h"
#include "Core/HW/MediaEngine.h"

#ifdef USE_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}

#endif  // USE_FFMPEG

// Wraps FFMPEG in a nice interface that's drop-in compatible with
// the old one. Decodes packet by packet - does NOT demux. That's done by
// MpegDemux. Only decodes Atrac3+, not regular Atrac3.

// Based on http://ffmpeg.org/doxygen/trunk/doc_2examples_2decoding_encoding_8c-example.html#_a13

// Ideally, Maxim's Atrac3+ decoder would be available as a standalone library
// that we could link, as that would be totally sufficient for the use case here.
// However, it will be maintained as a part of FFMPEG so that's the way we'll go
// for simplicity and sanity.

struct SimpleAudio {
public:
	SimpleAudio(int);
	SimpleAudio(u32, int);
	~SimpleAudio();

	bool Decode(void* inbuf, int inbytes, uint8_t *outbuf, int *outbytes);
	bool IsOK() const { return codec_ != 0; }

	u32 ctxPtr;
	int audioType;

private:
#ifdef USE_FFMPEG
	AVFrame *frame_;
	AVCodec *codec_;
	AVCodecContext  *codecCtx_;
	SwrContext      *swrCtx_;
	AVCodecID audioCodecId; // AV_CODEC_ID_XXX

	bool GetAudioCodecID(int audioType); // Get audioCodecId from audioType
#endif  // USE_FFMPEG
};


enum {
	PSP_CODEC_AT3PLUS = 0x00001000,
	PSP_CODEC_AT3 = 0x00001001,
	PSP_CODEC_MP3 = 0x00001002,
	PSP_CODEC_AAC = 0x00001003,
};

static const char *const codecNames[4] = {
	"AT3+", "AT3", "MP3", "AAC",
};

bool AudioDecode(SimpleAudio *ctx, void* inbuf, int inbytes, int *outbytes, uint8_t* outbuf);
void AudioClose(SimpleAudio **ctx);
static const char *GetCodecName(int codec) {
	if (codec >= PSP_CODEC_AT3PLUS && codec <= PSP_CODEC_AAC) {
		return codecNames[codec - PSP_CODEC_AT3PLUS];
	}
	else {
		return "(unk)";
	}
};
bool isValidCodec(int codec);
