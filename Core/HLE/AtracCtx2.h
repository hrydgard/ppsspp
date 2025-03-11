#pragma once

#include <cstdint>

#include "Core/HLE/AtracCtx.h"

class Atrac2 : public AtracBase {
public:
	Atrac2(int atracID, u32 contextAddr, int codecType);
	~Atrac2() {
		delete[] decodeTemp_;
	}

	AtracStatus BufferState() const {
		return context_->info.state;
	}

	void DoState(PointerWrap &p) override;

	int GetID() const override { return context_->info.atracID; }

	int GetNextDecodePosition(int *pos) const override;

	int RemainingFrames() const override;
	int LoopStatus() const override { return 0; }
	int Bitrate() const override;
	int LoopNum() const override { return context_->info.loopNum; }
	int SamplesPerFrame() const override { return context_->info.codec == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES; }
	int Channels() const override { return context_->info.numChan; }
	int BytesPerFrame() const override { return context_->info.sampleSize; }
	int SetLoopNum(int loopNum) override;

	void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) override;
	int AddStreamData(u32 bytesToAdd) override;
	u32 AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) override;
	int ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) override;
	int GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) override;
	int SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, int outputChannels) override;
	u32 SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) override;
	u32 SecondBufferSize() const override;

	u32 DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) override;
	int DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) override;
	u32 GetNextSamples() override;

	void InitLowLevel(u32 paramsAddr, bool jointStereo, int codecType) override;

	int GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const override;

	// These will not be used by the new implementation.
	void UpdateContextFromPSPMem() override {}
	void NotifyGetContextAddress() override {}

	bool IsNewAtracImpl() const override { return true; }

	u32 GetInternalCodecError() const override;

private:
	void InitContext(int offset, u32 bufferAddr, u32 readSize, u32 bufferSize);

	void SeekToSample(int sample);

	// Just the current decoded frame, in order to be able to cut off the first part of it
	// to write the initial partial frame.
	// Does not need to be saved.
	int16_t *decodeTemp_ = nullptr;

	// We skip some samples at the start.
	// TODO: This is ugly, I want a stateless solution..
	int discardedSamples_ = 0;
};
