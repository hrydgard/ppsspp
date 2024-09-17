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

#include "Core/HW/MediaEngine.h"
#include "Core/HLE/sceAudio.h"

// Decodes packet by packet - does NOT demux.

// audioType
enum PSPAudioType {
	PSP_CODEC_AT3PLUS = 0x00001000,
	PSP_CODEC_AT3 = 0x00001001,
	PSP_CODEC_MP3 = 0x00001002,
	PSP_CODEC_AAC = 0x00001003,
};

class AudioDecoder {
public:
	virtual ~AudioDecoder() {}

	virtual PSPAudioType GetAudioType() const = 0;

	// inbytesConsumed can include skipping metadata.
	// outSamples is in stereo samples. So you have to multiply by 4 for 16-bit stereo audio to get bytes.
	// For Atrac3, if *outSamples != 0, it'll cap the number of samples to output. In this case, its value can only shrink.
	// TODO: Implement that in the other decoders too, if needed.
	virtual bool Decode(const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int16_t *outbuf, int *outSamples) = 0;
	virtual bool IsOK() const = 0;

	virtual void SetChannels(int channels) = 0;
	virtual void FlushBuffers() {}

	// Just metadata.
	void SetCtxPtr(uint32_t ptr) { ctxPtr = ptr; }
	uint32_t GetCtxPtr() const { return ctxPtr; }

private:
	uint32_t ctxPtr = 0xFFFFFFFF;
};

void AudioClose(AudioDecoder **ctx);
const char *GetCodecName(int codec);  // audioType
bool IsValidCodec(PSPAudioType codec);
AudioDecoder *CreateAudioDecoder(PSPAudioType audioType, int sampleRateHz = 44100, int channels = 2, size_t blockAlign = 0, const uint8_t *extraData = nullptr, size_t extraDataSize = 0);

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
	u32 AuGetInfoToAddStreamData(u32 bufPtr, u32 sizePtr, u32 srcPosPtr);

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

	int freq = -1;  // used by AAC only?
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
	AudioDecoder *decoder = nullptr;

private:
	size_t FindNextMp3Sync();

	std::vector<u8> sourcebuff; // source buffer

	// buffers informations
	int AuBufAvailable = 0; // the available buffer of AuBuf to be able to recharge data
	int readPos = 0; // read position in audio source file
	int askedReadSize = 0; // the size of data requied to be read from file by the game
	int nextOutputHalf = 0;
};




