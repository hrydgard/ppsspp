#include "Common/Log.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/AtracCtx2.h"
#include "Core/HW/Atrac3Standalone.h"

// Convenient command line:
// Windows\x64\debug\PPSSPPHeadless.exe  --root pspautotests/tests/../ -o --compare --new-atrac --timeout=30 --graphics=software pspautotests/tests/audio/atrac/stream.prx
//
// See the big comment in sceAtrac.cpp for an overview of the different modes of operation.
//
// Tests left to fix:
// - resetpos
// - resetting
// - second/resetting
// - second/setbuffer
// - decode
// - getremainframe  (requires seek)

// To run on the real PSP, without gentest.py:
// cmd1> C:\dev\ppsspp\pspautotests\tests\audio\atrac> make
// cmd2> C:\dev\ppsspp\pspautotests> usbhostfs_pc -b 4000
// cmd3> C:\dev\ppsspp\pspautotests> pspsh -p 4000
// cmd3> > cd host0:/test/audio/atrac
// cmd3> stream.prx
// cmd1> C:\dev\ppsspp\pspautotests\tests\audio\atrac>copy /Y ..\..\..\__testoutput.txt stream.expected
// Then run the test, see above.

// Needs to support negative numbers, and to handle non-powers-of-two.
static int RoundDownToMultiple(int x, int n) {
	return (x % n == 0) ? x : x - (x % n) - (n * (x < 0));
}

Atrac2::Atrac2(int atracID, u32 contextAddr, int codecType) {
	context_ = PSPPointer<SceAtracContext>::Create(contextAddr);
	track_.codecType = codecType;
	context_->info.codec = codecType;
	context_->info.atracID = atracID;
	context_->info.state = ATRAC_STATUS_NO_DATA;
}

void Atrac2::DoState(PointerWrap &p) {
	_assert_msg_(false, "Savestates not yet support with new Atrac implementation.\n\nTurn it off in Developer settings.\n\n");
}

int Atrac2::RemainingFrames() const {
	const SceAtracIdInfo &info = context_->info;

	// Handle the easy cases first.
	switch (info.state) {
	case ATRAC_STATUS_NO_DATA:
	case ATRAC_STATUS_ALL_DATA_LOADED:
		return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;  // Not sure about no data.
	case ATRAC_STATUS_HALFWAY_BUFFER:
	{
		const int fileOffset = info.streamDataByte + info.dataOff;
		if (fileOffset >= info.dataEnd) {
			return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;
		}
		return (fileOffset - info.curOff) / info.sampleSize;
	}
	case ATRAC_STATUS_STREAMED_LOOP_FROM_END:
	case ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER:
	case ATRAC_STATUS_STREAMED_WITHOUT_LOOP:
		// Below logic.
		break;
	default:
		return SCE_ERROR_ATRAC_BAD_ATRACID;
	}

	const int fileOffset = (int)info.curOff + (int)info.streamDataByte;
	const int bytesLeft = (int)info.dataEnd - fileOffset;
	if (bytesLeft == 0) {
		if (info.state == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
			return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
		}
	}

	if ((int)info.decodePos >= track_.endSample) {
		if (info.state == ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
			return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		int loopEndAdjusted = track_.loopEndSample - track_.FirstOffsetExtra() - track_.firstSampleOffset;
		if (info.state == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && info.decodePos > loopEndAdjusted) {
			// No longer looping in this case, outside the loop.
			return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		if (loopNum_ == 0) {
			return PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		return info.streamDataByte / info.sampleSize;
	}

	// Since we're streaming, the remaining frames are what's valid in the buffer.
	return info.streamDataByte / info.sampleSize;
}

u32 Atrac2::SecondBufferSize() const {
	const SceAtracIdInfo &info = context_->info;
	return info.secondBufferByte;
}

u32 Atrac2::AddStreamDataSas(u32 bufPtr, u32 bytesToAdd) {
	_dbg_assert_(false);
	return 0;
}

int Atrac2::GetSoundSample(int *endSample, int *loopStartSample, int *loopEndSample) const {
	*endSample = track_.endSample;
	*loopStartSample = track_.loopStartSample == -1 ? -1 : track_.loopStartSample - track_.FirstSampleOffsetFull();
	*loopEndSample = track_.loopEndSample == -1 ? -1 : track_.loopEndSample - track_.FirstSampleOffsetFull();
	return 0;
}

int Atrac2::ResetPlayPosition(int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) {
	*delay = false;

	// This was mostly copied straight from the old impl.

	// Reuse the same calculation as before.
	AtracResetBufferInfo bufferInfo;
	GetResetBufferInfo(&bufferInfo, sample);

	if ((u32)bytesWrittenFirstBuf < bufferInfo.first.minWriteBytes || (u32)bytesWrittenFirstBuf > bufferInfo.first.writableBytes) {
		return SCE_ERROR_ATRAC_BAD_FIRST_RESET_SIZE;
	}
	if ((u32)bytesWrittenSecondBuf < bufferInfo.second.minWriteBytes || (u32)bytesWrittenSecondBuf > bufferInfo.second.writableBytes) {
		return SCE_ERROR_ATRAC_BAD_SECOND_RESET_SIZE;
	}

	const SceAtracIdInfo &info = context_->info;
	if (info.state == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Always adds zero bytes.
	} else if (info.state == ATRAC_STATUS_HALFWAY_BUFFER) {
		/*
		// Okay, it's a valid number of bytes.  Let's set them up.
		if (bytesWrittenFirstBuf != 0) {
			first_.fileoffset += bytesWrittenFirstBuf;
			first_.size += bytesWrittenFirstBuf;
			first_.offset += bytesWrittenFirstBuf;
		}

		// Did we transition to a full buffer?
		if (first_.size >= track_.fileSize) {
			first_.size = track_.fileSize;
			bufferState_ = ATRAC_STATUS_ALL_DATA_LOADED;
		}
		*/
	} else {
		if (bufferInfo.first.filePos > track_.fileSize) {
			*delay = true;
			return SCE_ERROR_ATRAC_API_FAIL;
		}

		/*

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
		*/
	}

	_dbg_assert_(track_.codecType == PSP_MODE_AT_3 || track_.codecType == PSP_MODE_AT_3_PLUS);
	SeekToSample(sample);

	return 0;
}

void Atrac2::SeekToSample(int sample) {
	// This was mostly copied straight from the old impl.

	SceAtracIdInfo &info = context_->info;

	// It seems like the PSP aligns the sample position to 0x800...?
	const u32 offsetSamples = track_.FirstSampleOffsetFull();
	const u32 unalignedSamples = (offsetSamples + sample) % track_.SamplesPerFrame();
	int seekFrame = sample + offsetSamples - unalignedSamples;

	if ((sample != info.decodePos || sample == 0) && decoder_ != nullptr) {
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
			decoder_->Decode(Memory::GetPointer(info.buffer + pos), track_.bytesPerFrame, nullptr, 2, nullptr, nullptr);
		}
	}

	// Probably more stuff that needs updating!
	info.decodePos = sample;
}

// This is basically sceAtracGetBufferInfoForResetting.
int Atrac2::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) {
	const SceAtracIdInfo &info = context_->info;

	if (info.state == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && info.secondBufferByte == 0) {
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_SECOND_BUFFER_NEEDED, "no second buffer");
	} else if ((u32)sample + track_.firstSampleOffset > (u32)track_.endSample + track_.firstSampleOffset) {
		// NOTE: Above we have to add firstSampleOffset to both sides - we seem to rely on wraparound.
		return hleLogWarning(Log::ME, SCE_ERROR_ATRAC_BAD_SAMPLE, "invalid sample position");
	}

	if (info.state == ATRAC_STATUS_ALL_DATA_LOADED) {
		bufferInfo->first.writePosPtr = info.buffer;
		// Everything is loaded, so nothing needs to be read.
		bufferInfo->first.writableBytes = 0;
		bufferInfo->first.minWriteBytes = 0;
		bufferInfo->first.filePos = 0;
	} else if (info.state == ATRAC_STATUS_HALFWAY_BUFFER) {
		// The old logic here was busted. This, instead, appears to just replicate GetStreamDataInfo.
		GetStreamDataInfo(&bufferInfo->first.writePosPtr, &bufferInfo->first.writableBytes, &bufferInfo->first.filePos);
		bufferInfo->first.minWriteBytes = 0;
	} else {
		// This is without the sample offset.  The file offset also includes the previous batch of samples?
		int sampleFileOffset = track_.FileOffsetBySample(sample - track_.firstSampleOffset - track_.SamplesPerFrame());

		// Update the writable bytes.  When streaming, this is just the number of bytes until the end.
		const u32 bufSizeAligned = RoundDownToMultiple(info.bufferByte, track_.bytesPerFrame);
		const int needsMoreFrames = track_.FirstOffsetExtra();  // ?

		bufferInfo->first.writePosPtr = info.buffer;
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

		if (info.secondBufferByte != 0) {
			// TODO: We have a second buffer.  Within it, minWriteBytes should be zero.
			// The filePos should be after the end of the second buffer (or zero.)
			// We actually need to ensure we READ from the second buffer before implementing that.
		}
	}

	// Reset never needs a second buffer write, since the loop is in a fixed place.
	// It seems like second.writePosPtr is always the same as the first buffer's pos, weirdly (doesn't make sense).
	bufferInfo->second.writePosPtr = info.buffer;
	bufferInfo->second.writableBytes = 0;
	bufferInfo->second.minWriteBytes = 0;
	bufferInfo->second.filePos = 0;

	return 0;
}

int Atrac2::SetLoopNum(int loopNum) {
	SceAtracIdInfo &info = context_->info;
	if (info.loopEnd <= 0) {
		return SCE_ERROR_ATRAC_NO_LOOP_INFORMATION;
	}
	info.loopNum = loopNum;
	return 0;
}

u32 Atrac2::GetNextSamples() {
	SceAtracIdInfo &info = context_->info;
	// TODO: Handle end-of-track short block.
	int samples = track_.SamplesPerFrame();
	if (discardedSamples_ > 0) {
		samples -= discardedSamples_;
	}
	return samples;
}

int Atrac2::GetNextDecodePosition(int *pos) const {
	const SceAtracIdInfo &info = context_->info;
	const int currentSample = info.decodePos - track_.FirstSampleOffsetFull();
	if (currentSample >= track_.endSample) {
		*pos = 0;
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}
	*pos = currentSample;
	return 0;
}

int Atrac2::AddStreamData(u32 bytesToAdd) {
	SceAtracIdInfo &info = context_->info;

	if (info.state == ATRAC_STATUS_HALFWAY_BUFFER) {
		const int newFileOffset = info.streamDataByte + info.dataOff + bytesToAdd;
		if (newFileOffset == info.dataEnd) {
			info.state = ATRAC_STATUS_ALL_DATA_LOADED;
		} else if (newFileOffset > info.dataEnd) {
			return SCE_ERROR_ATRAC_ADD_DATA_IS_TOO_BIG;
		}
		info.streamDataByte += bytesToAdd;
	} else {
		info.streamDataByte += bytesToAdd;
	}
	return 0;
}

void Atrac2::GetStreamDataInfo(u32 *writePtr, u32 *bytesToRead, u32 *readFileOffset) {
	const SceAtracIdInfo &info = context_->info;

	switch (info.state) {
	case ATRAC_STATUS_ALL_DATA_LOADED:
		// Nothing to do, the whole track is loaded already.
		*writePtr = info.buffer;
		*bytesToRead = 0;
		*readFileOffset = 0;
		break;

	case ATRAC_STATUS_HALFWAY_BUFFER:
	{
		// This is both the file offset and the offset in the buffer, since it's direct mapped
		// in this mode (no wrapping or any other trickery).
		const int fileOffset = (int)info.dataOff + (int)info.streamDataByte;
		const int bytesLeftInFile = (int)info.dataEnd - fileOffset;

		if (bytesLeftInFile == 0) {
			// We've got all the data, no more loading is needed.
			// Signalling this by setting everything to default.
			*writePtr = info.buffer;
			*bytesToRead = 0;
			*readFileOffset = 0;
			return;
		}
		// Just ask for the rest of the data. The game can supply as much of it as it wants at a time.
		*writePtr = info.buffer + fileOffset;
		*readFileOffset = fileOffset;
		*bytesToRead = bytesLeftInFile;
		break;
	}

	default:
	{
		// Streaming
		//
		// This really is the core logic of sceAtrac. It looks simple, and is pretty simple, but figuring it out
		// from just logs of variables wasn't all that easy... Initially I had it more complicated, but boiled it
		// all down to fairly simple logic.
		//
		// TODO: Take care of loop points.

		const int fileOffset = (int)info.curOff + (int)info.streamDataByte;
		const int bytesLeftInFile = (int)info.dataEnd - fileOffset;

		_dbg_assert_(bytesLeftInFile >= 0);

		if (bytesLeftInFile == 0) {
			// We've got all the data up to the end buffered, no more streaming is needed.
			// Signalling this by setting everything to default.
			*writePtr = info.buffer;
			*bytesToRead = 0;
			*readFileOffset = 0;
			return;
		}

		// Last allowed offset where a packet can start.
		const int lastPacketStart = info.bufferByte - info.sampleSize;  // Last possible place in the buffer to put the next packet

		// writePos is where the next write should be done in order to append to the circular buffer.
		// First frame after SetData, this will be equal to the buffer size, and the partial packet at the end
		// will actually be located at the start of the buffer (see the end of SetData for how it gets moved).
		const int writePos = info.streamOff + info.streamDataByte;
		if (writePos > lastPacketStart) {
			// The buffered data wraps around. This also applies at the first frame after SetData (SetData wraps the partial packet at the end).
			// Figure out how much space we are using at the end, rounded down to even packets.
			const int firstPart = RoundDownToMultiple(info.bufferByte - info.streamOff, info.sampleSize);
			const int secondPart = info.streamDataByte - firstPart;
			// OK, now to compute how much space we have left to fill.
			_dbg_assert_(secondPart <= info.streamOff);
			const int spaceLeft = info.streamOff - secondPart;

			*writePtr = info.buffer + secondPart;
			*bytesToRead = std::min(spaceLeft, bytesLeftInFile);
		} else {
			// In case the buffer is only partially filled at the start, we can be off packet alignment here!
			// But otherwise, we normally are actually on alignment.
			int size = info.streamOff + RoundDownToMultiple(info.bufferByte - info.streamOff, info.sampleSize);
			const int spaceLeft = size - writePos;

			*writePtr = info.buffer + writePos;
			*bytesToRead = std::min(spaceLeft, bytesLeftInFile);
		}

		*readFileOffset = fileOffset;
		break;
	}
	}
}

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	SceAtracIdInfo &info = context_->info;

	int samplesToWrite = track_.SamplesPerFrame();
	if (discardedSamples_) {
		_dbg_assert_(samplesToWrite >= discardedSamples_);
		samplesToWrite -= discardedSamples_;
		discardedSamples_ = 0;
	}

	// Try to handle the end, too.
	// NOTE: This should match GetNextSamples().
	if (info.decodePos + samplesToWrite > info.endSample + 1) {
		int samples = info.endSample + 1 - info.decodePos;
		if (samples < track_.SamplesPerFrame()) {
			samplesToWrite = samples;
		} else {
			ERROR_LOG(Log::ME, "Too many samples left: %08x", samples);
		}
	}

	if (info.decodePos >= info.endSample) {
		_dbg_assert_(info.curOff >= info.dataEnd);
		ERROR_LOG(Log::ME, "DecodeData: Reached the end, nothing to decode");
		*finish = 1;
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}

	// Check that there's enough data to decode.
	if (AtracStatusIsStreaming(info.state)) {
		if (info.streamDataByte < track_.bytesPerFrame) {
			// Seems some games actually check for this in order to refill, instead of relying on remainFrame. Pretty dumb. See #5564
			ERROR_LOG(Log::ME, "Half-way: Ran out of data to decode from");
			return SCE_ERROR_ATRAC_BUFFER_IS_EMPTY;
		}
	} else if (info.state == ATRAC_STATUS_HALFWAY_BUFFER) {
		const int fileOffset = info.streamDataByte + info.dataOff;
		if (info.curOff + track_.bytesPerFrame > fileOffset) {
			ERROR_LOG(Log::ME, "Half-way: Ran out of data to decode from");
			return SCE_ERROR_ATRAC_BUFFER_IS_EMPTY;
		}
	}

	u32 inAddr = info.buffer + info.streamOff;
	context_->codec.inBuf = inAddr;  // just because.
	int bytesConsumed = 0;
	int outSamples = 0;
	if (!decoder_->Decode(Memory::GetPointer(inAddr), track_.bytesPerFrame, &bytesConsumed, outputChannels_, decodeTemp_, &outSamples)) {
		// Decode failed.
		*SamplesNum = 0;
		*finish = 0;
		context_->codec.err = 0x20b;  // checked on hardware for 0xFF corruption. it's possible that there are more codes.
		return SCE_ERROR_ATRAC_API_FAIL;  // tested.
	}

	// Write the decoded samples to memory.
	// TODO: We can detect cases where we can safely just decode directly into output (full samplesToWrite, outbuf != nullptr)
	if (outbuf) {
		memcpy(outbuf, decodeTemp_, samplesToWrite * outputChannels_ * sizeof(int16_t));
	}

	if (AtracStatusIsStreaming(info.state)) {
		info.streamDataByte -= info.sampleSize;
		info.streamOff += info.sampleSize;
	}
	info.curOff += info.sampleSize;
	info.decodePos += samplesToWrite;

	// If we reached the end of the buffer, move the cursor back to the start.
	// SetData takes care of any split packet on the first lap (On other laps, no split packets
	// happen).
	if (AtracStatusIsStreaming(info.state) && info.streamOff + info.sampleSize > info.bufferByte) {
		INFO_LOG(Log::ME, "Hit the stream buffer wrap point (decoding).");
		info.streamOff = 0;
	}

	*SamplesNum = samplesToWrite;
	*remains = RemainingFrames();

	// detect the end.
	// We can do this either with curOff/dataEnd or decodePos/endSample, not sure which makes more sense.
	if (info.curOff >= info.dataEnd) {
		*finish = 1;
	}
	context_->codec.err = 0;
	return 0;
}

int Atrac2::SetData(const Track &track, u32 bufferAddr, u32 readSize, u32 bufferSize, int outputChannels) {
	// TODO: Remove track_.
	track_ = track;
	SceAtracIdInfo &info = context_->info;

	if (track_.codecType != PSP_MODE_AT_3 && track_.codecType != PSP_MODE_AT_3_PLUS) {
		// Shouldn't have gotten here, Analyze() checks this.
		ERROR_LOG(Log::ME, "unexpected codec type %d in set data", track_.codecType);
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	if (outputChannels != track_.channels) {
		// TODO: Figure out what this means
		WARN_LOG(Log::ME, "Atrac::SetData: outputChannels %d doesn't match track_.channels %d", outputChannels, track_.channels);
	}

	context_->codec.inBuf = bufferAddr;

	// Copied from the old implementation, let's see where they are useful.
	int firstExtra = track_.FirstOffsetExtra();

	// Copy parameters into struct.
	info.buffer = bufferAddr;
	info.bufferByte = bufferSize;
	info.firstValidSample = track_.FirstSampleOffsetFull();
	info.endSample = track_.endSample + info.firstValidSample;
	if (track_.loopStartSample != 0xFFFFFFFF) {
		info.loopStart = track_.loopStartSample;
		info.loopEnd = track_.loopEndSample;
	}
	info.codec = track_.codecType;
	info.sampleSize = track_.bytesPerFrame;
	info.numChan = track_.channels;
	info.numFrame = 0;
	info.dataOff = track_.dataByteOffset;
	info.curOff = track_.dataByteOffset;  // Note: This and streamOff get incremented by bytesPerFrame before the return from this function by skipping frames.
	info.streamOff = track_.dataByteOffset;
	info.streamDataByte = readSize - track_.dataByteOffset;
	info.dataEnd = track_.fileSize;
	info.decodePos = track_.FirstSampleOffsetFull();
	discardedSamples_ = track_.FirstSampleOffsetFull();

	if (readSize > track_.fileSize) {
		WARN_LOG(Log::ME, "readSize %d > track_.fileSize", readSize, track_.fileSize);
		readSize = track_.fileSize;
	}

	if (bufferSize >= track_.fileSize) {
		// Buffer is big enough to fit the whole track.
		if (readSize < bufferSize) {
			info.state = ATRAC_STATUS_HALFWAY_BUFFER;
		} else {
			info.state = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		// Streaming cases with various looping types.
		if (track_.loopEndSample <= 0) {
			// There's no looping, but we need to stream the data in our buffer.
			info.state = ATRAC_STATUS_STREAMED_WITHOUT_LOOP;
		} else if (track_.loopEndSample == track_.endSample + track_.FirstSampleOffsetFull()) {
			info.state = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
		} else {
			info.state = ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
		}
	}

	CreateDecoder();

	if (!decodeTemp_) {
		decodeTemp_ = new int16_t[track_.SamplesPerFrame() * track_.channels];
	}

	// TODO: Decode/discard any first dummy frames to the temp buffer. This initializes the decoder.
	// It really does seem to be what's happening here, as evidenced by inBuf in the codec struct - it gets initialized.
	// Alternatively, the dummy frame is just there to leave space for wrapping...
	while (discardedSamples_ >= track_.SamplesPerFrame()) {
		int bytesConsumed;
		int outSamples;
		if (!decoder_->Decode(Memory::GetPointer(info.buffer + info.streamOff), info.sampleSize, &bytesConsumed, track_.channels, decodeTemp_, &outSamples)) {
			ERROR_LOG(Log::ME, "Error decoding the 'dummy' buffer at offset %d in the buffer", info.streamOff);
		}
		info.curOff += track_.bytesPerFrame;
		if (AtracStatusIsStreaming(info.state)) {
			info.streamOff += track_.bytesPerFrame;
			info.streamDataByte -= info.sampleSize;
		}
		discardedSamples_ -= outSamples;
	}

	// We need to handle wrapping the overshot partial packet at the end.
	if (AtracStatusIsStreaming(info.state)) {
		// This logic is similar to GetStreamDataInfo.
		const int lastPacketStart = info.bufferByte - info.sampleSize;  // Last possible place in the buffer to put the next packet
		const int writePos = info.streamOff + info.streamDataByte;
		if (writePos > lastPacketStart) {
			// curOff also works (instead of streamOff) of course since it's also packet aligned, unlike the buffer size.
			const int cutLen = (info.bufferByte - info.streamOff) % info.sampleSize;
			const int cutRest = info.sampleSize - cutLen;
			_dbg_assert_(cutLen > 0);
			// Then, let's copy it.
			INFO_LOG(Log::ME, "Streaming: Packets didn't fit evenly. Last packet got split into %d/%d (sum=%d). Copying to start of buffer.", cutLen, cutRest, cutLen, cutRest, info.sampleSize);
			Memory::Memcpy(info.buffer, info.buffer + info.bufferByte - cutLen, cutLen);
		} else {
			INFO_LOG(Log::ME, "Streaming: Packets fit into the buffer fully. %08x < %08x", readSize, bufferSize);
			// In this case, seems we need to zero some bytes. In testing, this seems to be 336.
			Memory::Memset(info.buffer, 0, 128);
		}
	}

	return 0;
}

u32 Atrac2::GetInternalCodecError() const {
	if (context_.IsValid()) {
		return context_->codec.err;
	} else {
		return 0;
	}
}

u32 Atrac2::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	return 0;	
}

int Atrac2::Bitrate() const {
	const SceAtracIdInfo &info = context_->info;

	int bitrate = (info.sampleSize * 352800) / 1000;
	if (info.codec == PSP_MODE_AT_3_PLUS)
		bitrate = ((bitrate >> 11) + 8) & 0xFFFFFFF0;
	else
		bitrate = (bitrate + 511) >> 10;
	return bitrate;
}

void Atrac2::InitLowLevel(u32 paramsAddr, bool jointStereo, int codecType) {
	track_ = Track();
	track_.codecType = codecType;
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

	context_->info.decodePos = 0;
	context_->info.state = ATRAC_STATUS_LOW_LEVEL;
	CreateDecoder();
}

int Atrac2::DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) {
	return 0;
}
