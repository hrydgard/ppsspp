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
#include "sceMpeg.h"

#include <map>

// "Go Sudoku" is a good way to test this code...
const int size = 2048;
const int PSMF_VIDEO_STREAM_ID = 0xE0;
const int PSMF_AUDIO_STREAM_ID = 0xBD;
const int PSMF_AVC_STREAM = 0;
const int PSMF_ATRAC_STREAM = 1;
const int PSMF_PCM_STREAM = 2;
const int PSMF_DATA_STREAM = 3;
const int PSMF_AUDIO_STREAM = 15;

struct PsmfData {
  u32 version;
  u32 headerSize;
	u32 streamSize;
	u32 unk1;
	u32 unk2;
	u32 streamNum;
  u32 headerOffset;
};

struct PsmfEntry {
	int EPindex;
	int EPPicOffset;
	int EPPts;
	int EPOffset;
	int id;
};

class PsmfStream;

// This does NOT match the raw structure. Due to endianness etc,
// we read it manually.
// TODO: Change to work directly with the data in RAM instead of this
// JPSCP-esque class.
class Psmf {
public:
	Psmf(u32 data);
	u32 getNumStreams() { return 2; }

	u32 magic;
	u32 version;
	u32 streamOffset;
	u32 streamSize;
	u32 headerOffset;
	// 0x50
	u32 streamDataTotalSize;
	u32 presentationStartTime;
	u32 presentationEndTime;
	u32 streamDataNextBlockSize;
	u32 streamDataNextInnerBlockSize;
	int numStreams;

	int currentStreamNum;
	int currentAudioStreamNum;
	int currentVideoStreamNum;

	// parameters gotten from streams
	// I guess this is the seek information?
	u32 EPMapOffset;
	u32 EPMapEntriesNum;
	int videoWidth;
	int videoHeight;
	int audioChannels;
	int audioFrequency;

	std::map<int, PsmfStream *> streamMap;
};

class PsmfStream {
public:
	PsmfStream(int type, int channel) {
		this->type = type;
		this->channel = channel;
	}

	void readMPEGVideoStreamParams(u32 addr, Psmf *psmf) {
		int streamId = Memory::Read_U8(addr);
		int privateStreamId = Memory::Read_U8(addr + 1);
		// two unknowns here
		psmf->EPMapOffset = bswap32(Memory::Read_U32(addr + 4));
		psmf->EPMapEntriesNum = bswap32(Memory::Read_U32(addr + 8));
		psmf->videoWidth = Memory::Read_U8(addr + 12) * 16;
		psmf->videoHeight = Memory::Read_U8(addr + 13) * 16;

		INFO_LOG(HLE, "PSMF MPEG data found: id=%02x, privid=%02x, epmoff=%08x, epmnum=%08x, width=%i, height=%i",
			streamId, privateStreamId, psmf->EPMapOffset, psmf->EPMapEntriesNum, psmf->videoWidth, psmf->videoHeight);
	}

	void readPrivateAudioStreamParams(u32 addr, Psmf *psmf) {
		int streamId = Memory::Read_U8(addr);
		int privateStreamId = Memory::Read_U8(addr + 1);
		psmf->audioChannels = Memory::Read_U8(addr + 14);
		psmf->audioFrequency = Memory::Read_U8(addr + 15);
		// two unknowns here
		INFO_LOG(HLE, "PSMF private audio found: id=%02x, privid=%02x, channels=%i, freq=%i",
			streamId, privateStreamId, psmf->audioChannels, psmf->audioFrequency);
	}

	int type;
	int channel;
};


Psmf::Psmf(u32 data) {
	headerOffset = data;
	magic = Memory::Read_U32(data);
	version = Memory::Read_U32(data + 4);
	streamOffset = bswap32(Memory::Read_U32(data + 8));
	streamSize = bswap32(Memory::Read_U32(data + 12));
	streamDataTotalSize = bswap32(Memory::Read_U32(data + 0x50));
	presentationStartTime = bswap32(Memory::Read_U32(data + PSMF_FIRST_TIMESTAMP_OFFSET));
	presentationEndTime = bswap32(Memory::Read_U32(data + PSMF_LAST_TIMESTAMP_OFFSET));
	streamDataNextBlockSize = bswap32(Memory::Read_U32(data + 0x6A));
	streamDataNextInnerBlockSize = bswap32(Memory::Read_U32(data + 0x7C));
	numStreams = bswap16(Memory::Read_U16(data + 0x80));

	currentStreamNum = -1;
	currentAudioStreamNum = -1;
	currentVideoStreamNum = -1;

	for (int i = 0; i < numStreams; i++) {
		PsmfStream *stream = 0;
		u32 currentStreamAddr = data + 0x82 + i * 16;
		int streamId = Memory::Read_U8(currentStreamAddr);
		if ((streamId & PSMF_VIDEO_STREAM_ID) == PSMF_VIDEO_STREAM_ID) {
			stream = new PsmfStream(PSMF_AVC_STREAM, 0);
			stream->readMPEGVideoStreamParams(currentStreamAddr, this);
			currentVideoStreamNum++;
		} else if ((streamId & PSMF_AUDIO_STREAM_ID) == PSMF_AUDIO_STREAM_ID) {
			stream = new PsmfStream(PSMF_ATRAC_STREAM, 1);
			stream->readPrivateAudioStreamParams(currentStreamAddr, this);
			currentAudioStreamNum++;
		}
		if (stream) {
			currentStreamNum++;
			streamMap[currentStreamNum] = stream;
		}
	}
}

std::map<u32, Psmf *> psmfMap;

Psmf *getPsmf(u32 psmf)
{
	auto iter = psmfMap.find(psmf);
	if (iter != psmfMap.end())
		return iter->second;
	else
		return 0;
}

void __PsmfInit()
{
}

void __PsmfShutdown()
{
	for (auto it = psmfMap.begin(), end = psmfMap.end(); it != end; ++it)
		delete it->second;
	psmfMap.clear();
}

u32 scePsmfSetPsmf(u32 psmfStruct, u32 psmfData)
{
	INFO_LOG(HLE, "scePsmfSetPsmf(%08x, %08x)", psmfStruct, psmfData);

	Psmf *psmf = new Psmf(psmfData);
	psmfMap[psmfStruct] = psmf;

	PsmfData data = {0};
	data.version = psmf->version;
	data.headerSize = 0x800;
	data.streamSize = psmf->streamSize;
	data.streamNum = psmf->numStreams;
	data.headerOffset = psmf->headerOffset;
	Memory::WriteStruct(psmfStruct, &data);
	return 0;
}

u32 scePsmfGetNumberOfStreams(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetNumberOfStreams - invalid psmf");
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(HLE, "%i=scePsmfGetNumberOfStreams(%08x)", psmf->getNumStreams(), psmf);
	return psmf->getNumStreams();
}

u32 scePsmfGetNumberOfSpecificStreams(u32 psmfStruct, u32 streamType)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetNumberOfSpecificStreams - invalid psmf");
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(HLE, "scePsmfGetNumberOfSpecificStreams(%08x, %08x)", psmfStruct, streamType);
	return 1;
}

u32 scePsmfSpecifyStreamWithStreamType(u32 psmfStruct, u32 streamType, u32 channel)
{
	ERROR_LOG(HLE, "UNIMPL scePsmfSpecifyStreamWithStreamType(%08x, %08x, %i)", psmfStruct, streamType, channel);
	return 0;
}

u32 scePsmfSpecifyStreamWithStreamTypeNumber(u32 psmfStruct, u32 streamType, u32 typeNum)
{
	ERROR_LOG(HLE, "UNIMPL scePsmfSpecifyStreamWithStreamTypeNumber(%08x, %08x, %08x)", psmfStruct, streamType, typeNum);
	return 0;
}

u32 scePsmfGetVideoInfo(u32 psmfStruct, u32 videoInfoAddr)
{
	INFO_LOG(HLE, "scePsmfGetVideoInfo(%08x, %08x)", psmfStruct, videoInfoAddr);
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetNumberOfSpecificStreams - invalid psmf");
		return ERROR_PSMF_NOT_FOUND;
	}
	if (Memory::IsValidAddress(videoInfoAddr)) {
		Memory::Write_U32(psmf->videoWidth, videoInfoAddr);
		Memory::Write_U32(psmf->videoWidth, videoInfoAddr + 4);
	}
	return 0;
}

u32 scePsmfGetAudioInfo(u32 psmfStruct, u32 audioInfoAddr)
{
	INFO_LOG(HLE, "scePsmfGetAudioInfo(%08x, %08x)", psmfStruct, audioInfoAddr);
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetNumberOfSpecificStreams - invalid psmf");
		return ERROR_PSMF_NOT_FOUND;
	}
	if (Memory::IsValidAddress(audioInfoAddr)) {
		Memory::Write_U32(psmf->audioChannels, audioInfoAddr);
		Memory::Write_U32(psmf->audioFrequency, audioInfoAddr + 4);
	}
	return 0;
}


const HLEFunction scePsmf[] =
{
	{0xc22c8327,&WrapU_UU<scePsmfSetPsmf>,"scePsmfSetPsmfFunction"},
	{0xC7DB3A5B,0,"scePsmfGetCurrentStreamTypeFunction"},
	{0x28240568,0,"scePsmfGetCurrentStreamNumberFunction"},
	{0x1E6D9013,&WrapU_UUU<scePsmfSpecifyStreamWithStreamType>,"scePsmfSpecifyStreamWithStreamTypeFunction"},
	{0x0C120E1D,&WrapU_UUU<scePsmfSpecifyStreamWithStreamTypeNumber>,"scePsmfSpecifyStreamWithStreamTypeNumberFunction"},
	{0x4BC9BDE0,0,"scePsmfSpecifyStreamFunction"},
	{0x76D3AEBA,0,"scePsmfGetPresentationStartTimeFunction"},
	{0xBD8AE0D8,0,"scePsmfGetPresentationEndTimeFunction"},
	{0xEAED89CD,&WrapU_U<scePsmfGetNumberOfStreams>,"scePsmfGetNumberOfStreamsFunction"},
	{0x7491C438,0,"scePsmfGetNumberOfEPentriesFunction"},
	{0x0BA514E5,&WrapU_UU<scePsmfGetVideoInfo>,"scePsmfGetVideoInfoFunction"},
	{0xA83F7113,&WrapU_UU<scePsmfGetAudioInfo>,"scePsmfGetAudioInfoFunction"},
	{0x971A3A90,0,"scePsmfCheckEPmapFunction"},
	{0x68d42328,&WrapU_UU<scePsmfGetNumberOfSpecificStreams>,"scePsmfGetNumberOfSpecificStreamsFunction"},
	{0x5b70fcc1,0,"scePsmfQueryStreamOffsetFunction"},
	{0x9553cc91,0,"scePsmfQueryStreamSizeFunction"},
	{0xc7db3a5b,0,"scePsmfGetCurrentStreamTypeFunction"},
	{0xB78EB9E9,0,"scePsmfGetHeaderSizeFunction"},
	{0xA5EBFE81,0,"scePsmfGetStreamSizeFunction"},
	{0xE1283895,0,"scePsmfGetPsmfVersionFunction"},
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
