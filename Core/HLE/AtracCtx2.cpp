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
	context_->info.atracID = atracID;
	context_->info.state = ATRAC_STATUS_NO_DATA;
}

void Atrac2::DoState(PointerWrap &p) {
	_assert_msg_(false, "Savestates not yet support with new Atrac implementation.\n\nTurn it off in Developer settings.\n\n");
}

int Atrac2::RemainingFrames() const {
	const SceAtracIdInfo &info = context_->info;

	if (info.state == ATRAC_STATUS_ALL_DATA_LOADED) {
		// The buffer contains everything.
		return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;
	}

	const bool isStreaming = (info.state & ATRAC_STATUS_STREAMED_MASK) != 0;

	int fileOffset = (int)info.curOff + (int)info.streamDataByte;
	int bytesLeft = (int)info.dataEnd - fileOffset;
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
		if (isStreaming && loopNum_ == 0) {
			return PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY;
		}
		return info.streamDataByte / info.sampleSize;
	}

	if (isStreaming) {
		// Since we're streaming, the remaining frames are what's valid in the buffer.
		return info.streamDataByte / info.sampleSize;
	}

	// Fallback. probably wrong
	return info.dataEnd / info.sampleSize;
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
	return 0;
}

int Atrac2::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int sample) {
	_dbg_assert_(false);
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

int Atrac2::AddStreamData(u32 bytesToAdd) {
	SceAtracIdInfo &info = context_->info;
	// if (bytesToAdd > first_.writableBytes)
	//	return SCE_ERROR_ATRAC_ADD_DATA_IS_TOO_BIG;
	info.streamDataByte += bytesToAdd;
	return 0;
}

void Atrac2::GetStreamDataInfo(u32 *writePtr, u32 *bytesToRead, u32 *readFileOffset) {
	SceAtracIdInfo &info = context_->info;

	// TODO: Take care of looping.
	// Compute some helper variables.
	int fileOffset = (int)info.curOff + (int)info.streamDataByte;
	int bytesLeft = (int)info.dataEnd - fileOffset;
	int bufferSpace = (int)info.bufferByte - (int)info.streamDataByte;

	_dbg_assert_(bytesLeft >= 0);

	if (bytesLeft == 0) {
		switch (info.state) {
		case ATRAC_STATUS_STREAMED_WITHOUT_LOOP:
			// We have all the data up to the end buffered, no more streaming is needed.
			// Signalling this by setting everything to zero.
			*writePtr = info.buffer;
			*bytesToRead = 0;
			*readFileOffset = 0;
			return;
		default:
			ERROR_LOG(Log::ME, "Unhandled case of end of stream");
			*writePtr = info.buffer;
			*bytesToRead = 0;
			*readFileOffset = 0;
			return;
		}
	}

	// If on the first lap, we need some special handling to handle the cut packet.
	if (info.curOff < info.bufferByte) {
		int cutLen = (info.bufferByte - info.curOff) % info.sampleSize;
		*writePtr = info.buffer + cutLen;
		*bytesToRead = std::min(bytesLeft, bufferSpace - cutLen);
	} else {
		*writePtr = info.buffer + info.dataOff;
		*bytesToRead = std::min(bytesLeft, bufferSpace);
	}
	*readFileOffset = fileOffset;

	// Probably we treat
	INFO_LOG(Log::Audio, "asdf");
}

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufPtr, u32 *SamplesNum, u32 *finish, int *remains) {
	SceAtracIdInfo &info = context_->info;

	int samplesToWrite = track_.SamplesPerFrame();
	if (discardedSamples_) {
		samplesToWrite -= discardedSamples_;
		discardedSamples_ = 0;
	}

	// Try to handle the end, too.
	if (info.decodePos + samplesToWrite > info.endSample + 1) {
		int samples = info.endSample + 1 - info.decodePos;
		if (samples < track_.SamplesPerFrame()) {
			samplesToWrite = samples;
		} else {
			ERROR_LOG(Log::ME, "Too many samples left: %08x", samples);
		}
	}

	u32 inAddr = info.buffer + info.streamOff;
	context_->codec.inBuf = inAddr;  // just because.
	int bytesConsumed = 0;
	int outSamples = 0;
	if (!decoder_->Decode(Memory::GetPointer(inAddr), track_.bytesPerFrame, &bytesConsumed, outputChannels_, decodeTemp_, &outSamples)) {
		// Decode failed.
		*SamplesNum = 0;
		*finish = 1;
		// Is this the right error code? Needs testing.
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}

	info.streamDataByte -= info.sampleSize;
	info.streamOff += info.sampleSize;
	info.curOff += info.sampleSize;
	info.decodePos += samplesToWrite;

	// If we reached the end of the buffer, move the cursor back to the start.
	// SetData takes care of any split packet.
	if (info.streamOff + info.sampleSize > info.bufferByte) {
		// Check that we're on the first lap. Should only happen on the first lap around.
		_dbg_assert_(info.curOff - info.sampleSize < info.bufferByte);
		INFO_LOG(Log::ME, "Hit the buffer wrap.");
		info.streamOff = 0;
	}

	*SamplesNum = samplesToWrite;
	*remains = RemainingFrames();

	// detect the end.
	if (info.curOff >= info.dataEnd) {
		*finish = 1;
	}
	return 0;
}

int Atrac2::SetData(const Track &track, u32 bufferAddr, u32 readSize, u32 bufferSize, int outputChannels) {
	// TODO: Remove track_.
	track_ = track;

	if (track_.codecType != PSP_MODE_AT_3 && track_.codecType != PSP_MODE_AT_3_PLUS) {
		// Shouldn't have gotten here, Analyze() checks this.
		context_->info.state = ATRAC_STATUS_NO_DATA;
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

	SceAtracIdInfo &info = context_->info;
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
	info.curOff = track_.dataByteOffset;  // Note: This and streamOff get incremented by bytesPerFrame before the return from this function by a frame.
	info.streamOff = info.curOff;
	info.streamDataByte = readSize - info.curOff;
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

	// TODO: Decode the first dummy frame to the temp buffer. This initializes the decoder.
	// It really does seem to be what's happening here, as evidenced by inBuf in the codec struct - it gets initialized.
	// Alternatively, the dummy frame is just there to leave space for wrapping...
	if (track_.FirstSampleOffsetFull() >= track_.SamplesPerFrame()) {
		int bytesConsumed;
		int outSamples;
		if (!decoder_->Decode(Memory::GetPointer(info.buffer + info.streamOff), info.sampleSize, &bytesConsumed, track_.channels, decodeTemp_, &outSamples)) {
			ERROR_LOG(Log::ME, "Error decoding the 'dummy' buffer at offset %d in the buffer", info.streamOff);
		}
		info.curOff += track_.bytesPerFrame;
		info.streamOff += track_.bytesPerFrame;
		info.streamDataByte -= info.sampleSize;
		discardedSamples_ -= outSamples;
	}

	// We need to handle wrapping the overshot partial packet at the end. Let's start by computing it.
	int cutLen = (info.bufferByte - info.curOff) % info.sampleSize;
	int cutRest = info.sampleSize - cutLen;

	// Then, let's copy it.
	if (cutLen > 0) {
		INFO_LOG(Log::ME, "Packets didn't fit evenly. Last packet got split into %d/%d (sum=%d). Copying to start of buffer.", cutLen, cutRest, cutLen, cutRest, info.sampleSize);
		Memory::Memcpy(info.buffer, info.buffer + info.bufferByte - cutLen, cutLen);
	}

	return 0;
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
