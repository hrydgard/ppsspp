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

#include "sceKernel.h"
#include "sceUtility.h"

#define ATRAC_ERROR_API_FAIL 0x80630002
#define ATRAC_ERROR_ALL_DATA_DECODED 0x80630024



u32 sceAtracAddStreamData(int atracID, u32 bytesToAdd)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracAddStreamData(%i, %08x)", atracID, bytesToAdd);
	return 0;
}

u32 sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracDecodeData(%i, %08x, %08x, %08x, %08x)", atracID, outAddr, numSamplesAddr, finishFlagAddr, remainAddr);

	Memory::Write_U16(0, outAddr);	// Write a single 16-bit stereo
	Memory::Write_U16(0, outAddr + 2);

	Memory::Write_U32(1, numSamplesAddr);
	Memory::Write_U32(1, finishFlagAddr);	// Lie that decoding is finished
	Memory::Write_U32(0, remainAddr);	// Lie that decoding is finished

	return 0;
}

u32 sceAtracEndEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracEndEntry(.)");
	return 0;
}

u32 sceAtracGetAtracID(int codecType)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetAtracID(%i)", codecType);
	return 0;
}

u32 sceAtracGetBufferInfoForReseting(int atracID, int sample, u32 bufferInfoAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBufferInfoForReseting(%i, %i, %08x)",atracID, sample, bufferInfoAddr);
	return 0;
}

u32 sceAtracGetBitrate(int atracID, u32 outBitrateAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBitrate(%i, %08x)", atracID, outBitrateAddr);
	return 0;
}

u32 sceAtracGetChannel(int atracID, u32 channelAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetChannel(%i, %08x)", atracID, channelAddr);
	return 0;
}

u32 sceAtracGetLoopStatus(int atracID, u32 loopNbr, u32 statusAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetLoopStatus(%i, %08x, %08x)", atracID, loopNbr, statusAddr );
	return 0;
}

u32 sceAtracGetInternalErrorInfo(int atracID, u32 errorAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetInternalErrorInfo(%i, %08x)", atracID, errorAddr);
	return 0;
}

u32 sceAtracGetMaxSample(int atracID, u32 maxSamplesAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetMaxSample(%i, %08x)", atracID, maxSamplesAddr);
	return 0;
}

u32  sceAtracGetNextDecodePosition(int atracID, u32 outposAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetNextDecodePosition(%i, %08x)", atracID, outposAddr);
	Memory::Write_U32(1, outposAddr); // outpos
	return 0;
}

u32 sceAtracGetNextSample(int atracID, u32 outNAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetNextSample(%i, %08x)", atracID, outNAddr);
	Memory::Write_U32(0, outNAddr);
	return 0;
}

u32 sceAtracGetRemainFrame(int atracID, u32 outposAddr)
{
	ERROR_LOG(HLE, "sceAtracGetRemainFrame(%i, %08x)", atracID, outposAddr);
	Memory::Write_U32(12, outposAddr); // outpos
	return 0;
}

u32 sceAtracGetSecondBufferInfo(int atracID, u32 outposAddr, u32 outBytesAddr)
{
	ERROR_LOG(HLE, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)", atracID, outposAddr, outBytesAddr);
	Memory::Write_U32(0, outposAddr); // outpos
	Memory::Write_U32(0x10000, outBytesAddr); // outBytes
	return 0;
}

u32 sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetSoundSample(%i, %08x, %08x, %08x)", atracID, outEndSampleAddr, outLoopStartSampleAddr, outLoopEndSampleAddr);
	Memory::Write_U32(0x10000, outEndSampleAddr); // outEndSample
	Memory::Write_U32(-1, outLoopStartSampleAddr); // outLoopStartSample
	Memory::Write_U32(-1, outLoopEndSampleAddr); // outLoopEndSample
	return 0;
}

u32 sceAtracGetStreamDataInfo(int atracID, u32 writePointerAddr, u32 availableBytesAddr, u32 readOffsetAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x)", atracID, writePointerAddr, availableBytesAddr, readOffsetAddr);
	Memory::Write_U32(0, readOffsetAddr);
	Memory::Write_U32(0, availableBytesAddr);
	Memory::Write_U32(0, writePointerAddr);
	return 0;
}

u32 sceAtracReleaseAtracID(int atracID)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracReleaseAtracID(%i)", atracID);
	return 0;
}

u32 sceAtracResetPlayPosition(int atracID, int sample, int bytesWrittenFirstBuf, int bytesWrittenSecondBuf)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracResetPlayPosition(%i, %i, %i, %i)", atracID, sample, bytesWrittenFirstBuf, bytesWrittenSecondBuf);
	return 0;
}

u32 sceAtracSetHalfwayBuffer(int atracID, u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBuffer(%i, %08x, %8x, %8x)", atracID, halfBuffer, readSize, halfBufferSize);
	return 0;
}

u32 sceAtracSetSecondBuffer(int atracID, u32 secondBuffer, u32 secondBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetSecondBuffer(%i, %08x, %8x)", atracID, secondBuffer, secondBufferSize);
	return 0;
}

u32 sceAtracSetData(int atracID, u32 buffer, u32 bufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetData(%i, %08x, %08x)", atracID, buffer, bufferSize);
	return 0;
} 

int sceAtracSetDataAndGetID(u32 buffer, u32 bufferSize)
{	
	ERROR_LOG(HLE, "UNIMPL sceAtracSetDataAndGetID(%08x, %08x)", buffer, bufferSize);
	return 0;
}

int sceAtracSetHalfwayBufferAndGetID(int atracID, u32 halfBuffer, u32 readSize, u32 halfBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBufferAndGetID(%i, %08x, %08x, %08x)", atracID, halfBuffer, readSize, halfBufferSize);
	return 0;
}

u32 sceAtracStartEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracStartEntry(.)");
	return 0;
}

u32 sceAtracSetLoopNum(int atracID, int loopNum)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetLoopNum(%i, %i)", atracID, loopNum);
	return 0;
}

int sceAtracReinit()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracReinit(..)");
	return 0;
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
	{0xeca32a99,0,"sceAtracIsSecondBufferNeeded"},
	{0x0fae370e,WrapI_IUUU<sceAtracSetHalfwayBufferAndGetID>,"sceAtracSetHalfwayBufferAndGetID"},
	{0x2DD3E298,0,"sceAtrac3plus_2DD3E298"},
	{0x5CF9D852,0,"sceAtracSetMOutHalfwayBuffer"},
};


void Register_sceAtrac3plus()
{
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
