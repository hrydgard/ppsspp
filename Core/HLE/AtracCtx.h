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

#pragma once

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

#include "Core/HLE/sceAtrac.h"

struct AtracSingleResetBufferInfo {
	u32_le writePosPtr;
	u32_le writableBytes;
	u32_le minWriteBytes;
	u32_le filePos;
};

struct AtracResetBufferInfo {
	AtracSingleResetBufferInfo first;
	AtracSingleResetBufferInfo second;
};

#define AT3_MAGIC           0x0270
#define AT3_PLUS_MAGIC      0xFFFE
#define PSP_MODE_AT_3_PLUS  0x00001000
#define PSP_MODE_AT_3       0x00001001

#define ATRAC_ERROR_API_FAIL                 0x80630002
#define ATRAC_ERROR_NO_ATRACID               0x80630003
#define ATRAC_ERROR_INVALID_CODECTYPE        0x80630004
#define ATRAC_ERROR_BAD_ATRACID              0x80630005
#define ATRAC_ERROR_UNKNOWN_FORMAT           0x80630006
#define ATRAC_ERROR_WRONG_CODECTYPE          0x80630007
#define ATRAC_ERROR_BAD_CODEC_PARAMS         0x80630008
#define ATRAC_ERROR_ALL_DATA_LOADED          0x80630009
#define ATRAC_ERROR_NO_DATA                  0x80630010
#define ATRAC_ERROR_SIZE_TOO_SMALL           0x80630011
#define ATRAC_ERROR_SECOND_BUFFER_NEEDED     0x80630012
#define ATRAC_ERROR_INCORRECT_READ_SIZE      0x80630013
#define ATRAC_ERROR_BAD_SAMPLE               0x80630015
#define ATRAC_ERROR_BAD_FIRST_RESET_SIZE     0x80630016
#define ATRAC_ERROR_BAD_SECOND_RESET_SIZE    0x80630017
#define ATRAC_ERROR_ADD_DATA_IS_TOO_BIG      0x80630018
#define ATRAC_ERROR_NOT_MONO                 0x80630019
#define ATRAC_ERROR_NO_LOOP_INFORMATION      0x80630021
#define ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED 0x80630022
#define ATRAC_ERROR_BUFFER_IS_EMPTY          0x80630023
#define ATRAC_ERROR_ALL_DATA_DECODED         0x80630024
#define ATRAC_ERROR_IS_LOW_LEVEL             0x80630031
#define ATRAC_ERROR_IS_FOR_SCESAS            0x80630040
#define ATRAC_ERROR_AA3_INVALID_DATA         0x80631003
#define ATRAC_ERROR_AA3_SIZE_TOO_SMALL       0x80631004

const u32 ATRAC3_MAX_SAMPLES = 0x400;
const u32 ATRAC3PLUS_MAX_SAMPLES = 0x800;

const int PSP_ATRAC_ALLDATA_IS_ON_MEMORY = -1;
const int PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY = -2;
const int PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY = -3;

struct InputBuffer {
	// Address of the buffer.
	u32 addr;
	// Size of data read so far into dataBuf_ (to be removed.)
	u32 size;
	// Offset into addr at which new data is added.
	u32 offset;
	// Last writableBytes number (to be removed.)
	u32 writableBytes;
	// Unused, always 0.
	u32 neededBytes;
	// Total size of the entire file data.
	u32 filesize;
	// Offset into the file at which new data is read.
	u32 fileoffset;
};

struct AtracLoopInfo {
	int cuePointID;
	int type;
	int startSample;
	int endSample;
	int fraction;
	int playCount;
};

class AudioDecoder;

struct Atrac {
	~Atrac() {
		ResetData();
	}

	void ResetData();
	void UpdateBufferState() {
		if (bufferMaxSize_ >= first_.filesize) {
			if (first_.size < first_.filesize) {
				// The buffer is big enough, but we don't have all the data yet.
				bufferState_ = ATRAC_STATUS_HALFWAY_BUFFER;
			} else {
				bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
			}
		} else {
			if (loopEndSample_ <= 0) {
				// There's no looping, but we need to stream the data in our buffer.
				bufferState_ = ATRAC_STATUS_STREAMED_WITHOUT_LOOP;
			} else if (loopEndSample_ == endSample_ + firstSampleOffset_ + (int)FirstOffsetExtra()) {
				bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
			} else {
				bufferState_ = ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
			}
		}
	}

	void DoState(PointerWrap &p);

	// Input frame size
	int BytesPerFrame() const {
		return bytesPerFrame_;
	}
	// Output frame size
	u32 SamplesPerFrame() const {
		return codecType_ == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES;
	}

	u32 FirstOffsetExtra() const {
		return codecType_ == PSP_MODE_AT_3_PLUS ? 368 : 69;
	}

	u32 DecodePosBySample(int sample) const {
		return (u32)(firstSampleOffset_ + sample / (int)SamplesPerFrame() * bytesPerFrame_);
	}

	u32 FileOffsetBySample(int sample) const {
		int offsetSample = sample + firstSampleOffset_;
		int frameOffset = offsetSample / (int)SamplesPerFrame();
		return (u32)(dataOff_ + bytesPerFrame_ + frameOffset * bytesPerFrame_);
	}

	void UpdateBitrate();

	int Bitrate() const {
		return bitrate_;
	}
	int Channels() const {
		return channels_;
	}

	int RemainingFrames() const;

	int atracID_ = -1;

	u8 *dataBuf_ = nullptr;
	// Indicates that the dataBuf_ array should not be used.
	bool ignoreDataBuf_ = false;

	u32 decodePos_ = 0;

	u16 channels_ = 0;
	u16 outputChannels_ = 2;

	int currentSample_ = 0;
	int endSample_ = 0;
	int firstSampleOffset_ = 0;
	// Offset of the first sample in the input buffer
	int dataOff_ = 0;

	std::vector<AtracLoopInfo> loopinfo_;

	int loopStartSample_ = -1;
	int loopEndSample_ = -1;
	int loopNum_ = 0;

	InputBuffer first_{};
	InputBuffer second_{};

	PSPPointer<SceAtracContext> context_{};

	AtracStatus BufferState() const {
		return bufferState_;
	}

	void ForceSeekToSample(int sample);
	u8 *BufferStart();

	void SeekToSample(int sample);

	u32 CodecType() const {
		return codecType_;
	}
	AudioDecoder *GetDecoder() const {
		return decoder_;
	}

	uint32_t CurBufferAddress(int adjust = 0) {
		u32 off = FileOffsetBySample(currentSample_ + adjust);
		if (off < first_.size && ignoreDataBuf_) {
			return first_.addr + off;
		}
		// If it's in dataBug, it's not in PSP memory.
		return 0;
	}

	void CalculateStreamInfo(u32 *readOffset);

	u32 StreamBufferEnd() const {
		// The buffer is always aligned to a frame in size, not counting an optional header.
		// The header will only initially exist after the data is first set.
		u32 framesAfterHeader = (bufferMaxSize_ - bufferHeaderSize_) / bytesPerFrame_;
		return framesAfterHeader * bytesPerFrame_ + bufferHeaderSize_;
	}

	int Analyze(u32 addr, u32 size);
	int AnalyzeAA3(u32 addr, u32 size, u32 filesize);

	void UpdateContextFromPSPMem();
	void WriteContextToPSPMem();

	int AddStreamData(u32 bytesToAdd);
	u32 AddStreamDataSas(u32 bufPtr, u32 bytesToAdd);
	void CreateDecoder();
	void SetLoopNum(int loopNum);
	u32 ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf);
	void GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample);
	int SetData(u32 buffer, u32 readSize, u32 bufferSize, int successCode = 0);
	u32 SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize);
	u32 DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains);
	void ConsumeFrame();
	u32 GetNextSamples();

	void InitLowLevel(u32 paramsAddr, bool jointStereo);

	// To be moved into private.
	u32 codecType_ = 0;

private:
	void AnalyzeReset();

	u32 bitrate_ = 64;
	u16 bytesPerFrame_ = 0;
	u32 bufferMaxSize_ = 0;
	int jointStereo_ = 0;

	// Used by low-level decoding and to track streaming.
	u32 bufferPos_ = 0;
	u32 bufferValidBytes_ = 0;
	u32 bufferHeaderSize_ = 0;

	// TODO: Save the internal state of this, now technically possible.
	AudioDecoder *decoder_ = nullptr;

	AtracStatus bufferState_ = ATRAC_STATUS_NO_DATA;
};
