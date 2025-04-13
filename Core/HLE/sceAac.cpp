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

#include "Core/HLE/sceAac.h"

#include <algorithm>

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/Reporting.h"
#include "Core/HW/SimpleAudioDec.h"

static std::map<u32, AuCtx*> aacMap;

static AuCtx *getAacCtx(u32 id) {
	if (aacMap.find(id) == aacMap.end())
		return NULL;
	return aacMap[id];
}

void __AACShutdown() {
	for (auto it = aacMap.begin(), end = aacMap.end(); it != end; it++) {
		delete it->second;
	}
	aacMap.clear();
}

void __AACDoState(PointerWrap &p) {
	auto s = p.Section("sceAAC", 0, 1);
	if (!s)
		return;

	Do(p, aacMap);
}

static u32 sceAacExit(u32 id) {
	if (aacMap.find(id) != aacMap.end()) {
		delete aacMap[id];
		aacMap.erase(id);
	} else {
		return hleLogError(Log::ME, -1, "bad aac ID");
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceAacInit(u32 id)
{
	if (!Memory::IsValidAddress(id)) {
		return hleLogError(Log::ME, ERROR_AAC_INVALID_ADDRESS, "AAC Invalid id address %08x", id);
	}

	AuCtx *aac = new AuCtx();
	aac->startPos = Memory::Read_U64(id);				// AUDIO stream start position.
	aac->endPos = Memory::Read_U32(id + 8);				// AUDIO stream end position.
	aac->AuBuf = Memory::Read_U32(id + 16);             // Input AAC data buffer.	
	aac->AuBufSize = Memory::Read_U32(id + 20);         // Input AAC data buffer size.
	aac->PCMBuf = Memory::Read_U32(id + 24);            // Output PCM data buffer.
	aac->PCMBufSize = Memory::Read_U32(id + 28);        // Output PCM data buffer size.
	aac->freq = Memory::Read_U32(id + 32);              // Frequency.
	if (aac->AuBuf == 0 || aac->PCMBuf == 0) {
		ERROR_LOG(Log::ME, "sceAacInit() AAC INVALID ADDRESS AuBuf %08x PCMBuf %08x", aac->AuBuf, aac->PCMBuf);
		delete aac;
		return hleNoLog(ERROR_AAC_INVALID_ADDRESS);
	}
	if (aac->startPos > aac->endPos) {
		ERROR_LOG(Log::ME, "sceAacInit() AAC INVALID startPos %lli endPos %lli", aac->startPos, aac->endPos);
		delete aac;
		return hleNoLog(ERROR_AAC_INVALID_PARAMETER);
	}
	if (aac->AuBufSize < 8192 || aac->PCMBufSize < 8192) {
		ERROR_LOG(Log::ME, "sceAacInit() AAC INVALID PARAMETER, bufferSize %i outputSize %i", aac->AuBufSize, aac->PCMBufSize);
		delete aac;
		return hleNoLog(ERROR_AAC_INVALID_PARAMETER);
	}
	if (aac->freq != 24000 && aac->freq != 32000 && aac->freq != 44100 && aac->freq != 48000) {
		ERROR_LOG(Log::ME, "sceAacInit() AAC INVALID freq %i", aac->freq);
		delete aac;
		return hleNoLog(ERROR_AAC_INVALID_PARAMETER);
	}

	DEBUG_LOG(Log::ME, "startPos %llx endPos %llx AuBuf %08x AuBufSize %08x PCMbuf %08x PCMbufSize %08x freq %d",
		aac->startPos, aac->endPos, aac->AuBuf, aac->AuBufSize, aac->PCMBuf, aac->PCMBufSize, aac->freq);

	aac->Channels = 2;
	aac->MaxOutputSample = aac->PCMBufSize / 4;
	aac->SetReadPos((int)aac->startPos);

	// create aac decoder
	aac->decoder = CreateAudioDecoder(PSP_CODEC_AAC);

	// close the audio if id already exist.
	if (aacMap.find(id) != aacMap.end()) {
		delete aacMap[id];
		aacMap.erase(id);
	}
	aacMap[id] = aac;

	return hleNoLog(id);
}

static u32 sceAacInitResource(u32 numberIds) {
	// Do nothing here
	WARN_LOG_REPORT(Log::ME, "sceAacInitResource(%i)", numberIds);
	return hleNoLog(0);
}

static u32 sceAacTermResource() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceAacDecode(u32 id, u32 pcmAddr) {
	// return the size of output pcm, <0 error
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogDebug(Log::ME, ctx->AuDecode(pcmAddr));
}

static u32 sceAacGetLoopNum(u32 id) {
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogInfo(Log::ME, ctx->LoopNum);
}

static u32 sceAacSetLoopNum(u32 id, int loop) {
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	ctx->LoopNum = loop;
	return hleLogInfo(Log::ME, 0);
}

static int sceAacCheckStreamDataNeeded(u32 id) {
	// return 1 to read more data stream, 0 don't read, <0 error
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogDebug(Log::ME, ctx->AuCheckStreamDataNeeded());
}

static u32 sceAacNotifyAddStreamData(u32 id, int size) {
	// check how many bytes we have read from source file
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogDebug(Log::ME, ctx->AuNotifyAddStreamData(size));
}

static u32 sceAacGetInfoToAddStreamData(u32 id, u32 buff, u32 size, u32 srcPos) {
	// read from stream position srcPos of size bytes into buff
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogDebug(Log::ME, ctx->AuGetInfoToAddStreamData(buff, size, srcPos));
}

static u32 sceAacGetMaxOutputSample(u32 id) {
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogDebug(Log::ME, ctx->MaxOutputSample);
}

static u32 sceAacGetSumDecodedSample(u32 id) {
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogDebug(Log::ME, ctx->SumDecodedSamples);
}

static u32 sceAacResetPlayPosition(u32 id) {
	INFO_LOG(Log::ME, "sceAacResetPlayPosition(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		return hleLogError(Log::ME, -1, "bad aac id");
	}
	return hleLogInfo(Log::ME, ctx->AuResetPlayPosition());
}

const HLEFunction sceAac[] = {
	{0XE0C89ACA, &WrapU_U<sceAacInit>,                           "sceAacInit",                        'x', "x"      },
	{0X33B8C009, &WrapU_U<sceAacExit>,                           "sceAacExit",                        'x', "x"      },
	{0X5CFFC57C, &WrapU_U<sceAacInitResource>,                   "sceAacInitResource",                'x', "x"      },
	{0X23D35CAE, &WrapU_V<sceAacTermResource>,                   "sceAacTermResource",                'x', ""       },
	{0X7E4CFEE4, &WrapU_UU<sceAacDecode>,                        "sceAacDecode",                      'x', "xx"     },
	{0X523347D9, &WrapU_U<sceAacGetLoopNum>,                     "sceAacGetLoopNum",                  'x', "x"      },
	{0XBBDD6403, &WrapU_UI<sceAacSetLoopNum>,                    "sceAacSetLoopNum",                  'x', "xi"     },
	{0XD7C51541, &WrapI_U<sceAacCheckStreamDataNeeded>,          "sceAacCheckStreamDataNeeded",       'i', "x"      },
	{0XAC6DCBE3, &WrapU_UI<sceAacNotifyAddStreamData>,           "sceAacNotifyAddStreamData",         'x', "xi"     },
	{0X02098C69, &WrapU_UUUU<sceAacGetInfoToAddStreamData>,      "sceAacGetInfoToAddStreamData",      'x', "xxxx"   },
	{0X6DC7758A, &WrapU_U<sceAacGetMaxOutputSample>,             "sceAacGetMaxOutputSample",          'x', "x"      },
	{0X506BF66C, &WrapU_U<sceAacGetSumDecodedSample>,            "sceAacGetSumDecodedSample",         'x', "x"      },
	{0XD2DA2BBA, &WrapU_U<sceAacResetPlayPosition>,              "sceAacResetPlayPosition",           'x', "x"      },
};

void Register_sceAac() {
	RegisterHLEModule("sceAac", ARRAY_SIZE(sceAac), sceAac);
}
