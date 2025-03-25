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

struct AtracSasStreamState {
	u32 bufPtr[2]{};
	u32 bufSize[2]{};
	int streamOffset = 0;
	int fileOffset = 0;
	int curBuffer = 0;
	bool isStreaming = false;

	int CurPos() const {
		int retval = fileOffset - bufSize[curBuffer] + streamOffset;
		_dbg_assert_(retval >= 0);
		return retval;
	}
};

const int PSP_ATRAC_ALLDATA_IS_ON_MEMORY = -1;
const int PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY = -2;
const int PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY = -3;

// This is not a PSP-native struct.
// But, it's stored in its entirety in savestates, which makes it awkward to change it.
// This is used for both first_ and second_, but the latter doesn't use all the fields.
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

class AudioDecoder;

class AtracBase {
public:
	virtual ~AtracBase();

	virtual void DoState(PointerWrap &p) = 0;

	// TODO: Find a way to get rid of this from the base class.
	virtual void UpdateContextFromPSPMem() = 0;

	virtual int Channels() const = 0;

	int GetOutputChannels() const {
		return outputChannels_;
	}
	void SetOutputChannels(int channels) {
		// Only used for sceSas audio. To be refactored away in the future.
		outputChannels_ = channels;
	}

	virtual u32 GetInternalCodecError() const { return 0; }

	PSPPointer<SceAtracContext> context_{};

	virtual AtracStatus BufferState() const = 0;

	virtual int SetLoopNum(int loopNum) = 0;
	virtual int LoopNum() const = 0;
	virtual int LoopStatus() const = 0;

	virtual int CodecType() const = 0;

	AudioDecoder *Decoder() const {
		return decoder_;
	}

	void CreateDecoder(int codecType, int bytesPerFrame, int channels);

	virtual void NotifyGetContextAddress() = 0;

	virtual int GetNextDecodePosition(int *pos) const = 0;
	virtual int RemainingFrames() const = 0;
	virtual bool HasSecondBuffer() const = 0;
	virtual int Bitrate() const = 0;
	virtual int BytesPerFrame() const = 0;
	virtual int SamplesPerFrame() const = 0;

	virtual void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) = 0;
	virtual int AddStreamData(u32 bytesToAdd) = 0;
	virtual int ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) = 0;
	virtual int GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample, bool *delay) = 0;
	virtual int SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, int outputChannels) = 0;

	virtual int GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) = 0;
	virtual int SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) = 0;
	virtual u32 DecodeData(u8 *outbuf, u32 outbufPtr, int *SamplesNum, int *finish, int *remains) = 0;
	virtual int DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) = 0;

	virtual u32 GetNextSamples() = 0;
	virtual void InitLowLevel(const Atrac3LowLevelParams &params, int codecType) = 0;

	virtual void CheckForSas() = 0;
	virtual int EnqueueForSas(u32 address, u32 ptr) = 0;
	virtual void DecodeForSas(s16 *dstData, int *bytesWritten, int *finish) = 0;
	virtual const AtracSasStreamState *StreamStateForSas() const { return nullptr; }

	virtual int GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const = 0;

	virtual int GetContextVersion() const = 0;

protected:
	u16 outputChannels_ = 2;

	// TODO: Save the internal state of this, now technically possible.
	AudioDecoder *decoder_ = nullptr;
};

class Atrac : public AtracBase {
public:
	Atrac(int atracID, int codecType = 0) : atracID_(atracID) {
		if (codecType) {
			track_.codecType = codecType;
		}
	}
	~Atrac();

	uint32_t CurBufferAddress(int adjust = 0) const {
		u32 off = track_.FileOffsetBySample(currentSample_ + adjust);
		if (off < first_.size && ignoreDataBuf_) {
			return first_.addr + off;
		}
		// If it's in dataBug, it's not in PSP memory.
		return 0;
	}

	u8 *BufferStart();

	AtracStatus BufferState() const override {
		return bufferState_;
	}

	void DoState(PointerWrap &p) override;

	int GetNextDecodePosition(int *pos) const override;
	int RemainingFrames() const override;

	int CodecType() const override {
		return track_.codecType;
	}
	bool HasSecondBuffer() const override {
		return second_.size != 0;
	}
	int Channels() const override {
		return track_.channels;
	}
	int LoopNum() const override {
		return loopNum_;
	}
	int LoopStatus() const override {
		// This doesn't match tests.
		if (track_.loopinfo.size() > 0)
			return 1;
		else
			return 0;
	}
	int Bitrate() const override {
		return track_.Bitrate();
	}
	int BytesPerFrame() const override {
		return track_.BytesPerFrame();
	}
	int SamplesPerFrame() const override {
		return track_.SamplesPerFrame();
	}

	// This should be rare.
	Track &GetTrackMut() {
		return track_;
	}

	int SetLoopNum(int loopNum) override;

	// Ask where in memory new data should be written.
	void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) override;
	// Notify the player that the user has written some new data.
	int AddStreamData(u32 bytesToAdd) override;
	int ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) override;
	int GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample, bool *delay) override;
	int SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, int outputChannels) override;
	int GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) override;
	int SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) override;
	u32 DecodeData(u8 *outbuf, u32 outbufPtr, int *SamplesNum, int *finish, int *remains) override;
	int DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) override;

	void CheckForSas() override;
	int EnqueueForSas(u32 address, u32 ptr) override;
	void DecodeForSas(s16 *dstData, int *bytesWritten, int *finish) override;

	// Returns how many samples the next DecodeData will write.
	u32 GetNextSamples() override;
	void InitLowLevel(const Atrac3LowLevelParams &params, int codecType) override;

	int GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const override;

	void NotifyGetContextAddress() override;
	void UpdateContextFromPSPMem() override;
	void WriteContextToPSPMem();

	int GetContextVersion() const override { return 1; }

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

	Track track_{};

	InputBuffer first_{};
	InputBuffer second_{};  // only addr, size, fileoffset are used (incomplete)

	u8 *dataBuf_ = nullptr;
	// Indicates that the dataBuf_ array should not be used.
	bool ignoreDataBuf_ = false;

	int currentSample_ = 0;
	u32 decodePos_ = 0;
	u32 bufferMaxSize_ = 0;
	int loopNum_ = 0;

	// Used to track streaming.
	u32 bufferPos_ = 0;
	u32 bufferValidBytes_ = 0;
	u32 bufferHeaderSize_ = 0;

	int atracID_ = -1;
	AtracStatus bufferState_ = ATRAC_STATUS_NO_DATA;
};
