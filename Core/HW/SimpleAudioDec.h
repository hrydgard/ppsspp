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

#include "Core/HW/MediaEngine.h"
#include "Core/HLE/sceAudio.h"

struct AVFrame;
struct AVCodec;
struct AVCodecContext;
struct SwrContext;

#ifdef USE_FFMPEG

extern "C" {
#include "libavutil/version.h"
};

#endif

// Wraps FFMPEG for audio decoding in a nice interface.
// Decodes packet by packet - does NOT demux.

// Based on http://ffmpeg.org/doxygen/trunk/doc_2examples_2decoding_encoding_8c-example.html#_a13

// audioType
enum {
	PSP_CODEC_AT3PLUS = 0x00001000,
	PSP_CODEC_AT3 = 0x00001001,
	PSP_CODEC_MP3 = 0x00001002,
	PSP_CODEC_AAC = 0x00001003,
};

class SimpleAudio {
public:
	SimpleAudio(int audioType, int sample_rate = 44100, int channels = 2);
	~SimpleAudio();

	bool Decode(const uint8_t* inbuf, int inbytes, uint8_t *outbuf, int *outbytes);
	bool IsOK() const;

	int GetOutSamples();
	int GetSourcePos();
	int GetAudioCodecID(int audioType); // Get audioCodecId from audioType

	// Not save stated, only used by UI.  Used for ATRAC3 (non+) files.
	void SetExtraData(const u8 *data, int size, int wav_bytes_per_packet);

	void SetChannels(int channels);

	// These two are only here because of save states.
	int GetAudioType() const { return audioType; }
	void SetResampleFrequency(int freq) { wanted_resample_freq = freq; }

	// Just metadata.
	void SetCtxPtr(u32 ptr) { ctxPtr = ptr;  }
	u32 GetCtxPtr() const { return ctxPtr; }

private:
	void Init();
	bool OpenCodec(int block_align);

	u32 ctxPtr;
	int audioType;
	int sample_rate_;
	int channels_;
	int outSamples; // output samples per frame
	int srcPos; // bytes consumed in source during the last decoding
	int wanted_resample_freq; // wanted resampling rate/frequency

	AVFrame *frame_;
#if HAVE_LIBAVCODEC_CONST_AVCODEC // USE_FFMPEG is implied
	const
#endif
	AVCodec *codec_;
	AVCodecContext  *codecCtx_;
	SwrContext      *swrCtx_;

	bool codecOpen_;
};

void AudioClose(SimpleAudio **ctx);
const char *GetCodecName(int codec);  // audioType
bool IsValidCodec(int codec);

class AuCtx {
public:
	AuCtx();
	~AuCtx();

	u32 AuDecode(u32 pcmAddr);

	u32 AuNotifyAddStreamData(int size);
	int AuCheckStreamDataNeeded();
	int AuStreamBytesNeeded();
	int AuStreamWorkareaSize();
	u32 AuResetPlayPosition();
	u32 AuResetPlayPositionByFrame(int position);

	u32 AuSetLoopNum(int loop);
	u32 AuGetLoopNum();

	u32 AuGetInfoToAddStreamData(u32 bufPtr, u32 sizePtr, u32 srcPosPtr);
	u32 AuGetMaxOutputSample() const { return MaxOutputSample; }
	u32 AuGetSumDecodedSample() const { return SumDecodedSamples; }
	int AuGetChannelNum() const { return Channels; }
	int AuGetBitRate() const { return BitRate; }
	int AuGetSamplingRate() const { return SamplingRate; }
	int AuGetVersion() const { return Version; }
	int AuGetFrameNum() const { return FrameNum; }

	void SetReadPos(int pos) { readPos = pos; }
	int ReadPos() { return readPos;  }

	void DoState(PointerWrap &p);

	void EatSourceBuff(int amount) {
		if (amount > (int)sourcebuff.size()) {
			amount = (int)sourcebuff.size();
		}
		if (amount > 0)
			sourcebuff.erase(sourcebuff.begin(), sourcebuff.begin() + amount);
		AuBufAvailable -= amount;
	}
	// Au source information. Written to from for example sceAacInit so public for now.
	u64 startPos = 0;
	u64 endPos = 0;
	u32 AuBuf = 0;
	u32 AuBufSize = 0;
	u32 PCMBuf = 0;
	u32 PCMBufSize = 0;
	int freq = -1;
	int BitRate = 0;
	int SamplingRate = -1;
	int Channels = 0;
	int Version = -1;

	// State variables. These should be relatively easy to move into private.
	u32 SumDecodedSamples = 0;
	int LoopNum = -1;
	u32 MaxOutputSample = 0;
	int FrameNum = 0;

	// Au decoder
	SimpleAudio *decoder = nullptr;

	// Au type
	int audioType = 0;

private:
	size_t FindNextMp3Sync();

	std::vector<u8> sourcebuff; // source buffer

	// buffers informations
	int AuBufAvailable = 0; // the available buffer of AuBuf to be able to recharge data
	int readPos; // read position in audio source file
	int askedReadSize = 0; // the size of data requied to be read from file by the game
	int nextOutputHalf = 0;
};




