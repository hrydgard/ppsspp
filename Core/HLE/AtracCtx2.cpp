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
//
// Unit tests to pass
//   [x] setdata, ids
//   [x] addstreamdata
//   [ ] decode

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

	// 72 is about the size of the minimum required data to even be valid.
	if (size < 72) {
		return hleReportError(ME, ATRAC_ERROR_SIZE_TOO_SMALL, "buffer too small");
	}

	// TODO: Check the range (addr, size) instead.
	if (!Memory::IsValidAddress(addr)) {
		return hleReportWarning(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "invalid buffer address");
	}

	const int RIFF_CHUNK_MAGIC = 0x46464952;

	if (Memory::ReadUnchecked_U32(addr) != RIFF_CHUNK_MAGIC) {
		return hleReportError(ME, ATRAC_ERROR_UNKNOWN_FORMAT, "invalid RIFF header");
	}

	int retval = AnalyzeAtracTrack(addr, size, &track_);
	if (retval < 0) {
		return retval;
	}

	bufAddr_ = addr;
	bufValidBytes_ = size;
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
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED || bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		_assert_(track_.fileSize == bufSize_);
		if (bytesToAdd > bufSize_ - bufValidBytes_) {
			return hleLogWarning(ME, ATRAC_ERROR_ADD_DATA_IS_TOO_BIG, "buffer already loaded");
		}
		bufValidBytes_ += bytesToAdd;
		if (bufValidBytes_ == bufSize_) {
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
		return 0;
	}

	_dbg_assert_msg_(false, "handle streaming addstreamdata");
	return 0;
}

u32 Atrac2::AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) {
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
	else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// Here the message is: you need to read at least this many bytes to get to that position.
		// This is because we're filling the buffer start to finish, not streaming.
		bufferInfo->first.writePosPtr = bufAddr_ + bufValidBytes_;
		bufferInfo->first.writableBytes = track_.fileSize - bufValidBytes_;
		int minWriteBytes = track_.FileOffsetBySample(sample) - bufValidBytes_;
		if (minWriteBytes > 0) {
			bufferInfo->first.minWriteBytes = minWriteBytes;
		} else {
			bufferInfo->first.minWriteBytes = 0;
		}
		bufferInfo->first.filePos = bufValidBytes_;
	} else {
		// This is without the sample offset.  The file offset also includes the previous batch of samples?
		int sampleFileOffset = track_.FileOffsetBySample(sample - track_.firstSampleOffset - track_.SamplesPerFrame());

		// Update the writable bytes.  When streaming, this is just the number of bytes until the end.
		const u32 bufSizeAligned = (bufSize_ / track_.bytesPerFrame) * track_.bytesPerFrame;
		const int needsMoreFrames = track_.FirstOffsetExtra();

		bufferInfo->first.writePosPtr = bufAddr_;
		bufferInfo->first.writableBytes = std::min(track_.fileSize - sampleFileOffset, bufSizeAligned);
		if (((sample + track_.firstSampleOffset) % (int)track_.SamplesPerFrame()) >= (int)track_.SamplesPerFrame() - needsMoreFrames) {
			// Not clear why, but it seems it wants a bit extra in case the sample is late?
			bufferInfo->first.minWriteBytes = track_.bytesPerFrame * 3;
		} else {
			bufferInfo->first.minWriteBytes = track_.bytesPerFrame * 2;
		}
		if ((u32)sample < (u32)track_.firstSampleOffset && sampleFileOffset != track_.dataByteOffset) {
			sampleFileOffset -= track_.bytesPerFrame;
		}
		bufferInfo->first.filePos = sampleFileOffset;

		if (secondBufSize_ != 0) {
			// TODO: We have a second buffer.  Within it, minWriteBytes should be zero.
			// The filePos should be after the end of the second buffer (or zero.)
			// We actually need to ensure we READ from the second buffer before implementing that.
		}
	}

	// It seems like this is always the same as the first buffer's pos, weirdly.
	// Well, it makes some sense, after the reset we won't need any of the data in the buffer anymore
	// (unless we were playing right from the beginning).
	bufferInfo->second.writePosPtr = bufAddr_;
	// Reset never needs a second buffer write, since the loop is in a fixed place.
	bufferInfo->second.writableBytes = 0;
	bufferInfo->second.minWriteBytes = 0;
	bufferInfo->second.filePos = 0;
}

u32 Atrac2::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	// Reuse the same calculation as before.
	AtracResetBufferInfo bufferInfo;
	GetResetBufferInfo(&bufferInfo, sample);

	if ((u32)bytesWrittenFirstBuf < bufferInfo.first.minWriteBytes || (u32)bytesWrittenFirstBuf > bufferInfo.first.writableBytes) {
		return hleLogError(ME, ATRAC_ERROR_BAD_FIRST_RESET_SIZE, "first byte count not in valid range");
	}
	if ((u32)bytesWrittenSecondBuf < bufferInfo.second.minWriteBytes || (u32)bytesWrittenSecondBuf > bufferInfo.second.writableBytes) {
		return hleLogError(ME, ATRAC_ERROR_BAD_SECOND_RESET_SIZE, "second byte count not in valid range");
	}

	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Always adds zero bytes.
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// Okay, it's a valid number of bytes.  Let's set them up.
		if (bytesWrittenFirstBuf != 0) {
			fileReadOffset_ += bytesWrittenFirstBuf;
			bufValidBytes_ += bytesWrittenFirstBuf;
			bufWriteOffset_ += bytesWrittenFirstBuf;
		}

		// Did we transition to a full buffer?
		if (bufValidBytes_ >= track_.fileSize) {
			bufValidBytes_ = track_.fileSize;
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		if (bufferInfo.first.filePos > track_.fileSize) {
			return hleDelayResult(hleLogError(ME, ATRAC_ERROR_API_FAIL, "invalid file position"), "reset play pos", 200);
		}

		// Move the offset to the specified position.
		fileReadOffset_ = bufferInfo.first.filePos + bytesWrittenFirstBuf;
		bufValidBytes_ = fileReadOffset_;
		bufWriteOffset_ = bytesWrittenFirstBuf;

		streamWrapped_ = true;
		bufPos_ = track_.bytesPerFrame;
		bufValidBytes_ = bytesWrittenFirstBuf - bufPos_;
	}

	if (track_.codecType == PSP_MODE_AT_3 || track_.codecType == PSP_MODE_AT_3_PLUS) {
		// SeekToSample(sample);
	}

	WriteContextToPSPMem();
	return 0;
}

int Atrac2::SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) {
	// buffer/bufferSize are a little redundant here since we already got them from Analyze.
	// So let's just assert equality.
	_dbg_assert_(bufAddr_ == buffer);
	bufSize_ = bufferSize;

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

// Second buffer is only used for tails after a loop, it seems?
u32 Atrac2::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	u32 secondFileOffset = track_.FileOffsetBySample(track_.loopEndSample - track_.firstSampleOffset);
	u32 desiredSize = track_.fileSize - secondFileOffset;
	// 3 seems to be the number of frames required to handle a loop.
	if (secondBufferSize < desiredSize && secondBufferSize < (u32)track_.BytesPerFrame() * 3) {
		return hleReportError(ME, ATRAC_ERROR_SIZE_TOO_SMALL, "too small");
	}
	if (BufferState() != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		return hleReportError(ME, ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED, "not needed");
	}

	// second_.addr = secondBuffer;
	// second_.size = secondBufferSize;
	// second_.fileoffset = secondFileOffset;
	return hleLogSuccessI(ME, 0);
}

int Atrac2::SecondBufferSize() const {
	return 0;
}

// Simple wrapper around decoder->Decode that lets you cut a range out of the output.
// Might move this into the Decoder interface later.
// Uses a small internal buffer.
inline bool DecodeRange(AudioDecoder *decoder, const uint8_t *inbuf, int inbytes, int *inbytesConsumed, int outputChannels, int rangeStart, int rangeEnd, int16_t *outbuf, int *outSamples) {
	int16_t temp[4096];
	int outSamplesTemp = 0;
	bool result = decoder->Decode(inbuf, inbytes, inbytesConsumed, outputChannels, temp, &outSamplesTemp);
	if (!result) {
		*outSamples = 0;
		return false;
	}
	_dbg_assert_(rangeEnd <= outSamplesTemp);
	int bytesPerSample = outputChannels * sizeof(int16_t);
	if (outbuf) {
		memcpy(outbuf, (u8 *)temp + bytesPerSample * rangeStart, bytesPerSample * (rangeEnd - rangeStart));
	}
	if (outSamples) {
		*outSamples = rangeEnd - rangeStart;
	}
	return true;
}

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	// We already passed the end - return an error (many games check for this.)
	if (currentSample_ >= track_.endSample && LoopNum() == 0) {
		*SamplesNum = 0;
		*finish = 1;
		// refresh context_
		WriteContextToPSPMem();
		return ATRAC_ERROR_ALL_DATA_DECODED;
	}

	int bytesConsumed = 0;
	int outSamples = 0;

	// If at the beginning of the track, or near a loop might, might have to cut out a piece of a frame
	// to get ourselves aligned, due to the FirstSampleOffset2 (and/or irregular loop points).
	int rangeStart = 0;
	int rangeEnd = track_.SamplesPerFrame();

	int offsetSamples = track_.FirstSampleOffsetFull();
	int skipSamples = 0;
	u32 unalignedSamples = (offsetSamples + currentSample_) % track_.SamplesPerFrame();
	if (unalignedSamples != 0) {
		// We're off alignment, possibly due to a loop.  Force it back on.
		rangeStart = unalignedSamples;
	}

	const u32 srcFrameAddr = bufAddr_ + bufPos_;
	const u8 *srcFrame = Memory::GetPointer(srcFrameAddr);

	if (!DecodeRange(decoder_, srcFrame, track_.bytesPerFrame, &bytesConsumed, outputChannels_, rangeStart, rangeEnd, (int16_t *)outbuf, &outSamples)) {
		ERROR_LOG(ME, "Atrac decode failed");
	}

	// DEBUG_LOG(ME, "bufPos: %d currentSample: %d", bufPos_, currentSample_);

	*SamplesNum = outSamples;

	// Track source byte position
	bufPos_ += track_.bytesPerFrame;

	// Track sample position and loops
	currentSample_ += outSamples;
	if (LoopNum() != 0 && currentSample_ >= track_.loopEndSample - track_.FirstSampleOffsetFull()) {
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
		const int bytesWritten = outSamples * outputChannels_ * sizeof(int16_t);
		char tagData[128];
		size_t tagSize = FormatMemWriteTagAt(tagData, sizeof(tagData), "AtracDecode/", outbufPtr, bytesWritten);
		NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, bytesWritten, tagData, tagSize);
	}

	return 0;
}

// TODO: We should possibly compute this at the end of Decode?
u32 Atrac2::GetNextSamples() {
	// See how much remains to be decoded of an aligned block.
	u32 skipSamples = track_.FirstSampleOffsetFull();
	u32 firstSamples = (track_.SamplesPerFrame() - skipSamples) % track_.SamplesPerFrame();

	u32 numSamples = track_.endSample + 1 - currentSample_;
	if (currentSample_ == 0) {
		// First frame.
		numSamples = firstSamples;
	}

	u32 unalignedSamples = (skipSamples + currentSample_) % track_.SamplesPerFrame();
	if (unalignedSamples != 0) {
		// We're off alignment, possibly due to a loop.  Force it back on.
		numSamples = track_.SamplesPerFrame() - unalignedSamples;
	}
	if (numSamples > track_.SamplesPerFrame())
		numSamples = track_.SamplesPerFrame();

	if (bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END && (int)numSamples + currentSample_ > track_.endSample) {
		bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	}
	return numSamples;
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
