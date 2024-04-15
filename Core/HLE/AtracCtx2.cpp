#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
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

void Track::DoState(PointerWrap &p) {
	auto s = p.Section("Track", 1, 0);
	if (!s)
		return;

	Do(p, codecType);
	Do(p, fileSize);
	Do(p, channels);
	// This both does and doesn't belong in Track - it's fixed for an Atrac instance. Oh well.
	Do(p, bytesPerFrame);
	Do(p, firstSampleOffset);
	Do(p, endSample);
	// Offset of the first sample in the input buffer
	Do(p, dataByteOffset);
	Do(p, bitrate);
	Do(p, jointStereo);
	Do(p, channels);

	Do(p, loopinfo);
	Do(p, loopStartSample);
	Do(p, loopEndSample);
}

void Atrac2::DoState(PointerWrap &p) {
	auto s = p.Section("Atrac2", 1, 0);
	if (!s)
		return;

	_assert_msg_(false, "Savestates not yet support with new Atrac implementation.\n\nTurn it off in Developer settings.\n\n");

	track_.DoState(p);

	Do(p, bufAddr_);
	Do(p, bufSize_);
	Do(p, bufValidBytes_);
	Do(p, bufPos_);
	Do(p, currentSample_);
}

void Atrac2::WriteContextToPSPMem() {
	if (!context_.IsValid()) {
		return;
	}
	// context points into PSP memory.
	SceAtracContext *context = context_;
	context->info.buffer = bufAddr_;
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
	track_ = {};
	track_.AnalyzeReset();
	int retval = AnalyzeAtracTrack(addr, size, &track_);
	if (retval < 0) {
		return retval;
	}
	bufAddr_ = addr;
	bufSize_ = size;
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
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Meaning, infinite I guess?  We've got it all.
		return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;
	}
	return 0;
}

void Atrac2::GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) {
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Nothing to do. Let's just return a zero-sized range from the start of the buffer.
		*writePtr = bufAddr_;
		*writableBytes = 0;
		*readOffset = 0;
		return;
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		_dbg_assert_(bufSize_ == track_.fileSize);
		*writePtr = bufAddr_ + bufValidBytes_;
		*writableBytes = bufSize_ - bufValidBytes_;
		*readOffset = bufValidBytes_;
		return;
	}

	// OK, now the tricky ones.
	_dbg_assert_msg_(false, "handle streaming info");
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
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		bufferInfo->first.writePosPtr = bufAddr_;
		// Everything is loaded, so nothing needs to be read.
		bufferInfo->first.writableBytes = 0;
		bufferInfo->first.minWriteBytes = 0;
		bufferInfo->first.filePos = 0;
	}
}

int Atrac2::SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) {
	// buffer/bufferSize are a little redundant here since we already got them from Analyze.
	// So let's just assert equality.
	_dbg_assert_(bufAddr_ == buffer);
	_dbg_assert_(bufSize_ == bufferSize);

	if (bufferSize >= track_.fileSize) {
		if (readSize < bufferSize) {
			bufferState_ = ATRAC_STATUS_HALFWAY_BUFFER;
		} else {
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		if (track_.loopEndSample <= 0) {
			// There's no looping, but we need to stream the data in our buffer.
			bufferState_ = ATRAC_STATUS_STREAMED_WITHOUT_LOOP;
			WARN_LOG(Log::ME, "Streaming detected - no loop");
		} else if (track_.loopEndSample == track_.endSample + track_.FirstSampleOffsetFull()) {
			bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
			WARN_LOG(Log::ME, "Streaming detected - loop from end");
		} else {
			bufferState_ = ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
			WARN_LOG(Log::ME, "Streaming detected - loop with trailer");
		}
	}

	bufPos_ = track_.dataByteOffset;
	outputChannels_ = outputChannels;

	CreateDecoder();
	return hleLogSuccessI(Log::ME, successCode);
}

u32 Atrac2::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	return 0;
}

int Atrac2::SecondBufferSize() const {
	return 0;
}

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	const u32 srcFrameAddr = bufAddr_ + bufPos_;
	const u8 *srcFrame = Memory::GetPointer(srcFrameAddr);

	int bytesConsumed = 0;
	int outSamplesWritten = 0;
	decoder_->Decode(srcFrame, track_.bytesPerFrame, &bytesConsumed, outputChannels_, (int16_t *)outbuf, &outSamplesWritten);

	// DEBUG_LOG(ME, "bufPos: %d currentSample: %d", bufPos_, currentSample_);

	int samplesWritten = outSamplesWritten;
	*SamplesNum = samplesWritten;

	// Track source byte position
	bufPos_ += track_.bytesPerFrame;

	// Track sample position and loops
	currentSample_ += samplesWritten;
	if (currentSample_ >= track_.loopEndSample && LoopNum() != 0) {
		// TODO: Cut down *SamplesNum?
		DEBUG_LOG(Log::ME, "sceAtrac: Hit loop (%d) at %d (currentSample_ == %d), seeking to sample %d", loopNum_, track_.loopEndSample, currentSample_, track_.loopStartSample);

		// Figure out where to seek given loopStartSample
		currentSample_ = track_.loopStartSample;
		bufPos_ = track_.dataByteOffset + (currentSample_ / track_.SamplesPerFrame()) * track_.bytesPerFrame;

		if (bufferState_ != ATRAC_STATUS_FOR_SCESAS) {
			if (loopNum_ > 0)
				loopNum_--;
		}
	}

	if (bufPos_ >= bufSize_) {
		DEBUG_LOG(Log::ME, "sceAtrac: Hit end of input buffer (%d >= %d)! LoopNum=%d. Setting finish=1. currentSample: %d loopEndSample: %d", bufPos_, bufSize_, loopNum_, currentSample_, track_.loopEndSample);
		*finish = 1;
	} else {
		*finish = 0;
	}
	*remains = RemainingFrames();

	if (MemBlockInfoDetailed()) {
		int outBytesWritten = outSamplesWritten * outputChannels_ * sizeof(int16_t);
		char tagData[128];
		size_t tagSize = FormatMemWriteTagAt(tagData, sizeof(tagData), "AtracDecode/", outbufPtr, outBytesWritten);
		NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytesWritten, tagData, tagSize);
	}

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
