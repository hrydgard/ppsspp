#include "Common/Log.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/AtracCtx2.h"
#include "Core/HW/Atrac3Standalone.h"

// Convenient command line:
// Windows\x64\debug\PPSSPPHeadless.exe  --root pspautotests/tests/../ -o --compare --timeout=30 --graphics=software pspautotests/tests/audio/atrac/... --ignore pspautotests/tests/audio/atrac/second/resetting.prx --ignore pspautotests/tests/audio/atrac/second/replay.prx
//
// See the big comment in sceAtrac.cpp for an overview of the different modes of operation.


Atrac2::Atrac2(int atracID, u32 contextAddr, int codecType) {
	context_ = PSPPointer<SceAtracContext>::Create(contextAddr);
	track_.codecType = codecType;
	context_->info.codec = codecType;
}

void Atrac2::DoState(PointerWrap &p) {
	_assert_msg_(false, "Savestates not yet support with new Atrac implementation.\n\nTurn it off in Developer settings.\n\n");
}

int Atrac2::RemainingFrames() const {
	return 0;
}

u32 Atrac2::SecondBufferSize() const {
	return 0;
}

void Atrac2::GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) {

}

int Atrac2::AddStreamData(u32 bytesToAdd) {
	return 0;
}

u32 Atrac2::AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) {
	return 0;
}

int Atrac2::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) {
	*delay = false;
	return 0;
}

int Atrac2::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) {
	return 0;
}

int Atrac2::GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const {
	return 0;
}

int Atrac2::SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, int outputChannels) {
	if (readSize == bufferSize) {
		bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	} else {
		bufferState_ = ATRAC_STATUS_HALFWAY_BUFFER;
	}
	return 0;
}

u32 Atrac2::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	return 0;
}

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {

	return 0;
}

u32 Atrac2::GetNextSamples() {
	return 0;
}

void Atrac2::InitLowLevel(u32 paramsAddr, bool jointStereo, int codecType) {
}

int Atrac2::DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) {
	return 0;
}
