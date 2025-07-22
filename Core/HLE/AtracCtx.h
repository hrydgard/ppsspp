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
#include "Core/HLE/AtracBase.h"
#include "Core/Util/AtracTrack.h"

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
	int GetBufferInfoForResetting(AtracResetBufferInfo *bufferInfo, int sample, bool *delay) override;  // NOTE: Not const! This can cause SkipFrames! (although only in the AtracCtx2)
	int SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, u32 fileSize, int outputChannels, bool isAA3) override;
	int GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) const override;
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
