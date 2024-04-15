#include "Common/Log.h"
#include "AtracCtx2.h"

void Atrac2::DoState(PointerWrap &p) {
	_assert_msg_(false, "Savestates not yet support with new Atrac implementation");
}

void Atrac2::WriteContextToPSPMem() {

}

int Atrac2::Analyze(u32 addr, u32 size) {
	return 0;
}
int Atrac2::AnalyzeAA3(u32 addr, u32 size, u32 filesize) {
	return 0;
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

void Atrac2::SetLoopNum(int loopNum) {

}

u32 Atrac2::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	return 0;
}

void Atrac2::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) {

}

int Atrac2::SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) {
	return 0;
}

u32 Atrac2::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	return 0;
}

int Atrac2::GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) {
	return 0;
}

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	return 0;
}

u32 Atrac2::GetNextSamples() {
	return 0;
}

void Atrac2::InitLowLevel(u32 paramsAddr, bool jointStereo) {

}
