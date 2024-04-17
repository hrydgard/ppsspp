// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and https://www.ppsspp.org/.

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"

#include "Common/Log.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/AtracCtx2.h"
#include "Core/HW/Atrac3Standalone.h"
#include "Core/HLE/sceKernelMemory.h"

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
	_assert_msg_(false, "Savestates not yet support with new Atrac implementation.\n\nTurn it off in Developer settings.\n\n");
	auto s = p.Section("Atrac2", 1, 0);
	if (!s)
		return;

	track_.DoState(p);
	// TODO
}

void Atrac2::WriteContextToPSPMem() {
	if (!context_.IsValid()) {
		return;
	}
	// context points into PSP memory.
	SceAtracContext *context = context_;
	context->info.buffer = first_.addr;
	context->info.bufferByte = bufferMaxSize_;
	context->info.secondBuffer = second_.addr;
	context->info.secondBufferByte = second_.size;
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
	context->info.curOff = first_.fileoffset;
	context->info.decodePos = track_.DecodePosBySample(currentSample_);
	context->info.streamDataByte = first_.size - track_.dataByteOffset;

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
	first_.addr = addr;
	first_.size = size;
	track_.DebugLog();
	return 0;
}

int Atrac2::AnalyzeAA3(u32 addr, u32 size, u32 filesize) {
	int retval = AnalyzeAA3Track(addr, size, filesize, &track_);
	if (retval < 0) {
		return retval;
	}
	first_.addr = addr;
	first_.size = size;
	track_.DebugLog();
	return 0;
}

int Atrac2::RemainingFrames() const {
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Meaning, infinite I guess?  We've got it all.
		return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;
	}

	u32 currentFileOffset = track_.FileOffsetBySample(currentSample_ - track_.SamplesPerFrame() + track_.FirstOffsetExtra());
	if (first_.fileoffset >= track_.fileSize) {
		if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
			return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		int loopEndAdjusted = track_.loopEndSample - track_.FirstOffsetExtra() - track_.firstSampleOffset;
		if (bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && currentSample_ > loopEndAdjusted) {
			// No longer looping in this case, outside the loop.
			return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK && loopNum_ == 0) {
			return PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY;
		}
	}

	if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
		// Since we're streaming, the remaining frames are what's valid in the buffer.
		return bufferValidBytes_ / track_.bytesPerFrame;
	}

	// Since the first frame is shorter by this offset, add to round up at this offset.
	const int remainingBytes = first_.fileoffset - currentFileOffset;
	if (remainingBytes < 0) {
		// Just in case.  Shouldn't happen, but once did by mistake.
		return 0;
	}
	return remainingBytes / track_.bytesPerFrame;
}

u32 Atrac2::SecondBufferSize() const {
	return second_.size;
}

void Atrac2::GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) {

}

void Atrac2::CalculateStreamInfo(u32 *outReadOffset) {
	u32 readOffset = first_.fileoffset;
	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Nothing to write.
		readOffset = 0;
		first_.offset = 0;
		first_.writableBytes = 0;
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// If we're buffering the entire file, just give the same as readOffset.
		first_.offset = readOffset;
		// In this case, the bytes writable are just the remaining bytes, always.
		first_.writableBytes = track_.fileSize - readOffset;
	} else {
		u32 bufferEnd = StreamBufferEnd();
		u32 bufferValidExtended = bufferPos_ + bufferValidBytes_;
		if (bufferValidExtended < bufferEnd) {
			first_.offset = bufferValidExtended;
			first_.writableBytes = bufferEnd - bufferValidExtended;
		} else {
			u32 bufferStartUsed = bufferValidExtended - bufferEnd;
			first_.offset = bufferStartUsed;
			first_.writableBytes = bufferPos_ - bufferStartUsed;
		}

		if (readOffset >= track_.fileSize) {
			if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
				// We don't need anything more, so all 0s.
				readOffset = 0;
				first_.offset = 0;
				first_.writableBytes = 0;
			} else {
				readOffset = track_.FileOffsetBySample(track_.loopStartSample - track_.FirstSampleOffsetFull() - track_.SamplesPerFrame() * 2);
			}
		}

		if (readOffset + first_.writableBytes > track_.fileSize) {
			// Never ask for past the end of file, even when the space is free.
			first_.writableBytes = track_.fileSize - readOffset;
		}

		// If you don't think this should be here, remove it.  It's just a temporary safety check.
		if (first_.offset + first_.writableBytes > bufferMaxSize_) {
			ERROR_LOG(Log::ME, "Somehow calculated too many writable bytes: %d + %d > %d", first_.offset, first_.writableBytes, bufferMaxSize_);
			first_.offset = 0;
			first_.writableBytes = bufferMaxSize_;
		}
	}

	if (outReadOffset) {
		*outReadOffset = readOffset;
	}
}

int Atrac2::AddStreamData(u32 bytesToAdd) {
	u32 readOffset;
	CalculateStreamInfo(&readOffset);
	if (bytesToAdd > first_.writableBytes)
		return hleLogWarning(Log::ME, ATRAC_ERROR_ADD_DATA_IS_TOO_BIG, "too many bytes");

	if (bytesToAdd > 0) {
		first_.fileoffset = readOffset;
		int addbytes = std::min(bytesToAdd, track_.fileSize - first_.fileoffset);
		if (!ignoreDataBuf_) {
			Memory::Memcpy(dataBuf_ + first_.fileoffset, first_.addr + first_.offset, addbytes, "AtracAddStreamData");
		}
		first_.fileoffset += addbytes;
	}
	first_.size += bytesToAdd;
	if (first_.size >= track_.fileSize) {
		first_.size = track_.fileSize;
		if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER)
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		WriteContextToPSPMem();
	}

	first_.offset += bytesToAdd;
	bufferValidBytes_ += bytesToAdd;
	return 0;
}

u32 Atrac2::AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) {
	int addbytes = std::min(bytesToAdd, track_.fileSize - first_.fileoffset - track_.FirstOffsetExtra());
	first_.size += bytesToAdd;
	if (first_.size >= track_.fileSize) {
		first_.size = track_.fileSize;
		if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER)
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	}
	first_.fileoffset += addbytes;
	// refresh context_
	WriteContextToPSPMem();
	return 0;
}

u32 Atrac2::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	return 0;
}

void Atrac2::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) {

}

void Atrac2::UpdateBufferState() {
	if (bufferMaxSize_ >= track_.fileSize) {
		if (first_.size < track_.fileSize) {
			// The buffer is big enough, but we don't have all the data yet.
			bufferState_ = ATRAC_STATUS_HALFWAY_BUFFER;
		} else {
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		if (track_.loopEndSample <= 0) {
			// There's no looping, but we need to stream the data in our buffer.
			bufferState_ = ATRAC_STATUS_STREAMED_WITHOUT_LOOP;
		} else if (track_.loopEndSample == track_.endSample + track_.FirstSampleOffsetFull()) {
			bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
		} else {
			bufferState_ = ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
		}
	}
}

void Atrac2::ResetData() {
	delete decoder_;
	decoder_ = nullptr;

	if (dataBuf_)
		delete[] dataBuf_;
	dataBuf_ = 0;
	ignoreDataBuf_ = false;
	bufferState_ = ATRAC_STATUS_NO_DATA;

	if (context_.IsValid())
		kernelMemory.Free(context_.ptr);
}

int Atrac2::SetData(u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, int successCode) {
	outputChannels_ = outputChannels;

	first_.addr = buffer;
	first_.size = readSize;

	if (first_.size > track_.fileSize)
		first_.size = track_.fileSize;
	first_.fileoffset = first_.size;

	// got the size of temp buf, and calculate offset
	bufferMaxSize_ = bufferSize;
	first_.offset = first_.size;

	// some games may reuse an atracID for playing sound
	ResetData();
	UpdateBufferState();

	if (track_.codecType != PSP_MODE_AT_3 && track_.codecType != PSP_MODE_AT_3_PLUS) {
		// Shouldn't have gotten here, Analyze() checks this.
		bufferState_ = ATRAC_STATUS_NO_DATA;
		return hleReportError(Log::ME, ATRAC_ERROR_UNKNOWN_FORMAT, "unexpected codec type in set data");
	}

	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED || bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// This says, don't use the dataBuf_ array, use the PSP RAM.
		// This way, games can load data async into the buffer, and it still works.
		// TODO: Support this always, even for streaming.
		ignoreDataBuf_ = true;
	}
	if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP || bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END || bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		bufferHeaderSize_ = track_.dataByteOffset;
		// Seems we skip the first frame here? Usually the firstSampleOffset is set to match.
		bufferPos_ = track_.dataByteOffset + track_.bytesPerFrame;
		bufferValidBytes_ = first_.size - bufferPos_;
	}

	const char *codecName = track_.codecType == PSP_MODE_AT_3 ? "atrac3" : "atrac3+";
	const char *channelName = track_.channels == 1 ? "mono" : "stereo";

	CreateDecoder();
	return hleLogSuccessInfoI(Log::ME, successCode, "%s %s audio", codecName, channelName);
}

u32 Atrac2::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	u32 secondFileOffset = track_.FileOffsetBySample(track_.loopEndSample - track_.firstSampleOffset);
	u32 desiredSize = track_.fileSize - secondFileOffset;

	// 3 seems to be the number of frames required to handle a loop.
	if (secondBufferSize < desiredSize && secondBufferSize < (u32)track_.BytesPerFrame() * 3) {
		return hleReportError(Log::ME, ATRAC_ERROR_SIZE_TOO_SMALL, "too small");
	}
	if (BufferState() != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		return hleReportError(Log::ME, ATRAC_ERROR_SECOND_BUFFER_NOT_NEEDED, "not needed");
	}

	second_.addr = secondBuffer;
	second_.size = secondBufferSize;
	second_.fileoffset = secondFileOffset;
	return hleLogSuccessI(Log::ME, 0);
}

// It seems like the PSP aligns the sample position to 0x800...?
u32 Atrac2::StreamBufferEnd() const {
	// The buffer is always aligned to a frame in size, not counting an optional header.
	// The header will only initially exist after the data is first set.
	u32 framesAfterHeader = (bufferMaxSize_ - bufferHeaderSize_) / track_.bytesPerFrame;
	return framesAfterHeader * track_.bytesPerFrame + bufferHeaderSize_;
}

u32 Atrac2::GetNextSamples() {
	// This is a repeat of similar logic in DecodeData, since we need to compute the same thing. Should share it.

	// It seems like the PSP aligns the sample position to 0x800...?
	u32 skipSamples = track_.FirstSampleOffsetFull();
	u32 firstSamples = (track_.SamplesPerFrame() - skipSamples) % track_.SamplesPerFrame();
	u32 numSamples = track_.endSample + 1 - currentSample_;
	if (currentSample_ == 0 && firstSamples != 0) {
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

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	int loopNum = loopNum_;
	if (bufferState_ == ATRAC_STATUS_FOR_SCESAS) {
		// TODO: Might need more testing.
		loopNum = 0;
	}

	// We already passed the end - return an error (many games check for this.)
	if (currentSample_ >= track_.endSample && loopNum == 0) {
		*SamplesNum = 0;
		*finish = 1;
		// refresh context_
		WriteContextToPSPMem();
		return ATRAC_ERROR_ALL_DATA_DECODED;
	}

	// TODO: This isn't at all right, but at least it makes the music "last" some time.
	u32 numSamples = 0;

	// Same logic as GetNextSamples, should unify.
	int offsetSamples = track_.FirstSampleOffsetFull();
	int skipSamples = 0;
	u32 maxSamples = track_.endSample + 1 - currentSample_;
	u32 unalignedSamples = (offsetSamples + currentSample_) % track_.SamplesPerFrame();
	if (unalignedSamples != 0) {
		// We're off alignment, possibly due to a loop.  Force it back on.
		maxSamples = track_.SamplesPerFrame() - unalignedSamples;
		skipSamples = unalignedSamples;
	}

	if (skipSamples != 0 && bufferHeaderSize_ == 0) {
		// Skip the initial frame used to load state for the looped frame.
		// TODO: We will want to actually read this in.
		// TODO again: This seems to happen on the first frame of playback regardless of loops.
		// Can't be good.
		ConsumeFrame();
	}

	// SeekToSample(currentSample_);

	bool gotFrame = false;
	u32 off = track_.FileOffsetBySample(currentSample_ - skipSamples);
	if (off < first_.size) {
		const u32 srcFrameAddr = first_.addr + bufferPos_;
		const u8 *srcFrame = Memory::GetPointer(srcFrameAddr);

		uint8_t *indata = BufferStart() + off;
		int bytesConsumed = 0;
		int outSamples = 0;
		if (!decoder_->Decode(indata, track_.bytesPerFrame, &bytesConsumed, outputChannels_, (int16_t *)outbuf, &outSamples)) {
			// Decode failed.
			*SamplesNum = 0;
			*finish = 1;
			return ATRAC_ERROR_ALL_DATA_DECODED;
		}
		int outBytes = outSamples * outputChannels_ * sizeof(int16_t);
		gotFrame = true;

		numSamples = outSamples;
		uint32_t packetAddr = CurBufferAddress(-skipSamples);
		// got a frame
		int skipped = std::min((u32)skipSamples, numSamples);
		skipSamples -= skipped;
		numSamples = numSamples - skipped;
		// If we're at the end, clamp to samples we want.  It always returns a full chunk.
		numSamples = std::min(maxSamples, numSamples);

		if (packetAddr != 0 && MemBlockInfoDetailed()) {
			char tagData[128];
			size_t tagSize = FormatMemWriteTagAt(tagData, sizeof(tagData), "AtracDecode/", packetAddr, track_.bytesPerFrame);
			NotifyMemInfo(MemBlockFlags::READ, packetAddr, track_.bytesPerFrame, tagData, tagSize);
			NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, tagData, tagSize);
		} else {
			NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, "AtracDecode");
		}
		// We only want one frame per call, let's continue the next time.
	}

	if (!gotFrame && currentSample_ < track_.endSample) {
		// Never got a frame.  We may have dropped a GHA frame or otherwise have a bug.
		// For now, let's try to provide an extra "frame" if possible so games don't infinite loop.
		if (track_.FileOffsetBySample(currentSample_) < track_.fileSize) {
			numSamples = std::min(maxSamples, track_.SamplesPerFrame());
			u32 outBytes = numSamples * outputChannels_ * sizeof(s16);
			if (outbuf != nullptr) {
				memset(outbuf, 0, outBytes);
				NotifyMemInfo(MemBlockFlags::WRITE, outbufPtr, outBytes, "AtracDecode");
			}
		}
	}

	*SamplesNum = numSamples;
	// update current sample and decodePos
	currentSample_ += numSamples;
	decodePos_ = track_.DecodePosBySample(currentSample_);

	ConsumeFrame();

	int finishFlag = 0;
	// TODO: Verify.
	bool hitEnd = currentSample_ >= track_.endSample || (numSamples == 0 && first_.size >= track_.fileSize);
	int loopEndAdjusted = track_.loopEndSample - track_.FirstSampleOffsetFull();
	if ((hitEnd || currentSample_ > loopEndAdjusted) && loopNum != 0) {
		SeekToSample(track_.loopStartSample - track_.FirstSampleOffsetFull());
		if (bufferState_ != ATRAC_STATUS_FOR_SCESAS) {
			if (loopNum_ > 0)
				loopNum_--;
		}
		if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
			// Whatever bytes we have left were added from the loop.
			u32 loopOffset = track_.FileOffsetBySample(track_.loopStartSample - track_.FirstSampleOffsetFull() - track_.SamplesPerFrame() * 2);
			// TODO: Hmm, need to manage the buffer better.  But don't move fileoffset if we already have valid data.
			if (loopOffset > first_.fileoffset || loopOffset + bufferValidBytes_ < first_.fileoffset) {
				// Skip the initial frame at the start.
				first_.fileoffset = track_.FileOffsetBySample(track_.loopStartSample - track_.FirstSampleOffsetFull() - track_.SamplesPerFrame() * 2);
			}
		}
	} else if (hitEnd) {
		finishFlag = 1;

		// Still move forward, so we know that we've read everything.
		// This seems to be reflected in the context as well.
		currentSample_ += track_.SamplesPerFrame() - numSamples;
	}

	*finish = finishFlag;
	*remains = RemainingFrames();
	// refresh context_
	WriteContextToPSPMem();
	return 0;
}

void Atrac2::ConsumeFrame() {
	bufferPos_ += track_.bytesPerFrame;
	if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
		if (bufferValidBytes_ > track_.bytesPerFrame) {
			bufferValidBytes_ -= track_.bytesPerFrame;
		} else {
			bufferValidBytes_ = 0;
		}
	}

	u32 end = StreamBufferEnd();
	if (bufferPos_ >= end) {
		// Wrap around... theoretically, this should only happen at exactly StreamBufferEnd.
		if (bufferPos_ > end) {
			WARN_LOG(Log::ME, "ConsumeFrame: Past end");
		}
		bufferPos_ -= StreamBufferEnd();
		bufferHeaderSize_ = 0;
	}
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
