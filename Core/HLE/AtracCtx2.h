#pragma once

#include <cstdint>

#include "Core/HLE/AtracCtx.h"

class Atrac2 : public AtracBase {
public:
	// The default values are only used during save state load, in which case they get restored by DoState.
	Atrac2(u32 contextAddr = 0, int codecType = 0);
	~Atrac2() {
		delete[] decodeTemp_;
	}

	AtracStatus BufferState() const override {
		return context_->info.state;
	}

	void DoState(PointerWrap &p) override;

	int GetNextDecodePosition(int *pos) const override;

	int RemainingFrames() const override;
	int LoopStatus() const override;
	int Bitrate() const override;
	int LoopNum() const override;
	int SamplesPerFrame() const override { return context_->info.SamplesPerFrame(); }
	int Channels() const override { return context_->info.numChan; }
	int BytesPerFrame() const override { return context_->info.sampleSize; }
	int SetLoopNum(int loopNum) override;
	int CodecType() const override { return context_->info.codec; }

	void GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) override;
	int GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) override;
	int AddStreamData(u32 bytesToAdd) override;
	int ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) override;
	int GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample, bool *delay) override;
	int SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, int outputChannels) override;
	int SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) override;
	bool HasSecondBuffer() const override;

	u32 DecodeData(u8 *outbuf, u32 outbufPtr, int *SamplesNum, int *finish, int *remains) override;
	int DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) override;

	void CheckForSas() override;
	int EnqueueForSas(u32 address, u32 ptr) override;
	void DecodeForSas(s16 *dstData, int *bytesWritten, int *finish) override;
	const AtracSasStreamState *StreamStateForSas() const override { return context_->info.state == 0x10 ? &sas_ : nullptr; }

	u32 GetNextSamples() override;

	void InitLowLevel(const Atrac3LowLevelParams &params, int codecType) override;

	int GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const override;

	// These will not be used by the new implementation.
	void UpdateContextFromPSPMem() override {}
	void NotifyGetContextAddress() override {}

	int GetContextVersion() const override { return 2; }
	u32 GetInternalCodecError() const override;

private:
	u32 DecodeInternal(u32 outbufAddr, int *SamplesNum, int *finish);
	void GetResetBufferInfoInternal(AtracResetBufferInfo *bufferInfo, int sample);
	u32 ResetPlayPositionInternal(int seekPos, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf);

	u32 SkipFrames(int *skippedCount);
	void WrapLastPacket();

	// Just the current decoded frame, in order to be able to cut off the first part of it
	// to write the initial partial frame.
	// Does not need to be saved.
	int16_t *decodeTemp_ = nullptr;

	// This is hidden state inside sceSas, really. Not visible in the context.
	// But it doesn't really matter whether it's here or there.
	AtracSasStreamState sas_;
};
