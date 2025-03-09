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

#include <algorithm>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/BufferQueue.h"

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceAtrac.h"
#include "Core/HLE/AtracCtx.h"
#include "Core/HLE/AtracCtx2.h"
#include "Core/System.h"

// Notes about sceAtrac buffer management
//
// sceAtrac decodes from a buffer the game fills, where this buffer has a status, one of:
//   * Not yet initialized (state NO_DATA = 1)
//   * The entire size of the audio data, and filled with audio data (state ALL_DATA_LOADED = 2)
//   * The entire size, but only partially filled so far (state HALFWAY_BUFFER = 3)
//   * Smaller than the audio, sliding without any loop (state STREAMED_WITHOUT_LOOP = 4)
//   * Smaller than the audio, sliding with a loop at the end (state STREAMED_WITH_LOOP_AT_END = 5)
//   * Smaller with a second buffer to help with a loop in the middle (state STREAMED_WITH_SECOND_BUF = 6)
//   * Not managed, decoding using "low level" manual looping etc. (LOW_LEVEL = 8)
//   * Not managed, reserved externally - possibly by sceSas - through low level (RESERVED = 16)
//
// This buffer is generally filled by sceAtracAddStreamData, and where to fill it is given by
// either sceAtracGetStreamDataInfo when continuing to move forwards in the stream of audio data,
// or sceAtracGetBufferInfoForResetting when seeking to a specific location in the audio stream.
//
// State 6 indicates a second buffer is needed.  This buffer is used to manage looping correctly.
// To determine how to fill it, the game will call sceAtracGetSecondBufferInfo, then after filling
// the buffer it will call sceAtracSetSecondBuffer.
// The second buffer will just contain the data for the end of loop.  The "first" buffer may manage
// only the looped portion, or some of the part after the loop (depending on second buf size.)
// TODO: What games use this?
//
// Most files will be in RIFF format.  It's also possible to load in an OMA/AA3 format file, but
// ultimately this will share the same buffer - it's just offset a bit more.
//
// Low level decoding doesn't use the buffer, and decodes only a single packet at a time.
//
// Lastly, sceSas has some integration with sceAtrac, which allows setting an Atrac id as
// a voice for an SAS core.  In this mode, the game will directly modify some of the context,
// but will largely only interact using sceSas.
//
// Note that this buffer is THE view of the audio stream.  On a PSP, the firmware does not manage
// any cache or separate version of the buffer - at most it manages decode state from earlier in
// the buffer.

static const int atracDecodeDelay = 2300;

static bool atracInited = true;
static AtracBase *atracContexts[PSP_NUM_ATRAC_IDS];
static u32 atracContextTypes[PSP_NUM_ATRAC_IDS];
static int atracLibVersion = 0;
static u32 atracLibCrc = 0;

// For debugger only.
const AtracBase *__AtracGetCtx(int i, u32 *type) {
	if (type) {
		*type = atracContextTypes[i];
	}
	return atracContexts[i];
}

void __AtracInit() {
	_assert_(sizeof(SceAtracContext) == 256);

	atracInited = true;
	memset(atracContexts, 0, sizeof(atracContexts));

	// Start with 2 of each in this order.
	atracContextTypes[0] = PSP_MODE_AT_3_PLUS;
	atracContextTypes[1] = PSP_MODE_AT_3_PLUS;
	atracContextTypes[2] = PSP_MODE_AT_3;
	atracContextTypes[3] = PSP_MODE_AT_3;
	atracContextTypes[4] = 0;
	atracContextTypes[5] = 0;
}

void __AtracShutdown() {
	for (size_t i = 0; i < ARRAY_SIZE(atracContexts); ++i) {
		delete atracContexts[i];
		atracContexts[i] = nullptr;
	}
}

void __AtracLoadModule(int version, u32 crc) {
	atracLibVersion = version;
	atracLibCrc = crc;
	INFO_LOG(Log::ME, "AtracInit, atracLibVersion 0x%0x, atracLibcrc %x", atracLibVersion, atracLibCrc);
}

void __AtracDoState(PointerWrap &p) {
	auto s = p.Section("sceAtrac", 1, 2);
	if (!s)
		return;

	Do(p, atracInited);
	for (int i = 0; i < PSP_NUM_ATRAC_IDS; ++i) {
		bool valid = atracContexts[i] != nullptr;
		Do(p, valid);
		if (valid) {
			DoSubClass<AtracBase, Atrac>(p, atracContexts[i]);
		} else {
			delete atracContexts[i];
			atracContexts[i] = nullptr;
		}
	}
	DoArray(p, atracContextTypes, PSP_NUM_ATRAC_IDS);
	if (s < 2) {
		atracLibVersion = 0;
		atracLibCrc = 0;
	}
	else {
		Do(p, atracLibVersion);
		Do(p, atracLibCrc);
	}
}

static AtracBase *allocAtrac(bool forceOld = false) {
	if (g_Config.bUseExperimentalAtrac && !forceOld) {
		return new Atrac2();
	} else {
		return new Atrac();
	}
}

static AtracBase *getAtrac(int atracID) {
	if (atracID < 0 || atracID >= PSP_NUM_ATRAC_IDS) {
		return nullptr;
	}
	AtracBase *atrac = atracContexts[atracID];
	if (atrac) {
		atrac->UpdateContextFromPSPMem();
	}
	return atrac;
}

static int createAtrac(AtracBase *atrac) {
	for (int i = 0; i < (int)ARRAY_SIZE(atracContexts); ++i) {
		if (atracContextTypes[i] == atrac->CodecType() && atracContexts[i] == 0) {
			atracContexts[i] = atrac;
			atrac->SetAtracID(i);
			return i;
		}
	}
	return SCE_ERROR_ATRAC_NO_ATRACID;
}

static int deleteAtrac(int atracID) {
	if (atracID >= 0 && atracID < PSP_NUM_ATRAC_IDS) {
		if (atracContexts[atracID] != nullptr) {
			delete atracContexts[atracID];
			atracContexts[atracID] = nullptr;
			return 0;
		}
	}
	return SCE_ERROR_ATRAC_BAD_ATRACID;
}

// Really, allocate an Atrac context of a specific codec type.
// Useful to initialize a context for low level decode.
static u32 sceAtracGetAtracID(int codecType) {
	if (codecType != PSP_MODE_AT_3 && codecType != PSP_MODE_AT_3_PLUS) {
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_INVALID_CODECTYPE, "invalid codecType");
	}

	AtracBase *atrac = allocAtrac();
	atrac->GetTrackMut().codecType = codecType;
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(Log::ME, atracID, "no free ID");
	}

	return hleLogInfo(Log::ME, atracID);
}

static int AtracValidateData(const AtracBase *atrac) {
	if (!atrac) {
		return SCE_ERROR_ATRAC_BAD_ATRACID;
	} else if (atrac->BufferState() == ATRAC_STATUS_NO_DATA) {
		return SCE_ERROR_ATRAC_NO_DATA;
	} else {
		return 0;
	}
}

static int AtracValidateManaged(const AtracBase *atrac) {
	if (!atrac) {
		return SCE_ERROR_ATRAC_BAD_ATRACID;
	} else if (atrac->BufferState() == ATRAC_STATUS_NO_DATA) {
		return SCE_ERROR_ATRAC_NO_DATA;
	} else if (atrac->BufferState() == ATRAC_STATUS_LOW_LEVEL) {
		return SCE_ERROR_ATRAC_IS_LOW_LEVEL;
	} else if (atrac->BufferState() == ATRAC_STATUS_FOR_SCESAS) {
		return SCE_ERROR_ATRAC_IS_FOR_SCESAS;
	} else {
		return 0;
	}
}

// Notifies that more data is (OR will be very soon) available in the buffer.
// This implies it has been added to whatever position sceAtracGetStreamDataInfo would indicate.
//
// The total size of the buffer is atrac->bufferMaxSize_.
static u32 sceAtracAddStreamData(int atracID, u32 bytesToAdd) {
	AtracBase *atrac = getAtrac(atracID);
	int err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (atrac->BufferState() == ATRAC_STATUS_ALL_DATA_LOADED) {
		// Let's avoid spurious warnings.  Some games call this with 0 which is pretty harmless.
		if (bytesToAdd == 0)
			return hleLogDebug(Log::ME, SCE_ERROR_ATRAC_ALL_DATA_LOADED, "stream entirely loaded");
		return hleLogWarning(Log::ME, SCE_ERROR_ATRAC_ALL_DATA_LOADED, "stream entirely loaded");
	}

	int ret = atrac->AddStreamData(bytesToAdd);
	return hleLogDebugOrError(Log::ME, ret);
}

// Note that outAddr being null is completely valid here, used to skip data.
static u32 sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	u32 numSamples = 0;
	u32 finish = 0;
	int remains = 0;
	int ret = atrac->DecodeData(outAddr ? Memory::GetPointerWrite(outAddr) : nullptr, outAddr, &numSamples, &finish, &remains);
	if (ret != (int)SCE_ERROR_ATRAC_BAD_ATRACID && ret != (int)SCE_ERROR_ATRAC_NO_DATA) {
		if (Memory::IsValidAddress(numSamplesAddr))
			Memory::WriteUnchecked_U32(numSamples, numSamplesAddr);
		if (Memory::IsValidAddress(finishFlagAddr))
			Memory::WriteUnchecked_U32(finish, finishFlagAddr);
		// On error, no remaining frame value is written.
		if (ret == 0 && Memory::IsValidAddress(remainAddr))
			Memory::WriteUnchecked_U32(remains, remainAddr);
	}
	DEBUG_LOG(Log::ME, "%08x=sceAtracDecodeData(%i, %08x, %08x[%08x], %08x[%08x], %08x[%d])", ret, atracID, outAddr,
			  numSamplesAddr, numSamples,
			  finishFlagAddr, finish,
			  remainAddr, remains);
	if (ret == 0) {
		// decode data successfully, delay thread
		return hleDelayResult(hleNoLog(ret), "atrac decode data", atracDecodeDelay);
	}
	return hleNoLog(ret);
}

static u32 sceAtracEndEntry() {
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceAtracEndEntry()");
	return hleNoLog(0);
}

// Obtains information about what needs to be in the buffer to seek (or "reset")
// Generally called by games right before calling sceAtracResetPlayPosition().
static u32 sceAtracGetBufferInfoForResetting(int atracID, int sample, u32 bufferInfoAddr) {
	auto bufferInfo = PSPPointer<AtracResetBufferInfo>::Create(bufferInfoAddr);

	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (!bufferInfo.IsValid()) {
		return hleReportError(Log::ME, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid buffer, should crash");
	} else if (atrac->BufferState() == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && atrac->SecondBufferSize() == 0) {
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_SECOND_BUFFER_NEEDED, "no second buffer");
	} else if ((u32)sample + atrac->GetTrack().firstSampleOffset > (u32)atrac->GetTrack().endSample + atrac->GetTrack().firstSampleOffset) {
		// NOTE: Above we have to add firstSampleOffset to both sides - we seem to rely on wraparound.
		return hleLogWarning(Log::ME, SCE_ERROR_ATRAC_BAD_SAMPLE, "invalid sample position");
	} else {
		atrac->GetResetBufferInfo(bufferInfo, sample);
		return hleLogInfo(Log::ME, 0);
	}
}

static u32 sceAtracGetBitrate(int atracID, u32 outBitrateAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	atrac->GetTrackMut().UpdateBitrate();

	if (Memory::IsValidAddress(outBitrateAddr)) {
		Memory::WriteUnchecked_U32(atrac->GetTrack().bitrate, outBitrateAddr);
		return hleLogDebug(Log::ME, 0);
	} else {
		return hleLogError(Log::ME, 0, "invalid address");
	}
}

static u32 sceAtracGetChannel(int atracID, u32 channelAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (Memory::IsValidAddress(channelAddr)){
		Memory::WriteUnchecked_U32(atrac->GetTrack().channels, channelAddr);
		return hleLogDebug(Log::ME, 0);
	} else {
		return hleLogError(Log::ME, 0, "invalid address");
	}
}

static u32 sceAtracGetLoopStatus(int atracID, u32 loopNumAddr, u32 statusAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (Memory::IsValidAddress(loopNumAddr))
		Memory::WriteUnchecked_U32(atrac->LoopNum(), loopNumAddr);
	// return audio's loopinfo in at3 file
	if (Memory::IsValidAddress(statusAddr)) {
		if (atrac->GetTrack().loopinfo.size() > 0)
			Memory::WriteUnchecked_U32(1, statusAddr);
		else
			Memory::WriteUnchecked_U32(0, statusAddr);
		return hleLogDebug(Log::ME, 0);
	} else {
		return hleLogError(Log::ME, 0, "invalid address");
	}
}

static u32 sceAtracGetInternalErrorInfo(int atracID, u32 errorAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}
	ERROR_LOG(Log::ME, "UNIMPL sceAtracGetInternalErrorInfo(%i, %08x)", atracID, errorAddr);
	if (Memory::IsValidAddress(errorAddr))
		Memory::WriteUnchecked_U32(0, errorAddr);
	return 0;
}

static u32 sceAtracGetMaxSample(int atracID, u32 maxSamplesAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (Memory::IsValidAddress(maxSamplesAddr)) {
		int maxSamples = atrac->GetTrack().SamplesPerFrame();
		Memory::WriteUnchecked_U32(maxSamples, maxSamplesAddr);
		return hleLogDebug(Log::ME, 0);
	} else {
		return hleLogError(Log::ME, 0, "invalid address");
	}
}

static u32 sceAtracGetNextDecodePosition(int atracID, u32 outposAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (Memory::IsValidAddress(outposAddr)) {
		const int currentSample = atrac->CurrentSample();
		if (currentSample >= atrac->GetTrack().endSample) {
			Memory::WriteUnchecked_U32(0, outposAddr);
			return hleLogDebug(Log::ME, SCE_ERROR_ATRAC_ALL_DATA_DECODED, "all data decoded - beyond endSample");
		} else {
			Memory::WriteUnchecked_U32(currentSample, outposAddr);
			return hleLogDebug(Log::ME, 0);
		}
	} else {
		return hleLogError(Log::ME, 0, "invalid address");
	}
}

static u32 sceAtracGetNextSample(int atracID, u32 outNAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}
	if (atrac->CurrentSample() >= atrac->GetTrack().endSample) {
		if (Memory::IsValidAddress(outNAddr))
			Memory::WriteUnchecked_U32(0, outNAddr);
		return hleLogDebug(Log::ME, 0, "0 samples left");
	}

	u32 numSamples = atrac->GetNextSamples();

	if (Memory::IsValidAddress(outNAddr))
		Memory::WriteUnchecked_U32(numSamples, outNAddr);
	return hleLogDebug(Log::ME, 0, "%d samples left", numSamples);
}

// Obtains the number of frames remaining in the buffer which can be decoded.
// When no more data would be needed, this returns a negative number.
static u32 sceAtracGetRemainFrame(int atracID, u32 remainAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (!Memory::IsValidAddress(remainAddr)) {
		// Would crash.
		return hleReportError(Log::ME, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid remainingFrames pointer");
	}

	u32 remaining = atrac->RemainingFrames();
	Memory::WriteUnchecked_U32(remaining, remainAddr);
	return hleLogDebug(Log::ME, 0);
}

static u32 sceAtracGetSecondBufferInfo(int atracID, u32 fileOffsetAddr, u32 desiredSizeAddr) {
	auto fileOffset = PSPPointer<u32_le>::Create(fileOffsetAddr);
	auto desiredSize = PSPPointer<u32_le>::Create(desiredSizeAddr);

	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (!fileOffset.IsValid() || !desiredSize.IsValid()) {
		// Would crash.
		return hleReportError(Log::ME, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid addresses");
	}

	int result = atrac->GetSecondBufferInfo(fileOffset, desiredSize);
	switch (result) {
	case (int)SCE_ERROR_ATRAC_SECOND_BUFFER_NOT_NEEDED:
		return hleLogDebug(Log::ME, result);
	default:
		return hleLogDebugOrError(Log::ME, result);
	}
}

static u32 sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	auto outEndSample = PSPPointer<u32_le>::Create(outEndSampleAddr);
	if (outEndSample.IsValid())
		*outEndSample = atrac->GetTrack().endSample;
	auto outLoopStart = PSPPointer<u32_le>::Create(outLoopStartSampleAddr);
	if (outLoopStart.IsValid())
		*outLoopStart = atrac->GetTrack().loopStartSample == -1 ? -1 : atrac->GetTrack().loopStartSample - atrac->GetTrack().FirstSampleOffsetFull();
	auto outLoopEnd = PSPPointer<u32_le>::Create(outLoopEndSampleAddr);
	if (outLoopEnd.IsValid())
		*outLoopEnd = atrac->GetTrack().loopEndSample == -1 ? -1 : atrac->GetTrack().loopEndSample - atrac->GetTrack().FirstSampleOffsetFull();

	if (!outEndSample.IsValid() || !outLoopStart.IsValid() || !outLoopEnd.IsValid()) {
		return hleReportError(Log::ME, 0, "invalid address");
	}
	return hleLogDebug(Log::ME, 0);
}

// Games call this function to get some info for add more stream data,
// such as where the data read from, where the data add to,
// and how many bytes are allowed to add.
static u32 sceAtracGetStreamDataInfo(int atracID, u32 writePtrAddr, u32 writableBytesAddr, u32 readOffsetAddr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	u32 writePtr;
	u32 writableBytes;
	u32 readOffset;
	atrac->GetStreamDataInfo(&writePtr, &writableBytes, &readOffset);

	if (Memory::IsValidAddress(writePtrAddr))
		Memory::WriteUnchecked_U32(writePtr, writePtrAddr);
	if (Memory::IsValidAddress(writableBytesAddr))
		Memory::WriteUnchecked_U32(writableBytes, writableBytesAddr);
	if (Memory::IsValidAddress(readOffsetAddr))
		Memory::WriteUnchecked_U32(readOffset, readOffsetAddr);

	return hleLogDebug(Log::ME, 0);
}

static u32 sceAtracReleaseAtracID(int atracID) {
	int result = deleteAtrac(atracID);
	if (result < 0) {
		if (atracID >= 0) {
			return hleLogError(Log::ME, result, "did not exist");
		} else {
			return hleLogWarning(Log::ME, result, "did not exist");
		}
	}
	return hleLogInfo(Log::ME, result);
}

// This is called when a game wants to seek (or "reset") to a specific position in the audio data.
// Normally, sceAtracGetBufferInfoForResetting() is called to determine how to buffer.
// The game must add sufficient packets to the buffer in order to complete the seek.
static u32 sceAtracResetPlayPosition(int atracID, int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	if (atrac->BufferState() == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER && atrac->SecondBufferSize() == 0) {
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_SECOND_BUFFER_NEEDED, "no second buffer");
	} else if ((u32)sample + atrac->GetTrack().firstSampleOffset > (u32)atrac->GetTrack().endSample + atrac->GetTrack().firstSampleOffset) {
		// NOTE: Above we have to add firstSampleOffset to both sides - we seem to rely on wraparound.
		return hleLogWarning(Log::ME, SCE_ERROR_ATRAC_BAD_SAMPLE, "invalid sample position");
	}

	u32 res = atrac->ResetPlayPosition(sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
	if (res != 0) {
		// Already logged.
		return res;
	}

	return hleDelayResult(0, "reset play pos", 3000);
}

// Allowed to log like a HLE function, only called at the end of some.
static int _AtracSetData(int atracID, u32 buffer, u32 readSize, u32 bufferSize, int outputChannels, bool needReturnAtracID) {
	AtracBase *atrac = getAtrac(atracID);
	// Don't use AtracValidateManaged here.
	if (!atrac) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_ATRACID, "invalid atrac ID");
	}
	// SetData logs like a hle function.
	int ret = atrac->SetData(buffer, readSize, bufferSize, outputChannels, needReturnAtracID ? atracID : 0);
	// not sure the real delay time
	return hleDelayResult(hleLogDebugOrError(Log::ME, ret), "atrac set data", 100);
}

static u32 sceAtracSetHalfwayBuffer(int atracID, u32 buffer, u32 readSize, u32 bufferSize) {
	AtracBase *atrac = getAtrac(atracID);
	// Don't use AtracValidateManaged here.
	if (!atrac) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_ATRACID, "invalid atrac ID");
	}

	if (readSize > bufferSize) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_INCORRECT_READ_SIZE, "read size too large");
	}

	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		return hleLogError(Log::ME, ret);
	}

	return _AtracSetData(atracID, buffer, readSize, bufferSize, 2, false);
}

static u32 sceAtracSetSecondBuffer(int atracID, u32 secondBuffer, u32 secondBufferSize) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	return hleLogDebugOrError(Log::ME, atrac->SetSecondBuffer(secondBuffer, secondBufferSize));
}

static u32 sceAtracSetData(int atracID, u32 buffer, u32 bufferSize) {
	AtracBase *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_ATRACID, "bad atrac ID");
	}

	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		return hleLogError(Log::ME, ret);
	}

	if (atrac->GetTrack().codecType != atracContextTypes[atracID]) {
		// TODO: Should this not change the buffer size?
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_WRONG_CODECTYPE, "atracID uses different codec type than data");
	}

	return _AtracSetData(atracID, buffer, bufferSize, bufferSize, 2, false);
}

static int sceAtracSetDataAndGetID(u32 buffer, int bufferSize) {
	// A large value happens in Tales of VS, and isn't handled somewhere properly as a u32.
	// It's impossible for it to be that big anyway, so cap it.
	if (bufferSize < 0) {
		WARN_LOG(Log::ME, "sceAtracSetDataAndGetID(%08x, %08x): negative bufferSize", buffer, bufferSize);
		bufferSize = 0x10000000;
	}

	AtracBase *atrac = allocAtrac();
	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		delete atrac;
		return hleLogError(Log::ME, ret);
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(Log::ME, atracID, "no free ID");
	}

	return _AtracSetData(atracID, buffer, bufferSize, bufferSize, 2, true);
}

static int sceAtracSetHalfwayBufferAndGetID(u32 buffer, u32 readSize, u32 bufferSize) {
	if (readSize > bufferSize) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_INCORRECT_READ_SIZE, "read size too large");
	}
	AtracBase *atrac = allocAtrac();
	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		delete atrac;
		return hleLogError(Log::ME, ret);
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(Log::ME, atracID, "no free ID");
	}
	return _AtracSetData(atracID, buffer, readSize, bufferSize, 2, true);
}

static u32 sceAtracStartEntry() {
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceAtracStartEntry()");
	return 0;
}

static u32 sceAtracSetLoopNum(int atracID, int loopNum) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	int retval = atrac->SetLoopNum(loopNum);
	if (retval < 0 && loopNum == -1) {
		return hleLogDebug(Log::ME, retval);
	} else {
		return hleLogError(Log::ME, retval);
	}
}

static int sceAtracReinit(int at3Count, int at3plusCount) {
	for (int i = 0; i < PSP_NUM_ATRAC_IDS; ++i) {
		if (atracContexts[i] != nullptr) {
			return hleReportError(Log::ME, SCE_KERNEL_ERROR_BUSY, "cannot reinit while IDs in use");
		}
	}

	memset(atracContextTypes, 0, sizeof(atracContextTypes));
	int next = 0;
	int space = PSP_NUM_ATRAC_IDS;

	// This seems to deinit things.  Mostly, it cause a reschedule on next deinit (but -1, -1 does not.)
	if (at3Count == 0 && at3plusCount == 0) {
		atracInited = false;
		return hleDelayResult(hleLogInfo(Log::ME, 0, "deinit"), "atrac reinit", 200);
	}

	// First, ATRAC3+.  These IDs seem to cost double (probably memory.)
	// Intentionally signed.  9999 tries to allocate, -1 does not.
	for (int i = 0; i < at3plusCount; ++i) {
		space -= 2;
		if (space >= 0) {
			atracContextTypes[next++] = PSP_MODE_AT_3_PLUS;
		}
	}
	for (int i = 0; i < at3Count; ++i) {
		space -= 1;
		if (space >= 0) {
			atracContextTypes[next++] = PSP_MODE_AT_3;
		}
	}

	// If we ran out of space, we still initialize some, but return an error.
	int result = space >= 0 ? 0 : (int)SCE_KERNEL_ERROR_OUT_OF_MEMORY;
	if (atracInited || next == 0) {
		atracInited = true;
		return hleLogInfo(Log::ME, result);
	} else {
		atracInited = true;
		return hleDelayResult(hleLogInfo(Log::ME, result), "atrac reinit", 400);
	}
}

static int sceAtracGetOutputChannel(int atracID, u32 outputChanPtr) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateData(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}
	if (Memory::IsValidAddress(outputChanPtr)) {
		Memory::WriteUnchecked_U32(atrac->GetOutputChannels(), outputChanPtr);
		return hleLogDebug(Log::ME, 0);
	} else {
		return hleLogError(Log::ME, 0, "invalid address");
	}
}

static int sceAtracIsSecondBufferNeeded(int atracID) {
	AtracBase *atrac = getAtrac(atracID);
	u32 err = AtracValidateManaged(atrac);
	if (err != 0) {
		return hleLogError(Log::ME, err);
	}

	// Note that this returns true whether the buffer is already set or not.
	int needed = atrac->BufferState() == ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER ? 1 : 0;
	return hleLogDebug(Log::ME, needed);
}

static int sceAtracSetMOutHalfwayBuffer(int atracID, u32 buffer, u32 readSize, u32 bufferSize) {
	AtracBase *atrac = getAtrac(atracID);
	// Don't use AtracValidate* here.
	if (!atrac) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_ATRACID, "bad atrac ID");
	}
	if (readSize > bufferSize) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_INCORRECT_READ_SIZE, "read size too large");
	}

	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		return hleLogError(Log::ME, ret);
	}
	if (atrac->GetTrack().channels != 1) {
		// It seems it still sets the data.
		atrac->SetData(buffer, readSize, bufferSize, 2, 0);
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_NOT_MONO, "not mono data");
	} else {
		return _AtracSetData(atracID, buffer, readSize, bufferSize, 1, false);
	}
}

// Note: This doesn't seem to be part of any available libatrac3plus library.
static u32 sceAtracSetMOutData(int atracID, u32 buffer, u32 bufferSize) {
	AtracBase *atrac = getAtrac(atracID);
	// Don't use AtracValidate* here.
	if (!atrac) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_ATRACID, "bad atrac ID");
	}

	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		return hleLogError(Log::ME, ret);
	}
	if (atrac->GetTrack().channels != 1) {
		// It seems it still sets the data.
		atrac->SetData(buffer, bufferSize, bufferSize, 2, 0);
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_NOT_MONO, "not mono data");
	} else {
		return _AtracSetData(atracID, buffer, bufferSize, bufferSize, 1, false);
	}
}

// Note: This doesn't seem to be part of any available libatrac3plus library.
static int sceAtracSetMOutDataAndGetID(u32 buffer, u32 bufferSize) {
	AtracBase *atrac = allocAtrac();
	int ret = atrac->Analyze(buffer, bufferSize);
	if (ret < 0) {
		delete atrac;
		return hleLogError(Log::ME, ret);
	}
	if (atrac->GetTrack().channels != 1) {
		delete atrac;
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_NOT_MONO, "not mono data");
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(Log::ME, atracID, "no free ID");
	}

	return _AtracSetData(atracID, buffer, bufferSize, bufferSize, 1, true);
}

static int sceAtracSetMOutHalfwayBufferAndGetID(u32 buffer, u32 readSize, u32 bufferSize) {
	if (readSize > bufferSize) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_INCORRECT_READ_SIZE, "read size too large");
	}
	AtracBase *atrac = allocAtrac();
	int ret = atrac->Analyze(buffer, readSize);
	if (ret < 0) {
		delete atrac;
		return hleLogError(Log::ME, ret);
	}
	if (atrac->GetTrack().channels != 1) {
		delete atrac;
		return hleReportError(Log::ME, SCE_ERROR_ATRAC_NOT_MONO, "not mono data");
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(Log::ME, atracID, "no free ID");
	}

	return _AtracSetData(atracID, buffer, readSize, bufferSize, 1, true);
}

static int sceAtracSetAA3DataAndGetID(u32 buffer, u32 bufferSize, u32 fileSize, u32 metadataSizeAddr) {
	AtracBase *atrac = allocAtrac();
	int ret = atrac->AnalyzeAA3(buffer, bufferSize, fileSize);
	if (ret < 0) {
		delete atrac;
		return hleLogError(Log::ME, ret);
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(Log::ME, atracID, "no free ID");
	}

	return _AtracSetData(atracID, buffer, bufferSize, bufferSize, 2, true);
}

static u32 _sceAtracGetContextAddress(int atracID) {
	AtracBase *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(Log::ME, 0, "bad atrac id");
	}

	atrac->EnsureContext(atracID);
	atrac->WriteContextToPSPMem();
	return hleLogDebug(Log::ME, atrac->context_.ptr);
}

struct At3HeaderMap {
	u16 bytes;
	u16 channels;
	u8 jointStereo;

	bool Matches(const AtracBase *at) const {
		return bytes == at->GetTrack().BytesPerFrame() && channels == at->GetTrack().channels;
	}
};

// These should represent all possible supported bitrates (66, 104, and 132 for stereo.)
static const At3HeaderMap at3HeaderMap[] = {
	{ 0x00C0, 1, 0 }, // 132/2 (66) kbps mono
	{ 0x0098, 1, 0 }, // 105/2 (52.5) kbps mono
	{ 0x0180, 2, 0 }, // 132 kbps stereo
	{ 0x0130, 2, 0 }, // 105 kbps stereo
	// At this size, stereo can only use joint stereo.
	{ 0x00C0, 2, 1 }, // 66 kbps stereo
};

static int sceAtracLowLevelInitDecoder(int atracID, u32 paramsAddr) {
	AtracBase *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_ATRACID, "bad atrac ID");
	}

	if (!Memory::IsValidAddress(paramsAddr)) {
		// TODO: Returning zero as code was before.  Needs testing.
		return hleReportError(Log::ME, 0, "invalid pointers");
	}

	bool jointStereo = false;
	if (atrac->GetTrack().codecType == PSP_MODE_AT_3) {
		// See if we can match the actual jointStereo value.
		bool found = false;
		for (size_t i = 0; i < ARRAY_SIZE(at3HeaderMap); ++i) {
			if (at3HeaderMap[i].Matches(atrac)) {
				jointStereo = at3HeaderMap[i].jointStereo;
				found = true;
			}
		}
		if (!found) {
			WARN_LOG_REPORT_ONCE(at3headermap, Log::ME, "AT3 header map lacks entry for bpf: %i  channels: %i", atrac->GetTrack().BytesPerFrame(), atrac->GetTrack().channels);
			// TODO: Should we return an error code for these values?
		}
	}

	atrac->InitLowLevel(paramsAddr, jointStereo, atracID);

	const char *codecName = atrac->GetTrack().codecType == PSP_MODE_AT_3 ? "atrac3" : "atrac3+";
	const char *channelName = atrac->GetTrack().channels == 1 ? "mono" : "stereo";
	return hleLogInfo(Log::ME, 0, "%s %s audio", codecName, channelName);
}

static int sceAtracLowLevelDecode(int atracID, u32 sourceAddr, u32 sourceBytesConsumedAddr, u32 samplesAddr, u32 sampleBytesAddr) {
	auto srcp = PSPPointer<u8>::Create(sourceAddr);
	auto srcConsumed = PSPPointer<u32_le>::Create(sourceBytesConsumedAddr);
	auto outp = PSPPointer<s16>::Create(samplesAddr);
	auto outWritten = PSPPointer<u32_le>::Create(sampleBytesAddr);

	AtracBase *atrac = getAtrac(atracID);
	if (!atrac) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_BAD_ATRACID, "bad atrac ID");
	}

	if (!srcp.IsValid() || !srcConsumed.IsValid() || !outp.IsValid() || !outWritten.IsValid()) {
		// TODO: Returning zero as code was before.  Needs testing.
		return hleReportError(Log::ME, 0, "invalid pointers");
	}

	int bytesConsumed = 0;
	int outSamples = 0;
	int channels = atrac->GetOutputChannels();
	atrac->Decoder()->Decode(srcp, atrac->GetTrack().BytesPerFrame(), &bytesConsumed, channels, outp, &outSamples);
	int bytesWritten = outSamples * channels * sizeof(int16_t);
	*srcConsumed = bytesConsumed;
	*outWritten = bytesWritten;

	NotifyMemInfo(MemBlockFlags::WRITE, samplesAddr, bytesWritten, "AtracLowLevelDecode");
	return hleDelayResult(hleLogDebug(Log::ME, 0), "low level atrac decode data", atracDecodeDelay);
}

static int sceAtracSetAA3HalfwayBufferAndGetID(u32 buffer, u32 readSize, u32 bufferSize, u32 fileSize) {
	if (readSize > bufferSize) {
		return hleLogError(Log::ME, SCE_ERROR_ATRAC_INCORRECT_READ_SIZE, "read size too large");
	}

	AtracBase *atrac = allocAtrac();
	int ret = atrac->AnalyzeAA3(buffer, readSize, fileSize);
	if (ret < 0) {
		delete atrac;
		return hleLogError(Log::ME, ret);
	}
	int atracID = createAtrac(atrac);
	if (atracID < 0) {
		delete atrac;
		return hleLogError(Log::ME, atracID, "no free ID");
	}

	return _AtracSetData(atracID, buffer, readSize, bufferSize, 2, true);
}

// External interface used by sceSas' AT3 integration.

u32 AtracSasAddStreamData(int atracID, u32 bufPtr, u32 bytesToAdd) {
	AtracBase *atrac = getAtrac(atracID);
	if (!atrac)
		return hleLogWarning(Log::ME, 0, "bad atrac ID");
	return atrac->AddStreamDataSas(bufPtr, bytesToAdd);
}

u32 AtracSasDecodeData(int atracID, u8* outbuf, u32 outbufPtr, u32 *SamplesNum, u32* finish, int *remains) {
	AtracBase *atrac = getAtrac(atracID);
	if (!atrac)
		return hleLogWarning(Log::ME, 0, "bad atrac ID");
	return atrac->DecodeData(outbuf, outbufPtr, SamplesNum, finish, remains);
}

int AtracSasGetIDByContext(u32 contextAddr) {
	int atracID = (int)Memory::Read_U32(contextAddr + 0xfc);
	// Restored old hack here that forces outputChannels_ to 1, since sceSas expects mono output, unlike normal usage.
	// This is for savestate compatibility.
	// I think it would be better to simply pass in a 1 as a parameter to atrac->DecodeData in AtracSasDecodeData above.
	AtracBase *atrac = getAtrac(atracID);
	atrac->SetOutputChannels(1);
	return atracID;
}

const char *AtracStatusToString(AtracStatus status) {
	switch (status) {
	case ATRAC_STATUS_NO_DATA: return "NO_DATA";
	case ATRAC_STATUS_ALL_DATA_LOADED: return "ALL_DATA_LOADED";
	case ATRAC_STATUS_HALFWAY_BUFFER: return "HALFWAY_BUFFER";
	case ATRAC_STATUS_STREAMED_WITHOUT_LOOP: return "STREAMED_WITHOUT_LOOP";
	case ATRAC_STATUS_STREAMED_LOOP_FROM_END: return "STREAMED_LOOP_FROM_END";
	case ATRAC_STATUS_STREAMED_LOOP_WITH_TRAILER: return "STREAMED_LOOP_WITH_TRAILER";
	case ATRAC_STATUS_LOW_LEVEL: return "LOW_LEVEL";
	case ATRAC_STATUS_FOR_SCESAS: return "FOR_SCESAS";
	default: return "(unknown!)";
	}
}

const HLEFunction sceAtrac3plus[] = {
	{0X7DB31251, &WrapU_IU<sceAtracAddStreamData>,                 "sceAtracAddStreamData",                'x', "ix"   },
	{0X6A8C3CD5, &WrapU_IUUUU<sceAtracDecodeData>,                 "sceAtracDecodeData",                   'x', "ixppp"},
	{0XD5C28CC0, &WrapU_V<sceAtracEndEntry>,                       "sceAtracEndEntry",                     'x', ""     },
	{0X780F88D1, &WrapU_I<sceAtracGetAtracID>,                     "sceAtracGetAtracID",                   'i', "x"    },
	{0XCA3CA3D2, &WrapU_IIU<sceAtracGetBufferInfoForResetting>,    "sceAtracGetBufferInfoForReseting",     'x', "iix"  },
	{0XA554A158, &WrapU_IU<sceAtracGetBitrate>,                    "sceAtracGetBitrate",                   'x', "ip"   },
	{0X31668BAA, &WrapU_IU<sceAtracGetChannel>,                    "sceAtracGetChannel",                   'x', "ip"   },
	{0XFAA4F89B, &WrapU_IUU<sceAtracGetLoopStatus>,                "sceAtracGetLoopStatus",                'x', "ipp"  },
	{0XE88F759B, &WrapU_IU<sceAtracGetInternalErrorInfo>,          "sceAtracGetInternalErrorInfo",         'x', "ip"   },
	{0XD6A5F2F7, &WrapU_IU<sceAtracGetMaxSample>,                  "sceAtracGetMaxSample",                 'x', "ip"   },
	{0XE23E3A35, &WrapU_IU<sceAtracGetNextDecodePosition>,         "sceAtracGetNextDecodePosition",        'x', "ip"   },
	{0X36FAABFB, &WrapU_IU<sceAtracGetNextSample>,                 "sceAtracGetNextSample",                'x', "ip"   },
	{0X9AE849A7, &WrapU_IU<sceAtracGetRemainFrame>,                "sceAtracGetRemainFrame",               'x', "ip"   },
	{0X83E85EA0, &WrapU_IUU<sceAtracGetSecondBufferInfo>,          "sceAtracGetSecondBufferInfo",          'x', "ipp"  },
	{0XA2BBA8BE, &WrapU_IUUU<sceAtracGetSoundSample>,              "sceAtracGetSoundSample",               'x', "ippp" },
	{0X5D268707, &WrapU_IUUU<sceAtracGetStreamDataInfo>,           "sceAtracGetStreamDataInfo",            'x', "ippp" },
	{0X61EB33F5, &WrapU_I<sceAtracReleaseAtracID>,                 "sceAtracReleaseAtracID",               'x', "i"    },
	{0X644E5607, &WrapU_IIII<sceAtracResetPlayPosition>,           "sceAtracResetPlayPosition",            'x', "iiii" },
	{0X3F6E26B5, &WrapU_IUUU<sceAtracSetHalfwayBuffer>,            "sceAtracSetHalfwayBuffer",             'x', "ixxx" },
	{0X83BF7AFD, &WrapU_IUU<sceAtracSetSecondBuffer>,              "sceAtracSetSecondBuffer",              'x', "ixx"  },
	{0X0E2A73AB, &WrapU_IUU<sceAtracSetData>,                      "sceAtracSetData",                      'x', "ixx"  },
	{0X7A20E7AF, &WrapI_UI<sceAtracSetDataAndGetID>,               "sceAtracSetDataAndGetID",              'i', "xx"   },
	{0XD1F59FDB, &WrapU_V<sceAtracStartEntry>,                     "sceAtracStartEntry",                   'x', ""     },
	{0X868120B5, &WrapU_II<sceAtracSetLoopNum>,                    "sceAtracSetLoopNum",                   'x', "ii"   },
	{0X132F1ECA, &WrapI_II<sceAtracReinit>,                        "sceAtracReinit",                       'x', "ii"   },
	{0XECA32A99, &WrapI_I<sceAtracIsSecondBufferNeeded>,           "sceAtracIsSecondBufferNeeded",         'i', "i"    },
	{0X0FAE370E, &WrapI_UUU<sceAtracSetHalfwayBufferAndGetID>,     "sceAtracSetHalfwayBufferAndGetID",     'i', "xxx"  },
	{0X2DD3E298, &WrapU_IIU<sceAtracGetBufferInfoForResetting>,    "sceAtracGetBufferInfoForResetting",    'x', "iix"  },
	{0X5CF9D852, &WrapI_IUUU<sceAtracSetMOutHalfwayBuffer>,        "sceAtracSetMOutHalfwayBuffer",         'x', "ixxx" },
	{0XF6837A1A, &WrapU_IUU<sceAtracSetMOutData>,                  "sceAtracSetMOutData",                  'x', "ixx"  },
	{0X472E3825, &WrapI_UU<sceAtracSetMOutDataAndGetID>,           "sceAtracSetMOutDataAndGetID",          'i', "xx"   },
	{0X9CD7DE03, &WrapI_UUU<sceAtracSetMOutHalfwayBufferAndGetID>, "sceAtracSetMOutHalfwayBufferAndGetID", 'i', "xxx"  },
	{0XB3B5D042, &WrapI_IU<sceAtracGetOutputChannel>,              "sceAtracGetOutputChannel",             'x', "ip"   },
	{0X5622B7C1, &WrapI_UUUU<sceAtracSetAA3DataAndGetID>,          "sceAtracSetAA3DataAndGetID",           'i', "xxxp" },
	{0X5DD66588, &WrapI_UUUU<sceAtracSetAA3HalfwayBufferAndGetID>, "sceAtracSetAA3HalfwayBufferAndGetID",  'i', "xxxx" },
	{0X231FC6B7, &WrapU_I<_sceAtracGetContextAddress>,             "_sceAtracGetContextAddress",           'x', "i"    },
	{0X1575D64B, &WrapI_IU<sceAtracLowLevelInitDecoder>,           "sceAtracLowLevelInitDecoder",          'x', "ix"   },
	{0X0C116E1B, &WrapI_IUUUU<sceAtracLowLevelDecode>,             "sceAtracLowLevelDecode",               'x', "ixpxp"},
};

void Register_sceAtrac3plus() {
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
