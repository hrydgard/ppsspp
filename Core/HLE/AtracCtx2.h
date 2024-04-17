#pragma once

#include <cstdint>

#include "Core/HLE/AtracCtx.h"

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
	u32 SecondBufferSize() const override;

	u32 DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) override;
	u32 GetNextSamples() override;
	void InitLowLevel(u32 paramsAddr, bool jointStereo) override;

private:
	void UpdateBufferState();
	void ResetData();
	void ConsumeFrame();
	void CalculateStreamInfo(u32 *outReadOffset);
	u32 StreamBufferEnd() const;

	// This is not a PSP-native struct.
	// But, it's stored in its entirety in savestates, which makes it awkward to change it.
	struct InputBuffer {
		// Address of the buffer.
		u32 addr;
		// Size of data read so far into dataBuf_ (to be removed.)
		u32 size;
		// Offset into addr at which new data is added.
		u32 offset;
		// Last writableBytes number (this should be calculated as needed).
		u32 writableBytes;
		// Offset into the file at which new data is read.
		u32 fileoffset;
	};

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
