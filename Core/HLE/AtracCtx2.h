#pragma once

#include <cstdint>

#include "Core/HLE/AtracCtx.h"

// Reimplementation of sceAtrac.
// This time, no extra buffering, read everything from RAM properly.
class Atrac2 : public AtracBase {
public:
	void DoState(PointerWrap &p) override;
	void WriteContextToPSPMem() override;

	int Analyze(u32 addr, u32 size) override;
	int AnalyzeAA3(u32 addr, u32 size, u32 filesize) override;

	int CurrentSample() const override { return currentSample_; }
	int RemainingFrames() const override;

	int SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) override;

	void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) override;
	int AddStreamData(u32 bytesToAdd) override;
	u32 AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) override;

	void GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) override;
	u32 ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) override;

	u32 SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) override;
	int SecondBufferSize() const override;

	u32 GetNextSamples() override;
	u32 DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) override;

	void InitLowLevel(u32 paramsAddr, bool jointStereo) override;

private:
	int LoopNum() const {
		if (bufferState_ == ATRAC_STATUS_FOR_SCESAS) {
			// TODO: Might need more testing.
			return 0;
		} else {
			return loopNum_;
		}
	}

	// This is relative to track_.FirstSampleOffset2().
	int currentSample_ = 0;

	// The current buffer in-memory to read from. Can be static or streaming.
	u32 bufAddr_ = 0;
	u32 bufSize_ = 0;
	// In case of a "halfway" buffer, how much is filled.
	u32 bufValidBytes_ = 0;

	u32 bufWriteOffset_ = 0;  // Corresponds to first.offset
	u32 fileReadOffset_ = 0;  // Corresponds to first.fileoffset (Next offset in the host's file to read.)

	u32 bufPos_ = 0;
	bool streamWrapped_ = false;

	// Second buffer
	u32 secondBufAddr_ = 0;
	u32 secondBufSize_ = 0;
};
