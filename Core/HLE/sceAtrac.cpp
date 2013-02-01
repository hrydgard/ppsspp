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

// This is pretty much a stub implementation. Doesn't actually do anything, just tries to return values
// to keep games happy anyway. So, no ATRAC3 music until someone has reverse engineered Atrac3+.


#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../CoreTiming.h"

#include "sceKernel.h"
#include "sceUtility.h"

#define ATRAC_ERROR_API_FAIL		0x80630002
#define ATRAC_ERROR_ALL_DATA_DECODED	0x80630024

#define AT3_MAGIC		0x0270
#define AT3_PLUS_MAGIC		0xFFFE
#define PSP_MODE_AT_3_PLUS	0x00001000
#define PSP_MODE_AT_3		0x00001001

struct Atrac {
	Atrac() : decodePos(0), buffer(0), bufferSize(0) {}
	void DoState(PointerWrap &p) {
		p.Do(decodePos);
		p.Do(buffer);
		p.Do(bufferSize);
		p.DoMarker("Atrac");
	}

	u32 decodePos;
	u32 buffer;
	u32 bufferSize;
};

std::map<int, Atrac *> atracMap;

void __AtracInit()
{
}

void __AtracDoState(PointerWrap &p) {
	int n = (int) atracMap.size();
	p.Do(n);
	if (p.mode == p.MODE_READ) {
		for (auto it = atracMap.begin(), end = atracMap.end(); it != end; ++it) {
			delete it->second;
		}
		atracMap.clear();

		for (int i = 0; i < n; ++i) {
			int key;
			p.Do(key);
			Atrac *atrac = new Atrac;
			atrac->DoState(p);
			atracMap[key] = atrac;
		}
	} else {
		for (auto it = atracMap.begin(), end = atracMap.end(); it != end; ++it) {
			p.Do(it->first);
			it->second->DoState(p);
		}
	}

	p.DoMarker("sceAtrac");
}

void __AtracShutdown()
{
	for (auto it = atracMap.begin(), end = atracMap.end(); it != end; ++it) {
		delete it->second;
	}
	atracMap.clear();
}

// Temporary workaround to prevent excessive logging making games very slow.
// This is just the default cycle / 10, so about 1/10 second.
const u64 atracLogTickFrequency = 22200;
static bool atracShouldLogUnimpl(u64 &lastTicks) {
	u64 ticks = CoreTiming::GetTicks();
	bool result = ticks - lastTicks >= atracLogTickFrequency;
	lastTicks = ticks;

	return result;
}

// Switch if you want all errors.
#define ERROR_LOG_LIMITED(t, ...) { static u64 limited__lastTicks = 0; if (atracShouldLogUnimpl(limited__lastTicks)) ERROR_LOG(t, __VA_ARGS__); }
//#define ERROR_LOG_LIMITED(t, ...) { ERROR_LOG(t, __VA_ARGS__); }

Atrac *getAtrac(int atracID) {
	if (atracMap.find(atracID) == atracMap.end()) {
		return NULL;
	}
	return atracMap[atracID];
}

int createAtrac(Atrac *atrac) {
	int id = (int) atracMap.size();
	atracMap[id] = atrac;
	return id;
}

void deleteAtrac(int atracID) {
	if (atracMap.find(atracID) != atracMap.end()) {
		delete atracMap[atracID];
		atracMap.erase(atracID);
	}
}

int getCodecType(int addr) {
	int at3magic = Memory::Read_U16(addr+20);
	if (at3magic == AT3_MAGIC) {
		return PSP_MODE_AT_3;
	} else if (at3magic == AT3_PLUS_MAGIC) {
		return PSP_MODE_AT_3_PLUS;
	}
	return 0;
}

u32 sceAtracGetAtracID(int codecType)
{
	ERROR_LOG_LIMITED(HLE, "FAKE sceAtracGetAtracID(%i)", codecType);
	return createAtrac(new Atrac);
}

u32 sceAtracAddStreamData(int atracID, u32 bytesToAdd)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracAddStreamData(%i, %08x)", atracID, bytesToAdd);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	// TODO
	return 0;
}

u32 sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr)
{
	ERROR_LOG_LIMITED(HLE, "FAKE sceAtracDecodeData(%i, %08x, %08x, %08x, %08x)", atracID, outAddr, numSamplesAddr, finishFlagAddr, remainAddr);
	Atrac *atrac = getAtrac(atracID);

	Memory::Write_U16(0, outAddr);	// Write a single 16-bit stereo
	Memory::Write_U16(0, outAddr + 2);

	Memory::Write_U32(1, numSamplesAddr);
	Memory::Write_U32(1, finishFlagAddr);	// Lie that decoding is finished
	Memory::Write_U32(-1, remainAddr);	// Lie that decoding is finished

	if (atrac != NULL) {
		atrac->decodePos += 1;
	}

	return ATRAC_ERROR_ALL_DATA_DECODED;
}

u32 sceAtracEndEntry()
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracEndEntry(.)");
	return 0;
}

u32 sceAtracGetBufferInfoForReseting(int atracID, int sample, u32 bufferInfoAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetBufferInfoForReseting(%i, %i, %08x)",atracID, sample, bufferInfoAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		// TODO: Write the right stuff instead.
		Memory::Memset(bufferInfoAddr, 0, 32);
		//return -1;
	} else {
		Memory::Write_U32(atrac->buffer, bufferInfoAddr);
		Memory::Write_U32(atrac->bufferSize, bufferInfoAddr + 4);
		Memory::Write_U32(0, bufferInfoAddr + 8);
		Memory::Write_U32(0, bufferInfoAddr + 12);
		// TODO: Write the right stuff instead.
		Memory::Memset(bufferInfoAddr + 16, 0, 16);
	}
	return 0;
}

u32 sceAtracGetBitrate(int atracID, u32 outBitrateAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetBitrate(%i, %08x)", atracID, outBitrateAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(outBitrateAddr))
		Memory::Write_U32(64, outBitrateAddr);
	return 0;
}

u32 sceAtracGetChannel(int atracID, u32 channelAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetChannel(%i, %08x)", atracID, channelAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(channelAddr))
		Memory::Write_U32(2, channelAddr);
	return 0;
}

u32 sceAtracGetLoopStatus(int atracID, u32 loopNbr, u32 statusAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetLoopStatus(%i, %08x, %08x)", atracID, loopNbr, statusAddr );
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

u32 sceAtracGetInternalErrorInfo(int atracID, u32 errorAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetInternalErrorInfo(%i, %08x)", atracID, errorAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(errorAddr))
		Memory::Write_U32(0, errorAddr);
	return 0;
}

u32 sceAtracGetMaxSample(int atracID, u32 maxSamplesAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetMaxSample(%i, %08x)", atracID, maxSamplesAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	if (Memory::IsValidAddress(maxSamplesAddr))
		Memory::Write_U32(1024, maxSamplesAddr);
	return 0;
}

u32  sceAtracGetNextDecodePosition(int atracID, u32 outposAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetNextDecodePosition(%i, %08x)", atracID, outposAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	Memory::Write_U32(atrac != NULL ? atrac->decodePos : 0, outposAddr); // outpos
	return 0;
}

u32 sceAtracGetNextSample(int atracID, u32 outNAddr)
{
	ERROR_LOG_LIMITED(HLE, "FAKE sceAtracGetNextSample(%i, %08x)", atracID, outNAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	Memory::Write_U32(1, outNAddr);
	return 0;
}

u32 sceAtracGetRemainFrame(int atracID, u32 outposAddr)
{
	ERROR_LOG_LIMITED(HLE, "sceAtracGetRemainFrame(%i, %08x)", atracID, outposAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	Memory::Write_U32(12, outposAddr); // outpos
	return 0;
}

u32 sceAtracGetSecondBufferInfo(int atracID, u32 outposAddr, u32 outBytesAddr)
{
	ERROR_LOG_LIMITED(HLE, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)", atracID, outposAddr, outBytesAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	Memory::Write_U32(0, outposAddr); // outpos
	Memory::Write_U32(0x10000, outBytesAddr); // outBytes
	return 0;
}

u32 sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetSoundSample(%i, %08x, %08x, %08x)", atracID, outEndSampleAddr, outLoopStartSampleAddr, outLoopEndSampleAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	Memory::Write_U32(0x10000, outEndSampleAddr); // outEndSample
	Memory::Write_U32(-1, outLoopStartSampleAddr); // outLoopStartSample
	Memory::Write_U32(-1, outLoopEndSampleAddr); // outLoopEndSample
	return 0;
}

u32 sceAtracGetStreamDataInfo(int atracID, u32 writePointerAddr, u32 availableBytesAddr, u32 readOffsetAddr)
{
	ERROR_LOG_LIMITED(HLE, "FAKE sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x)", atracID, writePointerAddr, availableBytesAddr, readOffsetAddr);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	Memory::Write_U32(atrac ? atrac->buffer : 0, readOffsetAddr);
	Memory::Write_U32(0, availableBytesAddr);
	Memory::Write_U32(0, writePointerAddr);
	return 0;
}

u32 sceAtracReleaseAtracID(int atracID)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracReleaseAtracID(%i)", atracID);
	deleteAtrac(atracID);
	return 0;
}

u32 sceAtracResetPlayPosition(int atracID, int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracResetPlayPosition(%i, %i, %i, %i)", atracID, sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

u32 sceAtracSetHalfwayBuffer(int atracID, u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracSetHalfwayBuffer(%i, %08x, %8x, %8x)", atracID, halfBuffer, readSize, halfBufferSize);
	return 0;
}

u32 sceAtracSetSecondBuffer(int atracID, u32 secondBuffer, u32 secondBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetSecondBuffer(%i, %08x, %8x)", atracID, secondBuffer, secondBufferSize);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

u32 sceAtracSetData(int atracID, u32 buffer, u32 bufferSize)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracSetData(%i, %08x, %08x)", atracID, buffer, bufferSize);
	Atrac *atrac = getAtrac(atracID);
	if (atrac != NULL) {
		atrac->buffer = buffer;
		atrac->bufferSize = bufferSize;
	}
	return 0;
} 

int sceAtracSetDataAndGetID(u32 buffer, u32 bufferSize)
{	
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracSetDataAndGetID(%08x, %08x)", buffer, bufferSize);
	int codecType = getCodecType(buffer);

	Atrac *atrac = new Atrac();
	atrac->buffer = buffer;
	atrac->bufferSize = bufferSize;
	return createAtrac(atrac);
}

int sceAtracSetHalfwayBufferAndGetID(int atracID, u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracSetHalfwayBufferAndGetID(%i, %08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	int codecType = getCodecType(halfBuffer);

	Atrac *atrac = new Atrac();
	atrac->buffer = halfBuffer;
	atrac->bufferSize = halfBufferSize;
	return createAtrac(atrac);
}

u32 sceAtracStartEntry()
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracStartEntry(.)");
	return 0;
}

u32 sceAtracSetLoopNum(int atracID, int loopNum)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracSetLoopNum(%i, %i)", atracID, loopNum);
	return 0;
}

int sceAtracReinit()
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracReinit(..)");
	return 0;
}

int sceAtracGetOutputChannel(int atracID, u32 outputChanPtr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracGetOutputChannel(%i, %08x)", atracID, outputChanPtr);
	if (Memory::IsValidAddress(outputChanPtr))
		Memory::Write_U32(2, outputChanPtr);
	return 0;
}

int sceAtracIsSecondBufferNeeded(int atracID)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracIsSecondBufferNeeded(%i)", atracID);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

int sceAtracSetMOutHalfwayBuffer(int atracID, u32 MOutHalfBuffer, int readSize, int MOutHalfBufferSize)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracSetMOutHalfwayBuffer(%i, %08x, %i, %i)", atracID, MOutHalfBuffer, readSize, MOutHalfBufferSize);
	Atrac *atrac = getAtrac(atracID);
	if (!atrac) {
		//return -1;
	}
	return 0;
}

int sceAtracSetAA3DataAndGetID(u32 buffer, int bufferSize, int fileSize, u32 metadataSizeAddr)
{
	ERROR_LOG_LIMITED(HLE, "UNIMPL sceAtracSetAA3DataAndGetID(%08x, %i, %i, %08x)", buffer, bufferSize, fileSize, metadataSizeAddr);
	int codecType = getCodecType(buffer);

	Atrac *atrac = new Atrac();
	atrac->buffer = buffer;
	atrac->bufferSize = bufferSize;
	return createAtrac(atrac);
}

const HLEFunction sceAtrac3plus[] =
{
	{0x7db31251,WrapU_IU<sceAtracAddStreamData>,"sceAtracAddStreamData"},
	{0x6a8c3cd5,WrapU_IUUUU<sceAtracDecodeData>,"sceAtracDecodeData"},
	{0xd5c28cc0,WrapU_V<sceAtracEndEntry>,"sceAtracEndEntry"},
	{0x780f88d1,WrapU_I<sceAtracGetAtracID>,"sceAtracGetAtracID"},
	{0xca3ca3d2,WrapU_IIU<sceAtracGetBufferInfoForReseting>,"sceAtracGetBufferInfoForReseting"},
	{0xa554a158,WrapU_IU<sceAtracGetBitrate>,"sceAtracGetBitrate"},
	{0x31668baa,WrapU_IU<sceAtracGetChannel>,"sceAtracGetChannel"},
	{0xfaa4f89b,WrapU_IUU<sceAtracGetLoopStatus>,"sceAtracGetLoopStatus"},
	{0xe88f759b,WrapU_IU<sceAtracGetInternalErrorInfo>,"sceAtracGetInternalErrorInfo"},
	{0xd6a5f2f7,WrapU_IU<sceAtracGetMaxSample>,"sceAtracGetMaxSample"},
	{0xe23e3a35,WrapU_IU<sceAtracGetNextDecodePosition>,"sceAtracGetNextDecodePosition"},
	{0x36faabfb,WrapU_IU<sceAtracGetNextSample>,"sceAtracGetNextSample"},
	{0x9ae849a7,WrapU_IU<sceAtracGetRemainFrame>,"sceAtracGetRemainFrame"},
	{0x83e85ea0,WrapU_IUU<sceAtracGetSecondBufferInfo>,"sceAtracGetSecondBufferInfo"},
	{0xa2bba8be,WrapU_IUUU<sceAtracGetSoundSample>,"sceAtracGetSoundSample"},
	{0x5d268707,WrapU_IUUU<sceAtracGetStreamDataInfo>,"sceAtracGetStreamDataInfo"},
	{0x61eb33f5,WrapU_I<sceAtracReleaseAtracID>,"sceAtracReleaseAtracID"},
	{0x644e5607,WrapU_IIII<sceAtracResetPlayPosition>,"sceAtracResetPlayPosition"},
	{0x3f6e26b5,WrapU_IUUU<sceAtracSetHalfwayBuffer>,"sceAtracSetHalfwayBuffer"},
	{0x83bf7afd,WrapU_IUU<sceAtracSetSecondBuffer>,"sceAtracSetSecondBuffer"},
	{0x0E2A73AB,WrapU_IUU<sceAtracSetData>,"sceAtracSetData"}, //?
	{0x7a20e7af,WrapI_UU<sceAtracSetDataAndGetID>,"sceAtracSetDataAndGetID"},
	{0xd1f59fdb,WrapU_V<sceAtracStartEntry>,"sceAtracStartEntry"},
	{0x868120b5,WrapU_II<sceAtracSetLoopNum>,"sceAtracSetLoopNum"},
	{0x132f1eca,WrapI_V<sceAtracReinit>,"sceAtracReinit"},
	{0xeca32a99,WrapI_I<sceAtracIsSecondBufferNeeded>,"sceAtracIsSecondBufferNeeded"},
	{0x0fae370e,WrapI_IUUU<sceAtracSetHalfwayBufferAndGetID>,"sceAtracSetHalfwayBufferAndGetID"},
	{0x2DD3E298,WrapU_IIU<sceAtracGetBufferInfoForReseting>,"sceAtracGetBufferInfoForResetting"},
	{0x5CF9D852,WrapI_IUII<sceAtracSetMOutHalfwayBuffer>,"sceAtracSetMOutHalfwayBuffer"},
	{0xB3B5D042,WrapI_IU<sceAtracGetOutputChannel>,"sceAtracGetOutputChannel"},
	{0xF6837A1A,0,"sceAtracSetMOutData"},
	{0x472E3825,0,"sceAtracSetMOutDataAndGetID"},
	{0x9CD7DE03,0,"sceAtracSetMOutHalfwayBufferAndGetID"},
	{0x5622B7C1,WrapI_UIIU<sceAtracSetAA3DataAndGetID>,"sceAtracSetAA3DataAndGetID"},
	{0x5DD66588,0,"sceAtracSetAA3HalfwayBufferAndGetID"},
	{0x231FC6B7,0,"_sceAtracGetContextAddress"},
};


void Register_sceAtrac3plus()
{
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
