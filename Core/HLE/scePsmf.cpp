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

enum PsmfPlayerStatus {
	PSMF_PLAYER_STATUS_NONE = 0x0,
	PSMF_PLAYER_STATUS_INIT = 0x1,
	PSMF_PLAYER_STATUS_STANDBY = 0x2,
	PSMF_PLAYER_STATUS_PLAYING = 0x4,
	PSMF_PLAYER_STATUS_ERROR = 0x100,
	PSMF_PLAYER_STATUS_PLAYING_FINISHED = 0x200,
};

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
typedef std::map<int, PsmfStream *> PsmfStreamMap;

class Psmf {
public:
	Psmf(u32 data);
	~Psmf();
	u32 getNumStreams() { return 2; }
	void DoState(PointerWrap &p);

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

	PsmfStreamMap streamMap;
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

	void DoState(PointerWrap &p) {
		p.Do(type);
		p.Do(channel);
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

Psmf::~Psmf() {
	for (auto it = streamMap.begin(), end = streamMap.end(); it != end; ++it) {
		delete it->second;
	}
	streamMap.clear();
}

void Psmf::DoState(PointerWrap &p) {
	p.Do(magic);
	p.Do(version);
	p.Do(streamOffset);
	p.Do(streamSize);
	p.Do(headerOffset);
	p.Do(streamDataTotalSize);
	p.Do(presentationStartTime);
	p.Do(presentationEndTime);
	p.Do(streamDataNextBlockSize);
	p.Do(streamDataNextInnerBlockSize);
	p.Do(numStreams);

	p.Do(currentStreamNum);
	p.Do(currentAudioStreamNum);
	p.Do(currentVideoStreamNum);

	p.Do(EPMapOffset);
	p.Do(EPMapEntriesNum);
	p.Do(videoWidth);
	p.Do(videoHeight);
	p.Do(audioChannels);
	p.Do(audioFrequency);

	int n = (int) streamMap.size();
	p.Do(n);
	if (p.mode == p.MODE_READ) {
		// Already empty, if we're reading this is brand new.
		for (int i = 0; i < n; ++i) {
			int key;
			p.Do(key);
			PsmfStream *stream = new PsmfStream(0, 0);
			stream->DoState(p);
			streamMap[key] = stream;
		}
	} else {
		for (auto it = streamMap.begin(), end = streamMap.end(); it != end; ++it) {
			p.Do(it->first);
			it->second->DoState(p);
		}
	}

	p.DoMarker("Psmf");
}

static std::map<u32, Psmf *> psmfMap;
// TODO: Should have a map.
static PsmfPlayerStatus psmfPlayerStatus = PSMF_PLAYER_STATUS_NONE;

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
	psmfPlayerStatus = PSMF_PLAYER_STATUS_NONE;
}

void __PsmfDoState(PointerWrap &p)
{
	int n = (int) psmfMap.size();
	p.Do(n);
	if (p.mode == p.MODE_READ) {
		std::map<u32, Psmf *>::iterator it, end;
		for (it = psmfMap.begin(), end = psmfMap.end(); it != end; ++it) {
			delete it->second;
		}
		psmfMap.clear();

		for (int i = 0; i < n; ++i) {
			u32 key;
			p.Do(key);
			Psmf *psmf = new Psmf(0);
			psmf->DoState(p);
			psmfMap[key] = psmf;
		}
	} else {
		std::map<u32, Psmf *>::iterator it, end;
		for (it = psmfMap.begin(), end = psmfMap.end(); it != end; ++it) {
			p.Do(it->first);
			it->second->DoState(p);
		}
	}

	// TODO: Actually load this from a map.
	psmfPlayerStatus = PSMF_PLAYER_STATUS_NONE;

	p.DoMarker("scePsmf");
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
	INFO_LOG(HLE, "%i=scePsmfGetNumberOfStreams(%08x)", psmf->getNumStreams(), psmfStruct);
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

u32 scePsmfGetVideoInfo(u32 psmfStruct, u32 videoInfoAddr) {
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

u32 scePsmfGetAudioInfo(u32 psmfStruct, u32 audioInfoAddr) {
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

u32 scePsmfGetCurrentStreamType(u32 psmfStruct, u32 typeAddr, u32 channelAddr) {
	INFO_LOG(HLE, "scePsmfGetCurrentStreamType(%08x, %08x, %08x)", psmfStruct, typeAddr, channelAddr);
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetCurrentStreamType - invalid psmf");
		return ERROR_PSMF_NOT_FOUND;
	}
	if (Memory::IsValidAddress(typeAddr)) {
		u32 type = 0, channel = 0;
		if (psmf->streamMap.find(psmf->currentStreamNum) != psmf->streamMap.end())
			type = psmf->streamMap[psmf->currentStreamNum]->type;
		if (psmf->streamMap.find(psmf->currentStreamNum) != psmf->streamMap.end())
			channel = psmf->streamMap[psmf->currentStreamNum]->channel;
		Memory::Write_U32(type, typeAddr);
		Memory::Write_U32(channel, channelAddr);
	}
	return 0;
}

const HLEFunction scePsmf[] = {
	{0xc22c8327, WrapU_UU<scePsmfSetPsmf>, "scePsmfSetPsmf"},
	{0xC7DB3A5B, WrapU_UUU<scePsmfGetCurrentStreamType>, "scePsmfGetCurrentStreamType"},
	{0x28240568, 0, "scePsmfGetCurrentStreamNumber"},
	{0x1E6D9013, WrapU_UUU<scePsmfSpecifyStreamWithStreamType>, "scePsmfSpecifyStreamWithStreamType"},
	{0x0C120E1D, WrapU_UUU<scePsmfSpecifyStreamWithStreamTypeNumber>, "scePsmfSpecifyStreamWithStreamTypeNumber"},
	{0x4BC9BDE0, 0, "scePsmfSpecifyStream"},
	{0x76D3AEBA, 0, "scePsmfGetPresentationStartTime"},
	{0xBD8AE0D8, 0, "scePsmfGetPresentationEndTime"},
	{0xEAED89CD, WrapU_U<scePsmfGetNumberOfStreams>, "scePsmfGetNumberOfStreams"},
	{0x7491C438, 0, "scePsmfGetNumberOfEPentries"},
	{0x0BA514E5, WrapU_UU<scePsmfGetVideoInfo>, "scePsmfGetVideoInfo"},
	{0xA83F7113, WrapU_UU<scePsmfGetAudioInfo>, "scePsmfGetAudioInfo"},
	{0x971A3A90, 0, "scePsmfCheckEPmap"},
	{0x68d42328, WrapU_UU<scePsmfGetNumberOfSpecificStreams>, "scePsmfGetNumberOfSpecificStreams"},
	{0x5b70fcc1, 0, "scePsmfQueryStreamOffset"},
	{0x9553cc91, 0, "scePsmfQueryStreamSize"},
	{0xc7db3a5b, 0, "scePsmfGetCurrentStreamType"},
	{0xB78EB9E9, 0, "scePsmfGetHeaderSize"},
	{0xA5EBFE81, 0, "scePsmfGetStreamSize"},
	{0xE1283895, 0, "scePsmfGetPsmfVersion"},
};

int scePsmfPlayerCreate(u32 psmfPlayer, u32 dataPtr) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerCreate(%08x, %08x)", psmfPlayer, dataPtr);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_INIT;
	return 0;
}

int scePsmfPlayerStop(u32 psmfPlayer) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerStop(%08x)", psmfPlayer);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_STANDBY;
	return 0;
}

int scePsmfPlayerBreak(u32 psmfPlayer) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerBreak(%08x)", psmfPlayer);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_STANDBY;
	return 0;
}

int scePsmfPlayerSetPsmf(u32 psmfPlayer, const char *filename) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerSetPsmf(%08x, %s)", psmfPlayer, filename);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_STANDBY;
	return 0;
}

int scePsmfPlayerSetPsmfCB(u32 psmfPlayer, const char *filename) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerSetPsmfCB(%08x, %s)", psmfPlayer, filename);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_STANDBY;
	return 0;
}

int scePsmfPlayerGetAudioOutSize(u32 psmfPlayer) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerGetAudioOutSize(%08x)", psmfPlayer);
	// Probably wrong.
	return 2048 * 4;
}

int scePsmfPlayerStart(u32 psmfPlayer, u32 startInfoPtr, u32 startOffset) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerStart(%08x, %08x, %08x)", psmfPlayer, startInfoPtr, startOffset);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_PLAYING;
	return 0;
}

int scePsmfPlayerDelete(u32 psmfPlayer) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerDelete(%08x)", psmfPlayer);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_NONE;
	return 0;
}

int scePsmfPlayerUpdate(u32 psmfPlayer) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerUpdate(%08x)", psmfPlayer);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_PLAYING_FINISHED;
	return 0;
}

int scePsmfPlayerReleasePsmf(u32 psmfPlayer) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerReleasePsmf(%08x)", psmfPlayer);
	psmfPlayerStatus = PSMF_PLAYER_STATUS_INIT;
	return 0;
}

int scePsmfPlayerGetCurrentStatus(u32 psmfPlayer) {
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerGetCurrentStatus(%08x)", psmfPlayer);
	return psmfPlayerStatus;
}

const HLEFunction scePsmfPlayer[] =
{
	{0x235d8787, WrapI_UU<scePsmfPlayerCreate>, "scePsmfPlayerCreate"},
	{0x1078c008, WrapI_U<scePsmfPlayerStop>, "scePsmfPlayerStop"},
	{0x1e57a8e7, 0, "scePsmfPlayerConfigPlayer"},
	{0x2beb1569, WrapI_U<scePsmfPlayerBreak>, "scePsmfPlayerBreak"},
	{0x3d6d25a9, WrapI_UC<scePsmfPlayerSetPsmf>,"scePsmfPlayerSetPsmf"},
	{0x58B83577, WrapI_UC<scePsmfPlayerSetPsmfCB>, "scePsmfPlayerSetPsmfCB"},
	{0x3ea82a4b, WrapI_U<scePsmfPlayerGetAudioOutSize>, "scePsmfPlayerGetAudioOutSize"},
	{0x3ed62233, 0, "scePsmfPlayerGetCurrentPts"},
	{0x46f61f8b, 0, "scePsmfPlayerGetVideoData"},
	{0x68f07175, 0, "scePsmfPlayerGetCurrentAudioStream"},
	{0x75f03fa2, 0, "scePsmfPlayerSelectSpecificVideo"},
	{0x85461eff, 0, "scePsmfPlayerSelectSpecificAudio"},
	{0x8a9ebdcd, 0, "scePsmfPlayerSelectVideo"},
	{0x95a84ee5, WrapI_UUU<scePsmfPlayerStart>, "scePsmfPlayerStart"},
	{0x9b71a274, WrapI_U<scePsmfPlayerDelete>, "scePsmfPlayerDelete"},
	{0x9ff2b2e7, 0, "scePsmfPlayerGetCurrentVideoStream"},
	{0xa0b8ca55, WrapI_U<scePsmfPlayerUpdate>, "scePsmfPlayerUpdate"},
	{0xa3d81169, 0, "scePsmfPlayerChangePlayMode"},
	{0xb8d10c56, 0, "scePsmfPlayerSelectAudio"},
	{0xb9848a74, 0, "scePsmfPlayerGetAudioData"},
	{0xdf089680, 0, "scePsmfPlayerGetPsmfInfo"},
	{0xe792cd94, WrapI_U<scePsmfPlayerReleasePsmf>, "scePsmfPlayerReleasePsmf"},
	{0xf3efaa91, 0, "scePsmfPlayerGetCurrentPlayMode"},
	{0xf8ef08a6, WrapI_U<scePsmfPlayerGetCurrentStatus>, "scePsmfPlayerGetCurrentStatus"},
	{0x2D0E4E0A, 0, "scePsmfPlayerSetTempBuf"},
	{0x2673646B, 0, "scePsmfVerifyPsmf"},
	{0x4E624A34, 0, "scePsmfGetEPWithId"},
	{0x5F457515, 0, "scePsmfGetEPidWithTimestamp"},
};

void Register_scePsmf() {
	RegisterModule("scePsmf",ARRAY_SIZE(scePsmf),scePsmf);
}

void Register_scePsmfPlayer() {
	RegisterModule("scePsmfPlayer",ARRAY_SIZE(scePsmfPlayer),scePsmfPlayer);
}
