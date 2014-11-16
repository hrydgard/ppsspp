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

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceMp4.h"
#include "Core/HW/SimpleAudioDec.h"

static std::map<u32, AuCtx*> aacMap;

AuCtx *getAacCtx(u32 id) {
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

	p.Do(aacMap);
}

u32 sceMp4Init()
{
	INFO_LOG(ME, "sceMp4Init()");
	return 0;
}

u32 sceMp4Finish()
{
	ERROR_LOG(ME, "UNIMPL sceMp4Finish()");
	return 0;
}

u32 sceMp4Create(u32 mp4, u32 unknown2, u32 readBufferAddr, u32 readBufferSize)
{
	ERROR_LOG_REPORT(ME, "UNIMPL sceMp4Create(mp4 %i,unknown2 %08x,readBufferAddr %08x,readBufferSize %i)", mp4, unknown2, readBufferAddr, readBufferSize);
	return 0;
}

u32 sceMp4GetNumberOfSpecificTrack()
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetNumberOfSpecificTrack()");
	return 1;
}

u32 sceMp4GetMovieInfo(u32 mp4, u32 unknown2)
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetMovieInfo(mp4 %i, unknown2 %08x)",mp4, unknown2);
	return 0;
}

u32 sceMp4TrackSampleBufAvailableSize(u32 mp4, u32 unknown2)
{
	ERROR_LOG(ME, "UNIMPL sceMp4TrackSampleBufAvailableSize(mp4 %i, unknown2 %08x)", mp4, unknown2);
	return 0;
}

u32 sceMp4CreatesceMp4GetNumberOfMetaData()
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetNumberOfMetaData()");
	return 0;
}

u32 sceMp4Delete()
{
	ERROR_LOG(ME, "UNIMPL sceMp4Delete()");
	return 0;
}

u32 sceMp4AacDecodeInitResource(int unknown)
{
	ERROR_LOG(ME, "UNIMPL sceMp4AacDecodeInitResource(%i)",unknown);
	return 0;
}

u32 sceMp4InitAu(u32 mp4, u32 unknown2, u32 auAddr)
{
	// unknown2 = return value of sceMpegAvcResourceGetAvcEsBuf()
	ERROR_LOG(ME, "UNIMPL sceMp4InitAu(mp4 %i,unknown2 %08x,auAddr %08x)", mp4, unknown2, auAddr);
	return 0;
}

u32 sceMp4GetAvcAu(u32 mp4, u32 unknown2, u32 auAddr, u32 unknown4)
{
	// unknown2 = return value of sceMpegAvcResourceGetAvcEsBuf()
	ERROR_LOG(ME, "UNIMPL sceMp4InitAu(mp4 %i,unknown2 %08x,auAddr %08x,unknown4 %08x)", mp4, unknown2, auAddr, unknown4);
	return 0;
}


u32 sceMp4GetAvcTrackInfoData()
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetAvcTrackInfoData()");
	return 0;
}

u32 sceMp4TrackSampleBufConstruct(u32 mp4, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5, u32 unknown6, u32 unknown7)
{
	// unknown4 == value returned by sceMp4_BCA9389C
	ERROR_LOG(ME, "UNIMPL sceMp4TrackSampleBufConstruct(mp4 %i,unknown2 %08x,unknown3 %08x, unknown4 %08x, unknown5 %08x, unknown6 %08x, unknown7 %08x)", mp4, unknown2, unknown3, unknown4, unknown5, unknown6, unknown7);
	return 0;
}

u32 sceMp4TrackSampleBufQueryMemSize(u32 unknown1, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5)
{
	u32 value = std::max(unknown2 * unknown3, unknown4 << 1) + (unknown2 << 6) + unknown5 + 256;
	ERROR_LOG(ME, "sceMp4TrackSampleBufQueryMemSize return %i",value);
	return value;
}

u32 sceMp4AacDecode(u32 mp4, u32 auAddr, u32 bufferAddr, u32 init, u32 frequency)
{
	// Decode audio:
	// - init: 1 at first call, 0 afterwards
	// - frequency: 44100
	ERROR_LOG(ME, "sceMp4AacDecode(mp4 %i,auAddr %08x,bufferAddr %08x,init %i,frequency %i ", mp4, auAddr, bufferAddr, init, frequency);
	return 0;
	//This is hack
	//return -1;
}

u32 sceMp4GetAacAu(u32 mp4, u32 unknown2, u32 auAddr, u32 unknown4)
{
	// unknown4: pointer to a 40-bytes structure
	ERROR_LOG(ME, "sceMp4GetAacAu(mp4 %i,unknown2 %08x,auAddr %08x,unknown4 %i ", mp4, unknown2, auAddr, unknown4);
	return 0;
}

u32 sceMp4GetSampleInfo()
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetSampleInfo()");
	return 0;
}

u32 sceMp4GetSampleNumWithTimeStamp()
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetSampleNumWithTimeStamp()");
	return 0;
}

u32 sceMp4TrackSampleBufFlush()
{
	ERROR_LOG(ME, "UNIMPL sceMp4TrackSampleBufFlush()");
	return 0;
}

u32 sceMp4AacDecodeInit(int unknown)
{
	ERROR_LOG(ME, "UNIMPL sceMp4AacDecodeInit(%i)",unknown);
	return 0;
}

u32 sceMp4GetAacTrackInfoData()
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetAacTrackInfoData()");
	return 0;
}

u32 sceMp4GetNumberOfMetaData()
{
	ERROR_LOG(ME, "UNIMPL sceMp4GetNumberOfMetaData()");
	return 0;
}

u32 sceMp4RegistTrack(u32 mp4, u32 unknown2, u32 unknown3, u32 callbacks, u32 unknown5)
{
	ERROR_LOG(ME, "UNIMPL sceMp4RegistTrack(mp4 %i,unknown2 %i,unknown3 %i,callbacks %i unknown5 %i)",mp4,unknown2,unknown3,callbacks,unknown5);
	return 0;
}

u32 sceMp4SearchSyncSampleNum()
{
	ERROR_LOG(ME, "UNIMPL sceMp4SearchSyncSampleNum()");
	return 0;
}


// sceAac module starts from here

u32 sceAacExit(u32 id)
{
	INFO_LOG(ME, "sceAacExit(id %i)", id);
	if (aacMap.find(id) != aacMap.end()) {
		delete aacMap[id];
		aacMap.erase(id);
	}
	else{
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}
	return 0;
}

u32 sceAacInit(u32 id)
{
	INFO_LOG(ME, "UNIMPL sceAacInit(%08x)", id);
	if (!Memory::IsValidAddress(id)){
		ERROR_LOG(ME, "sceAacInit() AAC Invalid id address %08x", id);
		return ERROR_AAC_INVALID_ADDRESS;
	}

	AuCtx *aac = new AuCtx();
	aac->startPos = Memory::Read_U64(id);				// Audio stream start position.
	aac->endPos = Memory::Read_U32(id + 8);				// Audio stream end position.
	aac->AuBuf = Memory::Read_U32(id + 16);             // Input AAC data buffer.	
	aac->AuBufSize = Memory::Read_U32(id + 20);         // Input AAC data buffer size.
	aac->PCMBuf = Memory::Read_U32(id + 24);            // Output PCM data buffer.
	aac->PCMBufSize = Memory::Read_U32(id + 28);        // Output PCM data buffer size.
	aac->freq = Memory::Read_U32(id + 32);              // Frequency.
	if (aac->AuBuf == 0 || aac->PCMBuf == 0) {
		ERROR_LOG(ME, "sceAacInit() AAC INVALID ADDRESS AuBuf %08x PCMBuf %08x", aac->AuBuf, aac->PCMBuf);
		delete aac;
		return ERROR_AAC_INVALID_ADDRESS;
	}
	if (aac->startPos < 0 || aac->startPos > aac->endPos) {
		ERROR_LOG(ME, "sceAacInit() AAC INVALID startPos %lli endPos %lli", aac->startPos, aac->endPos);
		delete aac;
		return ERROR_AAC_INVALID_PARAMETER;
	}
	if (aac->AuBufSize < 8192 || aac->PCMBufSize < 8192) {
		ERROR_LOG(ME, "sceAacInit() AAC INVALID PARAMETER, bufferSize %i outputSize %i", aac->AuBufSize, aac->PCMBufSize);
		delete aac; 
		return ERROR_AAC_INVALID_PARAMETER;
	}
	if (aac->freq != 24000 && aac->freq != 32000 && aac->freq != 44100 && aac->freq != 48000) {
		ERROR_LOG(ME, "sceAacInit() AAC INVALID freq %i", aac->freq);
		delete aac;
		return ERROR_AAC_INVALID_PARAMETER;
	}

	DEBUG_LOG(ME, "startPos %llx endPos %llx AuBuf %08x AuBufSize %08x PCMbuf %08x PCMbufSize %08x freq %d",
		aac->startPos, aac->endPos, aac->AuBuf, aac->AuBufSize, aac->PCMBuf, aac->PCMBufSize, aac->freq);

	aac->Channels = 2;
	aac->MaxOutputSample = aac->PCMBufSize / 4;
	aac->readPos = aac->startPos;
	aac->audioType = PSP_CODEC_AAC;

	// create aac decoder
	aac->decoder = new SimpleAudio(aac->audioType);

	// close the audio if id already exist.
	if (aacMap.find(id) != aacMap.end()) {
		delete aacMap[id];
		aacMap.erase(id);
	}
	aacMap[id] = aac;

	return id;
}

u32 sceAacInitResource(u32 numberIds)
{
	// Do nothing here
	INFO_LOG_REPORT(ME, "sceAacInitResource(%i)", numberIds);
	return 0;
}

u32 sceAacTermResource()
{
	ERROR_LOG(ME, "UNIMPL sceAacTermResource()");
	return 0;
}

u32 sceAacDecode(u32 id, u32 pcmAddr)
{
	// return the size of output pcm, <0 error
	DEBUG_LOG(ME, "sceAacDecode(id %i, bufferAddress %08x)", id, pcmAddr);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuDecode(pcmAddr);
}

u32 sceAacGetLoopNum(u32 id)
{
	INFO_LOG(ME, "sceAacGetLoopNum(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}
	return ctx->AuGetLoopNum();
}

u32 sceAacSetLoopNum(u32 id, int loop)
{
	INFO_LOG(ME, "sceAacSetLoopNum(id %i,loop %d)", id, loop);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuSetLoopNum(loop);
}

int sceAacCheckStreamDataNeeded(u32 id)
{
	// return 1 to read more data stream, 0 don't read, <0 error
	DEBUG_LOG(ME, "sceAacCheckStreamDataNeeded(%i)", id);

	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuCheckStreamDataNeeded();
}

u32 sceAacNotifyAddStreamData(u32 id, int size)
{
	// check how many bytes we have read from source file
	DEBUG_LOG(ME, "sceAacNotifyAddStreamData(%i, %08x)", id, size);

	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuNotifyAddStreamData(size);
}

u32 sceAacGetInfoToAddStreamData(u32 id, u32 buff, u32 size, u32 srcPos)
{
	// read from stream position srcPos of size bytes into buff
	DEBUG_LOG(ME, "sceAacGetInfoToAddStreamData(%08X, %08X, %08X, %08X)", id, buff, size, srcPos);

	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac handle %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuGetInfoToAddStreamData(buff, size, srcPos);
}

u32 sceAacGetMaxOutputSample(u32 id)
{
	DEBUG_LOG(ME, "sceAacGetMaxOutputSample(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuGetMaxOutputSample();
}

u32 sceAacGetSumDecodedSample(u32 id)
{
	DEBUG_LOG(ME, "sceAacGetSumDecodedSample(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuGetSumDecodedSample();
}

u32 sceAacResetPlayPosition(u32 id)
{
	INFO_LOG(ME, "sceAacResetPlayPosition(id %i)", id);
	auto ctx = getAacCtx(id);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad aac id %08x", __FUNCTION__, id);
		return -1;
	}

	return ctx->AuResetPlayPosition();
}

const HLEFunction sceMp4[] =
{
	{0x68651CBC, WrapU_V<sceMp4Init>, "sceMp4Init"},
	{0x9042B257, WrapU_V<sceMp4Finish>, "sceMp4Finish"},
	{0xB1221EE7, WrapU_UUUU<sceMp4Create>, "sceMp4Create"},
	{0x538C2057, WrapU_V<sceMp4Delete>, "sceMp4Delete"},
	{0x113E9E7B, WrapU_V<sceMp4GetNumberOfMetaData>, "sceMp4GetNumberOfMetaData"},
	{0x7443AF1D, WrapU_UU<sceMp4GetMovieInfo>, "sceMp4GetMovieInfo"},
	{0x5EB65F26, WrapU_V<sceMp4GetNumberOfSpecificTrack>, "sceMp4GetNumberOfSpecificTrack"},
	{0x7ADFD01C, WrapU_UUUUU<sceMp4RegistTrack>, "sceMp4RegistTrack"},
	{0xBCA9389C, WrapU_UUUUU<sceMp4TrackSampleBufQueryMemSize>, "sceMp4TrackSampleBufQueryMemSize"},
	{0x9C8F4FC1, WrapU_UUUUUUU<sceMp4TrackSampleBufConstruct>, "sceMp4TrackSampleBufConstruct"},
	{0x0F0187D2, WrapU_V<sceMp4GetAvcTrackInfoData>, "sceMp4GetAvcTrackInfoData"},
	{0x9CE6F5CF, WrapU_V<sceMp4GetAacTrackInfoData>, "sceMp4GetAacTrackInfoData"},
	{0x4ED4AB1E, WrapU_I<sceMp4AacDecodeInitResource>, "sceMp4AacDecodeInitResource"},
	{0x10EE0D2C, WrapU_I<sceMp4AacDecodeInit>, "sceMp4AacDecodeInit"},
	{0x496E8A65, WrapU_V<sceMp4TrackSampleBufFlush>, "sceMp4TrackSampleBufFlush"},
	{0xB4B400D1, WrapU_V<sceMp4GetSampleNumWithTimeStamp>, "sceMp4GetSampleNumWithTimeStamp"},
	{0xF7C51EC1, WrapU_V<sceMp4GetSampleInfo>, "sceMp4GetSampleInfo"},
	{0x74A1CA3E, WrapU_V<sceMp4SearchSyncSampleNum>, "sceMp4SearchSyncSampleNum"},
	{0xD8250B75, 0, "sceMp4PutSampleNum"},
	{0x8754ECB8, WrapU_UU<sceMp4TrackSampleBufAvailableSize>, "sceMp4TrackSampleBufAvailableSize"},
	{0x31BCD7E0, 0, "sceMp4TrackSampleBufPut"},
	{0x5601A6F0, WrapU_UUUU<sceMp4GetAacAu>, "sceMp4GetAacAu"},
	{0x7663CB5C, WrapU_UUUUU<sceMp4AacDecode>, "sceMp4AacDecode"},
	{0x503A3CBA, WrapU_UUUU<sceMp4GetAvcAu>, "sceMp4GetAvcAu"},
	{0x01C76489, 0, "sceMp4TrackSampleBufDestruct"},
	{0x6710FE77, 0, "sceMp4UnregistTrack"},
	{0x5D72B333, 0, "sceMp4AacDecodeExit"},
	{0x7D332394, 0, "sceMp4AacDecodeTermResource"},
	{0x131BDE57, WrapU_UUU<sceMp4InitAu>, "sceMp4InitAu"},
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

// 395
const HLEFunction sceAac[] = {
	{0xE0C89ACA, WrapU_U<sceAacInit>, "sceAacInit"},
	{0x33B8C009, WrapU_U<sceAacExit>, "sceAacExit"},
	{0x5CFFC57C, WrapU_U<sceAacInitResource>, "sceAacInitResource"},
	{0x23D35CAE, WrapU_V<sceAacTermResource>, "sceAacTermResource"},
	{0x7E4CFEE4, WrapU_UU<sceAacDecode>, "sceAacDecode"},
	{0x523347D9, WrapU_U<sceAacGetLoopNum>, "sceAacGetLoopNum"},
	{0xBBDD6403, WrapU_UI<sceAacSetLoopNum>, "sceAacSetLoopNum"},
	{0xD7C51541, WrapI_U<sceAacCheckStreamDataNeeded>, "sceAacCheckStreamDataNeeded"},
	{0xAC6DCBE3, WrapU_UI<sceAacNotifyAddStreamData>, "sceAacNotifyAddStreamData"},
	{0x02098C69, WrapU_UUUU<sceAacGetInfoToAddStreamData>, "sceAacGetInfoToAddStreamData"},
	{0x6DC7758A, WrapU_U<sceAacGetMaxOutputSample>, "sceAacGetMaxOutputSample"},
	{0x506BF66C, WrapU_U<sceAacGetSumDecodedSample>, "sceAacGetSumDecodedSample"},
	{0xD2DA2BBA, WrapU_U<sceAacResetPlayPosition>, "sceAacResetPlayPosition"},
};

void Register_sceMp4()
{
	RegisterModule("sceMp4", ARRAY_SIZE(sceMp4), sceMp4);
	RegisterModule("sceAac", ARRAY_SIZE(sceAac), sceAac);
}
