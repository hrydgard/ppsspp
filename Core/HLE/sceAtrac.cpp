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

int sceAtracAddStreamData(int atracID, u32 bytesToAdd)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracAddStreamData(%i, %i)", atracID, bytesToAdd);
	return 0;
}

int sceAtracDecodeData(int atracID, u32 outAddr, u32 numSamplesAddr, u32 finishFlagAddr, u32 remainAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracDecodeData(%i, %08x, %08x, %08x, %08x)", atracID, outAddr, numSamplesAddr, finishFlagAddr, remainAddr);

	Memory::Write_U16(0, outAddr);	// Write a single 16-bit stereo
	Memory::Write_U16(0, outAddr + 2);

	Memory::Write_U32(1, numSamplesAddr);
	Memory::Write_U32(1, finishFlagAddr);	// Lie that decoding is finished
	Memory::Write_U32(0, remainAddr);	// Lie that decoding is finished

	return 0;
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

int sceAtracGetBitrate(int atracID, u32 outBitrateAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetBitrate");
	return 0;
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

int  sceAtracGetNextDecodePosition(int atracID, u32 outposAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetNextDecodePosition(%i, %08x)", atracID, outposAddr);
	Memory::Write_U32(1, outposAddr); // outpos
	return 0;
}

int sceAtracGetNextSample(int atracID, u32 outNAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetNextSample(%i, %08x)", atracID, outNAddr);
	Memory::Write_U32(0, outNAddr);
	return 0;
}

int sceAtracGetRemainFrame(int atracID, u32 outposAddr)
{
	ERROR_LOG(HLE, "sceAtracGetRemainFrame(%i, %08x)", atracID, outposAddr);
	Memory::Write_U32(12, outposAddr); // outpos
	return 0;
}

int sceAtracGetSecondBufferInfo(int atracID, u32 outposAddr, u32 outBytesAddr)
{
	ERROR_LOG(HLE, "sceAtracGetSecondBufferInfo(%i, %08x, %08x)", atracID, outposAddr, outBytesAddr);
	Memory::Write_U32(0, outposAddr); // outpos
	Memory::Write_U32(0x10000, outBytesAddr); // outBytes
	return 0;
}

int sceAtracGetSoundSample(int atracID, u32 outEndSampleAddr, u32 outLoopStartSampleAddr, u32 outLoopEndSampleAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceAtracGetSoundSample(%i, %08x, %08x, %08x)", atracID, outEndSampleAddr, outLoopStartSampleAddr, outLoopEndSampleAddr);
	Memory::Write_U32(0x10000, outEndSampleAddr); // outEndSample
	Memory::Write_U32(-1, outLoopStartSampleAddr); // outLoopStartSample
	Memory::Write_U32(-1, outLoopEndSampleAddr); // outLoopEndSample
	return 0;
}

int sceAtracGetStreamDataInfo(int atracID, u32 writePointerAddr, u32 availableBytesAddr, u32 readOffsetAddr)
{
	ERROR_LOG(HLE, "FAKE sceAtracGetStreamDataInfo(%i, %08x, %08x, %08x)", atracID, writePointerAddr, availableBytesAddr, readOffsetAddr);
	Memory::Write_U32(0, readOffsetAddr);
	Memory::Write_U32(0, availableBytesAddr);
	Memory::Write_U32(0, writePointerAddr);
	return 0;
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

int sceAtracReinit()
{
	ERROR_LOG(HLE, "UNIMPL sceAtracReinit(..)");
	return 0;
}


const HLEFunction sceAtrac3plus[] =
{
	{0x7db31251,WrapI_IU<sceAtracAddStreamData>,"sceAtracAddStreamData"},
	{0x6a8c3cd5,WrapI_IUUUU<sceAtracDecodeData>,"sceAtracDecodeData"},
	{0xd5c28cc0,sceAtracEndEntry,"sceAtracEndEntry"},
	{0x780f88d1,sceAtracGetAtracID,"sceAtracGetAtracID"},
	{0xca3ca3d2,sceAtracGetBufferInfoForReseting,"sceAtracGetBufferInfoForReseting"},
	{0xa554a158,WrapI_IU<sceAtracGetBitrate>,"sceAtracGetBitrate"},
	{0x31668baa,sceAtracGetChannel,"sceAtracGetChannel"},
	{0xfaa4f89b,sceAtracGetLoopStatus,"sceAtracGetLoopStatus"},
	{0xe88f759b,sceAtracGetInternalErrorInfo,"sceAtracGetInternalErrorInfo"},
	{0xd6a5f2f7,sceAtracGetMaxSample,"sceAtracGetMaxSample"},
	{0xe23e3a35,WrapI_IU<sceAtracGetNextDecodePosition>,"sceAtracGetNextDecodePosition"},
	{0x36faabfb,WrapI_IU<sceAtracGetNextSample>,"sceAtracGetNextSample"},
	{0x9ae849a7,WrapI_IU<sceAtracGetRemainFrame>,"sceAtracGetRemainFrame"},
	{0x83e85ea0,WrapI_IUU<sceAtracGetSecondBufferInfo>,"sceAtracGetSecondBufferInfo"},
	{0xa2bba8be,WrapI_IUUU<sceAtracGetSoundSample>,"sceAtracGetSoundSample"},
	{0x5d268707,WrapI_IUUU<sceAtracGetStreamDataInfo>,"sceAtracGetStreamDataInfo"},
	{0x61eb33f5,sceAtracReleaseAtracID,"sceAtracReleaseAtracID"},
	{0x644e5607,sceAtracResetPlayPosition,"sceAtracResetPlayPosition"},
	{0x3f6e26b5,sceAtracSetHalfwayBuffer,"sceAtracSetHalfwayBuffer"},
	{0x83bf7afd,sceAtracSetSecondBuffer,"sceAtracSetSecondBuffer"},
	{0x0E2A73AB,sceAtracSetData,"sceAtracSetData"}, //?
	{0x7a20e7af,sceAtracSetDataAndGetID,"sceAtracSetDataAndGetID"},
	{0xd1f59fdb,sceAtracStartEntry,"sceAtracStartEntry"},
	{0x868120b5,sceAtracSetLoopNum,"sceAtracSetLoopNum"},
	{0x132f1eca,WrapI_V<sceAtracReinit>,"sceAtracReinit"},
	{0xeca32a99,0,"sceAtracIsSecondBufferNeeded"},
	{0x0fae370e,sceAtracSetHalfwayBufferAndGetID,"sceAtracSetHalfwayBufferAndGetID"},
	{0x2DD3E298,0,"sceAtrac3plus_2DD3E298"},
	{0x5CF9D852,0,"sceAtracSetMOutHalfwayBuffer"},
};


void Register_sceAtrac3plus()
{
	// Two names
	RegisterModule("sceATRAC3plus_Library", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
	RegisterModule("sceAtrac3plus", ARRAY_SIZE(sceAtrac3plus), sceAtrac3plus);
}
