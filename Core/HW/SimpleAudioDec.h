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
#include "Core/HLE/sceAudio.h"

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
	int getOutSamples();
	int getSourcePos();
	bool ResetCodecCtx(int channels, int samplerate);
	void setResampleFrequency(int freq);

	u32 ctxPtr;
	int audioType;
	int outSamples; // output samples per frame
	int srcPos; // bytes consumed in source during the last decoding
	int wanted_resample_freq; // wanted resampling rate/frequency

#ifdef USE_FFMPEG
	AVFrame *frame_;
	AVCodec *codec_;
	AVCodecContext  *codecCtx_;
	SwrContext      *swrCtx_;
	AVCodecID audioCodecId; // AV_CODEC_ID_XXX

	bool GetAudioCodecID(int audioType); // Get audioCodecId from audioType
#endif  // USE_FFMPEG
};

// audioType
enum {
	PSP_CODEC_AT3PLUS = 0x00001000,
	PSP_CODEC_AT3 = 0x00001001,
	PSP_CODEC_MP3 = 0x00001002,
	PSP_CODEC_AAC = 0x00001003,
};

static const char *const codecNames[4] = {
	"AT3+", "AT3", "MP3", "AAC",
};

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


class AuCtx{
public:
	// Au source informations
	u64 startPos;
	u64 endPos;
	u32 AuBuf;
	u32 AuBufSize;
	u32 PCMBuf;
	u32 PCMBufSize;
	int freq;
	int BitRate;
	int SamplingRate;
	int Channels;
	int Version;

	// audio settings
	u32 SumDecodedSamples;
	int LoopNum;
	u32 MaxOutputSample;
	int FrameNum; // number of decoded frame
	
	// Au decoder
	SimpleAudio *decoder;

	// Au type
	int audioType;

	// buffers informations
	int AuBufAvailable; // the available buffer of AuBuf to be able to recharge data
	int readPos; // read position in audio source file
	int askedReadSize; // the size of data requied to be read from file by the game
	int realReadSize; // the really read size from file
	std::string sourcebuff; // source buffer

	AuCtx(){
		decoder = NULL;
		startPos = 0;
		endPos = 0;
		LoopNum = -1;
		AuBuf = 0;
		AuBufSize = 2048;
		PCMBuf = 0;
		PCMBufSize = 2048;
		AuBufAvailable = 0;
		SamplingRate = 44100;
		freq = SamplingRate;
		BitRate = 0;
		Channels = 2;
		Version = 0;
		SumDecodedSamples = 0;
		MaxOutputSample = 0;
		askedReadSize = 0;
		realReadSize = 0;
		audioType = 0;
		FrameNum = 0;
	};
	~AuCtx(){
		if (decoder){
			AudioClose(&decoder);
			decoder = NULL;
		}
	};

	u32 AuExit();
	u32 AuDecode(u32 pcmAddr);
	u32 AuGetLoopNum();
	u32 AuSetLoopNum(int loop);
	int AuCheckStreamDataNeeded();
	u32 AuNotifyAddStreamData(int size);
	u32 AuGetInfoToAddStreamData(u32 buff, u32 size, u32 srcPos);
	u32 AuGetMaxOutputSample();
	u32 AuGetSumDecodedSample();
	u32 AuResetPlayPosition();
	int AuGetChannelNum();
	int AuGetBitRate();
	int AuGetSamplingRate();
	u32 AuResetPlayPositionByFrame(int position);
	int AuGetVersion();
	int AuGetFrameNum();
	bool AuCreateCodecContextFromSource();

	void DoState(PointerWrap &p) {
		auto s = p.Section("AuContext", 0, 1);
		if (!s)
			return;

		p.Do(startPos);
		p.Do(endPos);
		p.Do(AuBuf);
		p.Do(AuBufSize);
		p.Do(PCMBuf);
		p.Do(PCMBufSize);
		p.Do(freq);
		p.Do(SumDecodedSamples);
		p.Do(LoopNum);
		p.Do(Channels);
		p.Do(MaxOutputSample);
		p.Do(readPos);
		p.Do(audioType);
		p.Do(BitRate);
		p.Do(SamplingRate);
		p.Do(askedReadSize);
		p.Do(realReadSize);
		p.Do(FrameNum);

		if (p.mode == p.MODE_READ){
			decoder = new SimpleAudio(audioType);
			AuBufAvailable = 0; // reset to read from file at position readPos
		}
	};
};




