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

u32 sceAtracAddStreamData(u32 atracID, u32 addByte)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracAddStreamData(%i, %i)", atracID, addByte);
	return 0;
}

u32 sceAtracDecodeData(u32 atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr)
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
	ERROR_LOG(HLE, "UNIMPL sceAtracEndEntry");
	return 0;
}

u32 sceAtracGetAtracID(u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetAtracID");
	return 0;
}

u32 sceAtracGetBufferInfoForReseting(u32, u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBufferInfoForReseting");
	return 0;
}

u32 sceAtracGetBitrate(u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBitrate");
	return 0;
}

u32 sceAtracGetChannel(u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetChannel");
	return 0;
}

u32 sceAtracGetLoopStatus(u32, u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetLoopStatus");
	return 0;
}

u32 sceAtracGetInternalErrorInfo(u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetInternalErrorInfo");
	return 0;
}

u32 sceAtracGetMaxSample(u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetMaxSample");
	return 0;
}

u32 sceAtracGetNextDecodePosition(u32 atracID, u32 samplePositionAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetNextDecodePosition(%i, %08x)", atracID, samplePositionAddr);
	u32 *outPos = (u32*)Memory::GetPointer(samplePositionAddr);
	*outPos = 1;
	return 0;
}

u32 sceAtracGetNextSample(u32 atracID, u32 outN)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetNextSample(%i, %08x)", atracID, outN);
	Memory::Write_U32(0, outN);
	return 0;
}

u32 sceAtracGetRemainFrame(u32 atracID, u32 remainAddr)
{
	ERROR_LOG(HLE, "sceAtracGetRemainFrame(%i, %08x)",atracID,remainAddr);
	u32 *outPos = (u32*)Memory::GetPointer(remainAddr);
	*outPos = 12;
	return 0;
}

u32 sceAtracGetSecondBufferInfo(u32 atracID, u32 positionAddr, u32 dataAddr)
{
	ERROR_LOG(HLE, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)",atracID,positionAddr,dataAddr);
	u32 *outPos = (u32*)Memory::GetPointer(positionAddr);
	u32 *outBytes = (u32*)Memory::GetPointer(dataAddr);
	*outPos = 0;
	*outBytes = 0x10000;
	return 0;
}

u32 sceAtracGetSoundSample(u32 atracID, u32 endSampleAddr, u32 loopStartAddr, u32 loopEndAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetSoundSample(%i, %08x, %08x, %08x)",atracID,endSampleAddr,loopStartAddr,loopEndAddr);
	u32 *outEndSample = (u32*)Memory::GetPointer(endSampleAddr);
	u32 *outLoopStartSample = (u32*)Memory::GetPointer(loopStartAddr);
	u32 *outLoopEndSample = (u32*)Memory::GetPointer(loopEndAddr);
	*outEndSample = 0x10000;
	*outLoopStartSample = -1;
	*outLoopEndSample = -1;
	return 0;
}

u32 sceAtracGetStreamDataInfo(u32 atracID, u32 writePointerAddr, u32 availableBytesAddr, u32 readOffsetAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x)", atracID, writePointerAddr, availableBytesAddr, readOffsetAddr);
	Memory::Write_U32(0, readOffsetAddr);
	Memory::Write_U32(0, availableBytesAddr);
	Memory::Write_U32(0, writePointerAddr);
	return 0;
}

u32 sceAtracReleaseAtracID(u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracReleaseAtracID");
	return 0;
}

u32 sceAtracResetPlayPosition(u32, u32, u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracResetPlayPosition");
	return 0;
}

u32 sceAtracSetHalfwayBuffer(u32, u32, u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBuffer");
	return 0;
}

u32 sceAtracSetSecondBuffer(u32 atracID, u32 secondBufferAddr, u32 secondBufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetSecondBuffer(%i, %08x, %i)", atracID, secondBufferAddr, secondBufferSize);
	return 0;
}

u32 sceAtracSetData(u32, u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetData");
	return 0;
}

u32 sceAtracSetDataAndGetID(u32 bufferAddr, u32 bufferSize)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetDataAndGetID(%08x, %i)", bufferAddr, bufferSize);
	return 1;
}

u32 sceAtracSetHalfwayBufferAndGetID(u32, u32, u32)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBufferAndGetID");
	return 0;
}

u32 sceAtracStartEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracStartEntry");
	return 0;
}

u32 sceAtracSetLoopNum(u32 atracID, u32 loopNum)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetLoopNum(%i, %i)", atracID, loopNum);
	return 0;
}


const HLEFunction sceAtrac3plus[] =
{
	{0x7db31251,&Wrap<sceAtracAddStreamData>,"sceAtracAddStreamData"},
	{0x6a8c3cd5,&Wrap<sceAtracDecodeData>,"sceAtracDecodeData"},
	{0xd5c28cc0,&Wrap<sceAtracEndEntry>,"sceAtracEndEntry"},
	{0x780f88d1,&Wrap<sceAtracGetAtracID>,"sceAtracGetAtracID"},
	{0xca3ca3d2,&Wrap<sceAtracGetBufferInfoForReseting>,"sceAtracGetBufferInfoForReseting"},
	{0xa554a158,&Wrap<sceAtracGetBitrate>,"sceAtracGetBitrate"},
	{0x31668baa,&Wrap<sceAtracGetChannel>,"sceAtracGetChannel"},
	{0xfaa4f89b,&Wrap<sceAtracGetLoopStatus>,"sceAtracGetLoopStatus"},
	{0xe88f759b,&Wrap<sceAtracGetInternalErrorInfo>,"sceAtracGetInternalErrorInfo"},
	{0xd6a5f2f7,&Wrap<sceAtracGetMaxSample>,"sceAtracGetMaxSample"},
	{0xe23e3a35,&Wrap<sceAtracGetNextDecodePosition>,"sceAtracGetNextDecodePosition"},
	{0x36faabfb,&Wrap<sceAtracGetNextSample>,"sceAtracGetNextSample"},
	{0x9ae849a7,&Wrap<sceAtracGetRemainFrame>,"sceAtracGetRemainFrame"},
	{0x83e85ea0,&Wrap<sceAtracGetSecondBufferInfo>,"sceAtracGetSecondBufferInfo"},
	{0xa2bba8be,&Wrap<sceAtracGetSoundSample>,"sceAtracGetSoundSample"},
	{0x5d268707,&Wrap<sceAtracGetStreamDataInfo>,"sceAtracGetStreamDataInfo"},
	{0x61eb33f5,&Wrap<sceAtracReleaseAtracID>,"sceAtracReleaseAtracID"},
	{0x644e5607,&Wrap<sceAtracResetPlayPosition>,"sceAtracResetPlayPosition"},
	{0x3f6e26b5,&Wrap<sceAtracSetHalfwayBuffer>,"sceAtracSetHalfwayBuffer"},
	{0x83bf7afd,&Wrap<sceAtracSetSecondBuffer>,"sceAtracSetSecondBuffer"},
	{0x0E2A73AB,&Wrap<sceAtracSetData>,"sceAtracSetData"}, //?
	{0x7a20e7af,&Wrap<sceAtracSetDataAndGetID>,"sceAtracSetDataAndGetID"},
	{0x0eb8dc38,&Wrap<sceAtracSetHalfwayBufferAndGetID>,"sceAtracSetHalfwayBufferAndGetID"},
	{0xd1f59fdb,&Wrap<sceAtracStartEntry>,"sceAtracStartEntry"},
	{0x868120b5,&Wrap<sceAtracSetLoopNum>,"sceAtracSetLoopNum"},
	{0x132f1eca,0,"sceAtracReinit"},
	{0xeca32a99,0,"sceAtracIsSecondBufferNeeded"},
	{0x0fae370e,0,"sceAtracSetHalfwayBufferAndGetID"},
	{0x2DD3E298,0,"sceAtrac3plus_2DD3E298"},
};


void Register_sceAtrac3plus()
{
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
