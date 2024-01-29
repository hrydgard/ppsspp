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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/scePsmf.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HW/MediaEngine.h"
#include "Core/CoreTiming.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include <map>
#include <algorithm>

// "Go Sudoku" is a good way to test this code...
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

static const int audioSamples = 2048;
static const int audioSamplesBytes = audioSamples * 4;
static int videoPixelMode = GE_CMODE_32BIT_ABGR8888;
static int videoLoopStatus = PSMF_PLAYER_CONFIG_NO_LOOP;
static int psmfPlayerLibVersion = 0;
static u32 psmfPlayerLibcrc = 0;

int eventPsmfPlayerStatusChange = -1;

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
	u32_le bufferSize;
	s32_le threadPriority;
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

// Some of our platforms don't play too nice with direct unaligned access.
static u32 ReadUnalignedU32BE(const u8 *p) {
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
	
	bool isValidCurrentStreamNumber() const {
		return currentStreamNum >= 0 && streamMap.find(currentStreamNum) != streamMap.end();
	}

	bool setStreamNum(u32 psmfStruct, int num, bool updateCached = true);
	bool setStreamWithType(u32 psmfStruct, int type, int channel);
	bool setStreamWithTypeNumber(u32 psmfStruct, int type, int n);

	int FindEPWithTimestamp(int pts) const;

	u32 magic = 0;
	u32 version = 0;
	u32 streamOffset = 0;
	u32 streamSize = 0;
	u32 headerSize = 0;
	u32 headerOffset = 0;
	u32 streamType = 0;
	u32 streamChannel = 0;
	// 0x50
	u32 streamDataTotalSize = 0;
	u32 presentationStartTime = 0;
	u32 presentationEndTime = 0;
	u32 streamDataNextBlockSize = 0;
	u32 streamDataNextInnerBlockSize = 0;

	int numStreams = 0;
	int currentStreamNum = 0;
	int currentStreamType = 0;
	int currentStreamChannel = 0;

	// parameters gotten from streams
	// I guess this is the seek information?
	u32 EPMapOffset = 0;
	u32 EPMapEntriesNum = 0;
	// These shouldn't be here, just here for convenience with old states.
	int videoWidth = 0;
	int videoHeight = 0;
	int audioChannels = 0;
	int audioFrequency = 0;
	std::vector<PsmfEntry> EPMap;

	PsmfStreamMap streamMap;
};

class PsmfPlayer {
public:
	// For savestates only.
	PsmfPlayer() : videoWidth(480), videoHeight(272) {
		mediaengine = new MediaEngine();
	}
	PsmfPlayer(const PsmfPlayerCreateData *data);
	~PsmfPlayer() {
		AbortFinish();
		if (mediaengine) 
			delete mediaengine;
		pspFileSystem.CloseFile(filehandle);
	}
	void DoState(PointerWrap &p);

	void ScheduleFinish(u32 handle) {
		if (!finishThread) {
			finishThread = new HLEHelperThread("scePsmfPlayer", "scePsmfPlayer", "__PsmfPlayerFinish", playbackThreadPriority, 0x200);
			finishThread->Start(handle, 0);
		}
	}

	void AbortFinish() {
		if (finishThread) {
			delete finishThread;
			finishThread = nullptr;
		}
	}

	bool HasReachedEnd() {
		// The pts are ignored - the end is when we're out of data.
		return mediaengine->IsVideoEnd() && (mediaengine->IsNoAudioData() || !mediaengine->IsActuallyPlayingAudio());
	}

	int filehandle = 0;
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
	int totalVideoStreams;
	int totalAudioStreams;
	int playerVersion;
	int videoStep;
	int warmUp;
	s64 seekDestTimeStamp;

	int videoWidth;
	int videoHeight;

	SceMpegAu psmfPlayerAtracAu;
	SceMpegAu psmfPlayerAvcAu;
	PsmfPlayerStatus status;

	MediaEngine *mediaengine;
	HLEHelperThread *finishThread = nullptr;
};

class PsmfStream {
public:
	enum {
		USE_PSMF = -2,
		INVALID = -1,
	};

	// Used for save states.
	PsmfStream() : videoWidth_(USE_PSMF), videoHeight_(USE_PSMF), audioChannels_(USE_PSMF), audioFrequency_(USE_PSMF) {
	}

	PsmfStream(int type, int channel) : videoWidth_(INVALID), videoHeight_(INVALID), audioChannels_(INVALID), audioFrequency_(INVALID) {
		type_ = type;
		channel_ = channel;
	}

	void readMPEGVideoStreamParams(const u8 *addr, const u8 *data, Psmf *psmf) {
		int streamId = addr[0];
		int privateStreamId = addr[1];
		// two unknowns here
		psmf->EPMapOffset = ReadUnalignedU32BE(&addr[4]);
		psmf->EPMapEntriesNum = ReadUnalignedU32BE(&addr[8]);
		videoWidth_ = addr[12] * 16;
		videoHeight_ = addr[13] * 16;

		const u32 EP_MAP_STRIDE = 1 + 1 + 4 + 4;
		if (psmf->headerOffset != 0 && !Memory::IsValidRange(psmf->headerOffset, psmf->EPMapOffset + EP_MAP_STRIDE * psmf->EPMapEntriesNum)) {
			ERROR_LOG(ME, "Invalid PSMF EP map entry count: %d", psmf->EPMapEntriesNum);
			psmf->EPMapEntriesNum = Memory::ValidSize(psmf->headerOffset + psmf->EPMapOffset, EP_MAP_STRIDE * psmf->EPMapEntriesNum) / EP_MAP_STRIDE;
		}

		psmf->EPMap.clear();
		for (u32 i = 0; i < psmf->EPMapEntriesNum; i++) {
			// TODO: Should look into validating these offsets. Got a crash report here.
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
		audioChannels_ = addr[14];
		// Note: "frequency" is usually 2.  But that's what scePsmfGetAudioInfo() writes too.
		audioFrequency_ = addr[15];
		// two unknowns here
		INFO_LOG(ME, "PSMF private audio found: id=%02x, privid=%02x, channels=%i, freq=%i", streamId, privateStreamId, psmf->audioChannels, psmf->audioFrequency);
	}

	bool matchesType(int ty) {
		if (ty == PSMF_AUDIO_STREAM) {
			return type_ == PSMF_ATRAC_STREAM || type_ == PSMF_PCM_STREAM;
		}
		return type_ == ty;
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("PsmfStream", 1, 2);
		if (!s)
			return;

		Do(p, type_);
		Do(p, channel_);
		if (s >= 2) {
			Do(p, videoWidth_);
			Do(p, videoHeight_);
			Do(p, audioChannels_);
			Do(p, audioFrequency_);
		}
	}

	int type_;
	int channel_;
	int videoWidth_;
	int videoHeight_;
	int audioChannels_;
	int audioFrequency_;
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
	currentStreamType = -1;
	currentStreamChannel = -1;

	if (data != 0 && !Memory::IsValidRange(data, 0x82 + numStreams * 16)) {
		ERROR_LOG(ME, "Invalid PSMF stream count: %d", numStreams);
		numStreams = Memory::ValidSize(data + 0x82, numStreams * 16) / 16;
	}

	for (int i = 0; i < numStreams; i++) {
		PsmfStream *stream = 0;
		const u8 *const currentStreamAddr = ptr + 0x82 + i * 16;
		int streamId = currentStreamAddr[0];
		if ((streamId & PSMF_VIDEO_STREAM_ID) == PSMF_VIDEO_STREAM_ID) {
			stream = new PsmfStream(PSMF_AVC_STREAM, streamId & 0x0F);
			stream->readMPEGVideoStreamParams(currentStreamAddr, ptr, this);
		} else if ((streamId & PSMF_AUDIO_STREAM_ID) == PSMF_AUDIO_STREAM_ID) {
			int type = PSMF_ATRAC_STREAM;
			int privateStreamId = currentStreamAddr[1];
			if ((privateStreamId & 0xF0) != 0) {
				WARN_LOG_REPORT(ME, "Unknown private stream type, assuming PCM: %02x", privateStreamId);
				type = PSMF_PCM_STREAM;
			}
			stream = new PsmfStream(type, privateStreamId & 0x0F);
			stream->readPrivateAudioStreamParams(currentStreamAddr, this);
		}
		if (stream) {
			currentStreamNum++;
			streamMap[currentStreamNum] = stream;
		}
	}

	// Default to the first stream.
	currentStreamNum = 0;
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
	mediaengine = new MediaEngine();
	finishThread = nullptr;
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
	auto s = p.Section("Psmf", 1, 3);
	if (!s)
		return;

	Do(p, magic);
	Do(p, version);
	Do(p, streamOffset);
	Do(p, streamSize);
	Do(p, headerOffset);
	Do(p, streamDataTotalSize);
	Do(p, presentationStartTime);
	Do(p, presentationEndTime);
	Do(p, streamDataNextBlockSize);
	Do(p, streamDataNextInnerBlockSize);
	Do(p, numStreams);

	Do(p, currentStreamNum);
	int legacyStreamNums = 0;
	Do(p, legacyStreamNums);
	Do(p, legacyStreamNums);

	Do(p, EPMapOffset);
	Do(p, EPMapEntriesNum);
	Do(p, videoWidth);
	Do(p, videoHeight);
	Do(p, audioChannels);
	Do(p, audioFrequency);

	if (s >= 2) {
		Do(p, EPMap);
	}

	Do(p, streamMap);
	if (s >= 3) {
		Do(p, currentStreamType);
		Do(p, currentStreamChannel);
	} else {
		currentStreamType = -1;
		currentStreamChannel = -1;
		auto streamInfo = streamMap.find(currentStreamNum);
		if (streamInfo != streamMap.end()) {
			currentStreamType = streamInfo->second->type_;
			currentStreamChannel = streamInfo->second->channel_;
		}
	}
}

void PsmfPlayer::DoState(PointerWrap &p) {
	auto s = p.Section("PsmfPlayer", 1, 8);
	if (!s)
		return;

	Do(p, videoCodec);
	Do(p, videoStreamNum);
	Do(p, audioCodec);
	Do(p, audioStreamNum);
	Do(p, playMode);
	Do(p, playSpeed);

	Do(p, displayBuffer);
	Do(p, displayBufferSize);
	Do(p, playbackThreadPriority);
	int oldMaxAheadTimestamp = 0;
	Do(p, oldMaxAheadTimestamp);
	if (s >= 4) {
		Do(p, totalDurationTimestamp);
	} else {
		long oldTimestamp;
		Do(p, oldTimestamp);
		totalDurationTimestamp = oldTimestamp;
	}
	if (s >= 2) {
		Do(p, totalVideoStreams);
		Do(p, totalAudioStreams);
		Do(p, playerVersion);
	} else {
		totalVideoStreams = 1;
		totalAudioStreams = 1;
		playerVersion = PSMF_PLAYER_VERSION_FULL;
	}
	if (s >= 3) {
		Do(p, videoStep);
	} else {
		videoStep = 0;
	}
	if (s >= 4) {
		Do(p, warmUp);
	} else {
		warmUp = 10000;
	}
	if (s >= 5) {
		Do(p, seekDestTimeStamp);
	} else {
		seekDestTimeStamp = 0;
	}
	DoClass(p, mediaengine);
	Do(p, filehandle);
	Do(p, fileoffset);
	Do(p, readSize);
	Do(p, streamSize);

	Do(p, status);
	if (s >= 4) {
		Do(p, psmfPlayerAtracAu);
	}
	Do(p, psmfPlayerAvcAu);
	if (s >= 7) {
		bool hasFinishThread = finishThread != nullptr;
		Do(p, hasFinishThread);
		if (hasFinishThread) {
			Do(p, finishThread);
		} else {
			if (finishThread)
				finishThread->Forget();
			delete finishThread;
			finishThread = nullptr;
		}
	} else if (s >= 6) {
		Do(p, finishThread);
	} else {
		if (finishThread)
			finishThread->Forget();
		delete finishThread;
		finishThread = nullptr;
	}

	if (s >= 8) {
		Do(p, videoWidth);
		Do(p, videoHeight);
	}
}

bool Psmf::setStreamNum(u32 psmfStruct, int num, bool updateCached) {
	auto data = PSPPointer<PsmfData>::Create(psmfStruct);
	currentStreamNum = num;
	data->streamNum = num;

	// One of the functions can set this to invalid without invalidating these values.
	if (updateCached) {
		currentStreamType = -1;
		currentStreamChannel = -1;
	}
	if (!isValidCurrentStreamNumber())
		return false;
	PsmfStreamMap::iterator iter = streamMap.find(currentStreamNum);
	if (iter == streamMap.end())
		return false;

	// This information seems to only be for the scePsmf lookups.
	currentStreamType = iter->second->type_;
	currentStreamChannel = iter->second->channel_;
	return true;
}

bool Psmf::setStreamWithType(u32 psmfStruct, int type, int channel) {
	for (auto iter : streamMap) {
		// Note: this does NOT support PSMF_AUDIO_STREAM.
		if (iter.second->type_ == type && iter.second->channel_ == channel) {
			return setStreamNum(psmfStruct, iter.first);
		}
	}
	return false;
}

bool Psmf::setStreamWithTypeNumber(u32 psmfStruct, int type, int n) {
	for (auto iter : streamMap) {
		if (iter.second->matchesType(type)) {
			if (n != 0) {
				// Keep counting...
				n--;
				continue;
			}
			// Okay, this is the one.
			return setStreamNum(psmfStruct, iter.first);
		}
	}
	return false;
}

int Psmf::FindEPWithTimestamp(int pts) const {
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

static Psmf *getPsmf(u32 psmf) {
	auto psmfstruct = PSPPointer<PsmfData>::Create(psmf);
	if (!psmfstruct.IsValid())
		return nullptr;

	auto iter = psmfMap.find(psmfstruct->headerOffset);
	if (iter != psmfMap.end()) {
		// TODO: Migrate to only using PSP RAM.
		// Each instance can have its own selected stream.  This is important.
		iter->second->currentStreamNum = psmfstruct->streamNum;
		return iter->second;
	} else {
		return nullptr;
	}
}

static PsmfPlayer *getPsmfPlayer(u32 psmfplayer)
{
	auto iter = psmfPlayerMap.find(Memory::Read_U32(psmfplayer));
	if (iter != psmfPlayerMap.end())
		return iter->second;
	else
		return 0;
}

static void __PsmfPlayerStatusChange(u64 userdata, int cyclesLate) {
	PsmfPlayerStatus status = PsmfPlayerStatus(userdata & 0xFFFFFFFF);
	u32 psmfPlayer = userdata >> 32;
	PsmfPlayer *player = getPsmfPlayer(psmfPlayer);
	if (player) {
		player->status = status;
	}
}

void __PsmfInit() {
	videoPixelMode = GE_CMODE_32BIT_ABGR8888;
	videoLoopStatus = PSMF_PLAYER_CONFIG_NO_LOOP;
	psmfPlayerLibVersion = 0;
	eventPsmfPlayerStatusChange = CoreTiming::RegisterEvent("PsmfPlayerStatusChange", &__PsmfPlayerStatusChange);
}

void __PsmfPlayerLoadModule(int devkitVersion, u32 crc) {
	psmfPlayerLibVersion = devkitVersion;
	psmfPlayerLibcrc = crc;
}

void __PsmfDoState(PointerWrap &p) {
	auto s = p.Section("scePsmf", 1);
	if (!s)
		return;

	Do(p, psmfMap);
}

void __PsmfPlayerDoState(PointerWrap &p) {
	auto s = p.Section("scePsmfPlayer", 1, 4);
	if (!s)
		return;

	Do(p, psmfPlayerMap);
	Do(p, videoPixelMode);
	Do(p, videoLoopStatus);
	if (s < 3) {
		eventPsmfPlayerStatusChange = -1;
	} else {
		Do(p, eventPsmfPlayerStatusChange);
	}
	CoreTiming::RestoreRegisterEvent(eventPsmfPlayerStatusChange, "PsmfPlayerStatusChangeEvent", &__PsmfPlayerStatusChange);
	if (s < 4) {
		psmfPlayerLibcrc = 0;
	} else {
		Do(p, psmfPlayerLibcrc);
	}
	if (s < 2) {
		// Assume the latest, which is what we were emulating before.
		psmfPlayerLibVersion = 0x06060010;
	} else {
		Do(p, psmfPlayerLibVersion);
	}
}

void __PsmfShutdown() {
	for (auto it = psmfMap.begin(), end = psmfMap.end(); it != end; ++it)
		delete it->second;
	for (auto it = psmfPlayerMap.begin(), end = psmfPlayerMap.end(); it != end; ++it) {
		// Don't bother freeing, may already be freed.
		if (it->second->finishThread)
			it->second->finishThread->Forget();
		delete it->second;
	}
	psmfMap.clear();
	psmfPlayerMap.clear();
}

static void DelayPsmfStateChange(u32 psmfPlayer, u32 newState, s64 delayUs) {
	CoreTiming::ScheduleEvent(usToCycles(delayUs), eventPsmfPlayerStatusChange, (u64)psmfPlayer << 32 | newState);
}

static u32 scePsmfSetPsmf(u32 psmfStruct, u32 psmfData) {
	if (!Memory::IsValidAddress(psmfStruct) || !Memory::IsValidAddress(psmfData)) {
		// Crashes on a PSP.
		return hleReportError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "bad address");
	}

	Psmf *psmf = new Psmf(Memory::GetPointer(psmfData), psmfData);
	if (psmf->magic != PSMF_MAGIC) {
		delete psmf;
		return hleLogError(ME, ERROR_PSMF_INVALID_PSMF, "invalid psmf data");
	}
	// Note: devkit 00000000 supports only '0012'(0F), '0013'(1F), and '0014'(2F).  03000310+ supports '0015'(3F.)
	if (psmf->version == 0) {
		delete psmf;
		return hleLogError(ME, ERROR_PSMF_BAD_VERSION, "invalid psmf version");
	}
	if (psmf->streamOffset == 0) {
		delete psmf;
		return hleLogError(ME, ERROR_PSMF_INVALID_VALUE, "invalid psmf version");
	}

	// Note: this structure changes between versions.
	// TODO: These values are not right, but games probably don't read them.
	auto data = PSPPointer<PsmfData>::Create(psmfStruct);
	memset((PsmfData *)data, 0, sizeof(PsmfData));
	data->version = psmf->version;
	data->headerSize = 0x800;
	data->streamSize = psmf->streamSize;
	// This should be and needs to be the current stream.
	data->streamNum = psmf->currentStreamNum;
	data->headerOffset = psmf->headerOffset;
	data.NotifyWrite("PsmfSetPsmf");

	// Because the Psmf struct is sometimes copied, we use a value inside as an id.
	auto iter = psmfMap.find(data->headerOffset);
	if (iter != psmfMap.end())
		delete iter->second;
	psmfMap[data->headerOffset] = psmf;

	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfGetNumberOfStreams(u32 psmfStruct) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}
	return hleLogSuccessI(ME, psmf->numStreams);
}

static u32 scePsmfGetNumberOfSpecificStreams(u32 psmfStruct, int streamType) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}

	int streamNum = 0;
	for (auto it : psmf->streamMap) {
		if (it.second->matchesType(streamType)) {
			streamNum++;
		}
	}

	return hleLogSuccessI(ME, streamNum);
}

static u32 scePsmfSpecifyStreamWithStreamType(u32 psmfStruct, u32 streamType, u32 channel) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}
	if (!psmf->setStreamWithType(psmfStruct, streamType, channel)) {
		// An invalid type seems to make the stream number invalid, but retain the old type/channel.
		psmf->setStreamNum(psmfStruct, ERROR_PSMF_INVALID_ID, false);
		// Also, returns 0 even when no stream found.
		return hleLogWarning(ME, 0, "no stream found");
	}
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfSpecifyStreamWithStreamTypeNumber(u32 psmfStruct, u32 streamType, u32 typeNum) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}
	if (!psmf->setStreamWithTypeNumber(psmfStruct, streamType, typeNum)) {
		// Don't update stream, just bail out.
		return hleLogWarning(ME, ERROR_PSMF_INVALID_ID, "no stream found");
	}
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfSpecifyStream(u32 psmfStruct, int streamNum) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}
	if (!psmf->setStreamNum(psmfStruct, streamNum)) {
		psmf->setStreamNum(psmfStruct, ERROR_PSMF_NOT_INITIALIZED);
		return hleLogWarning(ME, ERROR_PSMF_INVALID_ID, "bad stream id");
	}
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfGetVideoInfo(u32 psmfStruct, u32 videoInfoAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	} else if (!psmf->isValidCurrentStreamNumber()) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid stream selected");
	} else if (!Memory::IsValidRange(videoInfoAddr, 8)) {
		// Would crash.
		return hleLogError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "bad address");
	}

	auto info = psmf->streamMap[psmf->currentStreamNum];
	if (info->videoWidth_ == PsmfStream::INVALID) {
		return hleLogError(ME, ERROR_PSMF_INVALID_ID, "not a video stream");
	}
	Memory::Write_U32(info->videoWidth_ == PsmfStream::USE_PSMF ? psmf->videoWidth : info->videoWidth_, videoInfoAddr);
	Memory::Write_U32(info->videoHeight_ == PsmfStream::USE_PSMF ? psmf->videoHeight : info->videoHeight_, videoInfoAddr + 4);
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfGetAudioInfo(u32 psmfStruct, u32 audioInfoAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	} else if (!psmf->isValidCurrentStreamNumber()) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid stream selected");
	} else if (!Memory::IsValidRange(audioInfoAddr, 8)) {
		// Would crash.
		return hleLogError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "bad address");
	}

	auto info = psmf->streamMap[psmf->currentStreamNum];
	if (info->audioChannels_ == PsmfStream::INVALID) {
		return hleLogError(ME, ERROR_PSMF_INVALID_ID, "not an audio stream");
	}
	Memory::Write_U32(info->audioChannels_ == PsmfStream::USE_PSMF ? psmf->audioChannels : info->audioChannels_, audioInfoAddr);
	Memory::Write_U32(info->audioFrequency_ == PsmfStream::USE_PSMF ? psmf->audioFrequency : info->audioFrequency_, audioInfoAddr + 4);
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfGetCurrentStreamType(u32 psmfStruct, u32 typeAddr, u32 channelAddr) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}
	if (psmf->currentStreamNum == (int)ERROR_PSMF_NOT_INITIALIZED) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "no stream set");
	}
	if (!Memory::IsValidAddress(typeAddr) || !Memory::IsValidAddress(channelAddr)) {
		return hleLogError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "bad pointers");
	}
	if (psmf->currentStreamType != -1) {
		Memory::Write_U32(psmf->currentStreamType, typeAddr);
		Memory::Write_U32(psmf->currentStreamChannel, channelAddr);
	}
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfGetStreamSize(u32 psmfStruct, u32 sizeAddr)
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

static u32 scePsmfQueryStreamOffset(u32 bufferAddr, u32 offsetAddr)
{
	WARN_LOG(ME, "scePsmfQueryStreamOffset(%08x, %08x)", bufferAddr, offsetAddr);
	if (Memory::IsValidAddress(offsetAddr)) {
		Memory::Write_U32(bswap32(Memory::Read_U32(bufferAddr + PSMF_STREAM_OFFSET_OFFSET)), offsetAddr);
	}
	return 0;
}

static u32 scePsmfQueryStreamSize(u32 bufferAddr, u32 sizeAddr)
{
	WARN_LOG(ME, "scePsmfQueryStreamSize(%08x, %08x)", bufferAddr, sizeAddr);
	if (Memory::IsValidAddress(sizeAddr)) {
		Memory::Write_U32(bswap32(Memory::Read_U32(bufferAddr + PSMF_STREAM_SIZE_OFFSET)), sizeAddr);
	}
	return 0;
}

static u32 scePsmfGetHeaderSize(u32 psmfStruct, u32 sizeAddr)
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

static u32 scePsmfGetPsmfVersion(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetPsmfVersion(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetPsmfVersion(%08x)", psmfStruct);
	return psmf->version;
}

static u32 scePsmfVerifyPsmf(u32 psmfAddr)
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
	Memory::Memset(currentMIPS->r[MIPS_REG_SP] - 0x20, 0, 0x20, "PsmfStack");
	DEBUG_LOG(ME, "scePsmfVerifyPsmf(%08x)", psmfAddr);
	return 0;
}

static u32 scePsmfGetNumberOfEPentries(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfGetNumberOfEPentries(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}
	DEBUG_LOG(ME, "scePsmfGetNumberOfEPentries(%08x)", psmfStruct);
	return psmf->EPMapEntriesNum;
}

static u32 scePsmfGetPresentationStartTime(u32 psmfStruct, u32 startTimeAddr)
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

static u32 scePsmfGetPresentationEndTime(u32 psmfStruct, u32 endTimeAddr)
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

static u32 scePsmfGetCurrentStreamNumber(u32 psmfStruct) {
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}
	if (psmf->currentStreamNum < 0) {
		return hleLogError(ME, psmf->currentStreamNum, "invalid stream");
	}
	return hleLogSuccessI(ME, psmf->currentStreamNum);
}

static u32 scePsmfCheckEPMap(u32 psmfStruct)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		ERROR_LOG(ME, "scePsmfCheckEPMap(%08x): invalid psmf", psmfStruct);
		return ERROR_PSMF_NOT_FOUND;
	}

	DEBUG_LOG(ME, "scePsmfCheckEPMap(%08x)", psmfStruct);
	return psmf->EPMap.empty() ? ERROR_PSMF_NOT_FOUND : 0;
}

static u32 scePsmfGetEPWithId(u32 psmfStruct, int epid, u32 entryAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}

	if (epid < 0 || epid >= (int)psmf->EPMap.size()) {
		return hleLogError(ME, ERROR_PSMF_NOT_FOUND, "invalid id");
	}

	auto entry = PSPPointer<PsmfEntry>::Create(entryAddr);
	if (entry.IsValid()) {
		*entry = psmf->EPMap[epid];
		entry.NotifyWrite("PsmfGetEPWithId");
	}
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfGetEPWithTimestamp(u32 psmfStruct, u32 ts, u32 entryAddr)
{
	Psmf *psmf = getPsmf(psmfStruct);
	if (!psmf) {
		return hleLogError(ME, ERROR_PSMF_NOT_INITIALIZED, "invalid psmf");
	}

	if (ts < psmf->presentationStartTime) {
		return hleLogError(ME, ERROR_PSMF_NOT_FOUND, "invalid timestamp");
	}

	int epid = psmf->FindEPWithTimestamp(ts);
	if (epid < 0 || epid >= (int)psmf->EPMap.size()) {
		return hleLogError(ME, ERROR_PSMF_NOT_FOUND, "invalid id");
	}

	auto entry = PSPPointer<PsmfEntry>::Create(entryAddr);
	if (entry.IsValid()) {
		*entry = psmf->EPMap[epid];
		entry.NotifyWrite("PsmfGetEPWithTimestamp");
	}
	return hleLogSuccessI(ME, 0);
}

static u32 scePsmfGetEPidWithTimestamp(u32 psmfStruct, u32 ts)
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

static int scePsmfPlayerCreate(u32 psmfPlayer, u32 dataPtr) {
	auto player = PSPPointer<u32_le>::Create(psmfPlayer);
	const auto data = PSPPointer<const PsmfPlayerCreateData>::Create(dataPtr);

	if (!player.IsValid() || !data.IsValid()) {
		// Crashes on a PSP.
		return hleReportError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "bad pointers");
	}
	if (!data->buffer.IsValid()) {
		// Also crashes on a PSP.
		*player = 0;
		return hleReportError(ME, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "invalid buffer address %08x", data->buffer.ptr);
	}
	if (data->bufferSize < 0x00285800) {
		*player = 0;
		return hleReportError(ME, ERROR_PSMFPLAYER_BUFFER_SIZE, "buffer too small %08x", data->bufferSize);
	}
	if (data->threadPriority < 0x10 || data->threadPriority >= 0x6E) {
		*player = 0;
		return hleReportError(ME, ERROR_PSMFPLAYER_INVALID_PARAM, "bad thread priority %02x", data->threadPriority);
	}
	if (!psmfPlayerMap.empty()) {
		*player = 0;
		return hleReportError(ME, ERROR_MPEG_ALREADY_INIT, "already have an active player");
	}

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

	int delayUs = 20000;
	DelayPsmfStateChange(psmfPlayer, PSMF_PLAYER_STATUS_INIT, delayUs);
	INFO_LOG(ME, "psmfplayer create, psmfPlayerLibVersion 0x%0x, psmfPlayerLibcrc %x", psmfPlayerLibVersion, psmfPlayerLibcrc);
	return hleDelayResult(0, "player create", delayUs);	
}

static int scePsmfPlayerStop(u32 psmfPlayer) {
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		return hleLogError(ME, ERROR_PSMFPLAYER_INVALID_STATUS, "invalid psmf player");
	}
	if (psmfplayer->status < PSMF_PLAYER_STATUS_PLAYING) {
		return hleLogError(ME, ERROR_PSMFPLAYER_INVALID_STATUS, "not yet playing");
	}
	psmfplayer->AbortFinish();

	int delayUs = 3000;
	DelayPsmfStateChange(psmfPlayer, PSMF_PLAYER_STATUS_STANDBY, delayUs);
	return hleLogSuccessInfoI(ME, hleDelayResult(0, "psmfplayer stop", delayUs));
}

static int scePsmfPlayerBreak(u32 psmfPlayer) {
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		return hleLogError(ME, ERROR_PSMFPLAYER_INVALID_STATUS, "invalid psmf player", psmfPlayer);
	}

	psmfplayer->AbortFinish();

	return hleLogWarning(ME, 0);
}

static int _PsmfPlayerFillRingbuffer(PsmfPlayer *psmfplayer) {
	if (psmfplayer->filehandle <= 0)
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
		// Start looping, but only if we've finished.
		if (psmfplayer->HasReachedEnd()) {
			psmfplayer->readSize = 0;
			pspFileSystem.SeekFile(psmfplayer->filehandle, psmfplayer->fileoffset, FILEMOVE_BEGIN);
			psmfplayer->mediaengine->reloadStream();
		}
	}
	return 0;
}

static int _PsmfPlayerSetPsmfOffset(u32 psmfPlayer, const char *filename, int offset, bool docallback) {
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer || psmfplayer->status != PSMF_PLAYER_STATUS_INIT) {
		return hleReportError(ME, ERROR_PSMFPLAYER_INVALID_STATUS, "invalid psmf player or status");
	}
	if (!filename) {
		return hleLogError(ME, ERROR_PSMFPLAYER_INVALID_PARAM, "invalid filename");
	}

	int delayUs = 1100;

	psmfplayer->filehandle = pspFileSystem.OpenFile(filename, (FileAccess) FILEACCESS_READ);
	if (psmfplayer->filehandle < 0) {
		return hleLogError(ME, hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "psmfplayer set", delayUs), "invalid file data or does not exist");
	}

	if (offset != 0)
		pspFileSystem.SeekFile(psmfplayer->filehandle, offset, FILEMOVE_BEGIN);
	u8 *buf = psmfplayer->tempbuf;
	int tempbufSize = (int)sizeof(psmfplayer->tempbuf);
	int size = (int)pspFileSystem.ReadFile(psmfplayer->filehandle, buf, 2048);
	delayUs += 2000;

	const u32 magic = *(u32_le *)buf;
	if (magic != PSMF_MAGIC) {
		// TODO: Let's keep trying as we were before.
		ERROR_LOG_REPORT(ME, "scePsmfPlayerSetPsmf*: incorrect PSMF magic (%08x), bad data", magic);
		//return hleReportError(ME, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "incorrect PSMF magic (%08x), bad data", magic);
	}

	// TODO: Merge better with Psmf.
	u16 numStreams = *(u16_be *)(buf + 0x80);
	if (numStreams > 128) {
		return hleReportError(ME, hleDelayResult(SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "psmfplayer set", delayUs), "too many streams in PSMF video, bogus data");
	}

	psmfplayer->totalVideoStreams = 0;
	psmfplayer->totalAudioStreams = 0;
	psmfplayer->videoWidth = buf[142] * 16;
	psmfplayer->videoHeight = buf[143] * 16;

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
	if (psmfPlayerLibVersion >= 0x05050010) {
		psmfplayer->streamSize = *(s32_be *)(buf + PSMF_STREAM_SIZE_OFFSET);
	} else {
		// Older versions just read until the end of the file.
		PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
		psmfplayer->streamSize = info.size - offset - mpegoffset;
	}
	psmfplayer->fileoffset = offset + mpegoffset;
	psmfplayer->mediaengine->loadStream(buf, 2048, std::max(2048 * 500, tempbufSize));
	_PsmfPlayerFillRingbuffer(psmfplayer);
	psmfplayer->totalDurationTimestamp = psmfplayer->mediaengine->getLastTimeStamp();

	DelayPsmfStateChange(psmfPlayer, PSMF_PLAYER_STATUS_STANDBY, delayUs);
	return hleLogSuccessInfoI(ME, hleDelayResult(0, "psmfplayer set", delayUs));
}

static int scePsmfPlayerSetPsmf(u32 psmfPlayer, const char *filename) {
	return _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, 0, false);
}

static int scePsmfPlayerSetPsmfCB(u32 psmfPlayer, const char *filename) {
	// TODO: hleCheckCurrentCallbacks?
	return _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, 0, true);
}

static int scePsmfPlayerSetPsmfOffset(u32 psmfPlayer, const char *filename, int offset) {
	return _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, offset, false);
}

static int scePsmfPlayerSetPsmfOffsetCB(u32 psmfPlayer, const char *filename, int offset) {
	// TODO: hleCheckCurrentCallbacks?
	return _PsmfPlayerSetPsmfOffset(psmfPlayer, filename, offset, true);
}

static int scePsmfPlayerGetAudioOutSize(u32 psmfPlayer) {
	PsmfPlayer *psmfplayer = getPsmfPlayer(psmfPlayer);
	if (!psmfplayer) {
		return hleLogError(ME, ERROR_PSMFPLAYER_INVALID_STATUS, "invalid psmf player");
	}
	return hleLogWarning(ME, audioSamplesBytes);
}

static bool __PsmfPlayerContinueSeek(PsmfPlayer *psmfplayer, int tries = 50) {
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

static int scePsmfPlayerStart(u32 psmfPlayer, u32 psmfPlayerData, int initPts)
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

	WARN_LOG(ME, "scePsmfPlayerStart(%08x, %08x, %d (mode %d, speed %d)", psmfPlayer, psmfPlayerData, initPts, playerData->playMode, playerData->playSpeed);

	// Does not alter current pts, it just catches up when Update()/etc. get there.

	int delayUs = psmfplayer->status == PSMF_PLAYER_STATUS_PLAYING ? 3000 : 0;
	if (delayUs == 0)
		psmfplayer->status = PSMF_PLAYER_STATUS_PLAYING;
	else
		DelayPsmfStateChange(psmfPlayer, PSMF_PLAYER_STATUS_PLAYING, delayUs);
	psmfplayer->warmUp = 0;

	psmfplayer->mediaengine->openContext();

	s64 dist = initPts - psmfplayer->mediaengine->getVideoTimeStamp();
	if (dist < 0 || dist > static_cast<long long>(VIDEO_FRAME_DURATION_TS) * 60) {
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

static int scePsmfPlayerDelete(u32 psmfPlayer)
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

static int scePsmfPlayerUpdate(u32 psmfPlayer)
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
	if (psmfplayer->HasReachedEnd()) {
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

static int scePsmfPlayerReleasePsmf(u32 psmfPlayer)
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

static void __PsmfUpdatePts(PsmfPlayer *psmfplayer, PsmfVideoData *videoData) {
	// getVideoTimestamp() includes the frame duration, remove it for this frame's pts.
	psmfplayer->psmfPlayerAvcAu.pts = psmfplayer->mediaengine->getVideoTimeStamp() - VIDEO_FRAME_DURATION_TS;
	if (videoData) {
		videoData->displaypts = (u32)psmfplayer->psmfPlayerAvcAu.pts;
	}
}

static int scePsmfPlayerGetVideoData(u32 psmfPlayer, u32 videoDataAddr)
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
	if (!videoData.IsValid()) {
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

	// It's fine to pass an invalid value here if it's still warming up, but after that it's not okay.
	if (!Memory::IsValidAddress(videoData->displaybuf)) {
		ERROR_LOG(ME, "scePsmfPlayerGetVideoData(%08x, %08x): invalid buffer pointer %08x", psmfPlayer, videoDataAddr, videoData->displaybuf);
		return SCE_KERNEL_ERROR_INVALID_POINTER;
	}

	bool doVideoStep = true;
	if (psmfplayer->playMode == PSMF_PLAYER_MODE_PAUSE) {
		doVideoStep = false;
	} else if (!psmfplayer->mediaengine->IsNoAudioData() && psmfplayer->mediaengine->IsActuallyPlayingAudio()) {
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
	gpu->PerformWriteFormattedFromMemory(videoData->displaybuf, displaybufSize, bufw, (GEBufferFormat)videoPixelMode);
	__PsmfUpdatePts(psmfplayer, videoData);

	_PsmfPlayerFillRingbuffer(psmfplayer);

	DEBUG_LOG(ME, "%08x=scePsmfPlayerGetVideoData(%08x, %08x)", 0, psmfPlayer, videoDataAddr);
	return hleDelayResult(0, "psmfPlayer video decode", 3000);
}

static int scePsmfPlayerGetAudioData(u32 psmfPlayer, u32 audioDataAddr)
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
			Memory::Memset(audioDataAddr, 0, audioSamplesBytes, "PsmfAudioClear");
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

static int scePsmfPlayerGetCurrentStatus(u32 psmfPlayer)
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

static u32 scePsmfPlayerGetCurrentPts(u32 psmfPlayer, u32 currentPtsAddr)
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
		VERBOSE_LOG(ME, "scePsmfPlayerGetCurrentPts(%08x, %08x): no frame yet", psmfPlayer, currentPtsAddr);
		return ERROR_PSMFPLAYER_NO_MORE_DATA;
	}

	DEBUG_LOG(ME, "scePsmfPlayerGetCurrentPts(%08x, %08x)", psmfPlayer, currentPtsAddr);
	if (Memory::IsValidAddress(currentPtsAddr)) {
		Memory::Write_U32(psmfplayer->psmfPlayerAvcAu.pts, currentPtsAddr);
	}
	return 0;
}

static u32 scePsmfPlayerGetPsmfInfo(u32 psmfPlayer, u32 psmfInfoAddr, u32 widthAddr, u32 heightAddr) {
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

	if (psmfPlayerLibVersion == 0x03090510) {
		// LocoRoco 2 depends on these for sizing its video output. Without this, its height is zero
		// and nothing is drawn.
		// Can't ask mediaengine for width/height here, it's too early, so we grabbed it from the
		// header in scePsmfPlayerSetPsmf.
		if (Memory::IsValidAddress(widthAddr) && psmfplayer->videoWidth) {
			Memory::Write_U32(psmfplayer->videoWidth, widthAddr);
		}
		if (Memory::IsValidAddress(heightAddr) && psmfplayer->videoHeight) {
			Memory::Write_U32(psmfplayer->videoHeight, heightAddr);
		}
	}
	return 0;
}

static u32 scePsmfPlayerGetCurrentPlayMode(u32 psmfPlayer, u32 playModeAddr, u32 playSpeedAddr)
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

static u32 scePsmfPlayerGetCurrentVideoStream(u32 psmfPlayer, u32 videoCodecAddr, u32 videoStreamNumAddr)
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

static u32 scePsmfPlayerGetCurrentAudioStream(u32 psmfPlayer, u32 audioCodecAddr, u32 audioStreamNumAddr)
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

static int scePsmfPlayerSetTempBuf(u32 psmfPlayer, u32 tempBufAddr, u32 tempBufSize)
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

static u32 scePsmfPlayerChangePlayMode(u32 psmfPlayer, int playMode, int playSpeed)
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

static u32 scePsmfPlayerSelectAudio(u32 psmfPlayer)
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

static u32 scePsmfPlayerSelectVideo(u32 psmfPlayer)
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

static u32 scePsmfPlayerSelectSpecificVideo(u32 psmfPlayer, int videoCodec, int videoStreamNum)
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
static u32 scePsmfPlayerSelectSpecificAudio(u32 psmfPlayer, int audioCodec, int audioStreamNum)
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

static u32 scePsmfPlayerConfigPlayer(u32 psmfPlayer, int configMode, int configAttr)
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

static int __PsmfPlayerFinish(u32 psmfPlayer) {
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
	{0XC22C8327, &WrapU_UU<scePsmfSetPsmf>,                            "scePsmfSetPsmf",                           'x', "xx"  ,HLE_CLEAR_STACK_BYTES, 0x50},
	{0XC7DB3A5B, &WrapU_UUU<scePsmfGetCurrentStreamType>,              "scePsmfGetCurrentStreamType",              'i', "xpp" ,HLE_CLEAR_STACK_BYTES, 0x50},
	{0X28240568, &WrapU_U<scePsmfGetCurrentStreamNumber>,              "scePsmfGetCurrentStreamNumber",            'i', "x"  },
	{0X1E6D9013, &WrapU_UUU<scePsmfSpecifyStreamWithStreamType>,       "scePsmfSpecifyStreamWithStreamType",       'i', "xii" ,HLE_CLEAR_STACK_BYTES, 0x20},
	{0X0C120E1D, &WrapU_UUU<scePsmfSpecifyStreamWithStreamTypeNumber>, "scePsmfSpecifyStreamWithStreamTypeNumber", 'i', "xii"},
	{0X4BC9BDE0, &WrapU_UI<scePsmfSpecifyStream>,                      "scePsmfSpecifyStream",                     'i', "xi"  ,HLE_CLEAR_STACK_BYTES, 0x40},
	{0X76D3AEBA, &WrapU_UU<scePsmfGetPresentationStartTime>,           "scePsmfGetPresentationStartTime",          'x', "xx"  ,HLE_CLEAR_STACK_BYTES, 0x10},
	{0XBD8AE0D8, &WrapU_UU<scePsmfGetPresentationEndTime>,             "scePsmfGetPresentationEndTime",            'x', "xx"  ,HLE_CLEAR_STACK_BYTES, 0x10},
	{0XEAED89CD, &WrapU_U<scePsmfGetNumberOfStreams>,                  "scePsmfGetNumberOfStreams",                'i', "x"   ,HLE_CLEAR_STACK_BYTES, 0x10},
	{0X7491C438, &WrapU_U<scePsmfGetNumberOfEPentries>,                "scePsmfGetNumberOfEPentries",              'x', "x"   ,HLE_CLEAR_STACK_BYTES, 0x10},
	{0X0BA514E5, &WrapU_UU<scePsmfGetVideoInfo>,                       "scePsmfGetVideoInfo",                      'i', "xp"  ,HLE_CLEAR_STACK_BYTES, 0x20},
	{0XA83F7113, &WrapU_UU<scePsmfGetAudioInfo>,                       "scePsmfGetAudioInfo",                      'i', "xp"  ,HLE_CLEAR_STACK_BYTES, 0x20},
	{0X971A3A90, &WrapU_U<scePsmfCheckEPMap>,                          "scePsmfCheckEPmap",                        'x', "x"   ,HLE_CLEAR_STACK_BYTES, 0x10},
	{0X68D42328, &WrapU_UI<scePsmfGetNumberOfSpecificStreams>,         "scePsmfGetNumberOfSpecificStreams",        'i', "xi"  ,HLE_CLEAR_STACK_BYTES, 0x20},
	{0X5B70FCC1, &WrapU_UU<scePsmfQueryStreamOffset>,                  "scePsmfQueryStreamOffset",                 'x', "xx" },
	{0X9553CC91, &WrapU_UU<scePsmfQueryStreamSize>,                    "scePsmfQueryStreamSize",                   'x', "xx" },
	{0XB78EB9E9, &WrapU_UU<scePsmfGetHeaderSize>,                      "scePsmfGetHeaderSize",                     'x', "xx" },
	{0XA5EBFE81, &WrapU_UU<scePsmfGetStreamSize>,                      "scePsmfGetStreamSize",                     'x', "xx" },
	{0XE1283895, &WrapU_U<scePsmfGetPsmfVersion>,                      "scePsmfGetPsmfVersion",                    'x', "x"  },
	{0X2673646B, &WrapU_U<scePsmfVerifyPsmf>,                          "scePsmfVerifyPsmf",                        'x', "x"   ,HLE_CLEAR_STACK_BYTES, 0x100},
	{0X4E624A34, &WrapU_UIU<scePsmfGetEPWithId>,                       "scePsmfGetEPWithId",                       'x', "xix" ,HLE_CLEAR_STACK_BYTES, 0x10},
	{0X7C0E7AC3, &WrapU_UUU<scePsmfGetEPWithTimestamp>,                "scePsmfGetEPWithTimestamp",                'x', "xxx" ,HLE_CLEAR_STACK_BYTES, 0x10},
	{0X5F457515, &WrapU_UU<scePsmfGetEPidWithTimestamp>,               "scePsmfGetEPidWithTimestamp",              'x', "xx"  ,HLE_CLEAR_STACK_BYTES, 0x20},
	{0X43AC7DBB, nullptr,                                              "scePsmfGetPsmfMark",                       '?', ""   },
	{0XDE78E9FC, nullptr,                                              "scePsmfGetNumberOfPsmfMarks",              '?', ""   },
};

const HLEFunction scePsmfPlayer[] =
{
	{0X235D8787, &WrapI_UU<scePsmfPlayerCreate>,                       "scePsmfPlayerCreate",                      'i', "xx" },
	{0X1078C008, &WrapI_U<scePsmfPlayerStop>,                          "scePsmfPlayerStop",                        'i', "x"  },
	{0X1E57A8E7, &WrapU_UII<scePsmfPlayerConfigPlayer>,                "scePsmfPlayerConfigPlayer",                'x', "xii"},
	{0X2BEB1569, &WrapI_U<scePsmfPlayerBreak>,                         "scePsmfPlayerBreak",                       'i', "x"  },
	{0X3D6D25A9, &WrapI_UC<scePsmfPlayerSetPsmf>,                      "scePsmfPlayerSetPsmf",                     'i', "xs" },
	{0X58B83577, &WrapI_UC<scePsmfPlayerSetPsmfCB>,                    "scePsmfPlayerSetPsmfCB",                   'i', "xs" },
	{0X3EA82A4B, &WrapI_U<scePsmfPlayerGetAudioOutSize>,               "scePsmfPlayerGetAudioOutSize",             'i', "x"  },
	{0X3ED62233, &WrapU_UU<scePsmfPlayerGetCurrentPts>,                "scePsmfPlayerGetCurrentPts",               'x', "xx" },
	{0X46F61F8B, &WrapI_UU<scePsmfPlayerGetVideoData>,                 "scePsmfPlayerGetVideoData",                'i', "xx" },
	{0X68F07175, &WrapU_UUU<scePsmfPlayerGetCurrentAudioStream>,       "scePsmfPlayerGetCurrentAudioStream",       'x', "xxx"},
	{0X75F03FA2, &WrapU_UII<scePsmfPlayerSelectSpecificVideo>,         "scePsmfPlayerSelectSpecificVideo",         'x', "xii"},
	{0X85461EFF, &WrapU_UII<scePsmfPlayerSelectSpecificAudio>,         "scePsmfPlayerSelectSpecificAudio",         'x', "xii"},
	{0X8A9EBDCD, &WrapU_U<scePsmfPlayerSelectVideo>,                   "scePsmfPlayerSelectVideo",                 'x', "x"  },
	{0X95A84EE5, &WrapI_UUI<scePsmfPlayerStart>,                       "scePsmfPlayerStart",                       'i', "xxi"},
	{0X9B71A274, &WrapI_U<scePsmfPlayerDelete>,                        "scePsmfPlayerDelete",                      'i', "x"  },
	{0X9FF2B2E7, &WrapU_UUU<scePsmfPlayerGetCurrentVideoStream>,       "scePsmfPlayerGetCurrentVideoStream",       'x', "xxx"},
	{0XA0B8CA55, &WrapI_U<scePsmfPlayerUpdate>,                        "scePsmfPlayerUpdate",                      'i', "x"  },
	{0XA3D81169, &WrapU_UII<scePsmfPlayerChangePlayMode>,              "scePsmfPlayerChangePlayMode",              'x', "xii"},
	{0XB8D10C56, &WrapU_U<scePsmfPlayerSelectAudio>,                   "scePsmfPlayerSelectAudio",                 'x', "x"  },
	{0XB9848A74, &WrapI_UU<scePsmfPlayerGetAudioData>,                 "scePsmfPlayerGetAudioData",                'i', "xx" },
	{0XDF089680, &WrapU_UUUU<scePsmfPlayerGetPsmfInfo>,                  "scePsmfPlayerGetPsmfInfo",                 'x', "xxxx" },
	{0XE792CD94, &WrapI_U<scePsmfPlayerReleasePsmf>,                   "scePsmfPlayerReleasePsmf",                 'i', "x"  },
	{0XF3EFAA91, &WrapU_UUU<scePsmfPlayerGetCurrentPlayMode>,          "scePsmfPlayerGetCurrentPlayMode",          'x', "xxx"},
	{0XF8EF08A6, &WrapI_U<scePsmfPlayerGetCurrentStatus>,              "scePsmfPlayerGetCurrentStatus",            'i', "x"  },
	{0X2D0E4E0A, &WrapI_UUU<scePsmfPlayerSetTempBuf>,                  "scePsmfPlayerSetTempBuf",                  'i', "xxx"},
	{0X76C0F4AE, &WrapI_UCI<scePsmfPlayerSetPsmfOffset>,               "scePsmfPlayerSetPsmfOffset",               'i', "xsi"},
	{0XA72DB4F9, &WrapI_UCI<scePsmfPlayerSetPsmfOffsetCB>,             "scePsmfPlayerSetPsmfOffsetCB",             'i', "xsi"},
	{0X340C12CB, nullptr,                                              "scePsmfPlayer_340C12CB",                   '?', ""   },
	// Fake function for PPSSPP's use.
	{0X05B193B7, &WrapI_U<__PsmfPlayerFinish>,                         "__PsmfPlayerFinish",                       'i', "x"  },
};

void Register_scePsmf() {
	RegisterModule("scePsmf",ARRAY_SIZE(scePsmf),scePsmf);
}

void Register_scePsmfPlayer() {
	RegisterModule("scePsmfPlayer",ARRAY_SIZE(scePsmfPlayer),scePsmfPlayer);
}
