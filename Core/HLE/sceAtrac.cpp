// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

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

void sceAtracAddStreamData()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracAddStreamData(%i, %i)", PARAM(0), PARAM(1));
	RETURN(0);
}

void sceAtracDecodeData()
{
	u32 atracID = PARAM(0);
	u32 outAddr = PARAM(1);
	u32 numSamplesAddr = PARAM(2);
	u32 finishFlagAddr = PARAM(3);
	u32 remainAddr = PARAM(4);
	ERROR_LOG(HLE, "FAKE sceAtracDecodeData(%i, %08x, %08x, %08x, %08x)", atracID, outAddr, numSamplesAddr, finishFlagAddr, remainAddr);

	Memory::Write_U16(0, outAddr);	// Write a single 16-bit stereo
	Memory::Write_U16(0, outAddr + 2);

	Memory::Write_U32(1, numSamplesAddr);
	Memory::Write_U32(1, finishFlagAddr);	// Lie that decoding is finished
	Memory::Write_U32(0, remainAddr);	// Lie that decoding is finished

	RETURN(0);
}

void sceAtracEndEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracEndEntry");
	RETURN(0);
}

void sceAtracGetAtracID()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetAtracID");
	RETURN(0);
}

void sceAtracGetBufferInfoForReseting()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBufferInfoForReseting");
	RETURN(0);
}

void sceAtracGetBitrate()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBitrate");
	RETURN(0);
}

void sceAtracGetChannel()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetChannel");
	RETURN(0);
}

void sceAtracGetLoopStatus()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetLoopStatus");
	RETURN(0);
}

void sceAtracGetInternalErrorInfo()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetInternalErrorInfo");
	RETURN(0);
}

void sceAtracGetMaxSample()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetMaxSample");
	RETURN(0);
}

void sceAtracGetNextDecodePosition()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetNextDecodePosition(%i, %08x)", PARAM(0), PARAM(1));
	u32 *outPos = (u32*)Memory::GetPointer(PARAM(1));
	*outPos = 1;
	RETURN(0);
}

void sceAtracGetNextSample()
{
	u32 atracID = PARAM(0);
	u32 outN = PARAM(1);
	ERROR_LOG(HLE, "FAKE sceAtracGetNextSample(%i, %08x)", atracID, outN);
	Memory::Write_U32(0, outN);
	RETURN(0);
}

void sceAtracGetRemainFrame()
{
	ERROR_LOG(HLE, "sceAtracGetRemainFrame(%i, %08x)",PARAM(0),PARAM(1));
	u32 *outPos = (u32*)Memory::GetPointer(PARAM(1));
	*outPos = 12;
	RETURN(0);
}

void sceAtracGetSecondBufferInfo()
{
	ERROR_LOG(HLE, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)",PARAM(0),PARAM(1),PARAM(2));
	u32 *outPos = (u32*)Memory::GetPointer(PARAM(1));
	u32 *outBytes = (u32*)Memory::GetPointer(PARAM(2));
	*outPos = 0;
	*outBytes = 0x10000;
	RETURN(0);
}

void sceAtracGetSoundSample()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetSoundSample(%i, %08x, %08x, %08x)",PARAM(0),PARAM(1),PARAM(2),PARAM(3));
	u32 *outEndSample = (u32*)Memory::GetPointer(PARAM(1));
	u32 *outLoopStartSample = (u32*)Memory::GetPointer(PARAM(2));
	u32 *outLoopEndSample = (u32*)Memory::GetPointer(PARAM(2));
	*outEndSample = 0x10000;
	*outLoopStartSample = -1;
	*outLoopEndSample = -1;
	RETURN(0);
}

void sceAtracGetStreamDataInfo()
{
	u32 atracID = PARAM(0);
	u32 writePointerAddr = PARAM(1);
	u32 availableBytesAddr = PARAM(2);
	u32 readOffsetAddr = PARAM(3);
	ERROR_LOG(HLE, "FAKE sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x)", atracID, writePointerAddr, availableBytesAddr, readOffsetAddr);
	Memory::Write_U32(0, readOffsetAddr);
	Memory::Write_U32(0, availableBytesAddr);
	Memory::Write_U32(0, writePointerAddr);
	RETURN(0);
}

void sceAtracReleaseAtracID()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracReleaseAtracID");
	RETURN(0);
}

void sceAtracResetPlayPosition()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracResetPlayPosition");
	RETURN(0);
}

void sceAtracSetHalfwayBuffer()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBuffer");
	RETURN(0);
}

void sceAtracSetSecondBuffer()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetSecondBuffer(%i, %08x, %i)", PARAM(0),PARAM(1),PARAM(2));
	RETURN(0);
}

void sceAtracSetData()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetData");
	RETURN(0);
} //?

void sceAtracSetDataAndGetID()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetDataAndGetID(%08x, %i)", PARAM(0), PARAM(1));
	RETURN(1);
}

void sceAtracSetHalfwayBufferAndGetID()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetHalfwayBufferAndGetID");
	RETURN(0);
}

void sceAtracStartEntry()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracStartEntry");
	RETURN(0);
}

void sceAtracSetLoopNum()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracSetLoopNum(%i, %i)", PARAM(0), PARAM(1));
	RETURN(0);
}


const HLEFunction sceAtrac3plus[] =
{
	{0x7db31251,sceAtracAddStreamData,"sceAtracAddStreamData"},
	{0x6a8c3cd5,sceAtracDecodeData,"sceAtracDecodeData"},
	{0xd5c28cc0,sceAtracEndEntry,"sceAtracEndEntry"},
	{0x780f88d1,sceAtracGetAtracID,"sceAtracGetAtracID"},
	{0xca3ca3d2,sceAtracGetBufferInfoForReseting,"sceAtracGetBufferInfoForReseting"},
	{0xa554a158,sceAtracGetBitrate,"sceAtracGetBitrate"},
	{0x31668baa,sceAtracGetChannel,"sceAtracGetChannel"},
	{0xfaa4f89b,sceAtracGetLoopStatus,"sceAtracGetLoopStatus"},
	{0xe88f759b,sceAtracGetInternalErrorInfo,"sceAtracGetInternalErrorInfo"},
	{0xd6a5f2f7,sceAtracGetMaxSample,"sceAtracGetMaxSample"},
	{0xe23e3a35,sceAtracGetNextDecodePosition,"sceAtracGetNextDecodePosition"},
	{0x36faabfb,sceAtracGetNextSample,"sceAtracGetNextSample"},
	{0x9ae849a7,sceAtracGetRemainFrame,"sceAtracGetRemainFrame"},
	{0x83e85ea0,sceAtracGetSecondBufferInfo,"sceAtracGetSecondBufferInfo"},
	{0xa2bba8be,sceAtracGetSoundSample,"sceAtracGetSoundSample"},
	{0x5d268707,sceAtracGetStreamDataInfo,"sceAtracGetStreamDataInfo"},
	{0x61eb33f5,sceAtracReleaseAtracID,"sceAtracReleaseAtracID"},
	{0x644e5607,sceAtracResetPlayPosition,"sceAtracResetPlayPosition"},
	{0x3f6e26b5,sceAtracSetHalfwayBuffer,"sceAtracSetHalfwayBuffer"},
	{0x83bf7afd,sceAtracSetSecondBuffer,"sceAtracSetSecondBuffer"},
	{0x0E2A73AB,sceAtracSetData,"sceAtracSetData"}, //?
	{0x7a20e7af,sceAtracSetDataAndGetID,"sceAtracSetDataAndGetID"},
	{0x0eb8dc38,sceAtracSetHalfwayBufferAndGetID,"sceAtracSetHalfwayBufferAndGetID"},
	{0xd1f59fdb,sceAtracStartEntry,"sceAtracStartEntry"},
	{0x868120b5,sceAtracSetLoopNum,"sceAtracSetLoopNum"},
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
