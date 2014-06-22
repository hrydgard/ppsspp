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

struct AVFrame;
struct AVCodec;
struct AVCodecContext;
struct SwrContext;

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

	bool Decode(void* inbuf, int inbytes, uint8_t *outbuf, int *outbytes);
	bool IsOK() const;

	int GetOutSamples();
	int GetSourcePos();
	bool ResetCodecCtx(int channels, int samplerate);
	bool GetAudioCodecID(int audioType); // Get audioCodecId from audioType

	// Not save stated, only used by UI.  Used for ATRAC3 (non+) files.
	void SetExtraData(u8 *data, int size, int wav_bytes_per_packet);

	// These two are only here because of save states.
	int GetAudioType() const { return audioType; }
	void SetResampleFrequency(int freq) { wanted_resample_freq = freq; }

	// Just metadata.
	void SetCtxPtr(u32 ptr) { ctxPtr = ptr;  }
	u32 GetCtxPtr() const { return ctxPtr; }

private:
	void Init();
	bool OpenCodec();

	u32 ctxPtr;
	int audioType;
	int sample_rate_;
	int channels_;
	int outSamples; // output samples per frame
	int srcPos; // bytes consumed in source during the last decoding
	int wanted_resample_freq; // wanted resampling rate/frequency

	AVFrame *frame_;
	AVCodec *codec_;
	AVCodecContext  *codecCtx_;
	SwrContext      *swrCtx_;
	int audioCodecId; // AV_CODEC_ID_XXX

	// Not savestated, only used by UI.
	u8 *extradata_;
};

void AudioClose(SimpleAudio **ctx);
const char *GetCodecName(int codec);  // audioType
bool IsValidCodec(int codec);

class AuCtx {
public:
	AuCtx();
	~AuCtx();

	u32 AuDecode(u32 pcmAddr);
	u32 AuExit();

	u32 AuNotifyAddStreamData(int size);
	int AuCheckStreamDataNeeded();
	u32 AuResetPlayPosition();
	u32 AuResetPlayPositionByFrame(int position);

	u32 AuSetLoopNum(int loop);
	u32 AuGetLoopNum();

	u32 AuGetInfoToAddStreamData(u32 buff, u32 size, u32 srcPos);
	u32 AuGetMaxOutputSample() const { return MaxOutputSample; }
	u32 AuGetSumDecodedSample() const { return SumDecodedSamples; }
	int AuGetChannelNum() const { return Channels; }
	int AuGetBitRate() const { return BitRate; }
	int AuGetSamplingRate() const { return SamplingRate; }
	int AuGetVersion() const { return Version; }
	int AuGetFrameNum() const { return FrameNum; }

	void DoState(PointerWrap &p);

	void EatSourceBuff(int amount) {
		sourcebuff.erase(0, amount);
		AuBufAvailable -= amount;
	}
	// Au source information. Written to from for example sceAacInit so public for now.
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

	// State variables. These should be relatively easy to move into private.
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

private:
	std::string sourcebuff; // source buffer
};




