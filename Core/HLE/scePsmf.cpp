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
#include "Common/ChunkFile.h"
#include "Core/Reporting.h"

#include "Core/HLE/scePsmf.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HW/MediaEngine.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

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
const int PSMF_PLAYER_VERSION_FULL = 0;
const int PSMF_PLAYER_VERSION_BASIC = 1;
const int PSMF_PLAYER_VERSION_NET = 2;
const int PSMF_PLAYER_CONFIG_LOOP = 0;
const int PSMF_PLAYER_CONFIG_NO_LOOP = 1;
const int PSMF_PLAYER_CONFIG_MODE_LOOP = 0;
const int PSMF_PLAYER_CONFIG_MODE_PIXEL_TYPE = 1;

const int TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888 = 0X03;

int psmfCurrentPts = 0;
int psmfAvcStreamNum = 1;
int psmfAtracStreamNum = 1;
int psmfPcmStreamNum = 0;
int psmfPlayerVersion = PSMF_PLAYER_VERSION_FULL;
int psmfMaxAheadTimestamp = 40000;
int audioSamples = 2048;  
int audioSamplesBytes = audioSamples * 4;
int videoPixelMode = TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888;  
int videoLoopStatus = PSMF_PLAYER_CONFIG_NO_LOOP;  

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
	u32 headerOffset;
	u32 streamSize;
	u32 streamOffset;
	u32 streamNum;
	u32 unk1;
	u32 unk2;
};

struct PsmfPlayerData {
	int videoCodec;
	int videoStreamNum;
	int audioCodec;
	int audioStreamNum;
	int playMode;
	int playSpeed;
	long psmfPlayerLastTimestamp;
};

struct PsmfEntry {
	int EPIndex;
	int EPPicOffset;
	int EPPts;
	int EPOffset;
	int id;
};

int getMaxAheadTimestamp(int packets) {return std::max(40000, packets * 700);}

class PsmfStream;

// This does NOT match the raw structure. Due to endianness etc,
// we read it manually.
// TODO: Change to work directly with the data in RAM instead of this
// JPSCP-esque class.
typedef std::map<int, PsmfStream *> PsmfStreamMap;

class Psmf {
public:
	// For savestates only.
	Psmf() {}
	Psmf(u32 data);
	~Psmf();
	void DoState(PointerWrap &p);
	
	bool isValidCurrentStreamNumber() {
		return currentStreamNum >= 0 && currentStreamNum < streamMap.size();  // urgh, checking size isn't really right here.
	}

	void setStreamNum(int num);
	bool setStreamWithType(int type, int channel);

	u32 magic;
	u32 version;
	u32 streamOffset;
	u32 streamSize;
	u32 headerSize;
	u32 headerOffset;
	u32 streamType;
	u32 streamChannel;
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
	PsmfEntry psmfEntry;

	PsmfStreamMap streamMap;
};

class PsmfPlayer {
public:
	// For savestates only.
	PsmfPlayer() { mediaengine = new MediaEngine;}
	PsmfPlayer(u32 data);
	~PsmfPlayer() { if (mediaengine) delete mediaengine;}
	void DoState(PointerWrap &p);

	int videoCodec;
	int videoStreamNum;
	int audioCodec;
	int audioStreamNum;
	int playMode;
	int playSpeed;
	long psmfPlayerLastTimestamp;

	int displayBuffer;
	int displayBufferSize;
	int playbackThreadPriority;
	int psmfMaxAheadTimestamp;

	SceMpegAu psmfPlayerAtracAu;
	SceMpegAu psmfPlayerAvcAu;
	PsmfPlayerStatus status;

	MediaEngine* mediaengine;
};

class PsmfStream {
public:
	// Used for save states.
	PsmfStream() {
	}

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

		INFO_LOG(HLE, "PSMF MPEG data found: id=%02x, privid=%02x, epmoff=%08x, epmnum=%08x, width=%i, height=%i", streamId, privateStreamId, psmf->EPMapOffset, psmf->EPMapEntriesNum, psmf->videoWidth, psmf->videoHeight);
	}

	void readPrivateAudioStreamParams(u32 addr, Psmf *psmf) {
		int streamId = Memory::Read_U8(addr);
		int privateStreamId = Memory::Read_U8(addr + 1);
		psmf->audioChannels = Memory::Read_U8(addr + 14);
		psmf->audioFrequency = Memory::Read_U8(addr + 15);
		// two unknowns here
		INFO_LOG(HLE, "PSMF private audio found: id=%02x, privid=%02x, channels=%i, freq=%i", streamId, privateStreamId, psmf->audioChannels, psmf->audioFrequency);
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
	presentationStartTime = getMpegTimeStamp(Memory::GetPointer(data + PSMF_FIRST_TIMESTAMP_OFFSET));
	presentationEndTime = getMpegTimeStamp(Memory::GetPointer(data + PSMF_LAST_TIMESTAMP_OFFSET));
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
			stream = new PsmfStream(PSMF_AVC_STREAM, ++currentVideoStreamNum);
			stream->readMPEGVideoStreamParams(currentStreamAddr, this);
		} else if ((streamId & PSMF_AUDIO_STREAM_ID) == PSMF_AUDIO_STREAM_ID) {
			stream = new PsmfStream(PSMF_ATRAC_STREAM, ++currentAudioStreamNum);
			stream->readPrivateAudioStreamParams(currentStreamAddr, this);
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

PsmfPlayer::PsmfPlayer(u32 data) {
	videoCodec = Memory::Read_U32(data);
	videoStreamNum = Memory::Read_U32(data + 4);
	audioCodec = Memory::Read_U32(data + 8);
	audioStreamNum = Memory::Read_U32(data + 12);
	playMode = Memory::Read_U32(data+ 16);
	playSpeed = Memory::Read_U32(data + 20);
	psmfPlayerLastTimestamp = getMpegTimeStamp(Memory::GetPointer(data + PSMF_LAST_TIMESTAMP_OFFSET)) ;
	status = PSMF_PLAYER_STATUS_INIT;
	mediaengine = new MediaEngine;
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

	p.Do(streamMap);

	p.DoMarker("Psmf");
}

void PsmfPlayer::DoState(PointerWrap &p) {
	p.Do(videoCodec);
	p.Do(videoStreamNum);
	p.Do(audioCodec);
	p.Do(audioStreamNum);
	p.Do(playMode);
	p.Do(playSpeed);

	p.Do(displayBuffer);
	p.Do(displayBufferSize);
	p.Do(playbackThreadPriority);
	p.Do(psmfMaxAheadTimestamp);
	p.Do(psmfPlayerLastTimestamp);
	p.DoClass(mediaengine);

	p.DoMarker("PsmfPlayer");
}

void Psmf::setStreamNum(int num) {
	currentStreamNum = num;
	if (!isValidCurrentStreamNumber())
		return;
	PsmfStreamMap::iterator iter = streamMap.find(currentStreamNum);
	if (iter == streamMap.end())
		return;

	int type = iter->second->type;
	int channel = iter->second->channel;
	switch (type) {
	case PSMF_AVC_STREAM:
		if (currentVideoStreamNum != num) {
			// TODO: Tell video mediaengine or something about channel.
			currentVideoStreamNum = num;
		}
		break;

	case PSMF_ATRAC_STREAM:
	case PSMF_PCM_STREAM:
		if (currentAudioStreamNum != num) {
			// TODO: Tell audio mediaengine or something about channel.
			currentAudioStreamNum = num;
		}
		break;
	}
}

bool Psmf::setStreamWithType(int type, int channel) {
	for (PsmfStreamMap::iterator iter = streamMap.begin(); iter != streamMap.end(); ++iter) {
		if (iter->second->type == type) {
			setStreamNum(iter->first);
			return true;
		}
	}
	return false;
}


static std::map<u32, Psmf *> psmfMap;
static std::map<u32, PsmfPlayer *> psmfPlayerMap;

Psmf *getPsmf(u32 psmf)
{
	auto iter = psmfMap.find(psmf);
	if (iter != psmfMap.end())
		return iter->second;
	else
		return 0;
}

PsmfPlayer *getPsmfPlayer(u32 psmfplayer)
{
	auto iter = psmfPlayerMap.find(psmfplayer);
	if (iter != psmfPlayerMap.end())
		return iter->second;
	else
		return 0;
}

void __PsmfInit()
{
}

void __PsmfDoState(PointerWrap &p)
{
	p.Do(psmfMap);

	p.DoMarker("scePsmf");
}

void __PsmfPlayerDoState(PointerWrap &p)
{
	p.Do(psmfPlayerMap);

	p.DoMarker("scePsmfPlayer");
}

void __PsmfShutdown()
{
	for (auto it = psmfMap.begin(), end = psmfMap.end(); it != end; ++it)
		delete it->second;
	for (auto it = psmfPlayerMap.begin(), end = psmfPlayerMap.end(); it != end; ++it)
		delete it->second;
	psmfMap.clear();
	psmfPlayerMap.clear();
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
		ERROR_LOG(HLE, "scePsmfGetNumberOfStreams(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetNumberOfStreams(%08x)", psmfStruct);
	return psmf->numStreams;
}

u32 scePsmfGetNumberOfSpecificStreams(u32 psmfStruct, u32 streamType)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetNumberOfSpecificStreams(%08x, %08x): invalid psmf", psmfStruct, streamType);
		return ERROR_PSMF_NOT_FOUND;
	}
	WARN_LOG(HLE, "scePsmfGetNumberOfSpecificStreams(%08x, %08x)", psmfStruct, streamType);
	int streamNum = 0;
	int type = (streamType == PSMF_AUDIO_STREAM ? PSMF_ATRAC_STREAM : streamType);
	for (int i = psmf->streamMap.size() - 1; i >= 0; i--) {
		if (psmf->streamMap[i]->type == type)
			streamNum++;
	}
	return streamNum;
}

u32 scePsmfSpecifyStreamWithStreamType(u32 psmfStruct, u32 streamType, u32 channel)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfSpecifyStreamWithStreamType(%08x, %08x, %i): invalid psmf", psmfStruct, streamType, channel);
		return ERROR_PSMF_NOT_FOUND;
	}
	ERROR_LOG(HLE, "UNIMPL scePsmfSpecifyStreamWithStreamType(%08x, %08x, %i)", psmfStruct, streamType, channel);
	if (!psmf->setStreamWithType(streamType, channel)) {
		psmf->setStreamNum(-1);
	}
	return 0;
}

u32 scePsmfSpecifyStreamWithStreamTypeNumber(u32 psmfStruct, u32 streamType, u32 typeNum)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfSpecifyStreamWithStreamTypeNumber(%08x, %08x, %08x): invalid psmf", psmfStruct, streamType, typeNum);
		return ERROR_PSMF_NOT_FOUND;
	}
	ERROR_LOG_REPORT(HLE, "UNIMPL scePsmfSpecifyStreamWithStreamTypeNumber(%08x, %08x, %08x)", psmfStruct, streamType, typeNum);
	return 0;
}

u32 scePsmfSpecifyStream(u32 psmfStruct, int streamNum) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfSpecifyStream(%08x, %i): invalid psmf", psmfStruct, streamNum);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(HLE, "scePsmfSpecifyStream(%08x, %i)", psmfStruct, streamNum);
	psmf->setStreamNum(streamNum);
	return 0;
}

u32 scePsmfGetVideoInfo(u32 psmfStruct, u32 videoInfoAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetVideoInfo(%08x, %08x): invalid psmf", psmfStruct, videoInfoAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(HLE, "scePsmfGetVideoInfo(%08x, %08x)", psmfStruct, videoInfoAddr);
	if (Memory::IsValidAddress(videoInfoAddr)) {
		Memory::Write_U32(psmf->videoWidth, videoInfoAddr);
		Memory::Write_U32(psmf->videoHeight, videoInfoAddr + 4);
	}
	return 0;
}

u32 scePsmfGetAudioInfo(u32 psmfStruct, u32 audioInfoAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetAudioInfo(%08x, %08x): invalid psmf", psmfStruct, audioInfoAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(HLE, "scePsmfGetAudioInfo(%08x, %08x)", psmfStruct, audioInfoAddr);
	if (Memory::IsValidAddress(audioInfoAddr)) {
		Memory::Write_U32(psmf->audioChannels, audioInfoAddr);
		Memory::Write_U32(psmf->audioFrequency, audioInfoAddr + 4);
	}
	return 0;
}

u32 scePsmfGetCurrentStreamType(u32 psmfStruct, u32 typeAddr, u32 channelAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetCurrentStreamType(%08x, %08x, %08x): invalid psmf", psmfStruct, typeAddr, channelAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(HLE, "scePsmfGetCurrentStreamType(%08x, %08x, %08x)", psmfStruct, typeAddr, channelAddr);
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

u32 scePsmfGetStreamSize(u32 psmfStruct, u32 sizeAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetStreamSize(%08x, %08x): invalid psmf", psmfStruct, sizeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetStreamSize(%08x, %08x)", psmfStruct, sizeAddr);
	if (Memory::IsValidAddress(sizeAddr)) {
		Memory::Write_U32(psmf->streamSize, sizeAddr);
	}
	return 0;
}

u32 scePsmfQueryStreamOffset(u32 bufferAddr, u32 offsetAddr)
{
	WARN_LOG(HLE, "scePsmfQueryStreamOffset(%08x, %08x)", bufferAddr, offsetAddr);
	if (Memory::IsValidAddress(offsetAddr)) {
		Memory::Write_U32(bswap32(Memory::Read_U32(bufferAddr + PSMF_STREAM_OFFSET_OFFSET)), offsetAddr);
	}
	return 0;
}

u32 scePsmfQueryStreamSize(u32 bufferAddr, u32 sizeAddr)
{
	WARN_LOG(HLE, "scePsmfQueryStreamSize(%08x, %08x)", bufferAddr, sizeAddr);
	if (Memory::IsValidAddress(sizeAddr)) {
		Memory::Write_U32(bswap32(Memory::Read_U32(bufferAddr + PSMF_STREAM_SIZE_OFFSET)), sizeAddr);
	}
	return 0;
}

u32 scePsmfGetHeaderSize(u32 psmfStruct, u32 sizeAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetHeaderSize(%08x, %08x): invalid psmf", psmfStruct, sizeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetHeaderSize(%08x, %08x)", psmfStruct, sizeAddr);
	if (Memory::IsValidAddress(sizeAddr)) {
		Memory::Write_U32(psmf->headerSize, sizeAddr);
	}
	return 0;
}

u32 scePsmfGetPsmfVersion(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetHeaderSize(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetPsmfVersion(%08x)", psmfStruct);
	return psmf->version;
}

u32 scePsmfVerifyPsmf(u32 psmfAddr)
{
	int magic = Memory::Read_U32(psmfAddr);
	if (magic != PSMF_MAGIC) {
		ERROR_LOG(HLE, "scePsmfVerifyPsmf(%08x): bad magic %08x", psmfAddr, magic);
		return ERROR_PSMF_NOT_FOUND;
	}
	int version = Memory::Read_U32(psmfAddr + PSMF_STREAM_VERSION_OFFSET);
	if (version < 0) {
		ERROR_LOG(HLE, "scePsmfVerifyPsmf(%08x): bad version %08x", psmfAddr, version);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfVerifyPsmf(%08x)", psmfAddr);
	return 0;
}

u32 scePsmfGetNumberOfEPentries(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetNumberOfEPentries(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetNumberOfEPentries(%08x)", psmfStruct);
	return psmf->EPMapEntriesNum;
}

u32 scePsmfGetPresentationStartTime(u32 psmfStruct, u32 startTimeAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetPresentationStartTime(%08x, %08x): invalid psmf", psmfStruct, startTimeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetPresentationStartTime(%08x, %08x)", psmfStruct, startTimeAddr);
	if (Memory::IsValidAddress(startTimeAddr)) {
		Memory::Write_U32(psmf->presentationStartTime, startTimeAddr);
	}
	return 0;
}

u32 scePsmfGetPresentationEndTime(u32 psmfStruct, u32 endTimeAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetPresentationEndTime(%08x, %08x): invalid psmf", psmfStruct, endTimeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetPresentationEndTime(%08x, %08x)", psmfStruct, endTimeAddr);
	if (Memory::IsValidAddress(endTimeAddr)) {
		Memory::Write_U32(psmf->presentationEndTime, endTimeAddr);
	}
	return 0;
}

u32 scePsmfGetCurrentStreamNumber(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetCurrentStreamNumber(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}

	DEBUG_LOG(HLE, "scePsmfGetCurrentStreamNumber(%08x)", psmfStruct);
	return psmf->currentStreamNum;
}

u32 scePsmfCheckEPMap(u32 psmfPlayer) 
{
	INFO_LOG(HLE, "scePsmfCheckEPMap(%08x)", psmfPlayer);
	return 0;  // Should be okay according to JPCSP
}

u32 scePsmfGetEPWithId(u32 psmfStruct, int id, u32 outAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetEPWithId(%08x, %i, %08x): invalid psmf", psmfStruct, id, outAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetEPWithId(%08x, %i, %08x)", psmfStruct, id, outAddr);
	if (Memory::IsValidAddress(outAddr)) {
		Memory::Write_U32(psmf->psmfEntry.EPPts, outAddr);
		Memory::Write_U32(psmf->psmfEntry.EPOffset, outAddr + 4);
		Memory::Write_U32(psmf->psmfEntry.EPIndex, outAddr + 8);
		Memory::Write_U32(psmf->psmfEntry.EPPicOffset, outAddr + 12);
	}
	return 0;
}

u32 scePsmfGetEPWithTimestamp(u32 psmfStruct, u32 ts, u32 entryAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetEPWithTimestamp(%08x, %i, %08x): invalid psmf", psmfStruct, ts, entryAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetEPWithTimestamp(%08x, %i, %08x)", psmfStruct, ts, entryAddr);
	if (ts < psmf->presentationStartTime) {
		return ERROR_PSMF_INVALID_TIMESTAMP;
	}
	if (Memory::IsValidAddress(entryAddr)) {
		Memory::Write_U32(psmf->psmfEntry.EPPts, entryAddr);
		Memory::Write_U32(psmf->psmfEntry.EPOffset, entryAddr + 4);
		Memory::Write_U32(psmf->psmfEntry.EPIndex, entryAddr + 8);
		Memory::Write_U32(psmf->psmfEntry.EPPicOffset, entryAddr + 12);
	}
	return 0;
}

u32 scePsmfGetEPidWithTimestamp(u32 psmfStruct, u32 ts)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(HLE, "scePsmfGetEPidWithTimestamp(%08x, %i): invalid psmf", psmfStruct, ts);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfGetEPidWithTimestamp(%08x, %i)", psmfStruct, ts);
	if (ts < psmf->presentationStartTime) {
		return ERROR_PSMF_INVALID_TIMESTAMP;
	}

	return psmf->psmfEntry.id;
}

int scePsmfPlayerCreate(u32 psmfPlayer, u32 psmfPlayerDataAddr) 
{
	WARN_LOG(HLE, "scePsmfPlayerCreate(%08x, %08x)", psmfPlayer, psmfPlayerDataAddr);
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		// TODO: This is the wrong data.  PsmfPlayer needs a new interface.
		psmfplayer = new PsmfPlayer(psmfPlayerDataAddr);
		psmfPlayerMap[psmfPlayer] = psmfplayer;
	}

	if (Memory::IsValidAddress(psmfPlayerDataAddr)) {
		psmfplayer->displayBuffer = Memory::Read_U32(psmfPlayerDataAddr);          
		psmfplayer->displayBufferSize = Memory::Read_U32(psmfPlayerDataAddr + 4);     
		psmfplayer->playbackThreadPriority = Memory::Read_U32(psmfPlayerDataAddr + 8);
	}

	psmfplayer->psmfMaxAheadTimestamp = getMaxAheadTimestamp(581);
	psmfplayer->status = PSMF_PLAYER_STATUS_INIT;
	return 0;
}

int scePsmfPlayerStop(u32 psmfPlayer) 
{
	INFO_LOG(HLE, "scePsmfPlayerStop(%08x)", psmfPlayer);
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (psmfplayer)
		psmfplayer->status = PSMF_PLAYER_STATUS_STANDBY;
	return 0;
}

int scePsmfPlayerBreak(u32 psmfPlayer) 
{
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerBreak(%08x)", psmfPlayer);
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (psmfplayer)
		psmfplayer->status = PSMF_PLAYER_STATUS_STANDBY;
	return 0;
}

int scePsmfPlayerSetPsmf(u32 psmfPlayer, const char *filename) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (psmfplayer)
	{
		INFO_LOG(HLE, "scePsmfPlayerSetPsmf(%08x, %s)", psmfPlayer, filename);
		psmfplayer->status = PSMF_PLAYER_STATUS_STANDBY;
		psmfplayer->mediaengine->loadFile(filename);
		psmfplayer->psmfPlayerLastTimestamp = psmfplayer->mediaengine->getLastTimeStamp();
	}
	else
	{
		INFO_LOG(HLE, "scePsmfPlayerSetPsmf(%08x, %s): invalid psmf player", psmfPlayer, filename);
	}

	return 0;
}

int scePsmfPlayerSetPsmfCB(u32 psmfPlayer, const char *filename) 
{
	// TODO: hleCheckCurrentCallbacks?
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (psmfplayer)
	{
		INFO_LOG(HLE, "scePsmfPlayerSetPsmfCB(%08x, %s)", psmfPlayer, filename);
		psmfplayer->status = PSMF_PLAYER_STATUS_STANDBY;
		psmfplayer->mediaengine->loadFile(filename);
		psmfplayer->psmfPlayerLastTimestamp = psmfplayer->mediaengine->getLastTimeStamp();
	}
	else
	{
		INFO_LOG(HLE, "scePsmfPlayerSetPsmfCB(%08x, %s): invalid psmf player", psmfPlayer, filename);
	}

	return 0;
}

int scePsmfPlayerGetAudioOutSize(u32 psmfPlayer) 
{
	WARN_LOG(HLE, "scePsmfPlayerGetAudioOutSize(%08x)", psmfPlayer);
	return audioSamplesBytes;
}

int scePsmfPlayerStart(u32 psmfPlayer, u32 psmfPlayerData, int initPts) 
{
	WARN_LOG(HLE, "UNIMPL scePsmfPlayerStart(%08x, %08x, %08x)", psmfPlayer, psmfPlayerData, initPts);

	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		psmfplayer = new PsmfPlayer(psmfPlayerData);
		psmfPlayerMap[psmfPlayer] = psmfplayer;
	}

	if (Memory::IsValidAddress(psmfPlayerData)) {
		PsmfPlayerData data = {0};
		Memory::ReadStruct(psmfPlayerData, &data);
		psmfplayer->videoCodec = data.videoCodec;
		psmfplayer->videoStreamNum = data.videoStreamNum;
		psmfplayer->audioCodec = data.audioCodec;
		psmfplayer->audioStreamNum = data.audioStreamNum;
		psmfplayer->playMode = data.playMode;
		psmfplayer->playSpeed = data.playSpeed;
		/*data.videoCodec = psmfplayer->videoCodec;
		data.videoStreamNum = psmfplayer->videoStreamNum;
		data.audioCodec = psmfplayer->audioCodec;
		data.audioStreamNum = psmfplayer->audioStreamNum;
		data.playMode = psmfplayer->playMode;
		data.playSpeed = psmfplayer->playSpeed;
		data.psmfPlayerLastTimestamp = psmfplayer->psmfPlayerLastTimestamp;
		Memory::WriteStruct(psmfPlayerData, &data);*/
	}

	psmfplayer->psmfPlayerAtracAu.dts = initPts;
	psmfplayer->psmfPlayerAtracAu.pts = initPts;
	psmfplayer->psmfPlayerAvcAu.dts = initPts;
	psmfplayer->psmfPlayerAvcAu.pts = initPts;

	psmfplayer->status = PSMF_PLAYER_STATUS_PLAYING;

	psmfplayer->mediaengine->openContext();
	return 0;
}

int scePsmfPlayerDelete(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (psmfplayer) {
		INFO_LOG(HLE, "scePsmfPlayerDelete(%08x)", psmfPlayer);
		delete psmfplayer;
		psmfPlayerMap.erase(psmfPlayer);
	} else {
		ERROR_LOG(HLE, "scePsmfPlayerDelete(%08x): invalid psmf player", psmfPlayer);
	}
	return 0;
}

int scePsmfPlayerUpdate(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerUpdate(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMF_NOT_FOUND;
	}

	DEBUG_LOG(HLE, "scePsmfPlayerUpdate(%08x)", psmfPlayer);
	if (psmfplayer->psmfPlayerAvcAu.pts > 0) {
		if (psmfplayer->psmfPlayerAvcAu.pts >= psmfplayer->psmfPlayerLastTimestamp) {
			INFO_LOG(HLE, "video end reached");
			psmfplayer->status = PSMF_PLAYER_STATUS_PLAYING_FINISHED;
		}
	}
	return 0;
}

int scePsmfPlayerReleasePsmf(u32 psmfPlayer) 
{
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerReleasePsmf(%08x)", psmfPlayer);
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (psmfplayer)
		psmfplayer->status = PSMF_PLAYER_STATUS_INIT;
	return 0;
}

int scePsmfPlayerGetVideoData(u32 psmfPlayer, u32 videoDataAddr)
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetVideoData(%08x, %08x): invalid psmf player", psmfPlayer, videoDataAddr);
		return ERROR_PSMF_NOT_FOUND;
	}

	DEBUG_LOG(HLE, "scePsmfPlayerGetVideoData(%08x, %08x)", psmfPlayer, videoDataAddr);
	if (Memory::IsValidAddress(videoDataAddr)) {
		int frameWidth = Memory::Read_U32(videoDataAddr);
        u32 displaybuf = Memory::Read_U32(videoDataAddr + 4);
        int displaypts = Memory::Read_U32(videoDataAddr + 8);
		if (psmfplayer->mediaengine->stepVideo(videoPixelMode)) {
			int displaybufSize = psmfplayer->mediaengine->writeVideoImage(Memory::GetPointer(displaybuf), frameWidth, videoPixelMode);
			gpu->InvalidateCache(displaybuf, displaybufSize, GPU_INVALIDATE_SAFE);
		}
		psmfplayer->psmfPlayerAvcAu.pts = psmfplayer->mediaengine->getVideoTimeStamp();
		Memory::Write_U32(psmfplayer->psmfPlayerAvcAu.pts, videoDataAddr + 8);
	}

	int ret = psmfplayer->mediaengine->IsVideoEnd() ? ERROR_PSMFPLAYER_NO_MORE_DATA : 0;

	s64 deltapts = psmfplayer->mediaengine->getVideoTimeStamp() - psmfplayer->mediaengine->getAudioTimeStamp();
	int delaytime = 3000;
	if (deltapts > 0 && !psmfplayer->mediaengine->IsAudioEnd())
		delaytime = deltapts * 1000000 / 90000;
	if (!ret)
		return hleDelayResult(ret, "psmfPlayer video decode", delaytime);
	else
		return hleDelayResult(ret, "psmfPlayer all data decoded", 3000);
}

int scePsmfPlayerGetAudioData(u32 psmfPlayer, u32 audioDataAddr)
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetAudioData(%08x, %08x): invalid psmf player", psmfPlayer, audioDataAddr);
		return ERROR_PSMF_NOT_FOUND;
	}

	DEBUG_LOG(HLE, "scePsmfPlayerGetAudioData(%08x, %08x)", psmfPlayer, audioDataAddr);
	if (Memory::IsValidAddress(audioDataAddr)) {
		Memory::Memset(audioDataAddr, 0, audioSamplesBytes);
		psmfplayer->mediaengine->getAudioSamples(Memory::GetPointer(audioDataAddr));
	}
	int ret = psmfplayer->mediaengine->IsAudioEnd() ? ERROR_PSMFPLAYER_NO_MORE_DATA : 0;
	return hleDelayResult(ret, "psmfPlayer audio decode", 3000);
}

int scePsmfPlayerGetCurrentStatus(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetCurrentStatus(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "%d=scePsmfPlayerGetCurrentStatus(%08x)", psmfplayer->status, psmfPlayer);
	return psmfplayer->status;
}

u32 scePsmfPlayerGetCurrentPts(u32 psmfPlayer, u32 currentPtsAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetCurrentPts(%08x, %08x): invalid psmf player", psmfPlayer, currentPtsAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(HLE, "scePsmfPlayerGetCurrentPts(%08x, %08x)", psmfPlayer, currentPtsAddr);
	if (psmfplayer->status < PSMF_PLAYER_STATUS_STANDBY) {
		return ERROR_PSMFPLAYER_NOT_INITIALIZED;
	}

	if (Memory::IsValidAddress(currentPtsAddr)) {
		Memory::Write_U32(psmfplayer->psmfPlayerAvcAu.pts, currentPtsAddr);
	}	
	return 0;
}

u32 scePsmfPlayerGetPsmfInfo(u32 psmfPlayer, u32 psmfInfoAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetPsmfInfo(%08x, %08x): invalid psmf player", psmfPlayer, psmfInfoAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_STANDBY) {
		ERROR_LOG(HLE, "scePsmfPlayerGetPsmfInfo(%08x, %08x): not initialized", psmfPlayer, psmfInfoAddr);
		return ERROR_PSMFPLAYER_NOT_INITIALIZED;
	}

	WARN_LOG(HLE, "scePsmfPlayerGetPsmfInfo(%08x, %08x)", psmfPlayer, psmfInfoAddr);
	if (Memory::IsValidAddress(psmfInfoAddr)) {
		Memory::Write_U32(psmfplayer->psmfPlayerLastTimestamp, psmfInfoAddr);
		Memory::Write_U32(psmfplayer->videoStreamNum, psmfInfoAddr + 4);
		Memory::Write_U32(psmfplayer->audioStreamNum, psmfInfoAddr + 8);
		// pcm stream num?
		Memory::Write_U32(0, psmfInfoAddr + 12);
		// Player version?
		Memory::Write_U32(0, psmfInfoAddr + 16);
	}
	return 0;
}

u32 scePsmfPlayerGetCurrentPlayMode(u32 psmfPlayer, u32 playModeAddr, u32 playSpeedAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetCurrentPlayMode(%08x, %08x, %08x): invalid psmf player", psmfPlayer, playModeAddr, playSpeedAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	WARN_LOG(HLE, "scePsmfPlayerGetCurrentPlayMode(%08x, %08x, %08x)", psmfPlayer, playModeAddr, playSpeedAddr);
	if (Memory::IsValidAddress(playModeAddr)) {
		Memory::Write_U64(psmfplayer->playMode, playModeAddr);
	}
	if (Memory::IsValidAddress(playSpeedAddr)) {
		Memory::Write_U64(psmfplayer->playSpeed, playSpeedAddr);
	}
	return 0;
}

u32 scePsmfPlayerGetCurrentVideoStream(u32 psmfPlayer, u32 videoCodecAddr, u32 videoStreamNumAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetCurrentVideoStream(%08x, %08x, %08x): invalid psmf player", psmfPlayer, videoCodecAddr, videoStreamNumAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	WARN_LOG(HLE, "scePsmfPlayerGetCurrentVideoStream(%08x, %08x, %08x)", psmfPlayer, videoCodecAddr, videoStreamNumAddr);
	if (Memory::IsValidAddress(videoCodecAddr)) {
		Memory::Write_U64(psmfplayer->videoCodec, videoCodecAddr);
	}
	if (Memory::IsValidAddress(videoStreamNumAddr)) {
		Memory::Write_U64(psmfplayer->videoStreamNum, videoStreamNumAddr);
	}
	return 0;
}

u32 scePsmfPlayerGetCurrentAudioStream(u32 psmfPlayer, u32 audioCodecAddr, u32 audioStreamNumAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerGetCurrentAudioStream(%08x, %08x, %08x): invalid psmf player", psmfPlayer, audioCodecAddr, audioStreamNumAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	WARN_LOG(HLE, "scePsmfPlayerGetCurrentAudioStream(%08x, %08x, %08x)", psmfPlayer, audioCodecAddr, audioStreamNumAddr);
	if (Memory::IsValidAddress(audioCodecAddr)) {
		Memory::Write_U64(psmfplayer->audioCodec, audioCodecAddr);
	}
	if (Memory::IsValidAddress(audioStreamNumAddr)) {
		Memory::Write_U64(psmfplayer->audioStreamNum, audioStreamNumAddr);
	}
	return 0;
}

int scePsmfPlayerSetTempBuf(u32 psmfPlayer, u32 tempBufAddr, u32 tempBufSize) 
{
	ERROR_LOG(HLE, "UNIMPL scePsmfPlayerSetTempBuf(%08x, %08x, %08x)", psmfPlayer, tempBufAddr, tempBufSize);
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (psmfplayer)
		psmfplayer->status = PSMF_PLAYER_STATUS_INIT;
	return 0;
}

u32 scePsmfPlayerChangePlayMode(u32 psmfPlayer, int playMode, int playSpeed) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerChangePlayMode(%08x, %i, %i): invalid psmf player", psmfPlayer, playMode, playSpeed);
		return ERROR_PSMF_NOT_FOUND;
	}

	WARN_LOG(HLE, "scePsmfPlayerChangePlayMode(%08x, %i, %i)", psmfPlayer, playMode, playSpeed);
	psmfplayer->playMode = playMode;
	psmfplayer->playSpeed = playSpeed;
	return 0;
}

u32 scePsmfPlayerSelectAudio(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerSelectAudio(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMF_NOT_FOUND;
	}
	ERROR_LOG(HLE, "scePsmfPlayerSelectAudio(%08x)", psmfPlayer);
	psmfplayer->audioStreamNum++;
	return 0;
}

u32 scePsmfPlayerSelectVideo(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerSelectVideo(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMF_NOT_FOUND;
	}
	ERROR_LOG(HLE, "scePsmfPlayerSelectVideo(%08x)", psmfPlayer);
	psmfplayer->videoStreamNum++;
	return 0;
}

u32 scePsmfPlayerSelectSpecificVideo(u32 psmfPlayer, int videoCodec, int videoStreamNum) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i): invalid psmf player", psmfPlayer, videoCodec, videoStreamNum);
		return ERROR_PSMF_NOT_FOUND;
	}

	ERROR_LOG(HLE, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i)", psmfPlayer, videoCodec, videoStreamNum);
	psmfplayer->videoCodec = videoCodec;
	psmfplayer->videoStreamNum = videoStreamNum;
	psmfplayer->mediaengine->setVideoStream(videoStreamNum);
	return 0;
}

u32 scePsmfPlayerSelectSpecificAudio(u32 psmfPlayer, int audioCodec, int audioStreamNum) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i): invalid psmf player", psmfPlayer, audioCodec, audioStreamNum);
		return ERROR_PSMF_NOT_FOUND;
	}

	ERROR_LOG(HLE, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i)", psmfPlayer, audioCodec, audioStreamNum);
	psmfplayer->audioCodec = audioCodec;
	psmfplayer->audioStreamNum = audioStreamNum;
	psmfplayer->mediaengine->setAudioStream(audioStreamNum);
	return 0;
}

u32 scePsmfPlayerConfigPlayer(u32 psmfPlayer, int configMode, int configAttr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(HLE, "scePsmfPlayerConfigPlayer(%08x, %i, %i): invalid psmf player", psmfPlayer, configMode, configAttr);
		return ERROR_PSMF_NOT_FOUND;
	}
	if (configMode == PSMF_PLAYER_CONFIG_MODE_LOOP) {
		INFO_LOG(HLE, "scePsmfPlayerConfigPlayer(%08x, loop, %i)", psmfPlayer, configAttr);
		videoLoopStatus = configAttr;
	} else if (configMode == PSMF_PLAYER_CONFIG_MODE_PIXEL_TYPE) {
		INFO_LOG(HLE, "scePsmfPlayerConfigPlayer(%08x, pixelType, %i)", psmfPlayer, configAttr);
		// Does -1 mean default or something?
		if (configAttr != -1) {
			videoPixelMode = configAttr;
		}
	} else {
		ERROR_LOG_REPORT(HLE, "scePsmfPlayerConfigPlayer(%08x, %i, %i): unknown parameter", psmfPlayer, configMode, configAttr);
	}

	return 0;
}

const HLEFunction scePsmf[] = {
	{0xc22c8327, WrapU_UU<scePsmfSetPsmf>, "scePsmfSetPsmf"},
	{0xC7DB3A5B, WrapU_UUU<scePsmfGetCurrentStreamType>, "scePsmfGetCurrentStreamType"},
	{0x28240568, WrapU_U<scePsmfGetCurrentStreamNumber>, "scePsmfGetCurrentStreamNumber"},
	{0x1E6D9013, WrapU_UUU<scePsmfSpecifyStreamWithStreamType>, "scePsmfSpecifyStreamWithStreamType"},
	{0x0C120E1D, WrapU_UUU<scePsmfSpecifyStreamWithStreamTypeNumber>, "scePsmfSpecifyStreamWithStreamTypeNumber"},
	{0x4BC9BDE0, WrapU_UI<scePsmfSpecifyStream>, "scePsmfSpecifyStream"},
	{0x76D3AEBA, WrapU_UU<scePsmfGetPresentationStartTime>, "scePsmfGetPresentationStartTime"},
	{0xBD8AE0D8, WrapU_UU<scePsmfGetPresentationEndTime>, "scePsmfGetPresentationEndTime"},
	{0xEAED89CD, WrapU_U<scePsmfGetNumberOfStreams>, "scePsmfGetNumberOfStreams"},
	{0x7491C438, WrapU_U<scePsmfGetNumberOfEPentries>, "scePsmfGetNumberOfEPentries"},
	{0x0BA514E5, WrapU_UU<scePsmfGetVideoInfo>, "scePsmfGetVideoInfo"},
	{0xA83F7113, WrapU_UU<scePsmfGetAudioInfo>, "scePsmfGetAudioInfo"},
	{0x971A3A90, WrapU_U<scePsmfCheckEPMap>, "scePsmfCheckEPmap"},
	{0x68d42328, WrapU_UU<scePsmfGetNumberOfSpecificStreams>, "scePsmfGetNumberOfSpecificStreams"},
	{0x5b70fcc1, WrapU_UU<scePsmfQueryStreamOffset>, "scePsmfQueryStreamOffset"},
	{0x9553cc91, WrapU_UU<scePsmfQueryStreamSize>, "scePsmfQueryStreamSize"},
	{0xB78EB9E9, WrapU_UU<scePsmfGetHeaderSize>, "scePsmfGetHeaderSize"},
	{0xA5EBFE81, WrapU_UU<scePsmfGetStreamSize>, "scePsmfGetStreamSize"},
	{0xE1283895, WrapU_U<scePsmfGetPsmfVersion>, "scePsmfGetPsmfVersion"},
	{0x2673646B, WrapU_U<scePsmfVerifyPsmf>, "scePsmfVerifyPsmf"},
	{0x4E624A34, WrapU_UIU<scePsmfGetEPWithId>, "scePsmfGetEPWithId"},
	{0x7C0E7AC3, WrapU_UUU<scePsmfGetEPWithTimestamp>, "scePsmfGetEPWithTimestamp"},
	{0x5F457515, WrapU_UU<scePsmfGetEPidWithTimestamp>, "scePsmfGetEPidWithTimestamp"},
};

const HLEFunction scePsmfPlayer[] =
{
	{0x235d8787, WrapI_UU<scePsmfPlayerCreate>, "scePsmfPlayerCreate"},
	{0x1078c008, WrapI_U<scePsmfPlayerStop>, "scePsmfPlayerStop"},
	{0x1e57a8e7, WrapU_UII<scePsmfPlayerConfigPlayer>, "scePsmfPlayerConfigPlayer"},
	{0x2beb1569, WrapI_U<scePsmfPlayerBreak>, "scePsmfPlayerBreak"},
	{0x3d6d25a9, WrapI_UC<scePsmfPlayerSetPsmf>,"scePsmfPlayerSetPsmf"},
	{0x58B83577, WrapI_UC<scePsmfPlayerSetPsmfCB>, "scePsmfPlayerSetPsmfCB"},
	{0x3ea82a4b, WrapI_U<scePsmfPlayerGetAudioOutSize>, "scePsmfPlayerGetAudioOutSize"},
	{0x3ed62233, WrapU_UU<scePsmfPlayerGetCurrentPts>, "scePsmfPlayerGetCurrentPts"},
	{0x46f61f8b, WrapI_UU<scePsmfPlayerGetVideoData>, "scePsmfPlayerGetVideoData"},
	{0x68f07175, WrapU_UUU<scePsmfPlayerGetCurrentAudioStream>, "scePsmfPlayerGetCurrentAudioStream"},
	{0x75f03fa2, WrapU_UII<scePsmfPlayerSelectSpecificVideo>, "scePsmfPlayerSelectSpecificVideo"},
	{0x85461eff, WrapU_UII<scePsmfPlayerSelectSpecificAudio>, "scePsmfPlayerSelectSpecificAudio"},
	{0x8a9ebdcd, WrapU_U<scePsmfPlayerSelectVideo>, "scePsmfPlayerSelectVideo"},
	{0x95a84ee5, WrapI_UUI<scePsmfPlayerStart>, "scePsmfPlayerStart"},
	{0x9b71a274, WrapI_U<scePsmfPlayerDelete>, "scePsmfPlayerDelete"},
	{0x9ff2b2e7, WrapU_UUU<scePsmfPlayerGetCurrentVideoStream>, "scePsmfPlayerGetCurrentVideoStream"},
	{0xa0b8ca55, WrapI_U<scePsmfPlayerUpdate>, "scePsmfPlayerUpdate"},
	{0xa3d81169, WrapU_UII<scePsmfPlayerChangePlayMode>, "scePsmfPlayerChangePlayMode"},
	{0xb8d10c56, WrapU_U<scePsmfPlayerSelectAudio>, "scePsmfPlayerSelectAudio"},
	{0xb9848a74, WrapI_UU<scePsmfPlayerGetAudioData>, "scePsmfPlayerGetAudioData"},
	{0xdf089680, WrapU_UU<scePsmfPlayerGetPsmfInfo>, "scePsmfPlayerGetPsmfInfo"},
	{0xe792cd94, WrapI_U<scePsmfPlayerReleasePsmf>, "scePsmfPlayerReleasePsmf"},
	{0xf3efaa91, WrapU_UUU<scePsmfPlayerGetCurrentPlayMode>, "scePsmfPlayerGetCurrentPlayMode"},
	{0xf8ef08a6, WrapI_U<scePsmfPlayerGetCurrentStatus>, "scePsmfPlayerGetCurrentStatus"},
	{0x2D0E4E0A, WrapI_UUU<scePsmfPlayerSetTempBuf>, "scePsmfPlayerSetTempBuf"},
	{0x76C0F4AE, 0, "scePsmfPlayerSetPsmfOffset"},
	{0xA72DB4F9, 0, "scePsmfPlayerSetPsmfOffsetCB"},
};

void Register_scePsmf() {
	RegisterModule("scePsmf",ARRAY_SIZE(scePsmf),scePsmf);
}

void Register_scePsmfPlayer() {
	RegisterModule("scePsmfPlayer",ARRAY_SIZE(scePsmfPlayer),scePsmfPlayer);
}
