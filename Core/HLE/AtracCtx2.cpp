#include "Common/Log.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/AtracCtx2.h"
#include "Core/HW/Atrac3Standalone.h"

// Convenient command line:
// Windows\x64\debug\PPSSPPHeadless.exe  --root pspautotests/tests/../ --compare --timeout=5 --new-atrac --graphics=software pspautotests/tests/audio/atrac/decode.prx

// See the big comment in sceAtrac.cpp for an overview of the different modes of operation.
//
// Test cases
//
// Halfway buffer
//
// * None found yet
//
// All-data-loaded
//
// * MotoGP (menu music with specified loop). Simple repeated calls to sceAtracDecodeData
// * Archer MacLean's Mercury (in-game, not menu)
// * Crisis Core
//
// Streaming
//
// - Good ones (early)
//   * Everybody's Golf 2 (0x2000 buffer size, loop from end)
//   * Burnout Legends (no loop, 0x1800 buffer size)
//   * Suicide Barbie
// - Others
//   * Bleach
//   * God of War: Chains of Olympus
//   * Ape Academy 2 (bufsize 8192)
//   * Half Minute Hero (bufsize 65536)
//   * Flatout (tricky! needs investigation)

void Atrac2::DoState(PointerWrap &p) {
	_assert_msg_(false, "Savestates not yet support with new Atrac implementation.\n\nTurn it off in Developer settings.\n\n");
}

void Atrac2::WriteContextToPSPMem() {
	if (!context_.IsValid()) {
		return;
	}
	// context points into PSP memory.
	SceAtracContext *context = context_;
	context->info.buffer = 0; // bufferAddr_; // first_.addr;
	context->info.bufferByte = 0; // bufferMaxSize_;
	context->info.secondBuffer = 0;  // TODO
	context->info.secondBufferByte = 0;  // TODO
	context->info.codec = track_.codecType;
	context->info.loopNum = loopNum_;
	context->info.loopStart = track_.loopStartSample > 0 ? track_.loopStartSample : 0;
	context->info.loopEnd = track_.loopEndSample > 0 ? track_.loopEndSample : 0;

	// Note that we read in the state when loading the atrac object, so it's safe
	// to update it back here all the time.  Some games, like Sol Trigger, change it.
	// TODO: Should we just keep this in PSP ram then, or something?
	context->info.state = bufferState_;
	if (track_.firstSampleOffset != 0) {
		context->info.samplesPerChan = track_.FirstSampleOffsetFull();
	} else {
		context->info.samplesPerChan = (track_.codecType == PSP_MODE_AT_3_PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
	}
	context->info.sampleSize = track_.bytesPerFrame;
	context->info.numChan = track_.channels;
	context->info.dataOff = track_.dataByteOffset;
	context->info.endSample = track_.endSample + track_.FirstSampleOffsetFull();
	context->info.dataEnd = track_.fileSize;
	context->info.curOff = 0; // first_.fileoffset;
	context->info.decodePos = track_.DecodePosBySample(currentSample_);
	context->info.streamDataByte = 0; // first_.size - track_.dataOff;

	u8 *buf = (u8 *)context;
	*(u32_le *)(buf + 0xfc) = atracID_;

	NotifyMemInfo(MemBlockFlags::WRITE, context_.ptr, sizeof(SceAtracContext), "AtracContext");
}

int Atrac2::Analyze(u32 addr, u32 size) {
	int retval = AnalyzeAtracTrack(addr, size, &track_);
	if (retval < 0) {
		return retval;
	}
	return 0;
}
int Atrac2::AnalyzeAA3(u32 addr, u32 size, u32 filesize) {
	int retval = AnalyzeAA3Track(addr, size, filesize, &track_);
	if (retval < 0) {
		return retval;
	}

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

u32 Atrac2::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	return 0;
}

void Atrac2::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) {

}

int Atrac2::SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) {
	if (readSize == bufferSize) {
		bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	} else {
		bufferState_ = ATRAC_STATUS_HALFWAY_BUFFER;
	}
	return hleLogSuccessI(Log::ME, successCode);
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

void Atrac2::InitLowLevel(u32 paramsAddr, bool jointStereo) {
	track_.AnalyzeReset();
	track_.channels = Memory::Read_U32(paramsAddr);
	outputChannels_ = Memory::Read_U32(paramsAddr + 4);
	track_.bytesPerFrame = Memory::Read_U32(paramsAddr + 8);
	if (track_.codecType == PSP_MODE_AT_3) {
		track_.bitrate = (track_.bytesPerFrame * 352800) / 1000;
		track_.bitrate = (track_.bitrate + 511) >> 10;
		track_.jointStereo = false;
	} else if (track_.codecType == PSP_MODE_AT_3_PLUS) {
		track_.bitrate = (track_.bytesPerFrame * 352800) / 1000;
		track_.bitrate = ((track_.bitrate >> 11) + 8) & 0xFFFFFFF0;
		track_.jointStereo = false;
	}
	track_.dataByteOffset = 0;
	bufferState_ = ATRAC_STATUS_LOW_LEVEL;
	currentSample_ = 0;
	CreateDecoder();
	WriteContextToPSPMem();
}
