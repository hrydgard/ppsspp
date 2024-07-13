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

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceP3da.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"


static u32 sceP3daBridgeInit(u32 channelsNum, u32 samplesNum)
{
	ERROR_LOG_REPORT(Log::sceAudio, "UNIMPL sceP3daBridgeInit(%08x, %08x)", channelsNum, samplesNum);
	return 0;
}

static u32 sceP3daBridgeExit()
{
	ERROR_LOG_REPORT(Log::sceAudio, "UNIMPL sceP3daBridgeExit()");
	return 0;
}

static inline int getScaleValue(u32 channelsNum) {
	int val = 0;
	while (channelsNum > 1) {
		channelsNum >>= 1;
		val++;
	}
	return val;
}

static u32 sceP3daBridgeCore(u32 p3daCoreAddr, u32 channelsNum, u32 samplesNum, u32 inputAddr, u32 outputAddr)
{
	DEBUG_LOG(Log::sceAudio, "sceP3daBridgeCore(%08x, %08x, %08x, %08x, %08x)", p3daCoreAddr, channelsNum, samplesNum, inputAddr, outputAddr);
	if (Memory::IsValidAddress(inputAddr) && Memory::IsValidAddress(outputAddr)) {
		int scaleval = getScaleValue(channelsNum);
		s16_le *outbuf = (s16_le *)Memory::GetPointerWriteUnchecked(outputAddr);
		memset(outbuf, 0, samplesNum * sizeof(s16) * 2);
		for (u32 k = 0; k < channelsNum; k++) {
			u32 inaddr = Memory::Read_U32(inputAddr + k * 4);
			const s16 *inbuf = (const s16 *)Memory::GetPointerUnchecked(inaddr);
			if (!inbuf)
				continue;
			for (u32 i = 0; i < samplesNum; i++) {
				s16 sample = inbuf[i] >> scaleval;
				outbuf[i*2] += sample;
				outbuf[i*2 + 1] += sample;
			}
		}
	}
	// same as sas core
	return hleDelayResult(0, "p3da core", 240);
}

const HLEFunction sceP3da[] =
{
	{0X374500A5, &WrapU_UU<sceP3daBridgeInit>,       "sceP3daBridgeInit", 'x', "xx"   },
	{0X43F756A2, &WrapU_V<sceP3daBridgeExit>,        "sceP3daBridgeExit", 'x', ""     },
	{0X013016F3, &WrapU_UUUUU<sceP3daBridgeCore>,    "sceP3daBridgeCore", 'x', "xxxxx"},
};

void Register_sceP3da()
{
	RegisterModule("sceP3da", ARRAY_SIZE(sceP3da), sceP3da);
}
