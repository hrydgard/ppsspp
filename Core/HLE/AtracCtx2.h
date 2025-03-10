#pragma once

#include <cstdint>

#include "Core/HLE/AtracCtx.h"

class Atrac2 : public AtracBase {
public:
	~Atrac2() {
		delete[] decodeTemp_;
	}
	void SetAtracID(int atracID) override {
		EnsureContext(atracID);
		context_->info.atracID = atracID;
	}
	int GetAtracID() const override {
		_dbg_assert_(context_.IsValid());
		return context_->info.atracID;
	}

	AtracStatus BufferState() const {
		return context_->info.state;
	}

	void DoState(PointerWrap &p) override;
	void WriteContextToPSPMem() override {}
	void UpdateContextFromPSPMem() override {}

	int Analyze(u32 addr, u32 size) override;
	int AnalyzeAA3(u32 addr, u32 size, u32 filesize) override;

	int CurrentSample() const override;
	int RemainingFrames() const override;

	void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) override;
	int AddStreamData(u32 bytesToAdd) override;
	u32 AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) override;
	u32 ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) override;
	void GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) override;
	int SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) override;
	u32 SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) override;
	u32 SecondBufferSize() const override;

	u32 DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) override;
	u32 GetNextSamples() override;
	int SetLoopNum(int loopNum) override;
	void InitLowLevel(u32 paramsAddr, bool jointStereo, int atracID) override;
private:
	void AnalyzeReset();
	void SeekToSample(int sample);

	// Just the current decoded frame, in order to be able to cut off the first part of it
	// to write the initial partial frame.
	// Does not need to be saved.
	int16_t *decodeTemp_ = nullptr;

	// We skip some samples at the start.
	// TODO: This is ugly, I want a stateless solution..
	int discardedSamples_;
};
