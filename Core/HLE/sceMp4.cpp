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

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceMp4.h"
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

static u32 sceMp4Init()
{
	INFO_LOG(Log::ME, "sceMp4Init()");
	return 0;
}

static u32 sceMp4Finish()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4Finish()");
	return 0;
}

static u32 sceMp4Create(u32 mp4, u32 callbacks, u32 readBufferAddr, u32 readBufferSize)
{
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceMp4Create(mp4 %i,callbacks %08x,readBufferAddr %08x,readBufferSize %i)", mp4, callbacks, readBufferAddr, readBufferSize);
	return 0;
}

static u32 sceMp4GetNumberOfSpecificTrack()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4GetNumberOfSpecificTrack()");
	return 1;
}

static u32 sceMp4GetMovieInfo(u32 mp4, u32 unknown2)
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4GetMovieInfo(mp4 %i, unknown2 %08x)",mp4, unknown2);
	return 0;
}

static u32 sceMp4TrackSampleBufAvailableSize(u32 mp4, u32 trackAddr, u32 writableSamplesAddr, u32 writableBytesAddr) {
	return hleLogError(Log::ME, 0, "unimplemented");
}

static u32 sceMp4Delete(u32 mp4) {
	return hleLogError(Log::ME, 0, "unimplemented");
}

static u32 sceMp4AacDecodeInitResource(int unknown)
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4AacDecodeInitResource(%i)",unknown);
	return 0;
}

static u32 sceMp4InitAu(u32 mp4, u32 unknown2, u32 auAddr)
{
	// unknown2 = return value of sceMpegAvcResourceGetAvcEsBuf()
	ERROR_LOG(Log::ME, "UNIMPL sceMp4InitAu(mp4 %i,unknown2 %08x,auAddr %08x)", mp4, unknown2, auAddr);
	return 0;
}

static u32 sceMp4GetAvcAu(u32 mp4, u32 unknown2, u32 auAddr, u32 unknown4)
{
	// unknown2 = return value of sceMpegAvcResourceGetAvcEsBuf()
	ERROR_LOG(Log::ME, "UNIMPL sceMp4InitAu(mp4 %i,unknown2 %08x,auAddr %08x,unknown4 %08x)", mp4, unknown2, auAddr, unknown4);
	return 0;
}


static u32 sceMp4GetAvcTrackInfoData()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4GetAvcTrackInfoData()");
	return 0;
}

static u32 sceMp4TrackSampleBufConstruct(u32 mp4, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5, u32 unknown6, u32 unknown7)
{
	// unknown4 == value returned by sceMp4_BCA9389C
	ERROR_LOG(Log::ME, "UNIMPL sceMp4TrackSampleBufConstruct(mp4 %i,unknown2 %08x,unknown3 %08x, unknown4 %08x, unknown5 %08x, unknown6 %08x, unknown7 %08x)", mp4, unknown2, unknown3, unknown4, unknown5, unknown6, unknown7);
	return 0;
}

static u32 sceMp4TrackSampleBufQueryMemSize(u32 unknown1, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5)
{
	u32 value = std::max(unknown2 * unknown3, unknown4 << 1) + (unknown2 << 6) + unknown5 + 256;
	ERROR_LOG(Log::ME, "sceMp4TrackSampleBufQueryMemSize return %i",value);
	return value;
}

static u32 sceMp4AacDecode(u32 mp4, u32 auAddr, u32 bufferAddr, u32 init, u32 frequency)
{
	// Decode audio:
	// - init: 1 at first call, 0 afterwards
	// - frequency: 44100
	ERROR_LOG(Log::ME, "sceMp4AacDecode(mp4 %i,auAddr %08x,bufferAddr %08x,init %i,frequency %i ", mp4, auAddr, bufferAddr, init, frequency);
	return 0;
	//This is hack
	//return -1;
}

static u32 sceMp4GetAacAu(u32 mp4, u32 unknown2, u32 auAddr, u32 unknown4)
{
	// unknown4: pointer to a 40-bytes structure
	ERROR_LOG(Log::ME, "sceMp4GetAacAu(mp4 %i,unknown2 %08x,auAddr %08x,unknown4 %i ", mp4, unknown2, auAddr, unknown4);
	return 0;
}

static u32 sceMp4GetSampleInfo()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4GetSampleInfo()");
	return 0;
}

static u32 sceMp4GetSampleNumWithTimeStamp()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4GetSampleNumWithTimeStamp()");
	return 0;
}

static u32 sceMp4TrackSampleBufFlush()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4TrackSampleBufFlush()");
	return 0;
}

static u32 sceMp4AacDecodeInit(int unknown)
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4AacDecodeInit(%i)",unknown);
	return 0;
}

static u32 sceMp4GetAacTrackInfoData()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4GetAacTrackInfoData()");
	return 0;
}

static u32 sceMp4GetNumberOfMetaData()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4GetNumberOfMetaData()");
	return 0;
}

static u32 sceMp4RegistTrack(u32 mp4, u32 unknown2, u32 unknown3, u32 callbacks, u32 unknown5)
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4RegistTrack(mp4 %i,unknown2 %i,unknown3 %i,callbacks %i unknown5 %i)",mp4,unknown2,unknown3,callbacks,unknown5);
	return 0;
}

static u32 sceMp4SearchSyncSampleNum()
{
	ERROR_LOG(Log::ME, "UNIMPL sceMp4SearchSyncSampleNum()");
	return 0;
}


// sceAac module starts from here

static u32 sceAacExit(u32 id)
{
	INFO_LOG(Log::ME, "sceAacExit(id %i)", id);
	if (aacMap.find(id) != aacMap.end()) {
		delete aacMap[id];
		aacMap.erase(id);
	}
	else{
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}
	return 0;
}

static u32 sceAacInit(u32 id)
{
	INFO_LOG(Log::ME, "UNIMPL sceAacInit(%08x)", id);
	if (!Memory::IsValidAddress(id)){
		ERROR_LOG(Log::ME, "sceAacInit() AAC Invalid id address %08x", id);
		return ERROR_AAC_INVALID_ADDRESS;
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
		return ERROR_AAC_INVALID_ADDRESS;
	}
	if (aac->startPos > aac->endPos) {
		ERROR_LOG(Log::ME, "sceAacInit() AAC INVALID startPos %lli endPos %lli", aac->startPos, aac->endPos);
		delete aac;
		return ERROR_AAC_INVALID_PARAMETER;
	}
	if (aac->AuBufSize < 8192 || aac->PCMBufSize < 8192) {
		ERROR_LOG(Log::ME, "sceAacInit() AAC INVALID PARAMETER, bufferSize %i outputSize %i", aac->AuBufSize, aac->PCMBufSize);
		delete aac; 
		return ERROR_AAC_INVALID_PARAMETER;
	}
	if (aac->freq != 24000 && aac->freq != 32000 && aac->freq != 44100 && aac->freq != 48000) {
		ERROR_LOG(Log::ME, "sceAacInit() AAC INVALID freq %i", aac->freq);
		delete aac;
		return ERROR_AAC_INVALID_PARAMETER;
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

	return id;
}

static u32 sceAacInitResource(u32 numberIds)
{
	// Do nothing here
	INFO_LOG_REPORT(Log::ME, "sceAacInitResource(%i)", numberIds);
	return 0;
}

static u32 sceAacTermResource()
{
	ERROR_LOG(Log::ME, "UNIMPL sceAacTermResource()");
	return 0;
}

static u32 sceAacDecode(u32 id, u32 pcmAddr)
{
	// return the size of output pcm, <0 error
	DEBUG_LOG(Log::ME, "sceAacDecode(id %i, bufferAddress %08x)", id, pcmAddr);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuDecode(pcmAddr);
}

static u32 sceAacGetLoopNum(u32 id)
{
	INFO_LOG(Log::ME, "sceAacGetLoopNum(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}
	return ctx->LoopNum;
}

static u32 sceAacSetLoopNum(u32 id, int loop)
{
	INFO_LOG(Log::ME, "sceAacSetLoopNum(id %i,loop %d)", id, loop);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	ctx->LoopNum = loop;
	return 0;
}

static int sceAacCheckStreamDataNeeded(u32 id)
{
	// return 1 to read more data stream, 0 don't read, <0 error
	DEBUG_LOG(Log::ME, "sceAacCheckStreamDataNeeded(%i)", id);

	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuCheckStreamDataNeeded();
}

static u32 sceAacNotifyAddStreamData(u32 id, int size)
{
	// check how many bytes we have read from source file
	DEBUG_LOG(Log::ME, "sceAacNotifyAddStreamData(%i, %08x)", id, size);

	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuNotifyAddStreamData(size);
}

static u32 sceAacGetInfoToAddStreamData(u32 id, u32 buff, u32 size, u32 srcPos)
{
	// read from stream position srcPos of size bytes into buff
	DEBUG_LOG(Log::ME, "sceAacGetInfoToAddStreamData(%08X, %08X, %08X, %08X)", id, buff, size, srcPos);

	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac handle %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuGetInfoToAddStreamData(buff, size, srcPos);
}

static u32 sceAacGetMaxOutputSample(u32 id)
{
	DEBUG_LOG(Log::ME, "sceAacGetMaxOutputSample(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->MaxOutputSample;
}

static u32 sceAacGetSumDecodedSample(u32 id)
{
	DEBUG_LOG(Log::ME, "sceAacGetSumDecodedSample(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->SumDecodedSamples;
}

static u32 sceAacResetPlayPosition(u32 id)
{
	INFO_LOG(Log::ME, "sceAacResetPlayPosition(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuResetPlayPosition();
}

static u32 mp4msv_3C2183C7(u32 unknown1, u32 unknown2) {
	ERROR_LOG(Log::ME, "UNIMPL mp4msv_3C2183C7(%d, %x)", unknown1, unknown2);
	return 0;
}

static u32 mp4msv_9CA13D1A(u32 unknown1, u32 unknown2) {
	ERROR_LOG(Log::ME, "UNIMPL mp4msv_9CA13D1A(%d, %x)", unknown1, unknown2);
	return 0;
}

const HLEFunction sceMp4[] =
{
	{0X68651CBC, &WrapU_V<sceMp4Init>,                           "sceMp4Init",                        'x', ""       },
	{0X9042B257, &WrapU_V<sceMp4Finish>,                         "sceMp4Finish",                      'x', ""       },
	{0XB1221EE7, &WrapU_UUUU<sceMp4Create>,                      "sceMp4Create",                      'x', "xxxx"   },
	{0X538C2057, &WrapU_U<sceMp4Delete>,                         "sceMp4Delete",                      'x', "x"      },
	{0X113E9E7B, &WrapU_V<sceMp4GetNumberOfMetaData>,            "sceMp4GetNumberOfMetaData",         'x', ""       },
	{0X7443AF1D, &WrapU_UU<sceMp4GetMovieInfo>,                  "sceMp4GetMovieInfo",                'x', "xx"     },
	{0X5EB65F26, &WrapU_V<sceMp4GetNumberOfSpecificTrack>,       "sceMp4GetNumberOfSpecificTrack",    'x', ""       },
	{0X7ADFD01C, &WrapU_UUUUU<sceMp4RegistTrack>,                "sceMp4RegistTrack",                 'x', "xxxxx"  },
	{0XBCA9389C, &WrapU_UUUUU<sceMp4TrackSampleBufQueryMemSize>, "sceMp4TrackSampleBufQueryMemSize",  'x', "xxxxx"  },
	{0X9C8F4FC1, &WrapU_UUUUUUU<sceMp4TrackSampleBufConstruct>,  "sceMp4TrackSampleBufConstruct",     'x', "xxxxxxx"},
	{0X0F0187D2, &WrapU_V<sceMp4GetAvcTrackInfoData>,            "sceMp4GetAvcTrackInfoData",         'x', ""       },
	{0X9CE6F5CF, &WrapU_V<sceMp4GetAacTrackInfoData>,            "sceMp4GetAacTrackInfoData",         'x', ""       },
	{0X4ED4AB1E, &WrapU_I<sceMp4AacDecodeInitResource>,          "sceMp4AacDecodeInitResource",       'x', "i"      },
	{0X10EE0D2C, &WrapU_I<sceMp4AacDecodeInit>,                  "sceMp4AacDecodeInit",               'x', "i"      },
	{0X496E8A65, &WrapU_V<sceMp4TrackSampleBufFlush>,            "sceMp4TrackSampleBufFlush",         'x', ""       },
	{0XB4B400D1, &WrapU_V<sceMp4GetSampleNumWithTimeStamp>,      "sceMp4GetSampleNumWithTimeStamp",   'x', ""       },
	{0XF7C51EC1, &WrapU_V<sceMp4GetSampleInfo>,                  "sceMp4GetSampleInfo",               'x', ""       },
	{0X74A1CA3E, &WrapU_V<sceMp4SearchSyncSampleNum>,            "sceMp4SearchSyncSampleNum",         'x', ""       },
	{0XD8250B75, nullptr,                                        "sceMp4PutSampleNum",                '?', ""       },
	{0X8754ECB8, &WrapU_UUUU<sceMp4TrackSampleBufAvailableSize>, "sceMp4TrackSampleBufAvailableSize", 'x', "xppp"   },
	{0X31BCD7E0, nullptr,                                        "sceMp4TrackSampleBufPut",           '?', ""       },
	{0X5601A6F0, &WrapU_UUUU<sceMp4GetAacAu>,                    "sceMp4GetAacAu",                    'x', "xxxx"   },
	{0X7663CB5C, &WrapU_UUUUU<sceMp4AacDecode>,                  "sceMp4AacDecode",                   'x', "xxxxx"  },
	{0X503A3CBA, &WrapU_UUUU<sceMp4GetAvcAu>,                    "sceMp4GetAvcAu",                    'x', "xxxx"   },
	{0X01C76489, nullptr,                                        "sceMp4TrackSampleBufDestruct",      '?', ""       },
	{0X6710FE77, nullptr,                                        "sceMp4UnregistTrack",               '?', ""       },
	{0X5D72B333, nullptr,                                        "sceMp4AacDecodeExit",               '?', ""       },
	{0X7D332394, nullptr,                                        "sceMp4AacDecodeTermResource",       '?', ""       },
	{0X131BDE57, &WrapU_UUU<sceMp4InitAu>,                       "sceMp4InitAu",                      'x', "xxx"    },
	{0X17EAA97D, nullptr,                                        "sceMp4GetAvcAuWithoutSampleBuf",    '?', ""       },
	{0X28CCB940, nullptr,                                        "sceMp4GetTrackEditList",            '?', ""       },
	{0X3069C2B5, nullptr,                                        "sceMp4GetAvcParamSet",              '?', ""       },
	{0XD2AC9A7E, nullptr,                                        "sceMp4GetMetaData",                 '?', ""       },
	{0X4FB5B756, nullptr,                                        "sceMp4GetMetaDataInfo",             '?', ""       },
	{0X427BEF7F, nullptr,                                        "sceMp4GetTrackNumOfEditList",       '?', ""       },
	{0X532029B8, nullptr,                                        "sceMp4GetAacAuWithoutSampleBuf",    '?', ""       },
	{0XA6C724DC, nullptr,                                        "sceMp4GetSampleNum",                '?', ""       },
	{0X3C2183C7, nullptr,                                        "mp4msv_3C2183C7",                   '?', ""       },
	{0X9CA13D1A, nullptr,                                        "mp4msv_9CA13D1A",                   '?', ""       },
};

// 395
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

const HLEFunction mp4msv[] = {
	{0x3C2183C7, &WrapU_UU<mp4msv_3C2183C7>,                    "mp4msv_3C2183C7",               'x', "xx"      },
	{0x9CA13D1A, &WrapU_UU<mp4msv_9CA13D1A>,                    "mp4msv_9CA13D1A",               'x', "xx"      },

};

void Register_sceMp4()
{
	RegisterModule("sceMp4", ARRAY_SIZE(sceMp4), sceMp4);
	RegisterModule("sceAac", ARRAY_SIZE(sceAac), sceAac);	
}

void Register_mp4msv()
{
	RegisterModule("mp4msv", ARRAY_SIZE(mp4msv), mp4msv);
}
