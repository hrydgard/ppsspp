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
#include "Core/Reporting.h"


u32 sceMp4Init()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceMp4Init()");
  	return 0;
}

u32 sceMp4Finish()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4Finish()");
	return 0;
}

u32 sceMp4Create()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceMp4Create()");
	return 0;
}

u32 sceMp4GetNumberOfSpecificTrack()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetNumberOfSpecificTrack()");
	return 1;
}

u32 sceMp4GetMovieInfo()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetMovieInfo()");
	return 0;
}

u32 sceMp4CreatesceMp4GetNumberOfMetaData()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetNumberOfMetaData()");
	return 0;
}

u32 sceMp4Delete()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4Delete()");
	return 0;
}

u32 sceMp4AacDecodeInitResource()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4AacDecodeInitResource()");
	return 0;
}

u32 sceMp4GetAvcTrackInfoData()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetAvcTrackInfoData()");
	return 0;
}

u32 sceMp4TrackSampleBufConstruct()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4TrackSampleBufConstruct()");
	return 0;
}

u32 sceMp4TrackSampleBufQueryMemSize()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4TrackSampleBufQueryMemSize()");
	return 0;
}

u32 sceMp4GetSampleInfo()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetSampleInfo()");
	return 0;
}

u32 sceMp4GetSampleNumWithTimeStamp()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetSampleNumWithTimeStamp()");
	return 0;
}

u32 sceMp4TrackSampleBufFlush()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4TrackSampleBufFlush()");
	return 0;
}

u32 sceMp4AacDecodeInit()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4AacDecodeInit()");
	return 0;
}

u32 sceMp4GetAacTrackInfoData()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetAacTrackInfoData()");
	return 0;
}

u32 sceMp4GetNumberOfMetaData()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4GetNumberOfMetaData()");
	return 0;
}

u32 sceMp4RegistTrack()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4RegistTrack()");
	return 0;
}

u32 sceMp4SearchSyncSampleNum()
{
	ERROR_LOG(HLE, "UNIMPL sceMp4SearchSyncSampleNum()");
	return 0;
}

const HLEFunction sceMp4[] =
{
	{0x68651CBC, WrapU_V<sceMp4Init>, "sceMp4Init"},
	{0x9042B257, WrapU_V<sceMp4Finish>, "sceMp4Finish"},
	{0xB1221EE7, WrapU_V<sceMp4Create>, "sceMp4Create"},
	{0x538C2057, WrapU_V<sceMp4Delete>, "sceMp4Delete"},
	{0x113E9E7B, WrapU_V<sceMp4GetNumberOfMetaData>, "sceMp4GetNumberOfMetaData"},
	{0x7443AF1D, WrapU_V<sceMp4GetMovieInfo>, "sceMp4GetMovieInfo"},
	{0x5EB65F26, WrapU_V<sceMp4GetNumberOfSpecificTrack>, "sceMp4GetNumberOfSpecificTrack"},
	{0x7ADFD01C, WrapU_V<sceMp4RegistTrack>, "sceMp4RegistTrack"},
	{0xBCA9389C, WrapU_V<sceMp4TrackSampleBufQueryMemSize>, "sceMp4TrackSampleBufQueryMemSize"},
	{0x9C8F4FC1, WrapU_V<sceMp4TrackSampleBufConstruct>, "sceMp4TrackSampleBufConstruct"},
	{0x0F0187D2, WrapU_V<sceMp4GetAvcTrackInfoData>, "sceMp4GetAvcTrackInfoData"},
	{0x9CE6F5CF, WrapU_V<sceMp4GetAacTrackInfoData>, "sceMp4GetAacTrackInfoData"},
	{0x4ED4AB1E, WrapU_V<sceMp4AacDecodeInitResource>, "sceMp4AacDecodeInitResource"},
	{0x10EE0D2C, WrapU_V<sceMp4AacDecodeInit>, "sceMp4AacDecodeInit"},
	{0x496E8A65, WrapU_V<sceMp4TrackSampleBufFlush>, "sceMp4TrackSampleBufFlush"},
	{0xB4B400D1, WrapU_V<sceMp4GetSampleNumWithTimeStamp>, "sceMp4GetSampleNumWithTimeStamp"},
	{0xF7C51EC1, WrapU_V<sceMp4GetSampleInfo>, "sceMp4GetSampleInfo"},
	{0x74A1CA3E, WrapU_V<sceMp4SearchSyncSampleNum>, "sceMp4SearchSyncSampleNum"},
	{0xD8250B75, 0, "sceMp4PutSampleNum"},
	{0x8754ECB8, 0, "sceMp4TrackSampleBufAvailableSize"},
	{0x31BCD7E0, 0, "sceMp4TrackSampleBufPut"},
	{0x5601A6F0, 0, "sceMp4GetAacAu"},
	{0x7663CB5C, 0, "sceMp4AacDecode"},
	{0x503A3CBA, 0, "sceMp4GetAvcAu"},
	{0x01C76489, 0, "sceMp4TrackSampleBufDestruct"},
	{0x6710FE77, 0, "sceMp4UnregistTrack"},
	{0x5D72B333, 0, "sceMp4AacDecodeExit"},
	{0x7D332394, 0, "sceMp4AacDecodeTermResource"},
	{0x131BDE57, 0, "sceMp4InitAu"},
	{0x17EAA97D, 0, "sceMp4GetAvcAuWithoutSampleBuf"},
	{0x28CCB940, 0, "sceMp4GetTrackEditList"},
	{0x3069C2B5, 0, "sceMp4GetAvcParamSet"},
	{0xD2AC9A7E, 0, "sceMp4GetMetaData"},
	{0x4FB5B756, 0, "sceMp4GetMetaDataInfo"},
	{0x427BEF7F, 0, "sceMp4GetTrackNumOfEditList"},
	{0x532029B8, 0, "sceMp4GetAacAuWithoutSampleBuf"},
	{0xA6C724DC, 0, "sceMp4GetSampleNum"},
	{0x3C2183C7, 0, "mp4msv_3C2183C7"},
	{0x9CA13D1A, 0, "mp4msv_9CA13D1A"},
};

void Register_sceMp4()
{
	RegisterModule("sceMp4", ARRAY_SIZE(sceMp4), sceMp4);
}
