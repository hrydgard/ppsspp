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

#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

#include "Core/MemMap.h"
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

// This is not a PSP-native struct.
// But, it's stored in its entirety in savestates, which makes it awkward to change it.
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
	u32 _filesize_dontuse;
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

struct Track {
	// This both does and doesn't belong in Track - it's fixed for an Atrac instance. Oh well.
	u32 codecType = 0;

	// Size of the full track being streamed or played. Can be a lot larger than the in-memory buffer in the streaming modes.
	u32 fileSize = 0;

	// Not really used for much except queries, this keeps track of the bitrate of the track.
	u32 bitrate = 64;

	// Signifies whether to use a more efficient coding mode with less stereo separation. For our purposes, just metadata.
	int jointStereo = 0;

	// Number of audio channels in the track.
	u16 channels = 0;

	// The size of an encoded frame in bytes.
	u16 bytesPerFrame = 0;

	// Byte offset of the first encoded frame in the input buffer. Note: Some samples may be skipped according to firstSampleOffset.
	int dataByteOffset = 0;

	// How many samples to skip from the beginning of a track when decoding.
	// Actually, the real number is this added to FirstOffsetExtra(codecType). You can call
	// FirstSampleOffset2() to get that.
	// Some use of these offsets around the code seem to be inconsistent, sometimes the extra is included,
	// sometimes not.
	int firstSampleOffset = 0;

	// Last sample number. Though, we made it so in Analyze, it's exclusive in the file.
	// Does not take firstSampleOffset into account.
	int endSample = 0;

	// Loop configuration. The PSP only supports one loop but we store them all.
	std::vector<AtracLoopInfo> loopinfo;
	// The actual used loop offsets. These appear to be raw offsets, not taking FirstSampleOffset2() into account.
	int loopStartSample = -1;
	int loopEndSample = -1;

	// Input frame size
	int BytesPerFrame() const {
		return bytesPerFrame;
	}

	inline int FirstOffsetExtra() const {
		return codecType == PSP_MODE_AT_3_PLUS ? 368 : 69;
	}

	// Includes the extra offset. See firstSampleOffset comment above.
	int FirstSampleOffsetFull() const {
		return FirstOffsetExtra() + firstSampleOffset;
	}

	// Output frame size, different between the two supported codecs.
	u32 SamplesPerFrame() const {
		return codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES;
	}

	void UpdateBitrate() {
		bitrate = (bytesPerFrame * 352800) / 1000;
		if (codecType == PSP_MODE_AT_3_PLUS)
			bitrate = ((bitrate >> 11) + 8) & 0xFFFFFFF0;
		else
			bitrate = (bitrate + 511) >> 10;
	}

	// This appears to be buggy, should probably include FirstOffsetExtra?
	// Actually the units don't even make sense here.
	u32 DecodePosBySample(int sample) const {
		return (u32)(firstSampleOffset + sample / (int)SamplesPerFrame() * bytesPerFrame);
	}

	// This appears to be buggy, should probably include FirstOffsetExtra?
	u32 FileOffsetBySample(int sample) const {
		int offsetSample = sample + firstSampleOffset;
		int frameOffset = offsetSample / (int)SamplesPerFrame();
		return (u32)(dataByteOffset + bytesPerFrame + frameOffset * bytesPerFrame);
	}

	void AnalyzeReset() {
		endSample = -1;
		loopinfo.clear();
		loopStartSample = -1;
		loopEndSample = -1;
		channels = 2;
		// TODO: Could probably reset more.
	}

	void DebugLog();
};

int AnalyzeAA3Track(u32 addr, u32 size, u32 filesize, Track *track);
int AnalyzeAtracTrack(u32 addr, u32 size, Track *track);

class AtracBase {
public:
	virtual ~AtracBase() {}

	virtual void DoState(PointerWrap &p) = 0;

	const Track &GetTrack() const {
		return track_;
	}
	// This should be rare.
	Track &GetTrackMut() {
		return track_;
	}

	int GetOutputChannels() const {
		return outputChannels_;
	}
	void SetOutputChannels(int channels) {
		// Only used for sceSas audio. To be refactored away in the future.
		outputChannels_ = channels;
	}

	int atracID_ = -1;

	PSPPointer<SceAtracContext> context_{};

	AtracStatus BufferState() const {
		return bufferState_;
	}

	int LoopNum() const {
		return loopNum_;
	}
	u32 CodecType() const {
		return track_.codecType;
	}
	AudioDecoder *Decoder() const {
		return decoder_;
	}

	void CreateDecoder();

	virtual int CurrentSample() const = 0;
	virtual int RemainingFrames() const = 0;
	virtual u32 SecondBufferSize() const = 0;

	virtual int Analyze(u32 addr, u32 size) = 0;
	virtual int AnalyzeAA3(u32 addr, u32 size, u32 filesize) = 0;

	void UpdateContextFromPSPMem();
	virtual void WriteContextToPSPMem() = 0;

	virtual void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) = 0;
	virtual int AddStreamData(u32 bytesToAdd) = 0;
	virtual u32 AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) = 0;
	virtual void SetLoopNum(int loopNum);
	virtual u32 ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) = 0;
	virtual void GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) = 0;
	virtual int SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) = 0;

	virtual int GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize);
	virtual u32 SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) = 0;
	virtual u32 DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) = 0;
	virtual u32 GetNextSamples() = 0;
	virtual void InitLowLevel(u32 paramsAddr, bool jointStereo) = 0;

protected:
	Track track_{};
	u16 outputChannels_ = 2;
	int loopNum_ = 0;

	// TODO: Save the internal state of this, now technically possible.
	AudioDecoder *decoder_ = nullptr;
	AtracStatus bufferState_ = ATRAC_STATUS_NO_DATA;
};

class Atrac : public AtracBase {
public:
	~Atrac() {
		ResetData();
	}

	uint32_t CurBufferAddress(int adjust = 0) const {
		u32 off = track_.FileOffsetBySample(currentSample_ + adjust);
		if (off < first_.size && ignoreDataBuf_) {
			return first_.addr + off;
		}
		// If it's in dataBug, it's not in PSP memory.
		return 0;
	}

	u8 *BufferStart();
	void DoState(PointerWrap &p) override;
	void WriteContextToPSPMem() override;

	int Analyze(u32 addr, u32 size) override;
	int AnalyzeAA3(u32 addr, u32 size, u32 filesize) override;

	int CurrentSample() const override {
		return currentSample_;
	}
	int RemainingFrames() const override;
	u32 SecondBufferSize() const override {
		return second_.size;
	}

	void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) override;
	int AddStreamData(u32 bytesToAdd) override;
	u32 AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) override;
	u32 ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) override;
	void GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) override;
	int SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) override;
	u32 SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) override;
	u32 DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) override;
	u32 GetNextSamples() override;
	void InitLowLevel(u32 paramsAddr, bool jointStereo) override;

protected:
	void AnalyzeReset();

private:
	void UpdateBufferState();
	void ResetData();
	void SeekToSample(int sample);
	void ForceSeekToSample(int sample);
	u32 StreamBufferEnd() const {
		// The buffer is always aligned to a frame in size, not counting an optional header.
		// The header will only initially exist after the data is first set.
		u32 framesAfterHeader = (bufferMaxSize_ - bufferHeaderSize_) / track_.bytesPerFrame;
		return framesAfterHeader * track_.bytesPerFrame + bufferHeaderSize_;
	}
	void ConsumeFrame();
	void CalculateStreamInfo(u32 *readOffset);

	InputBuffer first_{};
	InputBuffer second_{};  // only addr, size, fileoffset are used (incomplete)

	u8 *dataBuf_ = nullptr;
	// Indicates that the dataBuf_ array should not be used.
	bool ignoreDataBuf_ = false;

	int currentSample_ = 0;
	u32 decodePos_ = 0;
	u32 bufferMaxSize_ = 0;

	// Used to track streaming.
	u32 bufferPos_ = 0;
	u32 bufferValidBytes_ = 0;
	u32 bufferHeaderSize_ = 0;
};
