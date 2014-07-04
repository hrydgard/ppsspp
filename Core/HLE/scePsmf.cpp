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
#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Common/ChunkFile.h"
#include "Core/Reporting.h"

#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/scePsmf.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HW/MediaEngine.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include <map>
#include <algorithm>

// "Go Sudoku" is a good way to test this code...
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

const int PSMF_PLAYER_WARMUP_FRAMES = 3;

static const int VIDEO_FRAME_DURATION_TS = 3003;

int psmfMaxAheadTimestamp = 40000;
int audioSamples = 2048;  
int audioSamplesBytes = audioSamples * 4;
int videoPixelMode = GE_CMODE_32BIT_ABGR8888;
int videoLoopStatus = PSMF_PLAYER_CONFIG_NO_LOOP;

enum PsmfPlayerError {
	ERROR_PSMF_NOT_INITIALIZED       = 0x80615001,
	ERROR_PSMF_BAD_VERSION           = 0x80615002,
	ERROR_PSMF_NOT_FOUND             = 0x80615025,
	ERROR_PSMF_INVALID_ID            = 0x80615100,
	ERROR_PSMF_INVALID_VALUE         = 0x806151fe,
	ERROR_PSMF_INVALID_TIMESTAMP     = 0x80615500,
	ERROR_PSMF_INVALID_PSMF          = 0x80615501,

	ERROR_PSMFPLAYER_INVALID_STATUS  = 0x80616001,
	ERROR_PSMFPLAYER_INVALID_STREAM  = 0x80616003,
	ERROR_PSMFPLAYER_BUFFER_SIZE     = 0x80616005,
	ERROR_PSMFPLAYER_INVALID_CONFIG  = 0x80616006,
	ERROR_PSMFPLAYER_INVALID_PARAM   = 0x80616008,
	ERROR_PSMFPLAYER_NO_MORE_DATA    = 0x8061600c,
};

enum PsmfPlayerStatus {
	PSMF_PLAYER_STATUS_NONE             = 0x0,
	PSMF_PLAYER_STATUS_INIT             = 0x1,
	PSMF_PLAYER_STATUS_STANDBY          = 0x2,
	PSMF_PLAYER_STATUS_PLAYING          = 0x4,
	PSMF_PLAYER_STATUS_ERROR            = 0x100,
	PSMF_PLAYER_STATUS_PLAYING_FINISHED = 0x200,
};

enum PsmfPlayerMode {
	PSMF_PLAYER_MODE_PLAY       = 0,
	PSMF_PLAYER_MODE_SLOWMOTION = 1,
	PSMF_PLAYER_MODE_STEPFRAME  = 2,
	PSMF_PLAYER_MODE_PAUSE      = 3,
	PSMF_PLAYER_MODE_FORWARD    = 4,
	PSMF_PLAYER_MODE_REWIND     = 5,
};

struct PsmfData {
	u32_le version;
	u32_le headerSize;
	u32_le headerOffset;
	u32_le streamSize;
	u32_le streamOffset;
	u32_le streamNum;
	u32_le unk1;
	u32_le unk2;
};

struct PsmfPlayerCreateData {
	PSPPointer<u8> buffer;
	u32 bufferSize;
	int threadPriority;
};

struct PsmfPlayerData {
	s32_le videoCodec;
	s32_le videoStreamNum;
	s32_le audioCodec;
	s32_le audioStreamNum;
	s32_le playMode;
	s32_le playSpeed;
};

struct PsmfInfo {
	u32_le lastFrameTS;
	s32_le numVideoStreams;
	s32_le numAudioStreams;
	s32_le numPCMStreams;
	s32_le playerVersion;
};

struct PsmfVideoData {
	s32_le frameWidth;
	u32_le displaybuf;
	u32_le displaypts;
};

struct PsmfEntry {
	int EPPts;
	int EPOffset;
	int EPIndex;
	int EPPicOffset;
};

int getMaxAheadTimestamp(int packets) {return std::max(40000, packets * 700);}

// Some of our platforms don't play too nice with direct unaligned access.
u32 ReadUnalignedU32BE(const u8 *p) {
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
}

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
	Psmf(const u8 *ptr, u32 data);
	~Psmf();
	void DoState(PointerWrap &p);
	
	bool isValidCurrentStreamNumber() {
		return currentStreamNum >= 0 && currentStreamNum < (int)streamMap.size();  // urgh, checking size isn't really right here.
	}

	void setStreamNum(int num);
	bool setStreamWithType(int type, int channel);

	int FindEPWithTimestamp(int pts);

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
	std::vector<PsmfEntry> EPMap;

	PsmfStreamMap streamMap;
};

class PsmfPlayer {
public:
	// For savestates only.
	PsmfPlayer() { mediaengine = new MediaEngine; filehandle = 0;}
	PsmfPlayer(const PsmfPlayerCreateData *data);
	~PsmfPlayer() { if (mediaengine) delete mediaengine; pspFileSystem.CloseFile(filehandle);}
	void DoState(PointerWrap &p);

	void ScheduleFinish(u32 handle) {
		if (!finishThread) {
			finishThread = new HLEHelperThread("scePsmfPlayer", "scePsmfPlayer", "__PsmfPlayerFinish", playbackThreadPriority, 0x100);
			finishThread->Start(handle, 0);
		}
	}
	void AbortFinish() {
		if (finishThread) {
			delete finishThread;
			finishThread = NULL;
		}
	}

	u32 filehandle;
	u32 fileoffset;
	int readSize;
	int streamSize;
	u8 tempbuf[0x10000];

	int videoCodec;
	int videoStreamNum;
	int audioCodec;
	int audioStreamNum;
	int playMode;
	int playSpeed;
	u64 totalDurationTimestamp;

	int displayBuffer;
	int displayBufferSize;
	int playbackThreadPriority;
	int psmfMaxAheadTimestamp;
	int totalVideoStreams;
	int totalAudioStreams;
	int playerVersion;
	int videoStep;
	int warmUp;
	s64 seekDestTimeStamp;

	SceMpegAu psmfPlayerAtracAu;
	SceMpegAu psmfPlayerAvcAu;
	PsmfPlayerStatus status;

	MediaEngine *mediaengine;
	HLEHelperThread *finishThread;
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

	void readMPEGVideoStreamParams(const u8 *addr, const u8 *data, Psmf *psmf) {
		int streamId = addr[0];
		int privateStreamId = addr[1];
		// two unknowns here
		psmf->EPMapOffset = ReadUnalignedU32BE(&addr[4]);
		psmf->EPMapEntriesNum = ReadUnalignedU32BE(&addr[8]);
		psmf->videoWidth = addr[12] * 16;
		psmf->videoHeight = addr[13] * 16;

		const u32 EP_MAP_STRIDE = 1 + 1 + 4 + 4;
		psmf->EPMap.clear();
		for (u32 i = 0; i < psmf->EPMapEntriesNum; i++) {
			const u8 *const entryAddr = data + psmf->EPMapOffset + EP_MAP_STRIDE * i;
			PsmfEntry entry;
			entry.EPIndex = entryAddr[0];
			entry.EPPicOffset = entryAddr[1];
			entry.EPPts = ReadUnalignedU32BE(&entryAddr[2]);
			entry.EPOffset = ReadUnalignedU32BE(&entryAddr[6]);
			psmf->EPMap.push_back(entry);
		}

		INFO_LOG(ME, "PSMF MPEG data found: id=%02x, privid=%02x, epmoff=%08x, epmnum=%08x, width=%i, height=%i", streamId, privateStreamId, psmf->EPMapOffset, psmf->EPMapEntriesNum, psmf->videoWidth, psmf->videoHeight);
	}

	void readPrivateAudioStreamParams(const u8 *addr, Psmf *psmf) {
		int streamId = addr[0];
		int privateStreamId = addr[1];
		psmf->audioChannels = addr[14];
		psmf->audioFrequency = addr[15];
		// two unknowns here
		INFO_LOG(ME, "PSMF private audio found: id=%02x, privid=%02x, channels=%i, freq=%i", streamId, privateStreamId, psmf->audioChannels, psmf->audioFrequency);
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("PsmfStream", 1);
		if (!s)
			return;

		p.Do(type);
		p.Do(channel);
	}


	int type;
	int channel;
};


Psmf::Psmf(const u8 *ptr, u32 data) {
	headerOffset = data;
	magic = *(u32_le *)&ptr[0];
	version = *(u32_le *)&ptr[4];
	streamOffset = ReadUnalignedU32BE(&ptr[8]);
	streamSize = ReadUnalignedU32BE(&ptr[12]);
	streamDataTotalSize = ReadUnalignedU32BE(&ptr[0x50]);
	presentationStartTime = getMpegTimeStamp(ptr + PSMF_FIRST_TIMESTAMP_OFFSET);
	presentationEndTime = getMpegTimeStamp(ptr + PSMF_LAST_TIMESTAMP_OFFSET);
	streamDataNextBlockSize = ReadUnalignedU32BE(&ptr[0x6A]);
	streamDataNextInnerBlockSize = ReadUnalignedU32BE(&ptr[0x7C]);
	numStreams = *(u16_be *)&ptr[0x80];
	// TODO: Always?
	headerSize = 0x800;

	currentStreamNum = -1;
	currentAudioStreamNum = -1;
	currentVideoStreamNum = -1;

	for (int i = 0; i < numStreams; i++) {
		PsmfStream *stream = 0;
		const u8 *const currentStreamAddr = ptr + 0x82 + i * 16;
		int streamId = currentStreamAddr[0];
		if ((streamId & PSMF_VIDEO_STREAM_ID) == PSMF_VIDEO_STREAM_ID) {
			stream = new PsmfStream(PSMF_AVC_STREAM, ++currentVideoStreamNum);
			stream->readMPEGVideoStreamParams(currentStreamAddr, ptr, this);
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

PsmfPlayer::PsmfPlayer(const PsmfPlayerCreateData *data) {
	videoCodec = -1;
	videoStreamNum = -1;
	audioCodec = -1;
	audioStreamNum = -1;
	playMode = 0;
	playSpeed = 1;
	totalDurationTimestamp = 0;
	status = PSMF_PLAYER_STATUS_INIT;
	mediaengine = new MediaEngine;
	finishThread = NULL;
	filehandle = 0;
	fileoffset = 0;
	readSize = 0;
	streamSize = 0;
	videoStep = 0;
	warmUp = 0;
	seekDestTimeStamp = 0;

	psmfPlayerAtracAu.dts =-1;
	psmfPlayerAtracAu.pts = -1;
	psmfPlayerAvcAu.dts = -1;
	psmfPlayerAvcAu.pts = -1;

	displayBuffer = data->buffer.ptr;
	displayBufferSize = data->bufferSize;
	playbackThreadPriority = data->threadPriority;
}

void Psmf::DoState(PointerWrap &p) {
	auto s = p.Section("Psmf", 1, 2);
	if (!s)
		return;

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

	if (s >= 2) {
		p.Do(EPMap);
	}

	p.Do(streamMap);
}

void PsmfPlayer::DoState(PointerWrap &p) {
	auto s = p.Section("PsmfPlayer", 1, 6);
	if (!s)
		return;

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
	if (s >= 4) {
		p.Do(totalDurationTimestamp);
	} else {
		long oldTimestamp;
		p.Do(oldTimestamp);
		totalDurationTimestamp = oldTimestamp;
	}
	if (s >= 2) {
		p.Do(totalVideoStreams);
		p.Do(totalAudioStreams);
		p.Do(playerVersion);
	} else {
		totalVideoStreams = 1;
		totalAudioStreams = 1;
		playerVersion = PSMF_PLAYER_VERSION_FULL;
	}
	if (s >= 3) {
		p.Do(videoStep);
	} else {
		videoStep = 0;
	}
	if (s >= 4) {
		p.Do(warmUp);
	} else {
		warmUp = 10000;
	}
	if (s >= 5) {
		p.Do(seekDestTimeStamp);
	} else {
		seekDestTimeStamp = 0;
	}
	p.DoClass(mediaengine);
	p.Do(filehandle);
	p.Do(fileoffset);
	p.Do(readSize);
	p.Do(streamSize);

	p.Do(status);
	if (s >= 4) {
		p.Do(psmfPlayerAtracAu);
	}
	p.Do(psmfPlayerAvcAu);
	if (s >= 6) {
		p.Do(finishThread);
	} else {
		finishThread = NULL;
	}
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
		if (iter->second->type == type && iter->second->channel == channel) {
			setStreamNum(iter->first);
			return true;
		}
	}
	return false;
}

int Psmf::FindEPWithTimestamp(int pts) {
	int best = -1;
	int bestPts = 0;

	for (int i = 0; i < (int)EPMap.size(); ++i) {
		const int matchPts = EPMap[i].EPPts;
		if (matchPts == pts) {
			// Exact match, take it.
			return i;
		}
		// TODO: Does it actually do fuzzy matching?
		if (matchPts < pts && matchPts >= bestPts) {
			best = i;
			bestPts = matchPts;
		}
	}

	return best;
}


static std::map<u32, Psmf *> psmfMap;
static std::map<u32, PsmfPlayer *> psmfPlayerMap;

Psmf *getPsmf(u32 psmf)
{
	auto psmfstruct = PSPPointer<PsmfData>::Create(psmf);
	if (!psmfstruct.IsValid())
		return 0;
	auto iter = psmfMap.find(psmfstruct->headerOffset);
	if (iter != psmfMap.end())
		return iter->second;
	else
		return 0;
}

PsmfPlayer *getPsmfPlayer(u32 psmfplayer)
{
	auto iter = psmfPlayerMap.find(Memory::Read_U32(psmfplayer));
	if (iter != psmfPlayerMap.end())
		return iter->second;
	else
		return 0;
}

void __PsmfInit()
{
	videoPixelMode = GE_CMODE_32BIT_ABGR8888;
	videoLoopStatus = PSMF_PLAYER_CONFIG_NO_LOOP;
}

void __PsmfDoState(PointerWrap &p)
{
	auto s = p.Section("scePsmf", 1);
	if (!s)
		return;

	p.Do(psmfMap);
}

void __PsmfPlayerDoState(PointerWrap &p)
{
	auto s = p.Section("scePsmfPlayer", 1);
	if (!s)
		return;

	p.Do(psmfPlayerMap);
	p.Do(videoPixelMode);
	p.Do(videoLoopStatus);
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
	if (!Memory::IsValidAddress(psmfData)) {
		// TODO: Check error code.
		ERROR_LOG_REPORT(ME, "scePsmfSetPsmf(%08x, %08x): bad address", psmfStruct, psmfData);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	INFO_LOG(ME, "scePsmfSetPsmf(%08x, %08x)", psmfStruct, psmfData);

	Psmf *psmf = new Psmf(Memory::GetPointer(psmfData), psmfData);

	PsmfData data = {0};
	data.version = psmf->version;
	data.headerSize = 0x800;
	data.streamSize = psmf->streamSize;
	data.streamNum = psmf->numStreams;
	data.headerOffset = psmf->headerOffset;
	auto iter = psmfMap.find(data.headerOffset);
	if (iter != psmfMap.end())
		delete iter->second;
	psmfMap[data.headerOffset] = psmf;
	Memory::WriteStruct(psmfStruct, &data);
	return 0;
}

u32 scePsmfGetNumberOfStreams(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetNumberOfStreams(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetNumberOfStreams(%08x)", psmfStruct);
	return psmf->numStreams;
}

u32 scePsmfGetNumberOfSpecificStreams(u32 psmfStruct, int streamType)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetNumberOfSpecificStreams(%08x, %08x): invalid psmf", psmfStruct, streamType);
		return ERROR_PSMF_NOT_FOUND;
	}
	WARN_LOG(ME, "scePsmfGetNumberOfSpecificStreams(%08x, %08x)", psmfStruct, streamType);
	int streamNum = 0;
	int type = (streamType == PSMF_AUDIO_STREAM ? PSMF_ATRAC_STREAM : streamType);
	for (int i = (int)psmf->streamMap.size() - 1; i >= 0; i--) {
		if (psmf->streamMap[i]->type == type)
			streamNum++;
	}
	return streamNum;
}

u32 scePsmfSpecifyStreamWithStreamType(u32 psmfStruct, u32 streamType, u32 channel)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfSpecifyStreamWithStreamType(%08x, %08x, %i): invalid psmf", psmfStruct, streamType, channel);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(ME, "scePsmfSpecifyStreamWithStreamType(%08x, %08x, %i)", psmfStruct, streamType, channel);
	if (!psmf->setStreamWithType(streamType, channel)) {
		psmf->setStreamNum(-1);
	}
	return 0;
}

u32 scePsmfSpecifyStreamWithStreamTypeNumber(u32 psmfStruct, u32 streamType, u32 typeNum)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfSpecifyStreamWithStreamTypeNumber(%08x, %08x, %08x): invalid psmf", psmfStruct, streamType, typeNum);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG_REPORT(ME, "scePsmfSpecifyStreamWithStreamTypeNumber(%08x, %08x, %08x)", psmfStruct, streamType, typeNum);
	// right now typeNum and channel are the same...
	if (!psmf->setStreamWithType(streamType, typeNum)) {
		psmf->setStreamNum(-1);
	}
	return 0;
}

u32 scePsmfSpecifyStream(u32 psmfStruct, int streamNum) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfSpecifyStream(%08x, %i): invalid psmf", psmfStruct, streamNum);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(ME, "scePsmfSpecifyStream(%08x, %i)", psmfStruct, streamNum);
	psmf->setStreamNum(streamNum);
	return 0;
}

u32 scePsmfGetVideoInfo(u32 psmfStruct, u32 videoInfoAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetVideoInfo(%08x, %08x): invalid psmf", psmfStruct, videoInfoAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(ME, "scePsmfGetVideoInfo(%08x, %08x)", psmfStruct, videoInfoAddr);
	if (Memory::IsValidAddress(videoInfoAddr)) {
		Memory::Write_U32(psmf->videoWidth, videoInfoAddr);
		Memory::Write_U32(psmf->videoHeight, videoInfoAddr + 4);
	}
	return 0;
}

u32 scePsmfGetAudioInfo(u32 psmfStruct, u32 audioInfoAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetAudioInfo(%08x, %08x): invalid psmf", psmfStruct, audioInfoAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(ME, "scePsmfGetAudioInfo(%08x, %08x)", psmfStruct, audioInfoAddr);
	if (Memory::IsValidAddress(audioInfoAddr)) {
		Memory::Write_U32(psmf->audioChannels, audioInfoAddr);
		Memory::Write_U32(psmf->audioFrequency, audioInfoAddr + 4);
	}
	return 0;
}

u32 scePsmfGetCurrentStreamType(u32 psmfStruct, u32 typeAddr, u32 channelAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetCurrentStreamType(%08x, %08x, %08x): invalid psmf", psmfStruct, typeAddr, channelAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	INFO_LOG(ME, "scePsmfGetCurrentStreamType(%08x, %08x, %08x)", psmfStruct, typeAddr, channelAddr);
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
		ERROR_LOG(ME, "scePsmfGetStreamSize(%08x, %08x): invalid psmf", psmfStruct, sizeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetStreamSize(%08x, %08x)", psmfStruct, sizeAddr);
	if (Memory::IsValidAddress(sizeAddr)) {
		Memory::Write_U32(psmf->streamSize, sizeAddr);
	}
	return 0;
}

u32 scePsmfQueryStreamOffset(u32 bufferAddr, u32 offsetAddr)
{
	WARN_LOG(ME, "scePsmfQueryStreamOffset(%08x, %08x)", bufferAddr, offsetAddr);
	if (Memory::IsValidAddress(offsetAddr)) {
		Memory::Write_U32(bswap32(Memory::Read_U32(bufferAddr + PSMF_STREAM_OFFSET_OFFSET)), offsetAddr);
	}
	return 0;
}

u32 scePsmfQueryStreamSize(u32 bufferAddr, u32 sizeAddr)
{
	WARN_LOG(ME, "scePsmfQueryStreamSize(%08x, %08x)", bufferAddr, sizeAddr);
	if (Memory::IsValidAddress(sizeAddr)) {
		Memory::Write_U32(bswap32(Memory::Read_U32(bufferAddr + PSMF_STREAM_SIZE_OFFSET)), sizeAddr);
	}
	return 0;
}

u32 scePsmfGetHeaderSize(u32 psmfStruct, u32 sizeAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetHeaderSize(%08x, %08x): invalid psmf", psmfStruct, sizeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetHeaderSize(%08x, %08x)", psmfStruct, sizeAddr);
	if (Memory::IsValidAddress(sizeAddr)) {
		Memory::Write_U32(psmf->headerSize, sizeAddr);
	}
	return 0;
}

u32 scePsmfGetPsmfVersion(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetPsmfVersion(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetPsmfVersion(%08x)", psmfStruct);
	return psmf->version;
}

u32 scePsmfVerifyPsmf(u32 psmfAddr)
{
	u32 magic = Memory::Read_U32(psmfAddr);
	if (magic != PSMF_MAGIC) {
		ERROR_LOG(ME, "scePsmfVerifyPsmf(%08x): bad magic %08x", psmfAddr, magic);
		return ERROR_PSMF_NOT_FOUND;
	}
	int version = Memory::Read_U32(psmfAddr + PSMF_STREAM_VERSION_OFFSET);
	if (version < 0) {
		ERROR_LOG(ME, "scePsmfVerifyPsmf(%08x): bad version %08x", psmfAddr, version);
		return ERROR_PSMF_NOT_FOUND;
	}
	// Kurohyou 2 (at least the demo) uses an uninitialized value that happens to be zero on the PSP.
	// It appears to be written by scePsmfVerifyPsmf(), so we write some bytes into the stack here.
	Memory::Memset(currentMIPS->r[MIPS_REG_SP] - 0x20, 0, 0x20);
	DEBUG_LOG(ME, "scePsmfVerifyPsmf(%08x)", psmfAddr);
	return 0;
}

u32 scePsmfGetNumberOfEPentries(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetNumberOfEPentries(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetNumberOfEPentries(%08x)", psmfStruct);
	return psmf->EPMapEntriesNum;
}

u32 scePsmfGetPresentationStartTime(u32 psmfStruct, u32 startTimeAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetPresentationStartTime(%08x, %08x): invalid psmf", psmfStruct, startTimeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetPresentationStartTime(%08x, %08x)", psmfStruct, startTimeAddr);
	if (Memory::IsValidAddress(startTimeAddr)) {
		Memory::Write_U32(psmf->presentationStartTime, startTimeAddr);
	}
	return 0;
}

u32 scePsmfGetPresentationEndTime(u32 psmfStruct, u32 endTimeAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetPresentationEndTime(%08x, %08x): invalid psmf", psmfStruct, endTimeAddr);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetPresentationEndTime(%08x, %08x)", psmfStruct, endTimeAddr);
	if (Memory::IsValidAddress(endTimeAddr)) {
		Memory::Write_U32(psmf->presentationEndTime, endTimeAddr);
	}
	return 0;
}

u32 scePsmfGetCurrentStreamNumber(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetCurrentStreamNumber(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}

	DEBUG_LOG(ME, "scePsmfGetCurrentStreamNumber(%08x)", psmfStruct);
	return psmf->currentStreamNum;
}

u32 scePsmfCheckEPMap(u32 psmfStruct) 
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfCheckEPMap(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}

	DEBUG_LOG(ME, "scePsmfCheckEPMap(%08x)", psmfStruct);
	return psmf->EPMap.empty() ? ERROR_PSMF_NOT_FOUND : 0;
}

u32 scePsmfGetEPWithId(u32 psmfStruct, int epid, u32 entryAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetEPWithId(%08x, %i, %08x): invalid psmf", psmfStruct, epid, entryAddr);
		return ERROR_PSMF_NOT_INITIALIZED;
	}
	DEBUG_LOG(ME, "scePsmfGetEPWithId(%08x, %i, %08x)", psmfStruct, epid, entryAddr);

	if (epid < 0 || epid >= (int)psmf->EPMap.size()) {
		ERROR_LOG(ME, "scePsmfGetEPWithId(%08x, %i): invalid id", psmfStruct, epid);
		return ERROR_PSMF_NOT_FOUND;
	}
	if (Memory::IsValidAddress(entryAddr)) {
		Memory::WriteStruct(entryAddr, &psmf->EPMap[epid]);
	}
	return 0;
}

u32 scePsmfGetEPWithTimestamp(u32 psmfStruct, u32 ts, u32 entryAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetEPWithTimestamp(%08x, %i, %08x): invalid psmf", psmfStruct, ts, entryAddr);
		return ERROR_PSMF_NOT_INITIALIZED;
	}
	DEBUG_LOG(ME, "scePsmfGetEPWithTimestamp(%08x, %i, %08x)", psmfStruct, ts, entryAddr);

	if (ts < psmf->presentationStartTime) {
		ERROR_LOG(ME, "scePsmfGetEPWithTimestamp(%08x, %i): invalid timestamp", psmfStruct, ts);
		return ERROR_PSMF_NOT_FOUND;
	}

	int epid = psmf->FindEPWithTimestamp(ts);
	if (epid < 0 || epid >= (int)psmf->EPMap.size()) {
		ERROR_LOG(ME, "scePsmfGetEPWithTimestamp(%08x, %i): invalid id", psmfStruct, epid);
		return ERROR_PSMF_NOT_FOUND;
	}

	if (Memory::IsValidAddress(entryAddr)) {
		Memory::WriteStruct(entryAddr, &psmf->EPMap[epid]);
	}
	return 0;
}

u32 scePsmfGetEPidWithTimestamp(u32 psmfStruct, u32 ts)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetEPidWithTimestamp(%08x, %i): invalid psmf", psmfStruct, ts);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetEPidWithTimestamp(%08x, %i)", psmfStruct, ts);

	if (psmf->EPMap.empty()) {
		ERROR_LOG(ME, "scePsmfGetEPidWithTimestamp(%08x): EPMap is empty", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}

	if (ts < psmf->presentationStartTime) {
		ERROR_LOG(ME, "scePsmfGetEPidWithTimestamp(%08x, %i): invalid timestamp", psmfStruct, ts);
		return ERROR_PSMF_INVALID_TIMESTAMP;
	}

	int epid = psmf->FindEPWithTimestamp(ts);
	if (epid < 0 || epid >= (int)psmf->EPMap.size()) {
		ERROR_LOG(ME, "scePsmfGetEPidWithTimestamp(%08x, %i): invalid id", psmfStruct, epid);
		return ERROR_PSMF_INVALID_ID;
	}

	return epid;
}

int scePsmfPlayerCreate(u32 psmfPlayer, u32 dataPtr)
{
	auto player = PSPPointer<u32>::Create(psmfPlayer);
	const auto data = PSPPointer<const PsmfPlayerCreateData>::Create(dataPtr);

	if (!player.IsValid() || !data.IsValid()) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerCreate(%08x, %08x): bad pointers", psmfPlayer, dataPtr);
		// Crashes on a PSP.
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}
	if (!data->buffer.IsValid()) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerCreate(%08x, %08x): invalid buffer address %08x", psmfPlayer, dataPtr, data->buffer.ptr);
		// Also crashes on a PSP.
		*player = 0;
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}
	if (data->bufferSize < 0x00285800) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerCreate(%08x, %08x): buffer too small %08x", psmfPlayer, dataPtr, data->bufferSize);
		*player = 0;
		return ERROR_PSMFPLAYER_BUFFER_SIZE;
	}
	if (data->threadPriority < 0x10 || data->threadPriority >= 0x6E) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerCreate(%08x, %08x): bad thread priority %02x", psmfPlayer, dataPtr, data->threadPriority);
		*player = 0;
		return ERROR_PSMFPLAYER_INVALID_PARAM;
	}
	if (!psmfPlayerMap.empty()) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerCreate(%08x, %08x): already have an active player", psmfPlayer, dataPtr);
		// TODO: Tests show this is what we should do.  Leaving it off for now to see if safe.
		//*player = 0;
		//return ERROR_MPEG_ALREADY_INIT;
	}

	INFO_LOG(ME, "scePsmfPlayerCreate(%08x, %08x)", psmfPlayer, dataPtr);
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		psmfplayer = new PsmfPlayer(data);
		if (psmfPlayerMap.find(psmfPlayer) != psmfPlayerMap.end())
			delete psmfPlayerMap[psmfPlayer];
		psmfPlayerMap[psmfPlayer] = psmfplayer;

		// Write something there to identify it with.
		*player = psmfPlayer;
	}

	// These really shouldn't be globals.  But, you can only have one psmfplayer anyway.
	videoPixelMode = GE_CMODE_32BIT_ABGR8888;
	videoLoopStatus = PSMF_PLAYER_CONFIG_NO_LOOP;

	psmfplayer->psmfMaxAheadTimestamp = getMaxAheadTimestamp(581);
	psmfplayer->status = PSMF_PLAYER_STATUS_INIT;
	return hleDelayResult(0, "player create", 20000);
}

int scePsmfPlayerStop(u32 psmfPlayer) {
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerStop(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerStop(%08x): not yet playing", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	psmfplayer->AbortFinish();

	INFO_LOG(ME, "scePsmfPlayerStop(%08x)", psmfPlayer);
	psmfplayer->status = PSMF_PLAYER_STATUS_STANDBY;
	return hleDelayResult(0, "psmfplayer stop", 3000);
}

int scePsmfPlayerBreak(u32 psmfPlayer)
{
	WARN_LOG(ME, "scePsmfPlayerBreak(%08x)", psmfPlayer);
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerBreak(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	psmfplayer->AbortFinish();

	return 0;
}

int _PsmfPlayerFillRingbuffer(PsmfPlayer *psmfplayer) {
	if (!psmfplayer->filehandle)
		return -1;
	u8* buf = psmfplayer->tempbuf;
	int tempbufSize = (int)sizeof(psmfplayer->tempbuf);
	int size;
	// Let's not burn a bunch of time adding data all at once.
	int addMax = std::max(2048 * 100, tempbufSize);
	do {
		size = std::min(psmfplayer->mediaengine->getRemainSize(), tempbufSize);
		size = std::min(psmfplayer->mediaengine->getAudioRemainSize(), size);
		size = std::min(psmfplayer->streamSize - psmfplayer->readSize, size);
		if (size <= 0)
			break;
		size = (int)pspFileSystem.ReadFile(psmfplayer->filehandle, buf, size);
		psmfplayer->readSize += size;
		psmfplayer->mediaengine->addStreamData(buf, size);
		addMax -= size;
		if (addMax <= 0)
			break;
	} while (size > 0);
	if (psmfplayer->readSize >= psmfplayer->streamSize && videoLoopStatus == PSMF_PLAYER_CONFIG_LOOP) {
		// start looping
		psmfplayer->readSize = 0;
		pspFileSystem.SeekFile(psmfplayer->filehandle, psmfplayer->fileoffset, FILEMOVE_BEGIN);
	}
	return 0;
}

int _PsmfPlayerSetPsmfOffset(u32 psmfPlayer, const char *filename, int offset, bool docallback) {
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer || psmfplayer->status != PSMF_PLAYER_STATUS_INIT) {
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (!filename) {
		return ERROR_PSMFPLAYER_INVALID_PARAM;
	}

	int delayUs = 1100;

	psmfplayer->filehandle = pspFileSystem.OpenFile(filename, (FileAccess) FILEACCESS_READ);
	if (!psmfplayer->filehandle) {
		return hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "psmfplayer set", delayUs);
	}

	if (psmfplayer->filehandle && psmfplayer->tempbuf) {
		if (offset != 0)
			pspFileSystem.SeekFile(psmfplayer->filehandle, offset, FILEMOVE_BEGIN);
		u8 *buf = psmfplayer->tempbuf;
		int tempbufSize = (int)sizeof(psmfplayer->tempbuf);
		int size = (int)pspFileSystem.ReadFile(psmfplayer->filehandle, buf, 2048);
		delayUs += 2000;

		const u32 magic = *(u32_le *)buf;
		if (magic != PSMF_MAGIC) {
			// TODO: Let's keep trying as we were before.
			ERROR_LOG_REPORT(ME, "scePsmfPlayerSetPsmf*: incorrect PSMF magic, bad data");
			//return SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
		}

		// TODO: Merge better with Psmf.
		u16 numStreams = *(u16_be *)(buf + 0x80);
		if (numStreams > 128) {
			ERROR_LOG_REPORT(ME, "scePsmfPlayerSetPsmf*: too many streams in PSMF video, bogus data");
			return hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "psmfplayer set", delayUs);
		}

		psmfplayer->totalVideoStreams = 0;
		psmfplayer->totalAudioStreams = 0;
		psmfplayer->playerVersion = PSMF_PLAYER_VERSION_FULL;
		for (u16 i = 0; i < numStreams; i++) {
			const u8 *currentStreamAddr = buf + 0x82 + i * 16;
			const int streamId = *currentStreamAddr;
			if ((streamId & PSMF_VIDEO_STREAM_ID) == PSMF_VIDEO_STREAM_ID) {
				++psmfplayer->totalVideoStreams;
				// If we don't have EP info for /any/ video stream, revert to BASIC.
				const u32 epOffset = ReadUnalignedU32BE(currentStreamAddr + 4);
				const u32 epEntries = ReadUnalignedU32BE(currentStreamAddr + 8);
				// TODO: Actually, if these don't match, it seems to be an invalid PSMF.
				if (epOffset == 0 || epEntries == 0) {
					psmfplayer->playerVersion = PSMF_PLAYER_VERSION_BASIC;
				}
			} else if ((streamId & PSMF_AUDIO_STREAM_ID) == PSMF_AUDIO_STREAM_ID) {
				++psmfplayer->totalAudioStreams;
			} else {
				WARN_LOG_REPORT(ME, "scePsmfPlayerSetPsmf*: unexpected streamID %x", streamId);
			}
		}
		// TODO: It seems like it's invalid if there's not at least 1 video stream.

		int mpegoffset = *(s32_be *)(buf + PSMF_STREAM_OFFSET_OFFSET);
		psmfplayer->readSize = size - mpegoffset;
		psmfplayer->streamSize = *(s32_be *)(buf + PSMF_STREAM_SIZE_OFFSET);
		psmfplayer->fileoffset = offset + mpegoffset;
		psmfplayer->mediaengine->loadStream(buf, 2048, std::max(2048 * 500, tempbufSize));
		_PsmfPlayerFillRingbuffer(psmfplayer);
		psmfplayer->totalDurationTimestamp = psmfplayer->mediaengine->getLastTimeStamp();
	}

	psmfplayer->status = PSMF_PLAYER_STATUS_STANDBY;

	return hleDelayResult(0, "psmfplayer set", delayUs);
}

int scePsmfPlayerSetPsmf(u32 psmfPlayer, const char *filename) 
{
	u32 result = _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, 0, false);
	if (result == ERROR_PSMFPLAYER_INVALID_STATUS) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSetPsmf(%08x, %s): invalid psmf player or status", psmfPlayer, filename);
	} else if (result == ERROR_PSMFPLAYER_INVALID_PARAM) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmf(%08x, %s): invalid filename", psmfPlayer, filename);
	} else if (result == SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmf(%08x, %s): invalid file data or does not exist", psmfPlayer, filename);
	} else {
		INFO_LOG(ME, "scePsmfPlayerSetPsmf(%08x, %s)", psmfPlayer, filename);
	}
	return result;
}

int scePsmfPlayerSetPsmfCB(u32 psmfPlayer, const char *filename) 
{
	// TODO: hleCheckCurrentCallbacks?
	u32 result = _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, 0, true);
	if (result == ERROR_PSMFPLAYER_INVALID_STATUS) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSetPsmfCB(%08x, %s): invalid psmf player or status", psmfPlayer, filename);
	} else if (result == ERROR_PSMFPLAYER_INVALID_PARAM) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmfCB(%08x, %s): invalid filename", psmfPlayer, filename);
	} else if (result == SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmfCB(%08x, %s): invalid file data or does not exist", psmfPlayer, filename);
	} else {
		INFO_LOG(ME, "scePsmfPlayerSetPsmfCB(%08x, %s)", psmfPlayer, filename);
	}
	return result;
}

int scePsmfPlayerSetPsmfOffset(u32 psmfPlayer, const char *filename, int offset) 
{
	u32 result = _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, offset, false);
	if (result == ERROR_PSMFPLAYER_INVALID_STATUS) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSetPsmfOffset(%08x, %s): invalid psmf player or status", psmfPlayer, filename);
	} else if (result == ERROR_PSMFPLAYER_INVALID_PARAM) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmfOffset(%08x, %s): invalid filename", psmfPlayer, filename);
	} else if (result == SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmfOffset(%08x, %s): invalid file data or does not exist", psmfPlayer, filename);
	} else {
		INFO_LOG(ME, "scePsmfPlayerSetPsmfOffset(%08x, %s)", psmfPlayer, filename);
	}
	return result;
}

int scePsmfPlayerSetPsmfOffsetCB(u32 psmfPlayer, const char *filename, int offset) 
{
	// TODO: hleCheckCurrentCallbacks?
	u32 result = _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, offset, true);
	if (result == ERROR_PSMFPLAYER_INVALID_STATUS) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSetPsmfOffsetCB(%08x, %s): invalid psmf player or status", psmfPlayer, filename);
	} else if (result == ERROR_PSMFPLAYER_INVALID_PARAM) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmfOffsetCB(%08x, %s): invalid filename", psmfPlayer, filename);
	} else if (result == SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT) {
		ERROR_LOG(ME, "scePsmfPlayerSetPsmfOffsetCB(%08x, %s): invalid file data or does not exist", psmfPlayer, filename);
	} else {
		INFO_LOG(ME, "scePsmfPlayerSetPsmfOffsetCB(%08x, %s)", psmfPlayer, filename);
	}
	return result;
}

int scePsmfPlayerGetAudioOutSize(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetAudioOutSize(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	WARN_LOG(ME, "%i = scePsmfPlayerGetAudioOutSize(%08x)", audioSamplesBytes, psmfPlayer);
	return audioSamplesBytes;
}

bool __PsmfPlayerContinueSeek(PsmfPlayer *psmfplayer, int tries = 50) {
	if (psmfplayer->seekDestTimeStamp <= 0) {
		return true;
	}

	while (!psmfplayer->mediaengine->seekTo(psmfplayer->seekDestTimeStamp, videoPixelMode)) {
		if (--tries <= 0) {
			return false;
		}
		_PsmfPlayerFillRingbuffer(psmfplayer);
		if (psmfplayer->mediaengine->IsVideoEnd()) {
			break;
		}
	}

	// Seek is done, so forget about it.
	psmfplayer->seekDestTimeStamp = 0;
	return true;
}

int scePsmfPlayerStart(u32 psmfPlayer, u32 psmfPlayerData, int initPts)
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerStart(%08x, %08x, %d): invalid psmf player", psmfPlayer, psmfPlayerData, initPts);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status == PSMF_PLAYER_STATUS_INIT) {
		ERROR_LOG(ME, "scePsmfPlayerStart(%08x, %08x, %d): psmf not yet set", psmfPlayer, psmfPlayerData, initPts);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	auto playerData = PSPPointer<PsmfPlayerData>::Create(psmfPlayerData);
	if (!playerData.IsValid()) {
		// Crashes on a PSP.
		ERROR_LOG(ME, "scePsmfPlayerStart(%08x, %08x, %d): bad data address", psmfPlayer, psmfPlayerData, initPts);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}
	if (playerData->playMode < 0 || playerData->playMode > (int)PSMF_PLAYER_MODE_REWIND) {
		ERROR_LOG(ME, "scePsmfPlayerStart(%08x, %08x, %d): invalid mode", psmfPlayer, psmfPlayerData, initPts);
		return ERROR_PSMFPLAYER_INVALID_PARAM;
	}
	if (initPts >= psmfplayer->mediaengine->getLastTimeStamp()) {
		ERROR_LOG(ME, "scePsmfPlayerStart(%08x, %08x, %d): pts is outside video", psmfPlayer, psmfPlayerData, initPts);
		return ERROR_PSMFPLAYER_INVALID_PARAM;
	}

	if (psmfplayer->totalAudioStreams > 0) {
		if (playerData->audioCodec != 0x0F && playerData->audioCodec != 0x01) {
			ERROR_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): invalid audio codec %02x", psmfPlayer, psmfPlayerData, initPts, playerData->audioCodec);
			return ERROR_PSMFPLAYER_INVALID_STREAM;
		}
		if (playerData->audioStreamNum >= psmfplayer->totalAudioStreams) {
			ERROR_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): unable to change audio stream to %d", psmfPlayer, psmfPlayerData, initPts, playerData->audioStreamNum);
			return ERROR_PSMFPLAYER_INVALID_CONFIG;
		}
	}
	if (playerData->videoCodec != 0x0E && playerData->videoCodec != 0x00) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): invalid video codec %02x", psmfPlayer, psmfPlayerData, initPts, playerData->videoCodec);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}
	if (playerData->videoStreamNum < 0 || playerData->videoStreamNum >= psmfplayer->totalVideoStreams) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): unable to change video stream to %d", psmfPlayer, psmfPlayerData, initPts, playerData->videoStreamNum);
		return ERROR_PSMFPLAYER_INVALID_CONFIG;
	}

	switch ((PsmfPlayerMode)(s32)playerData->playMode) {
	case PSMF_PLAYER_MODE_FORWARD:
	case PSMF_PLAYER_MODE_REWIND:
		if (psmfplayer->playerVersion == PSMF_PLAYER_VERSION_BASIC) {
			WARN_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): no EP data for FORWARD/REWIND", psmfPlayer, psmfPlayerData, initPts);
			return ERROR_PSMFPLAYER_INVALID_PARAM;
		}
		WARN_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): unsupported playMode", psmfPlayer, psmfPlayerData, initPts);
		break;

	case PSMF_PLAYER_MODE_PLAY:
	case PSMF_PLAYER_MODE_PAUSE:
		break;

	default:
		WARN_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): unsupported playMode", psmfPlayer, psmfPlayerData, initPts);
		break;
	}

	if (psmfplayer->playerVersion == PSMF_PLAYER_VERSION_BASIC && initPts != 0) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerStart(%08x, %08x, %d): unable to seek without EPmap", psmfPlayer, psmfPlayerData, initPts);
		return ERROR_PSMFPLAYER_INVALID_PARAM;
	}

	WARN_LOG(ME, "scePsmfPlayerStart(%08x, %08x, %d)", psmfPlayer, psmfPlayerData, initPts);

	psmfplayer->AbortFinish();
	psmfplayer->mediaengine->setVideoStream(playerData->videoStreamNum);
	psmfplayer->videoCodec = playerData->videoCodec;
	psmfplayer->videoStreamNum = playerData->videoStreamNum;
	if (psmfplayer->totalAudioStreams > 0) {
		psmfplayer->mediaengine->setAudioStream(playerData->audioStreamNum);
		psmfplayer->audioCodec = playerData->audioCodec;
		psmfplayer->audioStreamNum = playerData->audioStreamNum;
	}
	psmfplayer->playMode = playerData->playMode;
	psmfplayer->playSpeed = playerData->playSpeed;

	// Does not alter current pts, it just catches up when Update()/etc. get there.

	int delayUs = psmfplayer->status == PSMF_PLAYER_STATUS_PLAYING ? 3000 : 0;
	psmfplayer->status = PSMF_PLAYER_STATUS_PLAYING;
	psmfplayer->warmUp = 0;

	psmfplayer->mediaengine->openContext();

	s64 dist = initPts - psmfplayer->mediaengine->getVideoTimeStamp();
	if (dist < 0 || dist > VIDEO_FRAME_DURATION_TS * 60) {
		// When seeking backwards, we just start populating the stream from the start.
		pspFileSystem.SeekFile(psmfplayer->filehandle, 0, FILEMOVE_BEGIN);

		u8 *buf = psmfplayer->tempbuf;
		int tempbufSize = (int)sizeof(psmfplayer->tempbuf);
		int size = (int)pspFileSystem.ReadFile(psmfplayer->filehandle, buf, tempbufSize);
		psmfplayer->mediaengine->loadStream(buf, size, std::max(2048 * 500, tempbufSize));

		int mpegoffset = *(s32_be *)(buf + PSMF_STREAM_OFFSET_OFFSET);
		psmfplayer->readSize = size - mpegoffset;

		Psmf psmf(psmfplayer->tempbuf, 0);

		int lastOffset = 0;
		for (auto it = psmf.EPMap.begin(), end = psmf.EPMap.end(); it != end; ++it) {
			if (initPts <= it->EPPts - (int)psmf.presentationStartTime) {
				break;
			}
			lastOffset = it->EPOffset;
		}

		psmfplayer->readSize = lastOffset * 2048;
		pspFileSystem.SeekFile(psmfplayer->filehandle, psmfplayer->fileoffset + psmfplayer->readSize, FILEMOVE_BEGIN);

		_PsmfPlayerFillRingbuffer(psmfplayer);
	}

	psmfplayer->seekDestTimeStamp = initPts;
	__PsmfPlayerContinueSeek(psmfplayer);
	return delayUs == 0 ? 0 : hleDelayResult(0, "psmfplayer start", delayUs);
}

int scePsmfPlayerDelete(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerDelete(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	INFO_LOG(ME, "scePsmfPlayerDelete(%08x)", psmfPlayer);
	delete psmfplayer;
	psmfPlayerMap.erase(Memory::Read_U32(psmfPlayer));
	Memory::Write_U32(0, psmfPlayer);

	return hleDelayResult(0, "psmfplayer deleted", 20000);
}

int scePsmfPlayerUpdate(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerUpdate(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerUpdate(%08x): not playing yet", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	DEBUG_LOG(ME, "scePsmfPlayerUpdate(%08x)", psmfPlayer);
	bool videoPtsEnd = (s64)psmfplayer->psmfPlayerAvcAu.pts >= (s64)psmfplayer->totalDurationTimestamp - VIDEO_FRAME_DURATION_TS;
	if (videoPtsEnd || (psmfplayer->mediaengine->IsVideoEnd() && psmfplayer->mediaengine->IsNoAudioData())) {
		if (videoLoopStatus == PSMF_PLAYER_CONFIG_NO_LOOP && psmfplayer->videoStep >= 1) {
			if (psmfplayer->status != PSMF_PLAYER_STATUS_PLAYING_FINISHED) {
				psmfplayer->ScheduleFinish(psmfPlayer);
				INFO_LOG(ME, "scePsmfPlayerUpdate(%08x): video end scheduled", psmfPlayer);
			}
		}
	}
	psmfplayer->videoStep++;

	return 0;
}

int scePsmfPlayerReleasePsmf(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerReleasePsmf(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_STANDBY) {
		ERROR_LOG(ME, "scePsmfPlayerReleasePsmf(%08x): not set yet", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	WARN_LOG(ME, "scePsmfPlayerReleasePsmf(%08x)", psmfPlayer);
	psmfplayer->status = PSMF_PLAYER_STATUS_INIT;
	return 0;
}

void __PsmfUpdatePts(PsmfPlayer *psmfplayer, PsmfVideoData *videoData) {
	// getVideoTimestamp() includes the frame duration, remove it for this frame's pts.
	psmfplayer->psmfPlayerAvcAu.pts = psmfplayer->mediaengine->getVideoTimeStamp() - VIDEO_FRAME_DURATION_TS;
	if (videoData) {
		videoData->displaypts = (u32)psmfplayer->psmfPlayerAvcAu.pts;
	}
}

int scePsmfPlayerGetVideoData(u32 psmfPlayer, u32 videoDataAddr)
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetVideoData(%08x, %08x): invalid psmf player", psmfPlayer, videoDataAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerGetVideoData(%08x, %08x): psmf not playing", psmfPlayer, videoDataAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	auto videoData = PSPPointer<PsmfVideoData>::Create(videoDataAddr);
	if (!videoData.IsValid() || !Memory::IsValidAddress(videoData->displaybuf)) {
		ERROR_LOG(ME, "scePsmfPlayerGetVideoData(%08x, %08x): invalid data pointer", psmfPlayer, videoDataAddr);
		// Technically just crashes if videoData is not valid.
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}
	if (videoData->frameWidth < 0) {
		ERROR_LOG(ME, "scePsmfPlayerGetVideoData(%08x, %08x): illegal bufw %d", psmfPlayer, videoDataAddr, videoData->frameWidth);
		return SCE_KERNEL_ERROR_PRIV_REQUIRED;
	}
	if (videoData->frameWidth != 0 && videoData->frameWidth < psmfplayer->mediaengine->VideoWidth()) {
		ERROR_LOG(ME, "scePsmfPlayerGetVideoData(%08x, %08x): bufw %d smaller than width %d", psmfPlayer, videoDataAddr, videoData->frameWidth, psmfplayer->mediaengine->VideoWidth());
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}

	hleEatCycles(20000);

	if (!__PsmfPlayerContinueSeek(psmfplayer)) {
		DEBUG_LOG(HLE, "scePsmfPlayerGetVideoData(%08x, %08x): still seeking", psmfPlayer, videoDataAddr);
		return ERROR_PSMFPLAYER_NO_MORE_DATA;
	}

	// On a real PSP, this takes a potentially variable amount of time.
	// Normally a minimum of 3 without audio, 5 with.  But if you don't delay sufficiently between, hundreds.
	// It should be okay if we start videos quicker, but some games expect the first couple to fail.
	if (psmfplayer->warmUp < PSMF_PLAYER_WARMUP_FRAMES) {
		DEBUG_LOG(ME, "scePsmfPlayerGetVideoData(%08x, %08x): warming up", psmfPlayer, videoDataAddr);
		++psmfplayer->warmUp;
		return ERROR_PSMFPLAYER_NO_MORE_DATA;
	}
	// In case we change warm up later, save a high value in savestates - video started.
	psmfplayer->warmUp = 10000;

	bool doVideoStep = true;
	if (psmfplayer->playMode == PSMF_PLAYER_MODE_PAUSE) {
		doVideoStep = false;
	} else if (!psmfplayer->mediaengine->IsNoAudioData()) {
		s64 deltapts = psmfplayer->mediaengine->getVideoTimeStamp() - psmfplayer->mediaengine->getAudioTimeStamp();
		// Don't skip the very first frame, sometimes audio starts with an early timestamp.
		if (deltapts > 0 && psmfplayer->mediaengine->getVideoTimeStamp() > 0) {
			// Don't advance, just return the same frame again.
			// TODO: This also seems somewhat based on Update() calls, but audio is involved too...
			doVideoStep = false;
		} else {
			// This is an approximation, it should allow a certain amount ahead before skipping frames.
			while (deltapts <= -(VIDEO_FRAME_DURATION_TS * 5)) {
				psmfplayer->mediaengine->stepVideo(videoPixelMode, true);
				deltapts = psmfplayer->mediaengine->getVideoTimeStamp() - psmfplayer->mediaengine->getAudioTimeStamp();
			}
		}
	} else {
		// No audio, based on Update() calls.  playSpeed doesn't seem to matter?
		if (psmfplayer->videoStep <= 1 && psmfplayer->mediaengine->getVideoTimeStamp() > 0) {
			doVideoStep = false;
		} else {
			psmfplayer->videoStep = 0;
		}
	}

	if (doVideoStep) {
		psmfplayer->mediaengine->stepVideo(videoPixelMode);
	}

	// It seems the frameWidth is rounded down to even values, and defaults to 512.
	int bufw = videoData->frameWidth == 0 ? 512 : videoData->frameWidth & ~1;
	// Always write the video frame, even after the video has ended.
	int displaybufSize = psmfplayer->mediaengine->writeVideoImage(videoData->displaybuf, bufw, videoPixelMode);
	gpu->InvalidateCache(videoData->displaybuf, displaybufSize, GPU_INVALIDATE_SAFE);
	__PsmfUpdatePts(psmfplayer, videoData);

	_PsmfPlayerFillRingbuffer(psmfplayer);

	DEBUG_LOG(ME, "%08x=scePsmfPlayerGetVideoData(%08x, %08x)", 0, psmfPlayer, videoDataAddr);
	return hleDelayResult(0, "psmfPlayer video decode", 3000);
}

int scePsmfPlayerGetAudioData(u32 psmfPlayer, u32 audioDataAddr)
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetAudioData(%08x, %08x): invalid psmf player", psmfPlayer, audioDataAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerGetAudioData(%08x, %08x): not yet playing", psmfPlayer, audioDataAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (!Memory::IsValidAddress(audioDataAddr)) {
		ERROR_LOG(ME, "scePsmfPlayerGetAudioData(%08x, %08x): invalid audio pointer", psmfPlayer, audioDataAddr);
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}

	// Don't return audio frames before we would return video frames.
	if (psmfplayer->warmUp < PSMF_PLAYER_WARMUP_FRAMES) {
		DEBUG_LOG(ME, "scePsmfPlayerGetAudioData(%08x, %08x): warming up", psmfPlayer, audioDataAddr);
		return ERROR_PSMFPLAYER_NO_MORE_DATA;
	}

	if (psmfplayer->playMode == PSMF_PLAYER_MODE_PAUSE) {
		INFO_LOG(HLE, "scePsmfPlayerGetAudioData(%08x): paused mode", psmfPlayer);
		return ERROR_PSMFPLAYER_NO_MORE_DATA;
	}

	int ret = 0;
	if (psmfplayer->mediaengine->getAudioSamples(audioDataAddr) == 0) {
		if (psmfplayer->totalAudioStreams > 0 && (s64)psmfplayer->psmfPlayerAvcAu.pts < (s64)psmfplayer->totalDurationTimestamp - VIDEO_FRAME_DURATION_TS) {
			// Write zeros for any missing trailing frames so it syncs with the video.
			Memory::Memset(audioDataAddr, 0, audioSamplesBytes);
		} else {
			ret = (int)ERROR_PSMFPLAYER_NO_MORE_DATA;
		}
	}
	
	DEBUG_LOG(ME, "%08x=scePsmfPlayerGetAudioData(%08x, %08x)", ret, psmfPlayer, audioDataAddr);
	if (ret != 0) {
		hleEatCycles(10000);
	} else {
		hleEatCycles(30000);
	}
	hleReSchedule("psmfplayer audio decode");
	return ret;
}

int scePsmfPlayerGetCurrentStatus(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		// Mana Khemia and other games call this even when not necessary.
		// It's annoying so the logging is verbose'd out.
		VERBOSE_LOG(ME, "scePsmfPlayerGetCurrentStatus(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status == PSMF_PLAYER_STATUS_NONE) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentStatus(%08x): not initialized", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	DEBUG_LOG(ME, "%d=scePsmfPlayerGetCurrentStatus(%08x)", psmfplayer->status, psmfPlayer);
	return psmfplayer->status;
}

u32 scePsmfPlayerGetCurrentPts(u32 psmfPlayer, u32 currentPtsAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentPts(%08x, %08x): invalid psmf player", psmfPlayer, currentPtsAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_STANDBY) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentPts(%08x, %08x): not initialized", psmfPlayer, currentPtsAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->psmfPlayerAvcAu.pts < 0) {
		WARN_LOG(ME, "scePsmfPlayerGetCurrentPts(%08x, %08x): no frame yet", psmfPlayer, currentPtsAddr);
		return ERROR_PSMFPLAYER_NO_MORE_DATA;
	}

	DEBUG_LOG(ME, "scePsmfPlayerGetCurrentPts(%08x, %08x)", psmfPlayer, currentPtsAddr);
	if (Memory::IsValidAddress(currentPtsAddr)) {
		Memory::Write_U32(psmfplayer->psmfPlayerAvcAu.pts, currentPtsAddr);
	}
	return 0;
}

u32 scePsmfPlayerGetPsmfInfo(u32 psmfPlayer, u32 psmfInfoAddr) 
{
	auto info = PSPPointer<PsmfInfo>::Create(psmfInfoAddr);
	if (!Memory::IsValidAddress(psmfPlayer) || !info.IsValid()) {
		ERROR_LOG(ME, "scePsmfPlayerGetPsmfInfo(%08x, %08x): invalid addresses", psmfPlayer, psmfInfoAddr);
		// PSP would crash.
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetPsmfInfo(%08x, %08x): invalid psmf player", psmfPlayer, psmfInfoAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_STANDBY) {
		ERROR_LOG(ME, "scePsmfPlayerGetPsmfInfo(%08x): psmf not set yet", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	DEBUG_LOG(ME, "scePsmfPlayerGetPsmfInfo(%08x, %08x)", psmfPlayer, psmfInfoAddr);
	// The first frame is at 0, so subtract one frame's duration to get the last frame's timestamp.
	info->lastFrameTS = psmfplayer->totalDurationTimestamp - VIDEO_FRAME_DURATION_TS;
	info->numVideoStreams = psmfplayer->totalVideoStreams;
	info->numAudioStreams = psmfplayer->totalAudioStreams;
	// pcm stream num?
	info->numPCMStreams = 0;
	info->playerVersion = psmfplayer->playerVersion;

	return 0;
}

u32 scePsmfPlayerGetCurrentPlayMode(u32 psmfPlayer, u32 playModeAddr, u32 playSpeedAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentPlayMode(%08x, %08x, %08x): invalid psmf player", psmfPlayer, playModeAddr, playSpeedAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	DEBUG_LOG(ME, "scePsmfPlayerGetCurrentPlayMode(%08x, %08x, %08x)", psmfPlayer, playModeAddr, playSpeedAddr);
	if (Memory::IsValidAddress(playModeAddr)) {
		Memory::Write_U32(psmfplayer->playMode, playModeAddr);
	}
	if (Memory::IsValidAddress(playSpeedAddr)) {
		Memory::Write_U32(psmfplayer->playSpeed, playSpeedAddr);
	}
	return 0;
}

u32 scePsmfPlayerGetCurrentVideoStream(u32 psmfPlayer, u32 videoCodecAddr, u32 videoStreamNumAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentVideoStream(%08x, %08x, %08x): invalid psmf player", psmfPlayer, videoCodecAddr, videoStreamNumAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status == PSMF_PLAYER_STATUS_INIT) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentVideoStream(%08x): psmf not yet set", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	DEBUG_LOG(ME, "scePsmfPlayerGetCurrentVideoStream(%08x, %08x, %08x)", psmfPlayer, videoCodecAddr, videoStreamNumAddr);
	if (Memory::IsValidAddress(videoCodecAddr)) {
		Memory::Write_U32(psmfplayer->videoCodec == 0x0E ? 0 : psmfplayer->videoCodec, videoCodecAddr);
	}
	if (Memory::IsValidAddress(videoStreamNumAddr)) {
		Memory::Write_U32(psmfplayer->videoStreamNum, videoStreamNumAddr);
	}
	return 0;
}

u32 scePsmfPlayerGetCurrentAudioStream(u32 psmfPlayer, u32 audioCodecAddr, u32 audioStreamNumAddr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentAudioStream(%08x, %08x, %08x): invalid psmf player", psmfPlayer, audioCodecAddr, audioStreamNumAddr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status == PSMF_PLAYER_STATUS_INIT) {
		ERROR_LOG(ME, "scePsmfPlayerGetCurrentVideoStream(%08x): psmf not yet set", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	DEBUG_LOG(ME, "scePsmfPlayerGetCurrentAudioStream(%08x, %08x, %08x)", psmfPlayer, audioCodecAddr, audioStreamNumAddr);
	if (Memory::IsValidAddress(audioCodecAddr)) {
		Memory::Write_U32(psmfplayer->audioCodec == 0x0F ? 1 : psmfplayer->audioCodec, audioCodecAddr);
	}
	if (Memory::IsValidAddress(audioStreamNumAddr)) {
		Memory::Write_U32(psmfplayer->audioStreamNum, audioStreamNumAddr);
	}
	return 0;
}

int scePsmfPlayerSetTempBuf(u32 psmfPlayer, u32 tempBufAddr, u32 tempBufSize) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerSetTempBuf(%08x, %08x, %08x): invalid psmf player", psmfPlayer, tempBufAddr, tempBufSize);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status != PSMF_PLAYER_STATUS_INIT) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSetTempBuf(%08x, %08x, %08x): invalid status %x", psmfPlayer, tempBufAddr, tempBufSize, psmfplayer->status);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (tempBufSize < 0x00010000) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSetTempBuf(%08x, %08x, %08x): buffer too small", psmfPlayer, tempBufAddr, tempBufSize);
		return ERROR_PSMFPLAYER_INVALID_PARAM;
	}

	INFO_LOG(ME, "scePsmfPlayerSetTempBuf(%08x, %08x, %08x)", psmfPlayer, tempBufAddr, tempBufSize);
	// fake it right now, use tempbuf from memory directly
	//psmfplayer->tempbuf = tempBufAddr;
	//psmfplayer->tempbufSize = tempBufSize;

	return 0;
}

u32 scePsmfPlayerChangePlayMode(u32 psmfPlayer, int playMode, int playSpeed) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): invalid psmf player", psmfPlayer, playMode, playSpeed);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): not playing yet", psmfPlayer, playMode, playSpeed);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (playMode < 0 || playMode > (int)PSMF_PLAYER_MODE_REWIND) {
		ERROR_LOG(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): invalid mode", psmfPlayer, playMode, playSpeed);
		return ERROR_PSMFPLAYER_INVALID_CONFIG;
	}

	switch (playMode) {
	case PSMF_PLAYER_MODE_FORWARD:
	case PSMF_PLAYER_MODE_REWIND:
		if (psmfplayer->playerVersion == PSMF_PLAYER_VERSION_BASIC) {
			ERROR_LOG_REPORT(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): no EP data for FORWARD/REWIND", psmfPlayer, playMode, playSpeed);
			return ERROR_PSMFPLAYER_INVALID_STREAM;
		}
		psmfplayer->playSpeed = playSpeed;
		WARN_LOG_REPORT(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): unsupported playMode", psmfPlayer, playMode, playSpeed);
		break;

	case PSMF_PLAYER_MODE_PLAY:
	case PSMF_PLAYER_MODE_PAUSE:
		if (psmfplayer->playSpeed != playSpeed) {
			WARN_LOG_REPORT(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): play speed not changed", psmfPlayer, playMode, playSpeed);
		} else {
			DEBUG_LOG(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i)", psmfPlayer, playMode, playSpeed);
		}
		break;

	default:
		if (psmfplayer->playSpeed != playSpeed) {
			WARN_LOG_REPORT(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): play speed not changed", psmfPlayer, playMode, playSpeed);
		}
		WARN_LOG_REPORT(ME, "scePsmfPlayerChangePlayMode(%08x, %i, %i): unsupported playMode", psmfPlayer, playMode, playSpeed);
		break;
	}

	psmfplayer->playMode = playMode;
	return 0;
}

u32 scePsmfPlayerSelectAudio(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerSelectAudio(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status != PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerSelectAudio(%08x): not playing", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	int next = psmfplayer->audioStreamNum + 1;
	if (next >= psmfplayer->totalAudioStreams)
		next = 0;

	if (next == psmfplayer->audioStreamNum || !psmfplayer->mediaengine->setAudioStream(next)) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectAudio(%08x): no stream to switch to", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}

	WARN_LOG_REPORT(ME, "scePsmfPlayerSelectAudio(%08x)", psmfPlayer);
	psmfplayer->audioStreamNum = next;
	return 0;
}

u32 scePsmfPlayerSelectVideo(u32 psmfPlayer) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerSelectVideo(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status != PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerSelectVideo(%08x): not playing", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	int next = psmfplayer->videoStreamNum + 1;
	if (next >= psmfplayer->totalVideoStreams)
		next = 0;

	if (next == psmfplayer->videoStreamNum || !psmfplayer->mediaengine->setVideoStream(next)) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectVideo(%08x): no stream to switch to", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}

	WARN_LOG_REPORT(ME, "scePsmfPlayerSelectVideo(%08x)", psmfPlayer);
	psmfplayer->videoStreamNum = next;
	return 0;
}

u32 scePsmfPlayerSelectSpecificVideo(u32 psmfPlayer, int videoCodec, int videoStreamNum) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i): invalid psmf player", psmfPlayer, videoCodec, videoStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status != PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i): not playing", psmfPlayer, videoCodec, videoStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->totalVideoStreams < 2) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i): unable to change stream", psmfPlayer, videoCodec, videoStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}
	if (videoStreamNum < 0 || videoStreamNum >= psmfplayer->totalVideoStreams) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i): bad stream num param", psmfPlayer, videoCodec, videoStreamNum);
		return ERROR_PSMFPLAYER_INVALID_CONFIG;
	}
	if (videoCodec != 0x0E && videoCodec != 0x00) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i): invalid codec", psmfPlayer, videoCodec, videoStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}
	if (psmfplayer->totalVideoStreams < 2 || !psmfplayer->mediaengine->setVideoStream(videoStreamNum)) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i): unable to change stream", psmfPlayer, videoCodec, videoStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}

	WARN_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificVideo(%08x, %i, %i)", psmfPlayer, videoCodec, videoStreamNum);
	if (psmfplayer->videoStreamNum != videoStreamNum) {
		hleDelayResult(0, "psmf select video", 100);
	}
	psmfplayer->videoCodec = videoCodec;
	psmfplayer->videoStreamNum = videoStreamNum;
	return 0;
}

// WARNING: This function appears to be buggy in most libraries.
u32 scePsmfPlayerSelectSpecificAudio(u32 psmfPlayer, int audioCodec, int audioStreamNum) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i): invalid psmf player", psmfPlayer, audioCodec, audioStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status != PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG(ME, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i): not playing", psmfPlayer, audioCodec, audioStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->totalAudioStreams < 2) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i): unable to change stream", psmfPlayer, audioCodec, audioStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}
	if (audioStreamNum < 0 || audioStreamNum >= psmfplayer->totalAudioStreams) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i): bad stream num param", psmfPlayer, audioCodec, audioStreamNum);
		return ERROR_PSMFPLAYER_INVALID_CONFIG;
	}
	if (audioCodec != 0x0F && audioCodec != 0x01) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i): invalid codec", psmfPlayer, audioCodec, audioStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}
	if (psmfplayer->totalAudioStreams < 2 || !psmfplayer->mediaengine->setAudioStream(audioStreamNum)) {
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i): unable to change stream", psmfPlayer, audioCodec, audioStreamNum);
		return ERROR_PSMFPLAYER_INVALID_STREAM;
	}

	WARN_LOG_REPORT(ME, "scePsmfPlayerSelectSpecificAudio(%08x, %i, %i)", psmfPlayer, audioCodec, audioStreamNum);
	if (psmfplayer->audioStreamNum != audioStreamNum) {
		hleDelayResult(0, "psmf select audio", 100);
	}
	psmfplayer->audioCodec = audioCodec;
	psmfplayer->audioStreamNum = audioStreamNum;
	return 0;
}

u32 scePsmfPlayerConfigPlayer(u32 psmfPlayer, int configMode, int configAttr) 
{
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG(ME, "scePsmfPlayerConfigPlayer(%08x, %i, %i): invalid psmf player", psmfPlayer, configMode, configAttr);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	// This one works in any status as long as it's created.

	switch (configMode) {
	case PSMF_PLAYER_CONFIG_MODE_LOOP:
		if (configAttr != 0 && configAttr != 1) {
			ERROR_LOG_REPORT(ME, "scePsmfPlayerConfigPlayer(%08x, loop, %i): invalid value", psmfPlayer, configAttr);
			return ERROR_PSMFPLAYER_INVALID_PARAM;
		}
		INFO_LOG(ME, "scePsmfPlayerConfigPlayer(%08x, loop, %i)", psmfPlayer, configAttr);
		videoLoopStatus = configAttr;
		break;
	case PSMF_PLAYER_CONFIG_MODE_PIXEL_TYPE:
		if (configAttr < -1 || configAttr > 3) {
			ERROR_LOG_REPORT(ME, "scePsmfPlayerConfigPlayer(%08x, pixelType, %i): invalid value", psmfPlayer, configAttr);
			return ERROR_PSMFPLAYER_INVALID_PARAM;
		}
		INFO_LOG(ME, "scePsmfPlayerConfigPlayer(%08x, pixelType, %i)", psmfPlayer, configAttr);
		// Does -1 mean default or something?
		if (configAttr != -1) {
			videoPixelMode = configAttr;
		} else {
			// TODO: At least for one video, this was the same as 8888.
			videoPixelMode = GE_CMODE_32BIT_ABGR8888;
		}
		break;
	default:
		ERROR_LOG_REPORT(ME, "scePsmfPlayerConfigPlayer(%08x, %i, %i): unknown parameter", psmfPlayer, configMode, configAttr);
		return ERROR_PSMFPLAYER_INVALID_CONFIG;
	}

	return 0;
}

int __PsmfPlayerFinish(u32 psmfPlayer) {
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		ERROR_LOG_REPORT(ME, "__PsmfPlayerFinish(%08x): invalid psmf player", psmfPlayer);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}
	if (psmfplayer->status != PSMF_PLAYER_STATUS_PLAYING) {
		ERROR_LOG_REPORT(ME, "__PsmfPlayerFinish(%08x): unexpected status %d", psmfPlayer, psmfplayer->status);
		return ERROR_PSMFPLAYER_INVALID_STATUS;
	}

	INFO_LOG(ME, "__PsmfPlayerFinish(%08x): video end reached", psmfPlayer);
	psmfplayer->status = PSMF_PLAYER_STATUS_PLAYING_FINISHED;
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
	{0x68d42328, WrapU_UI<scePsmfGetNumberOfSpecificStreams>, "scePsmfGetNumberOfSpecificStreams"},
	{0x5b70fcc1, WrapU_UU<scePsmfQueryStreamOffset>, "scePsmfQueryStreamOffset"},
	{0x9553cc91, WrapU_UU<scePsmfQueryStreamSize>, "scePsmfQueryStreamSize"},
	{0xB78EB9E9, WrapU_UU<scePsmfGetHeaderSize>, "scePsmfGetHeaderSize"},
	{0xA5EBFE81, WrapU_UU<scePsmfGetStreamSize>, "scePsmfGetStreamSize"},
	{0xE1283895, WrapU_U<scePsmfGetPsmfVersion>, "scePsmfGetPsmfVersion"},
	{0x2673646B, WrapU_U<scePsmfVerifyPsmf>, "scePsmfVerifyPsmf"},
	{0x4E624A34, WrapU_UIU<scePsmfGetEPWithId>, "scePsmfGetEPWithId"},
	{0x7C0E7AC3, WrapU_UUU<scePsmfGetEPWithTimestamp>, "scePsmfGetEPWithTimestamp"},
	{0x5F457515, WrapU_UU<scePsmfGetEPidWithTimestamp>, "scePsmfGetEPidWithTimestamp"},
	{0x43ac7dbb, 0, "scePsmfGetPsmfMark"},
	{0xde78e9fc, 0, "scePsmfGetNumberOfPsmfMarks"},
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
	{0x76C0F4AE, WrapI_UCI<scePsmfPlayerSetPsmfOffset>, "scePsmfPlayerSetPsmfOffset"},
	{0xA72DB4F9, WrapI_UCI<scePsmfPlayerSetPsmfOffsetCB>, "scePsmfPlayerSetPsmfOffsetCB"},
	{0x340c12cb, 0, "scePsmfPlayer_340C12CB"},
	// Fake function for PPSSPP's use.
	{0x05b193b7, WrapI_U<__PsmfPlayerFinish>, "__PsmfPlayerFinish"},
};

void Register_scePsmf() {
	RegisterModule("scePsmf",ARRAY_SIZE(scePsmf),scePsmf);
}

void Register_scePsmfPlayer() {
	RegisterModule("scePsmfPlayer",ARRAY_SIZE(scePsmfPlayer),scePsmfPlayer);
}
