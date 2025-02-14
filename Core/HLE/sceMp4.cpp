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

static u32 sceMp4Init() {
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4Finish() {
	return hleLogError(Log::ME, 0);
}

static u32 sceMp4Create(u32 mp4, u32 callbacks, u32 readBufferAddr, u32 readBufferSize) {
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceMp4Create(mp4 %i,callbacks %08x,readBufferAddr %08x,readBufferSize %i)", mp4, callbacks, readBufferAddr, readBufferSize);
	return 0;
}

static u32 sceMp4GetNumberOfSpecificTrack() {
	return hleLogError(Log::ME, 1, "UNIMPL");
}

static u32 sceMp4GetMovieInfo(u32 mp4, u32 unknown2) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4TrackSampleBufAvailableSize(u32 mp4, u32 trackAddr, u32 writableSamplesAddr, u32 writableBytesAddr) {
	return hleLogError(Log::ME, 0, "unimplemented");
}

static u32 sceMp4Delete(u32 mp4) {
	return hleLogError(Log::ME, 0, "unimplemented");
}

static u32 sceMp4AacDecodeInitResource(int unknown) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4InitAu(u32 mp4, u32 unknown2, u32 auAddr) {
	// unknown2 = return value of sceMpegAvcResourceGetAvcEsBuf()
	return hleLogError(Log::ME, 0, "UNIMPL (mp4 %i,unknown2 %08x,auAddr %08x)", mp4, unknown2, auAddr);
}

static u32 sceMp4GetAvcAu(u32 mp4, u32 unknown2, u32 auAddr, u32 unknown4)
{
	// unknown2 = return value of sceMpegAvcResourceGetAvcEsBuf()
	return hleLogError(Log::ME, 0, "UNIMPL (mp4 %i,unknown2 %08x,auAddr %08x,unknown4 %08x)", mp4, unknown2, auAddr, unknown4);
}

static u32 sceMp4GetAvcTrackInfoData() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4TrackSampleBufConstruct(u32 mp4, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5, u32 unknown6, u32 unknown7) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4TrackSampleBufQueryMemSize(u32 unknown1, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5)
{
	u32 value = std::max(unknown2 * unknown3, unknown4 << 1) + (unknown2 << 6) + unknown5 + 256;
	return hleLogWarning(Log::ME, value);
}

static u32 sceMp4AacDecode(u32 mp4, u32 auAddr, u32 bufferAddr, u32 init, u32 frequency) {
	// Decode audio:
	// - init: 1 at first call, 0 afterwards
	// - frequency: 44100
	return hleLogError(Log::ME, 0, "mp4 % i, auAddr % 08x, bufferAddr % 08x, init % i, frequency % i ", mp4, auAddr, bufferAddr, init, frequency);
	//This is hack
	//return -1;
}

static u32 sceMp4GetAacAu(u32 mp4, u32 unknown2, u32 auAddr, u32 unknown4) {
	// unknown4: pointer to a 40-bytes structure
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4GetSampleInfo() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4GetSampleNumWithTimeStamp() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4TrackSampleBufFlush() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4AacDecodeInit(int unknown) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4GetAacTrackInfoData() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4GetNumberOfMetaData() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4RegistTrack(u32 mp4, u32 unknown2, u32 unknown3, u32 callbacks, u32 unknown5) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4SearchSyncSampleNum() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 mp4msv_3C2183C7(u32 unknown1, u32 unknown2) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 mp4msv_9CA13D1A(u32 unknown1, u32 unknown2) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

const HLEFunction sceMp4[] = {
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

const HLEFunction mp4msv[] = {
	{0x3C2183C7, &WrapU_UU<mp4msv_3C2183C7>,                    "mp4msv_3C2183C7",               'x', "xx"      },
	{0x9CA13D1A, &WrapU_UU<mp4msv_9CA13D1A>,                    "mp4msv_9CA13D1A",               'x', "xx"      },
};

void Register_sceMp4() {
	RegisterModule("sceMp4", ARRAY_SIZE(sceMp4), sceMp4);
}

void Register_mp4msv() {
	RegisterModule("mp4msv", ARRAY_SIZE(mp4msv), mp4msv);
}
