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
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "Core/MemMapHelpers.h"
#include "Core/System.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceAtrac.h"
#include "Core/HLE/AtracCtx.h"
#include "Core/HW/Atrac3Standalone.h"
#include "Core/HLE/sceKernelMemory.h"


const size_t overAllocBytes = 16384;

Atrac::~Atrac() {
	ResetData();
}

void Atrac::DoState(PointerWrap &p) {
	auto s = p.Section("Atrac", 1, 9);
	if (!s)
		return;

	Do(p, track_.channels);
	Do(p, outputChannels_);
	if (s >= 5) {
		Do(p, track_.jointStereo);
	}

	Do(p, atracID_);
	if (p.mode != p.MODE_READ) {
		first_._filesize_dontuse = track_.fileSize;
	}
	Do(p, first_);
	if (p.mode == p.MODE_READ) {
		track_.fileSize = first_._filesize_dontuse;
	}

	Do(p, bufferMaxSize_);
	Do(p, track_.codecType);

	Do(p, currentSample_);
	Do(p, track_.endSample);
	Do(p, track_.firstSampleOffset);
	if (s >= 3) {
		Do(p, track_.dataByteOffset);
	} else {
		track_.dataByteOffset = track_.firstSampleOffset;
	}

	u32 hasDataBuf = dataBuf_ != nullptr;
	Do(p, hasDataBuf);
	if (hasDataBuf) {
		if (p.mode == p.MODE_READ) {
			if (dataBuf_)
				delete[] dataBuf_;
			dataBuf_ = new u8[track_.fileSize + overAllocBytes];
			memset(dataBuf_, 0, track_.fileSize + overAllocBytes);
		}
		DoArray(p, dataBuf_, track_.fileSize);
	}
	Do(p, second_);

	Do(p, decodePos_);
	if (s < 9) {
		u32 oldDecodeEnd = 0;
		Do(p, oldDecodeEnd);
	}
	if (s >= 4) {
		Do(p, bufferPos_);
	} else {
		bufferPos_ = decodePos_;
	}

	Do(p, track_.bitrate);
	Do(p, track_.bytesPerFrame);

	Do(p, track_.loopinfo);
	if (s < 9) {
		int oldLoopInfoNum = 42;
		Do(p, oldLoopInfoNum);
	}

	Do(p, track_.loopStartSample);
	Do(p, track_.loopEndSample);
	Do(p, loopNum_);

	Do(p, context_);
	if (s >= 6) {
		Do(p, bufferState_);
	} else {
		if (dataBuf_ == nullptr) {
			bufferState_ = ATRAC_STATUS_NO_DATA;
		} else {
			UpdateBufferState();
		}
	}

	if (s >= 7) {
		Do(p, ignoreDataBuf_);
	} else {
		ignoreDataBuf_ = false;
	}

	if (s >= 9) {
		Do(p, bufferValidBytes_);
		Do(p, bufferHeaderSize_);
	} else {
		bufferHeaderSize_ = track_.dataByteOffset;
		bufferValidBytes_ = std::min(first_.size - track_.dataByteOffset, StreamBufferEnd() - track_.dataByteOffset);
		if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
			bufferPos_ = track_.dataByteOffset;
		}
	}

	if (s < 8 && bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		// We didn't actually allow the second buffer to be set this far back.
		// Pretend it's a regular loop, we'll just try our best.
		bufferState_ = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
	}

	// Make sure to do this late; it depends on track parameters.
	if (p.mode == p.MODE_READ && bufferState_ != ATRAC_STATUS_NO_DATA) {
		CreateDecoder(track_.codecType, track_.bytesPerFrame, track_.channels);
	}

	if (s >= 2 && s < 9) {
		bool oldResetBuffer = false;
		Do(p, oldResetBuffer);
	}
}

void Atrac::ResetData() {
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

u8 *Atrac::BufferStart() {
	return ignoreDataBuf_ ? Memory::GetPointerWrite(first_.addr) : dataBuf_;
}

AtracBase::~AtracBase() {
	delete decoder_;
}

void Atrac::UpdateContextFromPSPMem() {
	if (!context_.IsValid()) {
		return;
	}

	// Read in any changes from the game to the context.
	// TODO: Might be better to just always track in RAM. Actually, Atrac2 will do that.
	bufferState_ = context_->info.state;
	// This value is actually abused by games to store the SAS voice number.
	loopNum_ = context_->info.loopNum;
}

void Atrac::WriteContextToPSPMem() {
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
		context->info.firstValidSample = track_.FirstSampleOffsetFull();
	} else {
		context->info.firstValidSample = (track_.codecType == PSP_CODEC_AT3PLUS ? ATRAC3PLUS_MAX_SAMPLES : ATRAC3_MAX_SAMPLES);
	}
	context->info.sampleSize = track_.bytesPerFrame;
	context->info.numChan = track_.channels;
	context->info.dataOff = track_.dataByteOffset;
	context->info.endSample = track_.endSample + track_.FirstSampleOffsetFull();
	context->info.fileDataEnd = track_.fileSize;
	context->info.curFileOff = first_.fileoffset;
	context->info.decodePos = track_.DecodePosBySample(currentSample_);
	context->info.streamDataByte = first_.size - track_.dataByteOffset;

	u8 *buf = (u8 *)context;
	*(u32_le *)(buf + 0xfc) = atracID_;

	NotifyMemInfo(MemBlockFlags::WRITE, context_.ptr, sizeof(SceAtracContext), "AtracContext");
}

void Track::DebugLog() const {
	DEBUG_LOG(Log::Atrac, "ATRAC analyzed: %s channels: %d filesize: %d bitrate: %d kbps jointStereo: %d",
		codecType == PSP_CODEC_AT3 ? "AT3" : "AT3Plus", channels, fileSize, bitrate / 1024, jointStereo);
	DEBUG_LOG(Log::Atrac, "dataoff: %d firstSampleOffset: %d endSample: %d", dataByteOffset, firstSampleOffset, endSample);
	DEBUG_LOG(Log::Atrac, "loopStartSample: %d loopEndSample: %d", loopStartSample, loopEndSample);
	DEBUG_LOG(Log::Atrac, "sampleSize: %d (%03x)", bytesPerFrame, bytesPerFrame);
}

int Atrac::GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const {
	*endSample = track_.endSample;
	*loopStartSample = track_.loopStartSample == -1 ? -1 : track_.loopStartSample - track_.FirstSampleOffsetFull();
	*loopEndSample = track_.loopEndSample == -1 ? -1 : track_.loopEndSample - track_.FirstSampleOffsetFull();
	return 0;
}

int Atrac::GetNextDecodePosition(int *pos) const {
	if (currentSample_ >= track_.endSample) {
		*pos = 0;
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	} else {
		*pos = currentSample_;
		return 0;
	}
}

void Atrac::CalculateStreamInfo(u32 *outReadOffset) {
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
			ERROR_LOG_REPORT(Log::Atrac, "Somehow calculated too many writable bytes: %d + %d > %d", first_.offset, first_.writableBytes, bufferMaxSize_);
			first_.offset = 0;
			first_.writableBytes = bufferMaxSize_;
		}
	}

	if (outReadOffset) {
		*outReadOffset = readOffset;
	}
}

void AtracBase::CreateDecoder(int codecType, int bytesPerFrame, int channels) {
	if (decoder_) {
		delete decoder_;
	}

	// First, init the standalone decoder.
	if (codecType == PSP_CODEC_AT3) {
		// TODO: This is maybe not entirely reliable? Mui Mui house in LocoRoco 2 fails. Although also fails
		// when I override this, so maybe the issue is something different...
		bool jointStereo = IsAtrac3StreamJointStereo(codecType, bytesPerFrame, channels);

		// We don't pull this from the RIFF so that we can support OMA also.
		uint8_t extraData[14]{};
		// The only thing that changes are the jointStereo_ values.
		extraData[0] = 1;
		extraData[3] = channels << 3;
		extraData[6] = jointStereo;
		extraData[8] = jointStereo;
		extraData[10] = 1;
		decoder_ = CreateAtrac3Audio(channels, bytesPerFrame, extraData, sizeof(extraData));
	} else {
		decoder_ = CreateAtrac3PlusAudio(channels, bytesPerFrame);
	}
}

int Atrac::GetBufferInfoForResetting(AtracResetBufferInfo *bufferInfo, int sample, bool *delay) {
	*delay = false;
	if (BufferState() == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && !HasSecondBuffer()) {
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NEEDED;
	} else if ((u32)sample + track_.firstSampleOffset > (u32)track_.endSample + track_.firstSampleOffset) {
		// NOTE: Above we have to add firstSampleOffset to both sides - we seem to rely on wraparound.
		return SCE_ERROR_ATRAC_BAD_SAMPLE;
	}

	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		bufferInfo->first.writePosPtr = first_.addr;
		// Everything is loaded, so nothing needs to be read.
		bufferInfo->first.writableBytes = 0;
		bufferInfo->first.minWriteBytes = 0;
		bufferInfo->first.filePos = 0;
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// Here the message is: you need to read at least this many bytes to get to that position.
		// This is because we're filling the buffer start to finish, not streaming.
		bufferInfo->first.writePosPtr = first_.addr + first_.size;
		bufferInfo->first.writableBytes = track_.fileSize - first_.size;
		int minWriteBytes = track_.FileOffsetBySample(sample) - first_.size;
		if (minWriteBytes > 0) {
			bufferInfo->first.minWriteBytes = minWriteBytes;
		} else {
			bufferInfo->first.minWriteBytes = 0;
		}
		bufferInfo->first.filePos = first_.size;
	} else {
		// This is without the sample offset.  The file offset also includes the previous batch of samples?
		int sampleFileOffset = track_.FileOffsetBySample(sample - track_.firstSampleOffset - track_.SamplesPerFrame());

		// Update the writable bytes.  When streaming, this is just the number of bytes until the end.
		const u32 bufSizeAligned = (bufferMaxSize_ / track_.bytesPerFrame) * track_.bytesPerFrame;
		const int needsMoreFrames = track_.FirstOffsetExtra();  // ?

		bufferInfo->first.writePosPtr = first_.addr;
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

		if (second_.size != 0) {
			// TODO: We have a second buffer.  Within it, minWriteBytes should be zero.
			// The filePos should be after the end of the second buffer (or zero.)
			// We actually need to ensure we READ from the second buffer before implementing that.
		}
	}

	// It seems like this is always the same as the first buffer's pos, weirdly.
	bufferInfo->second.writePosPtr = first_.addr;
	// Reset never needs a second buffer write, since the loop is in a fixed place.
	bufferInfo->second.writableBytes = 0;
	bufferInfo->second.minWriteBytes = 0;
	bufferInfo->second.filePos = 0;
	return 0;
}

int Atrac::SetData(const Track &track, u32 buffer, u32 readSize, u32 bufferSize, int outputChannels) {
	// 72 is about the size of the minimum required data to even be valid.
	if (readSize < 72) {
		return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
	}

	// TODO: Check the range (addr, size) instead.
	if (!Memory::IsValidAddress(buffer)) {
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	first_ = {};
	first_.addr = buffer;
	first_.size = readSize;

	currentSample_ = 0;
	loopNum_ = 0;
	decodePos_ = 0;
	bufferPos_ = 0;
	outputChannels_ = outputChannels;

	track.DebugLog();
	track_ = track;
	first_._filesize_dontuse = track_.fileSize;

	if (outputChannels != track_.channels) {
		WARN_LOG(Log::Atrac, "Atrac::SetData: outputChannels %d doesn't match track_.channels %d", outputChannels, track_.channels);
	}

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

	if (track_.codecType != PSP_CODEC_AT3 && track_.codecType != PSP_CODEC_AT3PLUS) {
		// Shouldn't have gotten here, Analyze() checks this.
		bufferState_ = ATRAC_STATUS_NO_DATA;
		ERROR_LOG(Log::Atrac, "unexpected codec type %d in set data", track_.codecType);
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED || bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// This says, don't use the dataBuf_ array, use the PSP RAM.
		// This way, games can load data async into the buffer, and it still works.
		// TODO: Support this always, even for streaming.
		ignoreDataBuf_ = true;
	}
	if (bufferState_ == ATRAC_STATUS_STREAMED_WITHOUT_LOOP || bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END || bufferState_ == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		bufferHeaderSize_ = track_.dataByteOffset;
		bufferPos_ = track_.dataByteOffset + track_.bytesPerFrame;
		bufferValidBytes_ = first_.size - bufferPos_;
	}

	const char *codecName = track_.codecType == PSP_CODEC_AT3 ? "atrac3" : "atrac3+";
	const char *channelName = track_.channels == 1 ? "mono" : "stereo";

	// Over-allocate databuf to prevent going off the end if the bitstream is bad or if there are
	// bugs in the decoder. This happens, see issue #15788. Arbitrary, but let's make it a whole page on the popular
	// architecture that has the largest pages (M1).
	dataBuf_ = new u8[track_.fileSize + overAllocBytes];
	memset(dataBuf_, 0, track_.fileSize + overAllocBytes);
	if (!ignoreDataBuf_) {
		u32 copybytes = std::min(bufferSize, track_.fileSize);
		Memory::Memcpy(dataBuf_, buffer, copybytes, "AtracSetData");
	}
	CreateDecoder(track.codecType, track.bytesPerFrame, track.channels);
	INFO_LOG(Log::Atrac, "Atrac::SetData (buffer=%08x, readSize=%d, bufferSize=%d): %s %s (%d channels) audio", buffer, readSize, bufferSize, codecName, channelName, track_.channels);
	INFO_LOG(Log::Atrac, "BufferState: %s", AtracStatusToString(bufferState_));
	INFO_LOG(Log::Atrac,
		"buffer: %08x bufferSize: %d readSize: %d bufferPos: %d\n",
		buffer, bufferSize, readSize, bufferPos_
	);

	if (track_.channels == 2 && outputChannels == 1) {
		// We still do all the tasks, we just return this error.
		WARN_LOG(Log::Atrac, "Tried to load a stereo track into a mono context, returning NOT_MONO");
		return SCE_ERROR_ATRAC_NOT_MONO;
	}
	return 0;
}

int Atrac::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	u32 secondFileOffset = track_.FileOffsetBySample(track_.loopEndSample - track_.firstSampleOffset);
	u32 desiredSize = track_.fileSize - secondFileOffset;

	// 3 seems to be the number of frames required to handle a loop.
	if (secondBufferSize < desiredSize && secondBufferSize < (u32)track_.BytesPerFrame() * 3) {
		return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
	}
	if (BufferState() != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NOT_NEEDED;
	}

	second_.addr = secondBuffer;
	second_.size = secondBufferSize;
	second_.fileoffset = secondFileOffset;
	return 0;
}

int Atrac::GetSecondBufferInfo(u32 *fileOffset, u32 *desiredSize) const {
	if (BufferState() != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		// Writes zeroes in this error case.
		*fileOffset = 0;
		*desiredSize = 0;
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NOT_NEEDED;
	}

	*fileOffset = track_.FileOffsetBySample(track_.loopEndSample - track_.firstSampleOffset);
	*desiredSize = track_.fileSize - *fileOffset;
	return 0;
}

void Atrac::GetStreamDataInfo(u32 *writePtr, u32 *writableBytes, u32 *readOffset) {
	u32 calculatedReadOffset;
	CalculateStreamInfo(&calculatedReadOffset);

	*writePtr = first_.addr + first_.offset;
	*writableBytes = first_.writableBytes;
	*readOffset = calculatedReadOffset;
}

void Atrac::UpdateBufferState() {
	if (bufferMaxSize_ >= track_.fileSize) {
		if (first_.size < track_.fileSize) {
			// The buffer is big enough in RAM, but we don't have all the data yet.
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

// The game calls this after actually writing data to the buffer, as specified by the return values from GetStreamDataInfo.
// So, we should not have to call CalculateStreamInfo again here (although, might not be a bad idea for safety).
int Atrac::AddStreamData(u32 bytesToAdd) {
	u32 readOffset;
	CalculateStreamInfo(&readOffset);
	if (bytesToAdd > first_.writableBytes)
		return SCE_ERROR_ATRAC_ADD_DATA_IS_TOO_BIG;

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

	if (PSP_CoreParameter().compat.flags().AtracLoopHack && bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END && RemainingFrames() > 2) {
		loopNum_++;
		SeekToSample(track_.loopStartSample - track_.FirstSampleOffsetFull());
	}

	return 0;
}

u32 Atrac::GetNextSamples() {
	if (currentSample_ >= track_.endSample) {
		return 0;
	}

	// It seems like the PSP aligns the sample position to 0x800...?
	int skipSamples = track_.FirstSampleOffsetFull();
	int firstSamples = (track_.SamplesPerFrame() - skipSamples) % track_.SamplesPerFrame();
	int numSamples = track_.endSample + 1 - currentSample_;
	if (currentSample_ == 0 && firstSamples != 0) {
		numSamples = firstSamples;
	}
	int unalignedSamples = (skipSamples + currentSample_) % track_.SamplesPerFrame();
	if (unalignedSamples != 0) {
		// We're off alignment, possibly due to a loop.  Force it back on.
		numSamples = track_.SamplesPerFrame() - unalignedSamples;
	}
	if (numSamples > track_.SamplesPerFrame())
		numSamples = track_.SamplesPerFrame();
	if (bufferState_ == ATRAC_STATUS_STREAMED_LOOP_FROM_END && (int)numSamples + currentSample_ > track_.endSample) {
		// This probably only happens in PPSSPP due to our internal buffer, which needs to go away.
		bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
	}
	return numSamples;
}

void Atrac::ForceSeekToSample(int sample) {
	if (decoder_) {
		decoder_->FlushBuffers();
	}
	currentSample_ = sample;
}

void Atrac::SeekToSample(int sample) {
	// It seems like the PSP aligns the sample position to 0x800...?
	const u32 offsetSamples = track_.FirstSampleOffsetFull();
	const u32 unalignedSamples = (offsetSamples + sample) % track_.SamplesPerFrame();
	int seekFrame = sample + offsetSamples - unalignedSamples;

	if ((sample != currentSample_ || sample == 0) && decoder_ != nullptr) {
		// Prefill the decode buffer with packets before the first sample offset.
		decoder_->FlushBuffers();

		int adjust = 0;
		if (sample == 0) {
			int offsetSamples = track_.FirstSampleOffsetFull();
			adjust = -(int)(offsetSamples % track_.SamplesPerFrame());
		}
		const u32 off = track_.FileOffsetBySample(sample + adjust);
		const u32 backfill = track_.bytesPerFrame * 2;
		const u32 start = off - track_.dataByteOffset < backfill ? track_.dataByteOffset : off - backfill;

		for (u32 pos = start; pos < off; pos += track_.bytesPerFrame) {
			decoder_->Decode(BufferStart() + pos, track_.bytesPerFrame, nullptr, 2, nullptr, nullptr);
		}
	}

	currentSample_ = sample;
}

int Atrac::RemainingFrames() const {
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

void Atrac::ConsumeFrame() {
	bufferPos_ += track_.bytesPerFrame;
	if ((bufferState_ & ATRAC_STATUS_STREAMED_MASK) == ATRAC_STATUS_STREAMED_MASK) {
		if (bufferValidBytes_ > track_.bytesPerFrame) {
			bufferValidBytes_ -= track_.bytesPerFrame;
		} else {
			bufferValidBytes_ = 0;
		}
	}
	if (bufferPos_ >= StreamBufferEnd()) {
		// Wrap around... theoretically, this should only happen at exactly StreamBufferEnd.
		bufferPos_ -= StreamBufferEnd();
		bufferHeaderSize_ = 0;
	}
}

u32 Atrac::DecodeData(u8 *outbuf, u32 outbufPtr, int *SamplesNum, int *finish, int *remains) {
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
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}

	int numSamples = 0;

	// It seems like the PSP aligns the sample position to 0x800...?
	int offsetSamples = track_.FirstSampleOffsetFull();
	int skipSamples = 0;
	int maxSamples = track_.endSample + 1 - currentSample_;
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
		// Actually, this is explained now if we look at AtracCtx2, although this isn't really accurate.
		DEBUG_LOG(Log::Atrac, "Calling ConsumeFrame to skip the initial frame");
		ConsumeFrame();
	}

	SeekToSample(currentSample_);

	bool gotFrame = false;
	u32 off = track_.FileOffsetBySample(currentSample_ - skipSamples);

	DEBUG_LOG(Log::Atrac, "Decode(%08x): nextFileOff: %d", outbufPtr, off);

	if (off < first_.size) {
		uint8_t *indata = BufferStart() + off;
		int bytesConsumed = 0;
		int outSamples = track_.SamplesPerFrame();
		int outBytes = outSamples * outputChannels_ * sizeof(int16_t);
		gotFrame = true;

		numSamples = outSamples;
		uint32_t packetAddr = CurBufferAddress(-skipSamples);
		// got a frame
		int skipped = std::min(skipSamples, numSamples);
		skipSamples -= skipped;
		numSamples = numSamples - skipped;
		// If we're at the end, clamp to samples we want.  It always returns a full chunk.
		numSamples = std::min(maxSamples, numSamples);

		outSamples = numSamples;
		if (!decoder_->Decode(indata, track_.bytesPerFrame, &bytesConsumed, outputChannels_, (int16_t *)outbuf, &outSamples)) {
			// Decode failed.
			*SamplesNum = 0;
			*finish = 1;
			return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
		}

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
		if (track_.FileOffsetBySample(currentSample_) < (int)track_.fileSize) {
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
	if (remains) {
		*remains = RemainingFrames();
	}
	// refresh context_
	WriteContextToPSPMem();
	return 0;
}

int Atrac::SetLoopNum(int loopNum) {
	if (track_.loopinfo.size() == 0) {
		return SCE_ERROR_ATRAC_NO_LOOP_INFORMATION;
	}

	// Spammed in MHU
	loopNum_ = loopNum;
	// Logic here looks wacky?
	if (loopNum != 0 && track_.loopinfo.size() == 0) {
		// Just loop the whole audio
		// This is a rare modification of track_ after the fact.
		// Maybe we can get away with setting these by default.
		track_.loopStartSample = track_.FirstSampleOffsetFull();
		track_.loopEndSample = track_.endSample + track_.FirstSampleOffsetFull();
	}
	WriteContextToPSPMem();
	return 0;
}

int Atrac::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) {
	*delay = false;

	if (BufferState() == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && !HasSecondBuffer()) {
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NEEDED;
	} else if ((u32)sample + track_.firstSampleOffset > (u32)track_.endSample + track_.firstSampleOffset) {
		// NOTE: Above we have to add firstSampleOffset to both sides - we seem to rely on wraparound.
		return SCE_ERROR_ATRAC_BAD_SAMPLE;
	}

	// Reuse the same calculation as before.
	AtracResetBufferInfo bufferInfo;
	bool ignored;
	GetBufferInfoForResetting(&bufferInfo, sample, &ignored);

	if ((u32)bytesWrittenFirstBuf < bufferInfo.first.minWriteBytes || (u32)bytesWrittenFirstBuf > bufferInfo.first.writableBytes) {
		return SCE_ERROR_ATRAC_BAD_FIRST_RESET_SIZE;
	}
	if ((u32)bytesWrittenSecondBuf < bufferInfo.second.minWriteBytes || (u32)bytesWrittenSecondBuf > bufferInfo.second.writableBytes) {
		return SCE_ERROR_ATRAC_BAD_SECOND_RESET_SIZE;
	}

	if (bufferState_ == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Always adds zero bytes.
	} else if (bufferState_ == ATRAC_STATUS_HALFWAY_BUFFER) {
		// Okay, it's a valid number of bytes.  Let's set them up.
		if (bytesWrittenFirstBuf != 0) {
			if (!ignoreDataBuf_) {
				Memory::Memcpy(dataBuf_ + first_.size, first_.addr + first_.size, bytesWrittenFirstBuf, "AtracResetPlayPosition");
			}
			first_.fileoffset += bytesWrittenFirstBuf;
			first_.size += bytesWrittenFirstBuf;
			first_.offset += bytesWrittenFirstBuf;
		}

		// Did we transition to a full buffer?
		if (first_.size >= track_.fileSize) {
			first_.size = track_.fileSize;
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		if (bufferInfo.first.filePos > track_.fileSize) {
			*delay = true;
			// The decoder failed during skip-frame operation.
			return SCE_ERROR_ATRAC_API_FAIL;
		}

		// Move the offset to the specified position.
		first_.fileoffset = bufferInfo.first.filePos;

		if (bytesWrittenFirstBuf != 0) {
			if (!ignoreDataBuf_) {
				Memory::Memcpy(dataBuf_ + first_.fileoffset, first_.addr, bytesWrittenFirstBuf, "AtracResetPlayPosition");
			}
			first_.fileoffset += bytesWrittenFirstBuf;
		}
		first_.size = first_.fileoffset;
		first_.offset = bytesWrittenFirstBuf;

		bufferHeaderSize_ = 0;
		bufferPos_ = track_.bytesPerFrame;
		bufferValidBytes_ = bytesWrittenFirstBuf - bufferPos_;
	}

	if (track_.codecType == PSP_CODEC_AT3 || track_.codecType == PSP_CODEC_AT3PLUS) {
		SeekToSample(sample);
	}

	WriteContextToPSPMem();
	return 0;
}

void Atrac::InitLowLevel(const Atrac3LowLevelParams &params, int codecType) {
	track_ = Track();
	track_.codecType = codecType;
	track_.endSample = 0;
	track_.channels = params.encodedChannels;
	outputChannels_ = params.outputChannels;
	bufferMaxSize_ = params.bytesPerFrame;
	track_.bytesPerFrame = bufferMaxSize_;
	first_.writableBytes = track_.bytesPerFrame;
	ResetData();

	if (codecType == PSP_CODEC_AT3) {
		track_.bitrate = (track_.bytesPerFrame * 352800) / 1000;
		track_.bitrate = (track_.bitrate + 511) >> 10;
		track_.jointStereo = IsAtrac3StreamJointStereo(codecType, params.bytesPerFrame, params.encodedChannels);
	} else if (codecType == PSP_CODEC_AT3PLUS) {
		track_.bitrate = (track_.bytesPerFrame * 352800) / 1000;
		track_.bitrate = ((track_.bitrate >> 11) + 8) & 0xFFFFFFF0;
		track_.jointStereo = false;
	} else {
		_dbg_assert_msg_(false, "bad codec type %08x", codecType);
	}

	track_.dataByteOffset = 0;
	first_.size = 0;
	track_.fileSize = track_.bytesPerFrame;  // not really meaningful
	bufferState_ = ATRAC_STATUS_LOW_LEVEL;
	currentSample_ = 0;
	CreateDecoder(codecType, track_.bytesPerFrame, track_.channels);
	WriteContextToPSPMem();
}

int Atrac::DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) {
	const int channels = outputChannels_;
	int outSamples = 0;
	decoder_->Decode(srcData, track_.BytesPerFrame(), bytesConsumed, channels, dstData, &outSamples);
	*bytesWritten = outSamples * channels * sizeof(int16_t);
	// TODO: Possibly return a decode error on bad data.
	return 0;
}

void Atrac::CheckForSas() {
	SetOutputChannels(1);
}

int Atrac::EnqueueForSas(u32 bufPtr, u32 bytesToAdd) {
	int addbytes = std::min(bytesToAdd, track_.fileSize - first_.fileoffset - track_.FirstOffsetExtra());
	Memory::Memcpy(dataBuf_ + first_.fileoffset + track_.FirstOffsetExtra(), bufPtr, addbytes, "AtracAddStreamData");
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

void Atrac::DecodeForSas(s16 *dstData, int *bytesWritten, int *finish) {
	// Hack, but works.
	int samplesNum;
	DecodeData((u8 *)dstData, 0, &samplesNum, finish, nullptr);
}

void Atrac::NotifyGetContextAddress() {
	if (!context_.IsValid()) {
		// allocate a new context_
		u32 contextSize = sizeof(SceAtracContext);
		// Note that Alloc can increase contextSize to the "grain" size.
		context_ = kernelMemory.Alloc(contextSize, false, StringFromFormat("AtracCtx/%d", atracID_).c_str());
		if (context_.IsValid())
			Memory::Memset(context_.ptr, 0, contextSize, "AtracContextClear");
		WARN_LOG(Log::Atrac, "%08x=_sceAtracGetContextAddress(%i): allocated new context", context_.ptr, atracID_);
	} else {
		WARN_LOG(Log::Atrac, "%08x=_sceAtracGetContextAddress(%i)", context_.ptr, atracID_);
	}
	WriteContextToPSPMem();
}
