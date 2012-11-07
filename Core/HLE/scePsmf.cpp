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

#include "HLE.h"

#include "scePsmf.h"

// "Go Sudoku" is a good way to test this code...

struct PsmfData {
  u32 version;
  u32 unknown[3];
  u32 numStreams;
  u32 unknown2[5];
};


u32 scePsmfSetPsmf(u32 psmfStruct, u32 psmfData)
{
  INFO_LOG(HLE, "scePsmfSetPsmf(%08x, %08x)", psmfStruct, psmfData);
  PsmfData data = {0};
  data.version = Memory::Read_U32(psmfData + 4);
  data.numStreams = 2;
  Memory::Memcpy(psmfStruct, &data, sizeof(data));
  return 0;
}

u32 scePsmfGetNumberOfStreams(u32 psmfStruct)
{
  PsmfData *data = (PsmfData *)Memory::GetPointer(psmfStruct);
  INFO_LOG(HLE, "%i=scePsmfGetNumberOfStreams(%08x)", data->numStreams, psmfStruct);
  return data->numStreams;
}

u32 scePsmfGetNumberOfSpecificStreams(u32 psmfStruct, u32 streamType)
{
  INFO_LOG(HLE, "scePsmfGetNumberOfSpecificStreams(%08x, %08x)", psmfStruct, streamType);
  return 1;
}

u32 scePsmfSpecifyStreamWithStreamType(u32 psmfStruct, u32 streamType) // possibly more params
{
  INFO_LOG(HLE, "scePsmfSpecifyStreamWithStreamTypeFunction(%08x, %08x)", psmfStruct, streamType);
  return 0;
}

const HLEFunction scePsmf[] =
{
  {0xc22c8327,&WrapU_UU<scePsmfSetPsmf>,"scePsmfSetPsmfFunction"},
  {0xC7DB3A5B,0,"scePsmfGetCurrentStreamTypeFunction"},
  {0x28240568,0,"scePsmfGetCurrentStreamNumberFunction"},
  {0x1E6D9013,&WrapU_UU<scePsmfSpecifyStreamWithStreamType>,"scePsmfSpecifyStreamWithStreamTypeFunction"},
  {0x4BC9BDE0,0,"scePsmfSpecifyStreamFunction"},
  {0x76D3AEBA,0,"scePsmfGetPresentationStartTimeFunction"},
  {0xBD8AE0D8,0,"scePsmfGetPresentationEndTimeFunction"},
  {0xEAED89CD,&WrapU_U<scePsmfGetNumberOfStreams>,"scePsmfGetNumberOfStreamsFunction"},
  {0x7491C438,0,"scePsmfGetNumberOfEPentriesFunction"},
  {0x0BA514E5,0,"scePsmfGetVideoInfoFunction"},
  {0xA83F7113,0,"scePsmfGetAudioInfoFunction"},
  {0x971A3A90,0,"scePsmfCheckEPmapFunction"},
  {0x68d42328,&WrapU_UU<scePsmfGetNumberOfSpecificStreams>,"scePsmfGetNumberOfSpecificStreamsFunction"},
  {0x5b70fcc1,0,"scePsmfQueryStreamOffsetFunction"},
  {0x9553cc91,0,"scePsmfQueryStreamSizeFunction"},
  {0x0C120E1D,0,"scePsmfSpecifyStreamWithStreamTypeNumberFunction"},
  {0xc7db3a5b,0,"scePsmfGetCurrentStreamTypeFunction"},
  {0xB78EB9E9,0,"scePsmfGetHeaderSizeFunction"},
  {0xA5EBFE81,0,"scePsmfGetStreamSizeFunction"},
};

void scePsmfPlayerCreate() {
	DEBUG_LOG(HLE, "scePsmfPlayerCreate");
	RETURN(0);
}

void scePsmfPlayerReleasePsmf() {
	DEBUG_LOG(HLE, "scePsmfPlayerReleasePsmf");
	RETURN(0);
}


const HLEFunction scePsmfPlayer[] =
{
  {0x235d8787,scePsmfPlayerCreate,"scePsmfPlayerCreateFunction"},
  {0x1078c008,0,"scePsmfPlayerStopFunction"},
  {0x1e57a8e7,0,"scePsmfPlayerConfigPlayer"},
  {0x2beb1569,0,"scePsmfPlayerBreak"},
  {0x3d6d25a9,0,"scePsmfPlayerSetPsmfFunction"},
  {0x3ea82a4b,0,"scePsmfPlayerGetAudioOutSize"},
  {0x3ed62233,0,"scePsmfPlayerGetCurrentPts"},
  {0x46f61f8b,0,"scePsmfPlayerGetVideoData"},
  {0x68f07175,0,"scePsmfPlayerGetCurrentAudioStream"},
  {0x75f03fa2,0,"scePsmfPlayerSelectSpecificVideo"},
  {0x85461eff,0,"scePsmfPlayerSelectSpecificAudio"},
  {0x8a9ebdcd,0,"scePsmfPlayerSelectVideo"},
  {0x95a84ee5,0,"scePsmfPlayerStart"},
  {0x9b71a274,0,"scePsmfPlayerDeleteFunction"},
  {0x9ff2b2e7,0,"scePsmfPlayerGetCurrentVideoStream"},
  {0xa0b8ca55,0,"scePsmfPlayerUpdateFunction"},
  {0xa3d81169,0,"scePsmfPlayerChangePlayMode"},
  {0xb8d10c56,0,"scePsmfPlayerSelectAudio"},
  {0xb9848a74,0,"scePsmfPlayerGetAudioData"},
  {0xdf089680,0,"scePsmfPlayerGetPsmfInfo"},
  {0xe792cd94,scePsmfPlayerReleasePsmf,"scePsmfPlayerReleasePsmfFunction"},
  {0xf3efaa91,0,"scePsmfPlayerGetCurrentPlayMode"},
  {0xf8ef08a6,0,"scePsmfPlayerGetCurrentStatus"},
	{0x2D0E4E0A,0,"scePsmfPlayerSetTempBufFunction"},
	{0x58B83577,0,"scePsmfPlayerSetPsmfCBFunction"},
	{0x2673646B,0,"scePsmfVerifyPsmf"},
	{0x4E624A34,0,"scePsmfGetEPWithId"},
	{0x5F457515,0,"scePsmfGetEPidWithTimestampFunction"},
};

void Register_scePsmf() {
  RegisterModule("scePsmf",ARRAY_SIZE(scePsmf),scePsmf);
}

void Register_scePsmfPlayer() {
  RegisterModule("scePsmfPlayer",ARRAY_SIZE(scePsmfPlayer),scePsmfPlayer);
}
