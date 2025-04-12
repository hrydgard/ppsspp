#include <algorithm>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Log.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/System.h"
#include "Core/HLE/AtracCtx2.h"
#include "Core/HW/Atrac3Standalone.h"

// Convenient command line:
// Windows\x64\debug\PPSSPPHeadless.exe  --root pspautotests/tests/../ -o --compare --new-atrac --timeout=30 --graphics=software pspautotests/tests/audio/atrac/stream.prx
//
// See the big comment in sceAtrac.cpp for an overview of the different modes of operation.

// To run on the real PSP, without gentest.py:
// cmd1> C:\dev\ppsspp\pspautotests\tests\audio\atrac> make
// cmd2> C:\dev\ppsspp\pspautotests> usbhostfs_pc -b 4000
// cmd3> C:\dev\ppsspp\pspautotests> pspsh -p 4000
// cmd3> > cd host0:/test/audio/atrac
// cmd3> stream.prx
// cmd1> C:\dev\ppsspp\pspautotests\tests\audio\atrac>copy /Y ..\..\..\__testoutput.txt stream.expected
// Then run the test, see above.

// TODO: Add an AT3 dumping facility (and/or replacement?). Although the old implementation could do it more easily...

// Needs to support negative numbers, and to handle non-powers-of-two.
static int RoundDownToMultiple(int size, int grain) {
	return size - (size % grain);
}

static int RoundDownToMultipleWithOffset(int offset, int size, int grain) {
	if (size > offset) {
		return ((size - offset) / grain) * grain + offset;
	} else {
		return size;
	}
}

static int ComputeSkipFrames(const SceAtracIdInfo &info, int seekPos) {
	// No idea why this is the rule, but this is the rule.
	return (seekPos & info.SamplesFrameMask()) < info.SkipSamples() ? 2 : 1;
}

static int ComputeFileOffset(const SceAtracIdInfo &info, int seekPos) {
	int frameOffset = ((seekPos / info.SamplesPerFrame()) - 1) * info.sampleSize;
	if ((seekPos & info.SamplesFrameMask()) < info.SkipSamples() && (frameOffset != 0)) {
		frameOffset -= info.sampleSize;
	}
	return frameOffset + info.dataOff;
}

// Unlike the above, this one need to be inclusive.
static int ComputeLoopEndFileOffset(const SceAtracIdInfo &info, int seekPos) {
	return (seekPos / info.SamplesPerFrame() + 1) * info.sampleSize + info.dataOff;
}

static int ComputeSpaceUsed(const SceAtracIdInfo &info) {
	// The odd case: If streaming from the second buffer, and we're past the loop end (we're in the tail)...
	if (info.decodePos > info.loopEnd && info.curBuffer == 1) {
		int space = info.secondBufferByte;
		if (info.secondStreamOff < space) {
			space = RoundDownToMultipleWithOffset(info.secondStreamOff, info.secondBufferByte, info.sampleSize);
		}
		if ((info.secondStreamOff <= space) && (space - info.secondStreamOff < info.streamDataByte)) {
			return info.streamDataByte - (space - info.secondStreamOff);
		}
		return 0;
	}

	// The normal case.
	return info.streamDataByte;
}

static int ComputeRemainFrameStream(const SceAtracIdInfo &info) {
	if (info.streamDataByte >= info.fileDataEnd - info.curFileOff) {
		// Already done.
		return PSP_ATRAC_NONLOOP_STREAM_DATA_IS_ON_MEMORY;
	}
	// Since we're streaming, the remaining frames are what's valid in the buffer.
	return std::max(0, info.streamDataByte / info.sampleSize - (int)info.numSkipFrames);
}

// This got so complicated!
static int ComputeRemainFrameLooped(const SceAtracIdInfo &info) {
	const int loopStartFileOffset = ComputeFileOffset(info, info.loopStart);
	const int loopEndFileOffset = ComputeLoopEndFileOffset(info, info.loopEnd);
	const int writeFileOff = info.curFileOff + info.streamDataByte;
	const int leftToRead = writeFileOff - loopEndFileOffset;

	int remainFrames;
	if (writeFileOff <= loopEndFileOffset) {
		// Simple case - just divide to find the number of frames remaining in the buffer.
		remainFrames = info.streamDataByte / info.sampleSize;
	} else {
		// Darn, we need to take looping into account...
		const int skipFramesAtLoopStart = ComputeSkipFrames(info, info.loopStart);
		const int firstPartLength = loopEndFileOffset - loopStartFileOffset;
		const int secondPartLength = leftToRead % firstPartLength;
		// Sum up all the parts (the buffered, the space remaining before the loop point
		// and the space after the loop point), each divided by sample size, need to take skipped frames into account.
		remainFrames = (loopEndFileOffset - info.curFileOff) / info.sampleSize +
			(leftToRead / firstPartLength) * (firstPartLength / info.sampleSize - skipFramesAtLoopStart);
		if (secondPartLength > skipFramesAtLoopStart * info.sampleSize) {
			remainFrames += secondPartLength / info.sampleSize - skipFramesAtLoopStart;
		}
	}

	// Clamp to zero.
	remainFrames = std::max(0, remainFrames - (int)info.numSkipFrames);
	if (info.loopNum < 0) {
		// Infinite looping while streaming, we never return that we're done reading data.
		return remainFrames;
	}

	// Additional check for distance to end of playback if we're looping a finite amount of times.
	const int streamBufferEndFileOffset = info.curFileOff + info.streamDataByte;
	if (streamBufferEndFileOffset >= loopEndFileOffset) {
		const int numBufferedLoops = (streamBufferEndFileOffset - loopEndFileOffset) / (loopEndFileOffset - loopStartFileOffset);
		if (info.loopNum <= numBufferedLoops) {
			return PSP_ATRAC_LOOP_STREAM_DATA_IS_ON_MEMORY;
		}
	}
	return remainFrames;
}

int Atrac2::RemainingFrames() const {
	const SceAtracIdInfo &info = context_->info;

	// Handle the easy cases first.
	switch (info.state) {
	case ATRAC_STATUS_UNINITIALIZED:
	case ATRAC_STATUS_NO_DATA:
		return 0;
	case ATRAC_STATUS_ALL_DATA_LOADED:
		return PSP_ATRAC_ALLDATA_IS_ON_MEMORY;  // Not sure about no data.
	case ATRAC_STATUS_HALFWAY_BUFFER:
	{
		// Pretty simple - compute the remaining space, and divide by the sample size, adjusting for frames-to-skip.
		const int writeFileOff = info.dataOff + info.streamDataByte;
		if (info.curFileOff < writeFileOff) {
			return std::max(0, (writeFileOff - info.curFileOff) / info.sampleSize - (int)info.numSkipFrames);
		}
		return 0;
	}
	case ATRAC_STATUS_STREAMED_WITHOUT_LOOP:
		return ComputeRemainFrameStream(info);

	case ATRAC_STATUS_STREAMED_LOOP_FROM_END:
		return ComputeRemainFrameLooped(info);

	case ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER:
		if (info.decodePos <= info.loopEnd) {
			// If before the tail, just treat it as looped.
			return ComputeRemainFrameLooped(info);
		} else {
			// If in tail, treat is as unlooped.
			return ComputeRemainFrameStream(info);
		}
		break;
	default:
		return SCE_ERROR_ATRAC_BAD_ATRACID;
	}
}

Atrac2::Atrac2(u32 contextAddr, int codecType) {
	if (contextAddr) {
		context_ = PSPPointer<SceAtracContext>::Create(contextAddr);
		// First-time initialization. The rest is initialized in SetData.
		SceAtracIdInfo &info = context_->info;
		info.codec = codecType;
		info.state = ATRAC_STATUS_NO_DATA;
		info.curBuffer = 0;

		sas_.streamOffset = 0;
		sas_.bufPtr[0] = 0;
		sas_.bufPtr[1] = 0;
	} else {
		// We're loading state, we'll restore the context in DoState.
	}
}

void Atrac2::DoState(PointerWrap &p) {
	auto s = p.Section("Atrac2", 1, 3);
	if (!s)
		return;

	Do(p, outputChannels_);
	// The only thing we need to save now is the outputChannels_ and the context pointer. And technically, not even that since
	// it can be computed. Still, for future proofing, let's save it.
	Do(p, context_);

	// Actually, now we also need to save sas state. I guess this could also be saved on the Sas side, but this is easier.
	if (s >= 2) {
		Do(p, sas_.streamOffset);
		Do(p, sas_.bufPtr[0]);
	}
	// Added support for streaming sas audio, need some more context state.
	if (s >= 3) {
		Do(p, sas_.bufPtr[1]);
		Do(p, sas_.bufSize[0]);
		Do(p, sas_.bufSize[1]);
		Do(p, sas_.isStreaming);
		Do(p, sas_.curBuffer);
		Do(p, sas_.fileOffset);
	}

	const SceAtracIdInfo &info = context_->info;
	if (p.mode == p.MODE_READ && info.state != ATRAC_STATUS_NO_DATA) {
		CreateDecoder(info.codec, info.sampleSize, info.numChan);
	}
}

bool Atrac2::HasSecondBuffer() const {
	const SceAtracIdInfo &info = context_->info;
	return info.secondBufferByte != 0;
}

int Atrac2::GetSoundSample(int *outEndSample, int *outLoopStartSample, int *outLoopEndSample) const {
	const SceAtracIdInfo &info = context_->info;
	*outEndSample = info.endSample - info.firstValidSample;
	int loopEnd = -1;
	if (info.loopEnd == 0) {
		*outLoopStartSample = -1;
		*outLoopEndSample = -1;
	} else {
		*outLoopStartSample = info.loopStart - info.firstValidSample;
		*outLoopEndSample = info.loopEnd - info.firstValidSample;
	}
	return 0;
}

int Atrac2::ResetPlayPosition(int seekPos, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf, bool *delay) {
	*delay = false;

	// This was mostly copied straight from the old impl.
	SceAtracIdInfo &info = context_->info;
	if (info.state == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && info.secondBufferByte == 0) {
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NEEDED;
	}

	seekPos += info.firstValidSample;

	if ((u32)seekPos > (u32)info.endSample) {
		return SCE_ERROR_ATRAC_BAD_SAMPLE;
	}

	int result = ResetPlayPositionInternal(seekPos, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
	if (result >= 0) {
		int skipCount = 0;
		result = SkipFrames(&skipCount);
		if (skipCount) {
			*delay = true;
		}
	}
	return result;
}

u32 Atrac2::ResetPlayPositionInternal(int seekPos, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	// Redo the same calculation as before, for input validation.

	AtracResetBufferInfo bufferInfo;
	GetResetBufferInfoInternal(&bufferInfo, seekPos);

	// Input validation.
	if ((u32)bytesWrittenFirstBuf < bufferInfo.first.minWriteBytes || (u32)bytesWrittenFirstBuf > bufferInfo.first.writableBytes) {
		return SCE_ERROR_ATRAC_BAD_FIRST_RESET_SIZE;
	}
	if ((u32)bytesWrittenSecondBuf < bufferInfo.second.minWriteBytes || (u32)bytesWrittenSecondBuf > bufferInfo.second.writableBytes) {
		return SCE_ERROR_ATRAC_BAD_SECOND_RESET_SIZE;
	}

	SceAtracIdInfo &info = context_->info;
	info.decodePos = seekPos;
	info.numSkipFrames = ComputeSkipFrames(info, seekPos);
	info.loopNum = 0;
	info.curFileOff = ComputeFileOffset(info, seekPos);

	context_->codec.err = 0x20b; // wtf? testing shows it.

	switch (info.state) {
	case ATRAC_STATUS_ALL_DATA_LOADED:
		// We're done.
		return 0;

	case ATRAC_STATUS_HALFWAY_BUFFER:
		info.streamDataByte += bytesWrittenFirstBuf;
		if (info.dataOff + info.streamDataByte >= info.fileDataEnd) {
			// Buffer full, we can transition to a full buffer here, if all the bytes were written. Let's do it.
			info.state = ATRAC_STATUS_ALL_DATA_LOADED;
		}
		return 0;

	case ATRAC_STATUS_STREAMED_WITHOUT_LOOP:
	case ATRAC_STATUS_STREAMED_LOOP_FROM_END:
		// We just adopt the bytes that were written as our stream data, no math needed.
		info.streamDataByte = bytesWrittenFirstBuf;
		info.curBuffer = 0;
		info.streamOff = 0;
		return 0;

	case ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER:
	{
		// As usual with the second buffer and trailer, things get tricky here.
		const int loopEndFileOffset = ComputeLoopEndFileOffset(info, info.loopEnd);
		if (info.curFileOff >= loopEndFileOffset) {
			const int secondBufferSizeRounded = RoundDownToMultiple(info.secondBufferByte, info.sampleSize);
			if (info.curFileOff < loopEndFileOffset + secondBufferSizeRounded) {
				info.streamDataByte = ((loopEndFileOffset + secondBufferSizeRounded) - info.curFileOff) + bytesWrittenFirstBuf;
				info.curBuffer = 1;
				info.secondStreamOff = info.curFileOff - loopEndFileOffset;
			} else {
				info.streamDataByte = bytesWrittenFirstBuf;
				info.curBuffer = 2;  // Temporary value! Will immediately switch back to 0.
				info.streamOff = 0;
			}
		} else {
			info.streamDataByte = bytesWrittenFirstBuf;
			info.curBuffer = 0;
			info.streamOff = 0;
		}
		return 0;
	}
	default:
		_dbg_assert_(false);
		return 0;
	}
}

// This is basically sceAtracGetBufferInfoForResetting.
int Atrac2::GetResetBufferInfo(AtracResetBufferInfo *bufferInfo, int seekPos, bool *delay) {
	const SceAtracIdInfo &info = context_->info;

	if (info.state == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && info.secondBufferByte == 0) {
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NEEDED;
	}

	seekPos += info.firstValidSample;

	if ((u32)seekPos > (u32)info.endSample) {
		return SCE_ERROR_ATRAC_BAD_SAMPLE;
	}

	GetResetBufferInfoInternal(bufferInfo, seekPos);
	// Yes, this happens here! If there are any frames to skip, they get skipped!
	// Even though this looks like a function that shouldn't change the state.
	int skipCount = 0;
	int retval = SkipFrames(&skipCount);
	if (skipCount > 0)
		*delay = true;
	return retval;
}

void Atrac2::GetResetBufferInfoInternal(AtracResetBufferInfo *bufferInfo, int seekPos) {
	const SceAtracIdInfo &info = context_->info;

	switch (info.state) {
	case ATRAC_STATUS_NO_DATA:
	case ATRAC_STATUS_ALL_DATA_LOADED:
		// Everything is loaded, so nothing needs to be read.
		bufferInfo->first.writePosPtr = info.buffer;
		bufferInfo->first.writableBytes = 0;
		bufferInfo->first.minWriteBytes = 0;
		bufferInfo->first.filePos = 0;
		break;
	case ATRAC_STATUS_HALFWAY_BUFFER:
	{
		// Not too hard, we just ask to fill up the missing part of the buffer.
		const int streamPos = info.dataOff + info.streamDataByte;
		const int fileOff = info.dataOff + (seekPos / info.SamplesPerFrame() + 1) * info.sampleSize;
		bufferInfo->first.writePosPtr = info.buffer + streamPos;
		bufferInfo->first.writableBytes = info.fileDataEnd - streamPos;
		bufferInfo->first.filePos = streamPos;
		bufferInfo->first.minWriteBytes = std::max(0, fileOff - streamPos);
		break;
	}

	case ATRAC_STATUS_STREAMED_WITHOUT_LOOP:
	case ATRAC_STATUS_STREAMED_LOOP_FROM_END:
	{
		// Relatively easy, just can't forget those skipped frames.
		const int curFileOffset = ComputeFileOffset(info, seekPos);
		const int bufferEnd = RoundDownToMultiple(info.bufferByte, info.sampleSize);
		bufferInfo->first.writePosPtr = info.buffer;
		bufferInfo->first.writableBytes = std::min(info.fileDataEnd - curFileOffset, bufferEnd);
		bufferInfo->first.minWriteBytes = (ComputeSkipFrames(info, seekPos) + 1) * info.sampleSize;
		bufferInfo->first.filePos = curFileOffset;
		break;
	}
	case ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER:
	{
		// As usual, with the second buffer, things get crazy complicated...
		const int seekFileOffset = ComputeFileOffset(info, seekPos);
		const int loopEndFileOffset = ComputeLoopEndFileOffset(info, info.loopEnd) - 1;
		const int bufferEnd = RoundDownToMultiple(info.bufferByte, info.sampleSize);
		const int skipBytes = (ComputeSkipFrames(info, seekPos) + 1) * info.sampleSize;
		const int secondBufferEnd = RoundDownToMultiple(info.secondBufferByte, info.sampleSize);
		if (seekFileOffset < loopEndFileOffset) {
			const int remainingBeforeLoop = (loopEndFileOffset - seekFileOffset) + 1;
			bufferInfo->first.writePosPtr = info.buffer;
			bufferInfo->first.writableBytes = std::min(bufferEnd, remainingBeforeLoop);
			bufferInfo->first.minWriteBytes = std::min(skipBytes, remainingBeforeLoop);
			bufferInfo->first.filePos = seekFileOffset;
		} else if (loopEndFileOffset + secondBufferEnd <= seekFileOffset) {
			bufferInfo->first.writePosPtr = info.buffer;
			bufferInfo->first.writableBytes = std::min(info.fileDataEnd - seekFileOffset, bufferEnd);
			bufferInfo->first.minWriteBytes = skipBytes;
			bufferInfo->first.filePos = seekFileOffset;
		} else if (loopEndFileOffset + (int)info.secondBufferByte + 1 < info.fileDataEnd) {
			const int endOffset = loopEndFileOffset + secondBufferEnd + 1;
			bufferInfo->first.writePosPtr = info.buffer;
			bufferInfo->first.writableBytes = std::min(info.fileDataEnd - endOffset, bufferEnd);
			bufferInfo->first.minWriteBytes = std::max(0, seekFileOffset + skipBytes - endOffset);
			bufferInfo->first.filePos = endOffset;
		} else {
			bufferInfo->first.writePosPtr = info.buffer;
			bufferInfo->first.writableBytes = 0;
			bufferInfo->first.minWriteBytes = 0;
			bufferInfo->first.filePos = 0;
		}
		break;
	}
	default:
		_dbg_assert_(false);
		break;
	}

	// Reset never needs a second buffer write, since the loop is in a fixed place.
	// It seems like second.writePosPtr is always the same as the first buffer's pos, weirdly (doesn't make sense).
	bufferInfo->second.writePosPtr = info.buffer;
	bufferInfo->second.writableBytes = 0;
	bufferInfo->second.minWriteBytes = 0;
	bufferInfo->second.filePos = 0;
}

int Atrac2::SetLoopNum(int loopNum) {
	SceAtracIdInfo &info = context_->info;
	if (info.loopEnd <= 0) {
		// File doesn't contain loop information, looping isn't allowed.
		return SCE_ERROR_ATRAC_NO_LOOP_INFORMATION;
	}
	// Just override the current loop counter.
	info.loopNum = loopNum;
	return 0;
}

int Atrac2::LoopNum() const {
	return context_->info.loopNum;
}

int Atrac2::LoopStatus() const {
	const SceAtracIdInfo &info = context_->info;
	if (info.loopEnd == 0) {
		// No loop available.
		return 0;
	} else if (info.loopNum != 0) {
		// We've got at least one loop to go.
		return 1;
	} else {
		// Return 1 if we haven't passed the loop point.
		return info.decodePos <= info.loopEnd ? 1 : 0;
	}
}

u32 Atrac2::GetNextSamples() {
	SceAtracIdInfo &info = context_->info;
	// TODO: Need to reformulate this.
	const int endOfCurrentFrame = info.decodePos | info.SamplesFrameMask();  // bit trick!
	const int remainder = std::max(0, endOfCurrentFrame - info.endSample);
	const int adjusted = (info.decodePos & info.SamplesFrameMask()) + remainder;
	return std::max(0, info.SamplesPerFrame() - adjusted);
}

int Atrac2::GetNextDecodePosition(int *pos) const {
	const SceAtracIdInfo &info = context_->info;
	// Check if we reached the end.
	if (info.decodePos > info.endSample) {
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}
	// Check if remaining data in the file is smaller than a frame.
	if (info.fileDataEnd - info.curFileOff < info.sampleSize) {
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}
	*pos = info.decodePos - info.firstValidSample;
	return 0;
}

int Atrac2::AddStreamData(u32 bytesToAdd) {
	SceAtracIdInfo &info = context_->info;

	// WARNING: bytesToAdd might not be sampleSize aligned, even though we return a sampleSize-aligned size
	// in GetStreamDataInfo, so other parts of the code still has to handle unaligned data amounts.
	if (info.state == ATRAC_STATUS_HALFWAY_BUFFER) {
		const int newFileOffset = info.streamDataByte + info.dataOff + bytesToAdd;
		if (newFileOffset == info.fileDataEnd) {
			info.state = ATRAC_STATUS_ALL_DATA_LOADED;
		} else if (newFileOffset > info.fileDataEnd) {
			return SCE_ERROR_ATRAC_ADD_DATA_IS_TOO_BIG;
		}
		info.streamDataByte += bytesToAdd;
	} else {
		// TODO: Check for SCE_ERROR_ATRAC_ADD_DATA_IS_TOO_BIG in the other modes too.
		info.streamDataByte += bytesToAdd;
	}
	return 0;
}

static int ComputeLoopedStreamWritableBytes(const SceAtracIdInfo &info, const int loopStartFileOffset, const u32 loopEndFileOffset) {
	const u32 writeOffset = info.curFileOff + info.streamDataByte;
	if (writeOffset >= loopEndFileOffset) {
		const int loopLength = loopEndFileOffset - loopStartFileOffset;
		return loopLength - ((info.curFileOff + info.streamDataByte) - loopEndFileOffset) % loopLength;
	} else {
		return loopEndFileOffset - writeOffset;
	}
}

static int IncrementAndLoop(int curOffset, int increment, int loopStart, int loopEnd) {
	const int sum = curOffset + increment;
	if (sum >= loopEnd) {
		return loopStart + (sum - loopEnd) % (loopEnd - loopStart);
	} else {
		return sum;
	}
}

static int WrapAroundRoundedBufferSize(int offset, int bufferSize, int addend, int grainSize) {
	bufferSize = RoundDownToMultipleWithOffset(offset, bufferSize, grainSize);
	const int sum = offset + addend;
	if (bufferSize <= sum) {
		return sum - bufferSize;
	} else {
		return sum;
	}
}

static void ComputeStreamBufferDataInfo(const SceAtracIdInfo &info, u32 *writePtr, u32 *bytesToWrite, u32 *readFileOffset) {
	// Streaming data info
	//
	// This really is the core logic of sceAtrac. Initially I had it quite complicated, but boiled it
	// all down to fairly simple logic. And then boiled it down further and fixed bugs.
	// And then had to make it WAY complicated again to support looping... Sigh.
	const u32 streamOff = info.curBuffer != 1 ? info.streamOff : 0;
	const int spaceUsed = ComputeSpaceUsed(info);
	const int spaceLeftAfterStreamOff = RoundDownToMultipleWithOffset(streamOff, info.bufferByte, info.sampleSize);
	const int streamPos = streamOff + spaceUsed;
	int spaceLeftInBuffer;
	if (streamPos >= spaceLeftAfterStreamOff) {
		spaceLeftInBuffer = spaceLeftAfterStreamOff - spaceUsed;
	} else {
		spaceLeftInBuffer = spaceLeftAfterStreamOff - streamPos;
	}
	const int loopStartFileOffset = ComputeFileOffset(info, info.loopStart);
	const int loopEndFileOffset = ComputeLoopEndFileOffset(info, info.loopEnd);

	switch (info.state) {
	case ATRAC_STATUS_STREAMED_WITHOUT_LOOP:
	{
		*bytesToWrite = std::clamp(info.fileDataEnd - (info.curFileOff + info.streamDataByte), 0, spaceLeftInBuffer);
		const int streamFileOff = info.curFileOff + info.streamDataByte;
		if (streamFileOff < info.fileDataEnd) {
			*readFileOffset = streamFileOff;
			*writePtr = info.buffer + WrapAroundRoundedBufferSize(info.streamOff, info.bufferByte, info.streamDataByte, info.sampleSize);
		} else {
			*readFileOffset = 0;
			*writePtr = info.buffer;
		}
		break;
	}

	case ATRAC_STATUS_STREAMED_LOOP_FROM_END:
		*bytesToWrite = std::min(ComputeLoopedStreamWritableBytes(info, loopStartFileOffset, loopEndFileOffset), spaceLeftInBuffer);
		*readFileOffset = IncrementAndLoop(info.curFileOff, info.streamDataByte, loopStartFileOffset, loopEndFileOffset);
		*writePtr = info.buffer + WrapAroundRoundedBufferSize(info.streamOff, info.bufferByte, info.streamDataByte, info.sampleSize);
		break;

	case ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER:
	{
		// Behaves like WITHOUT_LOOP or STREAMED_LOOP_FROM_END depending on the decode position.
		if (info.decodePos <= info.loopEnd) {
			*bytesToWrite = std::min(ComputeLoopedStreamWritableBytes(info, loopStartFileOffset, loopEndFileOffset), spaceLeftInBuffer);
			*readFileOffset = IncrementAndLoop(info.curFileOff, info.streamDataByte, loopStartFileOffset, loopEndFileOffset);
		} else {
			const int streamFileOff = info.curFileOff + info.streamDataByte;
			*bytesToWrite = std::clamp(info.fileDataEnd - streamFileOff, 0, spaceLeftInBuffer);
			if (streamFileOff < info.fileDataEnd) {
				*readFileOffset = streamFileOff;
			} else {
				*readFileOffset = 0;
			}
		}
		if (info.decodePos <= info.loopEnd || info.curBuffer != 1) {
			*writePtr = info.buffer + WrapAroundRoundedBufferSize(info.streamOff, info.bufferByte, info.streamDataByte, info.sampleSize);
		} else {
			*writePtr = info.buffer + WrapAroundRoundedBufferSize(0, info.bufferByte, spaceUsed, info.sampleSize);
		}
		break;
	}

	default:
		// unreachable
		_dbg_assert_(false);
		break;
	}
}

void Atrac2::GetStreamDataInfo(u32 *writePtr, u32 *bytesToWrite, u32 *readFileOffset) {
	const SceAtracIdInfo &info = context_->info;

	switch (info.state) {
	case ATRAC_STATUS_ALL_DATA_LOADED:
		// Nothing to do, the whole track is loaded already.
		*writePtr = info.buffer;
		*bytesToWrite = 0;
		*readFileOffset = 0;
		break;

	case ATRAC_STATUS_HALFWAY_BUFFER:
	{
		// This is both the file offset and the offset in the buffer, since it's direct mapped
		// in this mode (no wrapping or any other trickery).
		const int fileOffset = (int)info.dataOff + (int)info.streamDataByte;
		const int bytesLeftInFile = (int)info.fileDataEnd - fileOffset;
		// Just ask for the rest of the data. The game can supply as much of it as it wants at a time.
		*writePtr = info.buffer + fileOffset;
		*bytesToWrite = bytesLeftInFile;
		*readFileOffset = fileOffset;
		break;
	}

	default:
		ComputeStreamBufferDataInfo(info, writePtr, bytesToWrite, readFileOffset);
		break;
	}
}

u32 Atrac2::DecodeData(u8 *outbuf, u32 outbufAddr, int *SamplesNum, int *finish, int *remains) {
	SceAtracIdInfo &info = context_->info;

	const int tries = info.numSkipFrames + 1;
	for (int i = 0; i < tries; i++) {
		u32 result = DecodeInternal(outbufAddr, SamplesNum, finish);
		if (result != 0) {
			*SamplesNum = 0;
			return result;
		}
	}

	*remains = RemainingFrames();
	return 0;
}

u32 Atrac2::DecodeInternal(u32 outbufAddr, int *SamplesNum, int *finish) {
	SceAtracIdInfo &info = context_->info;

	// Check that there's enough data to decode.

	// Check for end of file.
	const int samplesToDecode = GetNextSamples();
	const int nextFileOff = info.curFileOff + info.sampleSize;
	if (nextFileOff > info.fileDataEnd || info.decodePos > info.endSample) {
		*finish = 1;
		return SCE_ERROR_ATRAC_ALL_DATA_DECODED;
	}

	// Check for streaming buffer run-out.
	if (AtracStatusIsStreaming(info.state) && info.streamDataByte < info.sampleSize) {
		*finish = 0;
		return SCE_ERROR_ATRAC_BUFFER_IS_EMPTY;
	}

	// Check for halfway buffer end.
	if (info.state == ATRAC_STATUS_HALFWAY_BUFFER && info.dataOff + info.streamDataByte < nextFileOff) {
		*finish = 0;
		return SCE_ERROR_ATRAC_BUFFER_IS_EMPTY;
	}

	if (info.state == ATRAC_STATUS_FOR_SCESAS) {
		_dbg_assert_(false);
	}

	u32 streamOff;
	u32 bufferPtr;
	if (!AtracStatusIsStreaming(info.state)) {
		bufferPtr = info.buffer;
		streamOff = info.curFileOff;
	} else {
		const int bufferIndex = info.curBuffer & 1;
		bufferPtr = bufferIndex == 0 ? info.buffer : info.secondBuffer;
		streamOff = bufferIndex == 0 ? info.streamOff : info.secondStreamOff;
	}

	u32 inAddr = bufferPtr + streamOff;
	int16_t *outPtr;

	_dbg_assert_(samplesToDecode <= info.SamplesPerFrame());
	if (samplesToDecode != info.SamplesPerFrame()) {
		if (!decodeTemp_) {
			decodeTemp_ = new int16_t[info.SamplesPerFrame() * outputChannels_];
		}
		outPtr = decodeTemp_;
	} else {
		outPtr = outbufAddr ? (int16_t *)Memory::GetPointer(outbufAddr) : 0;  // outbufAddr can be 0 during skip!
	}

	context_->codec.inBuf = inAddr;
	context_->codec.outBuf = outbufAddr;

	if (!Memory::IsValidAddress(inAddr)) {
		ERROR_LOG(Log::ME, "DecodeInternal: Bad inAddr %08x", inAddr);
		return SCE_ERROR_ATRAC_API_FAIL;
	}

	int bytesConsumed = 0;
	int outSamples = 0;
	if (!decoder_->Decode(Memory::GetPointerUnchecked(inAddr), info.sampleSize, &bytesConsumed, outputChannels_, outPtr, &outSamples)) {
		// Decode failed.
		*finish = 0;
		// TODO: The error code here varies based on what the problem is, but not sure of the right values.
		// 0000020b and 0000020c have been observed for 0xFF and/or garbage data, there may be more codes.
		context_->codec.err = 0x20b;
		return SCE_ERROR_ATRAC_API_FAIL;  // tested.
	} else {
		context_->codec.err = 0;
	}

	_dbg_assert_(bytesConsumed == info.sampleSize);

	// Advance the file offset.
	info.curFileOff += info.sampleSize;

	if (info.numSkipFrames == 0) {
		*SamplesNum = samplesToDecode;
		if (info.endSample < info.decodePos + samplesToDecode) {
			*finish = info.loopNum == 0;
		} else {
			*finish = 0;
		}
		u8 *outBuf = outbufAddr ? Memory::GetPointerWrite(outbufAddr) : nullptr;
		if (samplesToDecode != info.SamplesPerFrame() && samplesToDecode != 0 && outBuf) {
			memcpy(outBuf, decodeTemp_, samplesToDecode * outputChannels_ * sizeof(int16_t));
		}

		// Handle increments and looping.
		info.decodePos += samplesToDecode;
		if (info.loopEnd != 0 && info.loopNum != 0 && info.decodePos > info.loopEnd) {
			info.curFileOff = ComputeFileOffset(info, info.loopStart);
			info.numSkipFrames = ComputeSkipFrames(info, info.loopStart);
			info.decodePos = info.loopStart;
			if (info.loopNum > 0) {
				info.loopNum--;
			}
		}
	} else {
		info.numSkipFrames--;
	}

	// Handle streaming special cases.
	if (AtracStatusIsStreaming(info.state)) {
		info.streamDataByte -= info.sampleSize;
		if (info.curBuffer == 1) {
			// If currently streaming from the second buffer...
			int nextStreamOff = info.secondStreamOff + info.sampleSize;
			if ((int)info.secondBufferByte < nextStreamOff + info.sampleSize) {
				// Done/ran out
				info.streamOff = 0;
				info.secondStreamOff = 0;
				info.curBuffer = 2;
			} else {
				info.secondStreamOff = nextStreamOff;
			}
		} else {
			// Normal streaming from the main buffer. Let's first look at wrapping around the end...
			const int nextStreamOff = info.streamOff + info.sampleSize;
			if (nextStreamOff + info.sampleSize > (int)info.bufferByte) {
				info.streamOff = 0;
			} else {
				info.streamOff = nextStreamOff;
			}

			// OK, that's the simple stuff done. Moving on to second buffer streaming...
			// This is quite a condition!
			// Basically, if we're in state LOOP_WITH_TRAILER and currently streaming from the main buffer,
			// and either there's no loop or we're just done with the final loop and haven't reached the loop point yet...
			if (info.state == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && info.curBuffer == 0 &&
				(info.loopEnd == 0 || (info.loopNum == 0 && info.loopEnd < info.decodePos))) {
				// If, at that point, our file streaming offset has indeed reached the loop point...
				if (info.curFileOff >= ComputeLoopEndFileOffset(info, info.loopEnd)) {
					// Then we switch to streaming from the secondary buffer, and also let's copy the last partial
					// packet from the second buffer back to the start of the main buffer...
					info.curBuffer = 1;
					info.streamDataByte = info.secondBufferByte;
					info.secondStreamOff = 0;
					memcpy(Memory::GetPointerWrite(info.buffer),
						Memory::GetPointer(info.secondBuffer + (info.secondBufferByte - info.secondBufferByte % info.sampleSize)),
						info.secondBufferByte % info.sampleSize);
				}
			}
		}
	}
	return 0;
}

int Atrac2::SetData(const Track &track, u32 bufferAddr, u32 readSize, u32 bufferSize, int outputChannels) {
	// 72 is about the size of the minimum required data to even be valid.
	if (readSize < 72) {
		return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
	}

	// TODO: Check the range (addr, size) instead.
	if (!Memory::IsValidAddress(bufferAddr)) {
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	track.DebugLog();

	SceAtracIdInfo &info = context_->info;

	if (track.codecType != PSP_CODEC_AT3 && track.codecType != PSP_CODEC_AT3PLUS) {
		// Shouldn't have gotten here, Analyze() checks this.
		ERROR_LOG(Log::ME, "unexpected codec type %d in set data", track.codecType);
		return SCE_ERROR_ATRAC_UNKNOWN_FORMAT;
	}

	if (outputChannels != track.channels) {
		INFO_LOG(Log::ME, "Atrac2::SetData: outputChannels %d doesn't match track_.channels %d, decoder will expand.", outputChannels, track.channels);
	}

	if (readSize >= track.fileSize) {
		INFO_LOG(Log::ME, "The full file was set directly - we can dump it.");
		char filename[512];
		snprintf(filename, sizeof(filename), "%s_%d%s", track.codecType == PSP_CODEC_AT3 ? "at3" : "at3plus", track.endSample, track.channels == 1 ? "_mono" : "");
		DumpFileIfEnabled(Memory::GetPointer(bufferAddr), readSize, filename, DumpFileType::Atrac3);
	}

	CreateDecoder(track.codecType, track.bytesPerFrame, track.channels);

	outputChannels_ = outputChannels;

	if (!track.loopinfo.empty()) {
		info.loopNum = track.loopinfo[0].playCount;
		info.loopStart = track.loopStartSample;
		info.loopEnd = track.loopEndSample;
	} else {
		info.loopNum = 0;
		info.loopStart = 0;
		info.loopEnd = 0;
	}

	context_->codec.inBuf = bufferAddr;

	if (readSize > track.fileSize) {
		INFO_LOG(Log::ME, "readSize (%d) > track_.fileSize (%d)", readSize, track.fileSize);
	}

	if (bufferSize >= track.fileSize) {
		// Buffer is big enough to fit the whole track.
		if (readSize < bufferSize) {
			info.state = ATRAC_STATUS_HALFWAY_BUFFER;
		} else {
			info.state = ATRAC_STATUS_ALL_DATA_LOADED;
		}
	} else {
		// Streaming cases with various looping types.
		if (track.loopEndSample <= 0) {
			// There's no looping, but we need to stream the data in our buffer.
			info.state = ATRAC_STATUS_STREAMED_WITHOUT_LOOP;
		} else if (track.loopEndSample == track.endSample + track.FirstSampleOffsetFull()) {
			info.state = ATRAC_STATUS_STREAMED_LOOP_FROM_END;
		} else {
			info.state = ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER;
		}
	}

	DEBUG_LOG(Log::ME, "Atrac mode setup: %s", AtracStatusToString(info.state));

	// Copy parameters into state struct.
	info.codec = track.codecType;
	info.numChan = track.channels;
	info.sampleSize = track.bytesPerFrame;
	info.buffer = bufferAddr;
	info.bufferByte = bufferSize;
	info.firstValidSample = track.FirstSampleOffsetFull();
	info.endSample = track.endSample + info.firstValidSample;
	if (track.loopStartSample != 0xFFFFFFFF) {
		info.loopStart = track.loopStartSample;
		info.loopEnd = track.loopEndSample;
		// Sanity check the loop points, useful for testing.
	}
	info.dataOff = track.dataByteOffset;
	info.curFileOff = track.dataByteOffset;
	info.streamOff = track.dataByteOffset;
	info.streamDataByte = readSize - info.dataOff;
	info.fileDataEnd = track.fileSize;
	info.decodePos = track.FirstSampleOffsetFull();
	info.numSkipFrames = info.firstValidSample / info.SamplesPerFrame();
	// NOTE: we do not write into secondBuffer/secondBufferByte! they linger...

	int skipCount = 0;  // TODO: use for delay
	u32 retval = SkipFrames(&skipCount);

	// Seen in Mui Mui house. Things go very wrong after this..
	if (retval == SCE_ERROR_ATRAC_API_FAIL) {
		ERROR_LOG(Log::ME, "Bad frame during initial skip");
	} else if (retval != 0) {
		ERROR_LOG(Log::ME, "SkipFrames during InitContext returned an error: %08x", retval);
	}
	WrapLastPacket();
	return retval;
}

void Atrac2::WrapLastPacket() {
	SceAtracIdInfo &info = context_->info;

	// We need to handle wrapping the overshot partial packet at the end.
	if (AtracStatusIsStreaming(info.state)) {
		// This logic is similar to GetStreamDataInfo.
		int distanceToEnd = RoundDownToMultiple(info.bufferByte - info.streamOff, info.sampleSize);
		if (info.streamDataByte < distanceToEnd) {
			// There's space left without wrapping. Don't do anything.
			// INFO_LOG(Log::ME, "Packets fit into the buffer fully. %08x < %08x", readSize, bufferSize);
			// In this case, seems we need to zero some bytes. In one test, this seems to be 336.
			// Maybe there's a logical bug and the copy happens even when not needed, it's just that it'll
			// copy zeroes. Either way, let's just copy some bytes to make our sanity check hexdump pass.
			Memory::Memset(info.buffer, 0, 128);
		} else {
			// Wraps around.
			const int copyStart = info.streamOff + distanceToEnd;
			const int copyLen = info.bufferByte - copyStart;
			// Then, let's copy it.
			DEBUG_LOG(Log::ME, "Packets didn't fit evenly. Last packet got split into %d/%d (sum=%d). Copying to start of buffer.",
				copyLen, info.sampleSize - copyLen, info.sampleSize);
			Memory::Memcpy(info.buffer, info.buffer + copyStart, copyLen);
		}
	}
}

u32 Atrac2::SkipFrames(int *skipCount) {
	SceAtracIdInfo &info = context_->info;
	*skipCount = 0;
	int finishIgnored;
	while (true) {
		if (info.numSkipFrames == 0) {
			return 0;
		}
		u32 retval = DecodeInternal(0, 0, &finishIgnored);
		if (retval != 0) {
			if (retval == SCE_ERROR_ATRAC_API_FAIL) {
				(*skipCount)++;
			}
			return retval;
		}
		(*skipCount)++;
	}
	return 0;
}

// Where to read from to fill the second buffer.
int Atrac2::GetSecondBufferInfo(u32 *fileOffset, u32 *readSize) {
	const SceAtracIdInfo &info = context_->info;
	if (info.state != ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
		// No second buffer needed in this state.
		*fileOffset = 0;
		*readSize = 0;
		return SCE_ERROR_ATRAC_SECOND_BUFFER_NOT_NEEDED;
	}

	const int loopEndFileOffset = ComputeLoopEndFileOffset(info, info.loopEnd);
	*fileOffset = loopEndFileOffset;
	*readSize = info.fileDataEnd - loopEndFileOffset;
	return 0;
}

int Atrac2::SetSecondBuffer(u32 secondBuffer, u32 secondBufferSize) {
	SceAtracIdInfo &info = context_->info;

	u32 loopEndFileOffset = ComputeLoopEndFileOffset(info, info.loopEnd);
	if ((info.sampleSize * 3 <= (int)secondBufferSize ||
		(info.fileDataEnd - loopEndFileOffset) <= (int)secondBufferSize)) {
		if (info.state == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER) {
			info.secondBuffer = secondBuffer;
			info.secondBufferByte = secondBufferSize;
			info.secondStreamOff = 0;
			return 0;
		} else {
			return SCE_ERROR_ATRAC_SECOND_BUFFER_NOT_NEEDED;
		}
	}
	return SCE_ERROR_ATRAC_SIZE_TOO_SMALL;
}

u32 Atrac2::GetInternalCodecError() const {
	if (context_.IsValid()) {
		return context_->codec.err;
	} else {
		return 0;
	}
}

int Atrac2::Bitrate() const {
	const SceAtracIdInfo &info = context_->info;

	int bitrate = (info.sampleSize * 352800) / 1000;
	if (info.codec == PSP_CODEC_AT3PLUS)
		bitrate = ((bitrate >> 11) + 8) & 0xFFFFFFF0;
	else
		bitrate = (bitrate + 511) >> 10;
	return bitrate;
}

void Atrac2::InitLowLevel(const Atrac3LowLevelParams &params, int codecType) {
	SceAtracIdInfo &info = context_->info;
	info.codec = codecType;
	info.numChan = params.encodedChannels;
	outputChannels_ = params.outputChannels;
	info.sampleSize = params.bytesPerFrame;
	info.dataOff = 0;
	info.decodePos = 0;
	info.state = ATRAC_STATUS_LOW_LEVEL;
	CreateDecoder(codecType, info.sampleSize, info.numChan);
}

int Atrac2::DecodeLowLevel(const u8 *srcData, int *bytesConsumed, s16 *dstData, int *bytesWritten) {
	SceAtracIdInfo &info = context_->info;

	const int channels = outputChannels_;
	int outSamples = 0;
	bool success = decoder_->Decode(srcData, info.sampleSize, bytesConsumed, channels, dstData, &outSamples);
	if (!success) {
		ERROR_LOG(Log::ME, "Low level decoding failed: sampleSize: %d bytesConsumed: %d", info.sampleSize, *bytesConsumed);
		*bytesConsumed = 0;
		*bytesWritten = 0;
		return SCE_ERROR_ATRAC_API_FAIL;  // need to check what return value we get here.
	}
	*bytesWritten = outSamples * channels * sizeof(int16_t);
	// TODO: Possibly return a decode error on bad data.
	return 0;
}

void Atrac2::CheckForSas() {
	SceAtracIdInfo &info = context_->info;
	if (info.numChan != 1) {
		WARN_LOG(Log::ME, "Caller forgot to set channels to 1");
	}
	if (info.state != 0x10) {
		WARN_LOG(Log::ME, "Caller forgot to set state to 0x10");
	}
	sas_.isStreaming = info.fileDataEnd > (s32)info.bufferByte;
	if (sas_.isStreaming) {
		INFO_LOG(Log::ME, "SasAtrac stream mode");
	} else {
		INFO_LOG(Log::ME, "SasAtrac non-streaming mode");
	}
}

int Atrac2::EnqueueForSas(u32 address, u32 ptr) {
	SceAtracIdInfo &info = context_->info;
	// Set the new buffer up to be adopted by the next call to Decode that needs more data.
	// Note: Can't call this if the decoder isn't asking for another buffer to be queued.
	if (info.secondBuffer != 0xFFFFFFFF) {
		return SCE_SAS_ERROR_ATRAC3_ALREADY_QUEUED;
	}

	if (address == 0 && ptr == 0) {
		WARN_LOG(Log::ME, "Caller tries to send us a zero buffer. Something went wrong.");
	}

	DEBUG_LOG(Log::ME, "EnqueueForSas: Second buffer updated to %08x, sz: %08x", address, ptr);
	info.secondBuffer = address;
	info.secondBufferByte = ptr;
	return 0;
}

// Completely different streaming setup!
void Atrac2::DecodeForSas(s16 *dstData, int *bytesWritten, int *finish) {
	SceAtracIdInfo &info = context_->info;
	*bytesWritten = 0;

	// First frame handling. Not sure if accurate. Set up the initial buffer as the current streaming buffer.
	// Also works for the non-streaming case.
	if (info.buffer) {
		sas_.curBuffer = 0;
		sas_.bufPtr[0] = info.buffer;
		sas_.bufSize[0] = info.bufferByte - info.streamOff;  // also equals info.streamDataByte
		sas_.streamOffset = 0;
		sas_.fileOffset = info.bufferByte;  // Possibly should just set it to info.curFileOff
		info.buffer = 0;  // yes, this happens.
	}

	u8 assembly[1000];
	// Keep decoding from the current buffer until it runs out.
	if (sas_.streamOffset + info.sampleSize <= sas_.bufSize[sas_.curBuffer]) {
		// Just decode.
		const u8 *srcData = Memory::GetPointer(sas_.bufPtr[sas_.curBuffer] + sas_.streamOffset);
		int bytesConsumed = 0;
		bool decodeResult = decoder_->Decode(srcData, info.sampleSize, &bytesConsumed, 1, dstData, bytesWritten);
		if (!decodeResult) {
			ERROR_LOG(Log::ME, "SAS failed to decode regular packet");
		}
		sas_.streamOffset += bytesConsumed;
	} else if (sas_.isStreaming) {
		// TODO: Do we need special handling for the first buffer, since SetData will wrap around that packet? I think yes!
		DEBUG_LOG(Log::ME, "Streaming atrac through sas, and hit the end of buffer %d", sas_.curBuffer);

		// Compute the part sizes using the current size.
		int part1Size = sas_.bufSize[sas_.curBuffer] - sas_.streamOffset;
		int part2Size = info.sampleSize - part1Size;
		_dbg_assert_(part1Size >= 0);
		if (part1Size >= 0) {
			// Grab the partial packet, before we switch over to the other buffer.
			Memory::Memcpy(assembly, sas_.bufPtr[sas_.curBuffer] + sas_.streamOffset, part1Size);
		}

		// Check if we hit the end.
		if (sas_.fileOffset >= info.fileDataEnd) {
			DEBUG_LOG(Log::ME, "Streaming and hit the file end.");
			*bytesWritten = 0;
			*finish = 1;
			return;
		}

		// Check that a new buffer actually exists
		if (info.secondBuffer == sas_.bufPtr[sas_.curBuffer]) {
			ERROR_LOG(Log::ME, "Can't enqueue the same buffer twice in a row!");
			*bytesWritten = 0;
			*finish = 1;
			return;
		}

		if ((int)info.secondBuffer < 0) {
			ERROR_LOG(Log::ME, "AtracSas streaming ran out of data, no secondbuffer pending");
			*bytesWritten = 0;
			*finish = 1;
			return;
		}

		// Switch to the other buffer.
		sas_.curBuffer ^= 1;

		sas_.bufPtr[sas_.curBuffer] = info.secondBuffer;
		sas_.bufSize[sas_.curBuffer] = info.secondBufferByte;
		sas_.fileOffset += info.secondBufferByte;

		sas_.streamOffset = part2Size;

		// If we'll reach the end during this buffer, set second buffer to 0, signaling that we don't need more data.
		if (sas_.fileOffset >= info.fileDataEnd) {
			// We've reached the end.
			info.secondBuffer = 0;
			DEBUG_LOG(Log::ME, "%08x >= %08x: Reached the end.", sas_.fileOffset, info.fileDataEnd);
		} else {
			// Signal to the caller that we accept a new next buffer.
			info.secondBuffer = 0xFFFFFFFF;
		}

		DEBUG_LOG(Log::ME, "Switching over to buffer %d, updating buffer to %08x, sz: %08x. %s", sas_.curBuffer, info.secondBuffer, info.secondBufferByte, info.secondBuffer == 0xFFFFFFFF ? "Signalling for more data." : "");

		// Copy the second half (or if part1Size == 0, the whole packet) to the assembly buffer.
		Memory::Memcpy(assembly + part1Size, sas_.bufPtr[sas_.curBuffer], part2Size);
		// Decode the packet from the assembly, whether it's was assembled from two or one.
		const u8 *srcData = assembly;
		int bytesConsumed = 0;
		bool decodeResult = decoder_->Decode(srcData, info.sampleSize, &bytesConsumed, 1, dstData, bytesWritten);
		if (!decodeResult) {
			ERROR_LOG(Log::ME, "SAS failed to decode assembled packet");
		}
	}
}
