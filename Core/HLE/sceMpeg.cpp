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


// This code is part shamelessly "inspired" from JPSCP.
#include <map>

#include "sceMpeg.h"
#include "sceKernelThread.h"
#include "HLE.h"
#include "../HW/MediaEngine.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

// MPEG AVC elementary stream.
static const int MPEG_AVC_ES_SIZE = 2048;          // MPEG packet size.

// MPEG ATRAC elementary stream.
static const int MPEG_ATRAC_ES_SIZE = 2112;
static const int MPEG_ATRAC_ES_OUTPUT_SIZE = 8192;

// MPEG PCM elementary stream.
static const int MPEG_PCM_ES_SIZE = 320;
static const int MPEG_PCM_ES_OUTPUT_SIZE = 320;

// MPEG Userdata elementary stream.
static const int MPEG_DATA_ES_SIZE = 0xA0000;
static const int MPEG_DATA_ES_OUTPUT_SIZE = 0xA0000;

// MPEG analysis results.
static const int MPEG_VERSION_0012 = 0;
static const int MPEG_VERSION_0013 = 1;
static const int MPEG_VERSION_0014 = 2;
static const int MPEG_VERSION_0015 = 3;

// MPEG streams.
static const int MPEG_AVC_STREAM = 0;
static const int MPEG_ATRAC_STREAM = 1;
static const int MPEG_PCM_STREAM = 2;
static const int MPEG_DATA_STREAM = 3;      // Arbitrary user defined type. Can represent audio or video.
static const int MPEG_AUDIO_STREAM = 15;
static const int MPEG_AU_MODE_DECODE = 0;
static const int MPEG_AU_MODE_SKIP = 1;
static const u32 MPEG_MEMSIZE = 0x10000;          // 64k.

static const int MPEG_AVC_DECODE_SUCCESS = 1;       // Internal value.
static const int MPEG_AVC_DECODE_ERROR_FATAL = 0x80628002;

static const int atracDecodeDelayMs = 3000;
static const int avcFirstDelayMs = 3600;
static const int avcDecodeDelayMs = 5400;         // Varies between 4700 and 6000.
static const int avcEmptyDelayMs = 320;
static const int mpegDecodeErrorDelayMs = 100;
static const int mpegTimestampPerSecond = 90000;  // How many MPEG Timestamp units in a second.
static const int videoTimestampStep = 3003;       // Value based on pmfplayer (mpegTimestampPerSecond / 29.970 (fps)).
static const int audioTimestampStep = 4180;       // For audio play at 44100 Hz (2048 samples / 44100 * mpegTimestampPerSecond == 4180)
//static const int audioFirstTimestamp = 89249;     // The first MPEG audio AU has always this timestamp
static const int audioFirstTimestamp = 90000;     // The first MPEG audio AU has always this timestamp
static const s64 UNKNOWN_TIMESTAMP = -1;

// At least 2048 bytes of MPEG data is provided when analysing the MPEG header
static const int MPEG_HEADER_BUFFER_MINIMUM_SIZE = 2048;

static const int NUM_ES_BUFFERS = 2;

static const int PSP_ERROR_MPEG_NO_DATA = 0x80618001;

static const int TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650 = 0X00;
static const int TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888 = 0X03;

int getMaxAheadTimestamp(const SceMpegRingBuffer &ringbuf) {
	return std::max(40000, ringbuf.packets * 700);  // empiric value from JPCSP, thanks!
}

// Internal structure
struct AvcContext {
	int avcDetailFrameWidth;
	int avcDetailFrameHeight;
	int avcDecodeResult;
	int avcFrameStatus;
};

struct StreamInfo {	
	int type;
	int num;
	int sid;
	bool needsReset;
};

typedef std::map<u32, StreamInfo> StreamInfoMap;

// Internal structure
struct MpegContext {
	MpegContext() : mediaengine(NULL) {}
	~MpegContext() {
		if (mediaengine != NULL) {
			delete mediaengine;
		}
	}

	void DoState(PointerWrap &p) {
		p.Do(defaultFrameWidth);
		p.Do(videoFrameCount);
		p.Do(audioFrameCount);
		p.Do(endOfAudioReached);
		p.Do(endOfVideoReached);
		p.Do(videoPixelMode);
		p.Do(mpegMagic);
		p.Do(mpegVersion);
		p.Do(mpegRawVersion);
		p.Do(mpegOffset);
		p.Do(mpegStreamSize);
		p.Do(mpegFirstTimestamp);
		p.Do(mpegLastTimestamp);
		p.Do(mpegFirstDate);
		p.Do(mpegLastDate);
		p.Do(mpegRingbufferAddr);
		p.Do(mpegStreamAddr);
		p.DoArray(esBuffers, NUM_ES_BUFFERS);
		p.Do(avc);
		p.Do(avcRegistered);
		p.Do(atracRegistered);
		p.Do(pcmRegistered);
		p.Do(dataRegistered);
		p.Do(ignoreAtrac);
		p.Do(ignorePcm);
		p.Do(ignoreAvc);
		p.Do(isAnalyzed);
		p.Do<u32, StreamInfo>(streamMap);
		p.DoClass(mediaengine);
		p.DoMarker("MpegContext");
	}

	u32 defaultFrameWidth;
	int videoFrameCount;
	int audioFrameCount;
	bool endOfAudioReached;
	bool endOfVideoReached;
	int videoPixelMode;
	u32 mpegMagic;
	int mpegVersion;
	u32 mpegRawVersion;
	u32 mpegOffset;
	u32 mpegStreamSize;
	s64 mpegFirstTimestamp;
	s64 mpegLastTimestamp;
	u32 mpegFirstDate;
	u32 mpegLastDate;
	u32 mpegRingbufferAddr;
	u32 mpegStreamAddr;
	bool esBuffers[NUM_ES_BUFFERS];
	AvcContext avc;

	bool avcRegistered;
	bool atracRegistered;
	bool pcmRegistered;
	bool dataRegistered;

	bool ignoreAtrac;
	bool ignorePcm;
	bool ignoreAvc;

	bool isAnalyzed;

	StreamInfoMap streamMap;
	MediaEngine *mediaengine;
};

static bool isMpegInit;
static u32 streamIdGen;
static bool isCurrentMpegAnalyzed;
static int actionPostPut;
static std::map<u32, MpegContext *> mpegMap;
static u32 lastMpegHandle = 0;

MpegContext *getMpegCtx(u32 mpegAddr) {
	u32 mpeg = Memory::Read_U32(mpegAddr);

	// TODO: Remove.
	if (mpegMap.find(mpeg) == mpegMap.end())
	{
		ERROR_LOG_REPORT(HLE, "Bad mpeg handle %08x - using last one (%08x) instead", mpeg, lastMpegHandle);
		mpeg = lastMpegHandle;
	}

	if (mpegMap.find(mpeg) == mpegMap.end())
		return NULL;
	return mpegMap[mpeg];
}

u32 getMpegHandle(u32 mpeg) {
	return Memory::Read_U32(mpeg);
}

static void InitRingbuffer(SceMpegRingBuffer *buf, int packets, int data, int size, int callback_addr, int callback_args) {
	buf->packets = packets;
	buf->packetsRead = 0;
	buf->packetsWritten = 0;
	buf->packetsFree = 0;
	buf->packetSize = 2048;
	buf->data = data;
	buf->callback_addr = callback_addr;
	buf->callback_args = callback_args;
	buf->dataUpperBound = data + packets * 2048;
	buf->semaID = -1;
	buf->mpeg = 0;
}	

u32 convertTimestampToDate(u32 ts) {
	return ts;  // TODO
}

void AnalyzeMpeg(u32 buffer_addr, MpegContext *ctx) {
	ctx->mpegStreamAddr = buffer_addr;
	ctx->mpegMagic = Memory::Read_U32(buffer_addr);
	ctx->mpegRawVersion = Memory::Read_U32(buffer_addr + PSMF_STREAM_VERSION_OFFSET);
	switch (ctx->mpegRawVersion) {
	case PSMF_VERSION_0012:
		ctx->mpegVersion = MPEG_VERSION_0012;
		break;
	case PSMF_VERSION_0013:
		ctx->mpegVersion = MPEG_VERSION_0013;
		break;
	case PSMF_VERSION_0014:
		ctx->mpegVersion = MPEG_VERSION_0014;
		break;
	case PSMF_VERSION_0015:
		ctx->mpegVersion = MPEG_VERSION_0015;
		break;
	default:
		ctx->mpegVersion = -1;
		break;
	}
	ctx->mpegOffset = bswap32(Memory::Read_U32(buffer_addr + PSMF_STREAM_OFFSET_OFFSET));
	ctx->mpegStreamSize = bswap32(Memory::Read_U32(buffer_addr + PSMF_STREAM_SIZE_OFFSET));
	ctx->mpegFirstTimestamp = getMpegTimeStamp(Memory::GetPointer(buffer_addr + PSMF_FIRST_TIMESTAMP_OFFSET));
	ctx->mpegLastTimestamp = getMpegTimeStamp(Memory::GetPointer(buffer_addr + PSMF_LAST_TIMESTAMP_OFFSET));
	ctx->mpegFirstDate = convertTimestampToDate(ctx->mpegFirstTimestamp);
	ctx->mpegLastDate = convertTimestampToDate(ctx->mpegLastTimestamp);
	ctx->avc.avcDetailFrameWidth = (Memory::Read_U8(buffer_addr + 142) * 0x10);
	ctx->avc.avcDetailFrameHeight = (Memory::Read_U8(buffer_addr + 143) * 0x10);
	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;
	ctx->avc.avcFrameStatus = 0;

	ctx->videoFrameCount = 0;
	ctx->audioFrameCount = 0;
	ctx->endOfAudioReached = false;
	ctx->endOfVideoReached = false;

	if (ctx->mpegMagic != PSMF_MAGIC || ctx->mpegVersion < 0 ||
		(ctx->mpegOffset & 2047) != 0 || ctx->mpegOffset == 0) {
		// mpeg header is invalid!
		return;
	}

	if (ctx->mediaengine && (ctx->mpegStreamSize > 0) && !ctx->isAnalyzed) {
		// init mediaEngine
		ctx->mediaengine->loadStream(Memory::GetPointer(buffer_addr), ctx->mpegOffset, ctx->mpegOffset + ctx->mpegStreamSize);
		ctx->mediaengine->setVideoDim();
	}
	// When used with scePsmf, some applications attempt to use sceMpegQueryStreamOffset
	// and sceMpegQueryStreamSize, which forces a packet overwrite in the Media Engine and in
	// the MPEG ringbuffer.
	// Mark the current MPEG as analyzed to filter this, and restore it at sceMpegFinish.
	ctx->isAnalyzed = true;

	INFO_LOG(ME, "Stream offset: %d, Stream size: 0x%X", ctx->mpegOffset, ctx->mpegStreamSize);
	INFO_LOG(ME, "First timestamp: %lld, Last timestamp: %lld", ctx->mpegFirstTimestamp, ctx->mpegLastTimestamp);
}

class PostPutAction : public Action {
public:
	PostPutAction() {}
	void setRingAddr(u32 ringAddr) { ringAddr_ = ringAddr; }
	static Action *Create() { return new PostPutAction; }
	void DoState(PointerWrap &p) { p.Do(ringAddr_); p.DoMarker("PostPutAction"); }
	void run(MipsCall &call);
private:
	u32 ringAddr_;
};

void __MpegInit() {
	lastMpegHandle = 0;
	streamIdGen = 1;
	isCurrentMpegAnalyzed = false;
	isMpegInit = false;
	actionPostPut = __KernelRegisterActionType(PostPutAction::Create);

#ifdef USING_FFMPEG
	avcodec_register_all();
	av_register_all();
#endif
}

void __MpegDoState(PointerWrap &p) {
	p.Do(lastMpegHandle);
	p.Do(streamIdGen);
	p.Do(isCurrentMpegAnalyzed);
	p.Do(isMpegInit);
	p.Do(actionPostPut);
	__KernelRestoreActionType(actionPostPut, PostPutAction::Create);

	p.Do(mpegMap);

	p.DoMarker("sceMpeg");
}

void __MpegShutdown() {
	std::map<u32, MpegContext *>::iterator it, end;
	for (it = mpegMap.begin(), end = mpegMap.end(); it != end; ++it) {
		delete it->second;
	}
	mpegMap.clear();
}

u32 sceMpegInit() {
	if (isMpegInit) {
		WARN_LOG(HLE, "sceMpegInit(): already initialized");
		// TODO: Need to properly hook module load/unload for this to work right.
		//return ERROR_MPEG_ALREADY_INIT;
	} else {
		INFO_LOG(HLE, "sceMpegInit()");
	}
	isMpegInit = true;
	return hleDelayResult(0, "mpeg init", 750);
}

u32 sceMpegRingbufferQueryMemSize(int packets)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferQueryMemSize(%i)", packets);
	int size = packets * (104 + 2048);
	return size;
}

u32 sceMpegRingbufferConstruct(u32 ringbufferAddr, u32 numPackets, u32 data, u32 size, u32 callbackAddr, u32 callbackArg)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferConstruct(%08x, %i, %08x, %i, %08x, %i)", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
	SceMpegRingBuffer ring;
	InitRingbuffer(&ring, numPackets, data, size, callbackAddr, callbackArg);
	Memory::WriteStruct(ringbufferAddr, &ring);
	return 0;
}

u32 sceMpegCreate(u32 mpegAddr, u32 dataPtr, u32 size, u32 ringbufferAddr, u32 frameWidth, u32 mode, u32 ddrTop)
{
	if (size < MPEG_MEMSIZE) {
		WARN_LOG(HLE, "ERROR_MPEG_NO_MEMORY=sceMpegCreate(%08x, %08x, %i, %08x, %i, %i, %i)",
			mpegAddr, dataPtr, size, ringbufferAddr, frameWidth, mode, ddrTop);
		return ERROR_MPEG_NO_MEMORY;
	}

	SceMpegRingBuffer ringbuffer;
	if(ringbufferAddr != 0){
		Memory::ReadStruct(ringbufferAddr, &ringbuffer);
		if (ringbuffer.packetSize == 0) {
			ringbuffer.packetsFree = 0;
		} else {
			ringbuffer.packetsFree = (ringbuffer.dataUpperBound - ringbuffer.data) / ringbuffer.packetSize;
		}
		ringbuffer.mpeg = mpegAddr;
		Memory::WriteStruct(ringbufferAddr, &ringbuffer);
	}

	// Generate, and write mpeg handle into mpeg data, for some reason
	int mpegHandle = dataPtr + 0x30;
	Memory::Write_U32(mpegHandle, mpegAddr);

	Memory::Memcpy(mpegHandle, "LIBMPEG.001", 12);
	Memory::Write_U32(-1, mpegHandle + 12);
	Memory::Write_U32(ringbufferAddr, mpegHandle + 16);
	Memory::Write_U32(ringbuffer.dataUpperBound, mpegHandle + 20);

	MpegContext *ctx = new MpegContext;
	mpegMap[mpegHandle] = ctx;
	lastMpegHandle = mpegHandle;

	ctx->mpegRingbufferAddr = ringbufferAddr;
	ctx->videoFrameCount = 0;
	ctx->audioFrameCount = 0;
	// TODO: What's the actual default?
	ctx->videoPixelMode = TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888;
	ctx->avcRegistered = false;
	ctx->atracRegistered = false;
	ctx->pcmRegistered = false;
	ctx->dataRegistered = false;
	ctx->ignoreAtrac = false;
	ctx->ignorePcm = false;
	ctx->ignoreAvc = false;
	ctx->defaultFrameWidth = frameWidth;
	for (int i = 0; i < NUM_ES_BUFFERS; i++) {
		ctx->esBuffers[i] = false;
	}

	// Detailed "analysis" is done later in Query* for some reason.
	ctx->isAnalyzed = false;
	ctx->mediaengine = new MediaEngine();

	INFO_LOG(HLE, "%08x=sceMpegCreate(%08x, %08x, %i, %08x, %i, %i, %i)",
		mpegHandle, mpegAddr, dataPtr, size, ringbufferAddr, frameWidth, mode, ddrTop);
	return hleDelayResult(0, "mpeg create", 29000);
}

int sceMpegDelete(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegDelete(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegDelete(%08x)", mpeg);

	delete ctx;
	mpegMap.erase(Memory::Read_U32(mpeg));

	return 0;
}


int sceMpegAvcDecodeMode(u32 mpeg, u32 modeAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegAvcDecodeMode(%08x, %08x): bad mpeg handle", mpeg, modeAddr);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegAvcDecodeMode(%08x, %08x)", mpeg, modeAddr);
	if (Memory::IsValidAddress(modeAddr)) {
		int mode = Memory::Read_U32(modeAddr);
		int pixelMode = Memory::Read_U32(modeAddr + 4);
		if (pixelMode >= TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650 && pixelMode <= TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888) {
			ctx->videoPixelMode = pixelMode;
		} else {
			ERROR_LOG(HLE, "sceMpegAvcDecodeMode(%i, %i): unknown pixelMode ", mode, pixelMode);
		}
	} else {
			ERROR_LOG(HLE, "sceMpegAvcDecodeMode(%08x, %08x): invalid modeAddr", mpeg, modeAddr);
			return -1;
	}

	return 0;
}

int sceMpegQueryStreamOffset(u32 mpeg, u32 bufferAddr, u32 offsetAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegQueryStreamOffset(%08x, %08x, %08x): bad mpeg handle", mpeg, bufferAddr, offsetAddr);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegQueryStreamOffset(%08x, %08x, %08x)", mpeg, bufferAddr, offsetAddr);

	// Kinda destructive, no?
	AnalyzeMpeg(bufferAddr, ctx);

	if (ctx->mpegMagic != PSMF_MAGIC) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad PSMF magic");
		Memory::Write_U32(0, offsetAddr);
		return ERROR_MPEG_INVALID_VALUE;
	} else if (ctx->mpegVersion < 0) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad version");
		Memory::Write_U32(0, offsetAddr);
		return ERROR_MPEG_BAD_VERSION;
	} else if ((ctx->mpegOffset & 2047) != 0 || ctx->mpegOffset == 0) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad offset");
		Memory::Write_U32(0, offsetAddr);
		return ERROR_MPEG_INVALID_VALUE;
	}

	Memory::Write_U32(ctx->mpegOffset, offsetAddr);
	return 0;
}

u32 sceMpegQueryStreamSize(u32 bufferAddr, u32 sizeAddr)
{
	DEBUG_LOG(HLE, "sceMpegQueryStreamSize(%08x, %08x)", bufferAddr, sizeAddr);

	MpegContext ctx;
	ctx.mediaengine = 0;

	AnalyzeMpeg(bufferAddr, &ctx);

	if (ctx.mpegMagic != PSMF_MAGIC) {
		ERROR_LOG(HLE, "sceMpegQueryStreamSize: Bad PSMF magic");
		Memory::Write_U32(0, sizeAddr);
		return ERROR_MPEG_INVALID_VALUE;
	} else if ((ctx.mpegOffset & 2047) != 0 ) {
		ERROR_LOG(HLE, "sceMpegQueryStreamSize: Bad offset");
		Memory::Write_U32(0, sizeAddr);
		return ERROR_MPEG_INVALID_VALUE;
	}

	Memory::Write_U32(ctx.mpegStreamSize, sizeAddr);
	return 0;
}

int sceMpegRegistStream(u32 mpeg, u32 streamType, u32 streamNum)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx)
	{
		WARN_LOG(HLE, "sceMpegRegistStream(%08x, %i, %i): bad mpeg handle", mpeg, streamType, streamNum);
		return -1;
	}

	INFO_LOG(HLE, "sceMpegRegistStream(%08x, %i, %i)", mpeg, streamType, streamNum);

	switch (streamType) {
	case MPEG_AVC_STREAM:
		ctx->avcRegistered = true;
		ctx->mediaengine->setVideoStream(streamNum);
		break;
	case MPEG_AUDIO_STREAM:
	case MPEG_ATRAC_STREAM:
		ctx->atracRegistered = true;
		ctx->mediaengine->setAudioStream(streamNum);
		break;
	case MPEG_PCM_STREAM:
		ctx->pcmRegistered = true;
		break;
	case MPEG_DATA_STREAM:
		ctx->dataRegistered = true;
		break;
	default : 
		DEBUG_LOG(HLE, "sceMpegRegistStream(%i) : unknown stream type", streamType);
		break;
	}
	// ...
	u32 sid = streamIdGen++;
	StreamInfo info;
	info.type = streamType;
	info.num = streamNum;
	info.needsReset = true;
	ctx->streamMap[sid] = info;
	return sid;
}

int sceMpegMallocAvcEsBuf(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegMallocAvcEsBuf(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegMallocAvcEsBuf(%08x)", mpeg);

	// Doesn't actually malloc, just keeps track of a couple of flags
	for (int i = 0; i < NUM_ES_BUFFERS; i++) {
		if (!ctx->esBuffers[i]) {
			ctx->esBuffers[i] = true;
			return i + 1;
		}
	}
	// No es buffer
	return 0;
}

int sceMpegFreeAvcEsBuf(u32 mpeg, int esBuf)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegFreeAvcEsBuf(%08x, %i): bad mpeg handle", mpeg, esBuf);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegFreeAvcEsBuf(%08x, %i)", mpeg, esBuf);

	if (esBuf == 0) {
		return ERROR_MPEG_INVALID_VALUE;
	}
	if (esBuf >= 1 && esBuf <= NUM_ES_BUFFERS) {
		// TODO: Check if it's already been free'd?
		ctx->esBuffers[esBuf - 1] = false;
	}
	return 0;
}

u32 sceMpegAvcDecode(u32 mpeg, u32 auAddr, u32 frameWidth, u32 bufferAddr, u32 initAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegAvcDecode(%08x, %08x, %d, %08x, %08x): bad mpeg handle", mpeg, auAddr, frameWidth, bufferAddr, initAddr);
		return 0;
	}

	if (!Memory::IsValidAddress(auAddr) || !Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(initAddr)) {
		ERROR_LOG(HLE, "sceMpegAvcDecode: bad addresses");
		return 0;
	}

	if (frameWidth == 0) {  // wtf, go sudoku passes in 0xccccccccc
		if (!ctx->defaultFrameWidth) {
			frameWidth = ctx->avc.avcDetailFrameWidth;
		} else {
			frameWidth = ctx->defaultFrameWidth;
		}
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);

	SceMpegRingBuffer ringbuffer = {0};
	if (Memory::IsValidAddress(ctx->mpegRingbufferAddr)) {
		Memory::ReadStruct(ctx->mpegRingbufferAddr, &ringbuffer);
	} else {
		ERROR_LOG(HLE, "Bogus mpegringbufferaddr");
		return -1;
	}

	if (ringbuffer.packetsRead == 0 || ctx->mediaengine->IsVideoEnd()) {
		WARN_LOG(HLE, "sceMpegAvcDecode(%08x, %08x, %d, %08x, %08x): mpeg buffer empty", mpeg, auAddr, frameWidth, bufferAddr, initAddr);
		return hleDelayResult(MPEG_AVC_DECODE_ERROR_FATAL, "mpeg buffer empty", avcEmptyDelayMs);
	}

	u32 buffer = Memory::Read_U32(bufferAddr);
	u32 init = Memory::Read_U32(initAddr);
	DEBUG_LOG(HLE, "*buffer = %08x, *init = %08x", buffer, init);

	if (ctx->mediaengine->stepVideo(ctx->videoPixelMode)) {
		int bufferSize = ctx->mediaengine->writeVideoImage(Memory::GetPointer(buffer), frameWidth, ctx->videoPixelMode);
		gpu->InvalidateCache(buffer, bufferSize, GPU_INVALIDATE_SAFE);
		ctx->avc.avcFrameStatus = 1;
		ctx->videoFrameCount++;
	} else {
		ctx->avc.avcFrameStatus = 0;
	}
	ringbuffer.packetsFree = std::max(0, ringbuffer.packets - ctx->mediaengine->getBufferedSize() / 2048);

	avcAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;

	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;

	// Flush structs back to memory
	avcAu.write(auAddr);
	Memory::WriteStruct(ctx->mpegRingbufferAddr, &ringbuffer);

	Memory::Write_U32(ctx->avc.avcFrameStatus, initAddr);  // 1 = showing, 0 = not showing

	DEBUG_LOG(HLE, "sceMpegAvcDecode(%08x, %08x, %i, %08x, %08x)", mpeg, auAddr, frameWidth, bufferAddr, initAddr);

	if (ctx->videoFrameCount <= 1)
		return hleDelayResult(0, "mpeg decode", avcFirstDelayMs);
	else
		return hleDelayResult(0, "mpeg decode", avcDecodeDelayMs);
	//hleEatMicro(3300);
	//return hleDelayResult(0, "mpeg decode", 200);
}

u32 sceMpegAvcDecodeStop(u32 mpeg, u32 frameWidth, u32 bufferAddr, u32 statusAddr)
{
	ERROR_LOG(HLE, "sceMpegAvcDecodeStop(%08x, %08x, %08x, %08x)", mpeg, frameWidth, bufferAddr, statusAddr);
	if (Memory::IsValidAddress(statusAddr)) {
		Memory::Write_U32(0,statusAddr);
	} else {
		ERROR_LOG(HLE, "sceMpegAvcDecodeStop(%08x, %08x): invalid statusAddr", mpeg, statusAddr);
		return -1;
	}
	return 0;
}

u32 sceMpegUnRegistStream(u32 mpeg, int streamUid)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx)
	{
		WARN_LOG(HLE, "sceMpegUnRegistStream(%08x, %i): bad mpeg handle", mpeg, streamUid);
		return -1;
	}

	StreamInfo info = {0};

	switch (info.type) {
	case MPEG_AVC_STREAM:
		ctx->avcRegistered = false;
		break;
	case MPEG_AUDIO_STREAM:
	case MPEG_ATRAC_STREAM:
		ctx->atracRegistered = false;
		break;
	case MPEG_PCM_STREAM:
		ctx->pcmRegistered = false;
		break;
	case MPEG_DATA_STREAM:
		ctx->dataRegistered = false;
		break;
	default : 
		DEBUG_LOG(HLE, "sceMpegUnRegistStream(%i) : unknown streamID ", streamUid);
		break;
	}
	ctx->streamMap[streamUid] = info;
	info.type = -1;
	info.sid = -1 ;
	info.needsReset = true;
	ctx->isAnalyzed = false;
	return 0;
}

int sceMpegAvcDecodeDetail(u32 mpeg, u32 detailAddr)
{
	if (!Memory::IsValidAddress(detailAddr))
	{
		WARN_LOG(HLE, "sceMpegAvcDecodeDetail(%08x, %08x): invalid detailAddr", mpeg, detailAddr);
		return -1;
	}
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx)
	{
		WARN_LOG(HLE, "sceMpegAvcDecodeDetail(%08x, %08x): bad mpeg handle", mpeg, detailAddr);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegAvcDecodeDetail(%08x, %08x)", mpeg, detailAddr);

	Memory::Write_U32(ctx->avc.avcDecodeResult, detailAddr + 0);
	Memory::Write_U32(ctx->videoFrameCount, detailAddr + 4);
	Memory::Write_U32(ctx->avc.avcDetailFrameWidth, detailAddr + 8);
	Memory::Write_U32(ctx->avc.avcDetailFrameHeight, detailAddr + 12);
	Memory::Write_U32(0, detailAddr + 16);
	Memory::Write_U32(0, detailAddr + 20);
	Memory::Write_U32(0, detailAddr + 24);
	Memory::Write_U32(0, detailAddr + 28);
	Memory::Write_U32(ctx->avc.avcFrameStatus, detailAddr + 32);
	return 0;
}

u32 sceMpegAvcDecodeStopYCbCr(u32 mpeg, u32 bufferAddr, u32 statusAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcDecodeStopYCbCr(%08x, %08x, %08x)", mpeg, bufferAddr, statusAddr);
	return 0;
}

int sceMpegAvcDecodeYCbCr(u32 mpeg, u32 auAddr, u32 bufferAddr, u32 initAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegAvcDecodeYCbCr(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, auAddr, bufferAddr, initAddr);
		return 0;
	}

	if (!Memory::IsValidAddress(auAddr) || !Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(initAddr)) {
		ERROR_LOG(HLE, "sceMpegAvcDecodeYCbCr: bad addresses");
		return 0;
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);

	SceMpegRingBuffer ringbuffer = {0};
	if (Memory::IsValidAddress(ctx->mpegRingbufferAddr)) {
		Memory::ReadStruct(ctx->mpegRingbufferAddr, &ringbuffer);
	} else {
		ERROR_LOG(HLE, "Bogus mpegringbufferaddr");
		return -1;
	}

	if (ringbuffer.packetsRead == 0 || ctx->mediaengine->IsVideoEnd()) {
		WARN_LOG(HLE, "sceMpegAvcDecodeYCbCr(%08x, %08x, %08x, %08x): mpeg buffer empty", mpeg, auAddr, bufferAddr, initAddr);
		return hleDelayResult(MPEG_AVC_DECODE_ERROR_FATAL, "mpeg buffer empty", avcEmptyDelayMs);
	}

	u32 buffer = Memory::Read_U32(bufferAddr);
	u32 init = Memory::Read_U32(initAddr);
	DEBUG_LOG(HLE, "*buffer = %08x, *init = %08x", buffer, init);

	if (ctx->mediaengine->stepVideo(ctx->videoPixelMode)) {
		// Don't draw here, we'll draw in the Csc func.
		ctx->avc.avcFrameStatus = 1;
		ctx->videoFrameCount++;
	}else {
		ctx->avc.avcFrameStatus = 0;
	}
	ringbuffer.packetsFree = std::max(0, ringbuffer.packets - ctx->mediaengine->getBufferedSize() / 2048);

	avcAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;

	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;

	// Flush structs back to memory
	avcAu.write(auAddr);
	Memory::WriteStruct(ctx->mpegRingbufferAddr, &ringbuffer);

	Memory::Write_U32(ctx->avc.avcFrameStatus, initAddr);  // 1 = showing, 0 = not showing

	DEBUG_LOG(HLE, "sceMpegAvcDecodeYCbCr(%08x, %08x, %08x, %08x)", mpeg, auAddr, bufferAddr, initAddr);

	if (ctx->videoFrameCount <= 1)
		return hleDelayResult(0, "mpeg decode", avcFirstDelayMs);
	else
		return hleDelayResult(0, "mpeg decode", avcDecodeDelayMs);
	//hleEatMicro(3300);
	//return hleDelayResult(0, "mpeg decode", 200);
}

u32 sceMpegAvcDecodeFlush(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcDecodeFlush(%08x)", mpeg);
	if ( ctx->videoFrameCount > 0 || ctx->audioFrameCount > 0) {
		//__MpegFinish();
	}
	return 0;
}

int sceMpegInitAu(u32 mpeg, u32 bufferAddr, u32 auPointer)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegInitAu(%08x, %i, %08x): bad mpeg handle", mpeg, bufferAddr, auPointer);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegInitAu(%08x, %i, %08x)", mpeg, bufferAddr, auPointer);

	SceMpegAu sceAu;
	sceAu.read(auPointer);

	if (bufferAddr >= 1 && bufferAddr <= (u32)NUM_ES_BUFFERS && ctx->esBuffers[bufferAddr - 1]) {
		// This esbuffer has been allocated for Avc.
		sceAu.esBuffer = bufferAddr;   // Can this be right??? not much of a buffer pointer..
		sceAu.esSize = MPEG_AVC_ES_SIZE;
		sceAu.dts = 0;
		sceAu.pts = 0;

		sceAu.write(auPointer);
	} else {
		// This esbuffer has been left as Atrac.
		sceAu.esBuffer = bufferAddr;
		sceAu.esSize = MPEG_ATRAC_ES_SIZE;
		sceAu.pts = 0;
		sceAu.dts = UNKNOWN_TIMESTAMP;

		sceAu.write(auPointer);
	}
	return 0;
}

int sceMpegQueryAtracEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (!Memory::IsValidAddress(esSizeAddr) || !Memory::IsValidAddress(outSizeAddr)) {
		ERROR_LOG(HLE, "sceMpegQueryAtracEsSize(%08x, %08x, %08x) - bad address", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegQueryAtracEsSize(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);
	Memory::Write_U32(MPEG_ATRAC_ES_SIZE, esSizeAddr);
	Memory::Write_U32(MPEG_ATRAC_ES_OUTPUT_SIZE, outSizeAddr);
	return 0;
}

int sceMpegRingbufferAvailableSize(u32 ringbufferAddr)
{
	PSPPointer<SceMpegRingBuffer> ringbuffer;
	ringbuffer = ringbufferAddr;

	if (!ringbuffer.Valid()) {
		ERROR_LOG(HLE, "sceMpegRingbufferAvailableSize(%08x) - bad address", ringbufferAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(ringbuffer->mpeg);
	if (!ctx) {
		ERROR_LOG(HLE, "sceMpegRingbufferAvailableSize(%08x) - bad mpeg", ringbufferAddr);
		return -1;
	}

	hleEatCycles(2020);
	DEBUG_LOG(HLE, "%i=sceMpegRingbufferAvailableSize(%08x)", ringbuffer->packetsFree, ringbufferAddr);
	return ringbuffer->packetsFree;
}

void PostPutAction::run(MipsCall &call) {
	SceMpegRingBuffer ringbuffer;
	Memory::ReadStruct(ringAddr_, &ringbuffer);

	MpegContext *ctx = getMpegCtx(ringbuffer.mpeg);

	int packetsAdded = currentMIPS->r[2];
	if (packetsAdded > 0) {
		if (packetsAdded > ringbuffer.packetsFree) {
			WARN_LOG(HLE, "sceMpegRingbufferPut clamping packetsAdded old=%i new=%i", packetsAdded, ringbuffer.packetsFree);
			packetsAdded = ringbuffer.packetsFree;
		}
		int actuallyAdded = ctx->mediaengine->addStreamData(Memory::GetPointer(ringbuffer.data), packetsAdded * 2048) / 2048;
		if (actuallyAdded != packetsAdded) {
			WARN_LOG_REPORT(HLE, "sceMpegRingbufferPut(): unable to enqueue all added packets, going to overwrite some frames.");
		}
		ringbuffer.packetsRead += packetsAdded;
		ringbuffer.packetsWritten += packetsAdded;
		ringbuffer.packetsFree -= packetsAdded;
	}
	DEBUG_LOG(HLE, "packetAdded: %i packetsRead: %i packetsTotal: %i", packetsAdded, ringbuffer.packetsRead, ringbuffer.packets);

	Memory::WriteStruct(ringAddr_, &ringbuffer);
	call.setReturnValue(packetsAdded);
}


// Program signals that it has written data to the ringbuffer and gets a callback ?
u32 sceMpegRingbufferPut(u32 ringbufferAddr, u32 numPackets, u32 available)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferPut(%08x, %i, %i)", ringbufferAddr, numPackets, available);
	numPackets = std::min(numPackets, available);
	if (numPackets <= 0)
		return 0;

	SceMpegRingBuffer ringbuffer;
	Memory::ReadStruct(ringbufferAddr, &ringbuffer);

	MpegContext *ctx = getMpegCtx(ringbuffer.mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegRingbufferPut(%08x, %i, %i): bad mpeg handle %08x", ringbufferAddr, numPackets, available, ringbuffer.mpeg);
		return 0;
	}

	// Execute callback function as a direct MipsCall, no blocking here so no messing around with wait states etc
	if (ringbuffer.callback_addr) {
		PostPutAction *action = (PostPutAction *)__KernelCreateAction(actionPostPut);
		action->setRingAddr(ringbufferAddr);
		// TODO: Should call this multiple times until we get numPackets.
		// Normally this would be if it did not read enough, but also if available > packets.
		// Should ultimately return the TOTAL number of returned packets.
		u32 packetsThisRound = std::min(numPackets, (u32)ringbuffer.packets);
		u32 args[3] = {(u32)ringbuffer.data, packetsThisRound, (u32)ringbuffer.callback_args};
		__KernelDirectMipsCall(ringbuffer.callback_addr, action, args, 3, false);
	} else {
		ERROR_LOG(HLE, "sceMpegRingbufferPut: callback_addr zero");
	}
	return 0;
}

int sceMpegGetAvcAu(u32 mpeg, u32 streamId, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegGetAvcAu(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, streamId, auAddr, attrAddr);
		return -1;
	}

	SceMpegRingBuffer mpegRingbuffer;
	Memory::ReadStruct(ctx->mpegRingbufferAddr, &mpegRingbuffer);

	SceMpegAu sceAu;
	sceAu.read(auAddr);

	if (mpegRingbuffer.packetsRead == 0 || mpegRingbuffer.packetsFree == mpegRingbuffer.packets) {
		DEBUG_LOG(HLE, "PSP_ERROR_MPEG_NO_DATA=sceMpegGetAvcAu(%08x, %08x, %08x, %08x)", mpeg, streamId, auAddr, attrAddr);
		sceAu.pts = -1;
		sceAu.dts = -1;
		sceAu.write(auAddr);
		// TODO: Does this really reschedule?
		return hleDelayResult(PSP_ERROR_MPEG_NO_DATA, "mpeg get avc", mpegDecodeErrorDelayMs);
	}

	auto streamInfo = ctx->streamMap.find(streamId);
	if (streamInfo == ctx->streamMap.end())
	{
		ERROR_LOG(HLE, "sceMpegGetAvcAu - bad stream id %i", streamId);
		return -1;
	}

	if (streamInfo->second.needsReset)
	{
		sceAu.pts = 0;
		streamInfo->second.needsReset = false;
	}

	/*// Wait for audio if too much ahead
	if (ctx->atracRegistered && (ctx->mediaengine->getVideoTimeStamp() > ctx->mediaengine->getAudioTimeStamp() + getMaxAheadTimestamp(mpegRingbuffer)))
	{
		ERROR_LOG(HLE, "sceMpegGetAvcAu - video too much ahead");
		// TODO: Does this really reschedule?
		return hleDelayResult(PSP_ERROR_MPEG_NO_DATA, "mpeg get avc", mpegDecodeErrorDelayMs);
	}*/

	int result = 0;

	sceAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;
	sceAu.dts = sceAu.pts - videoTimestampStep;
	if (ctx->mediaengine->IsVideoEnd()) {
		INFO_LOG(HLE, "video end reach. pts: %i dts: %i", (int)sceAu.pts, (int)ctx->mediaengine->getLastTimeStamp());
		mpegRingbuffer.packetsFree = mpegRingbuffer.packets;
		Memory::WriteStruct(ctx->mpegRingbufferAddr, &mpegRingbuffer);

		result = PSP_ERROR_MPEG_NO_DATA;
	}

	// The avcau struct may have been modified by mediaengine, write it back.
	sceAu.write(auAddr);

	if (Memory::IsValidAddress(attrAddr)) {
		Memory::Write_U32(1, attrAddr);
	}

	DEBUG_LOG(HLE, "%x=sceMpegGetAvcAu(%08x, %08x, %08x, %08x)", result, mpeg, streamId, auAddr, attrAddr);
	// TODO: sceMpegGetAvcAu seems to modify esSize, and delay when it's > 1000 or something.
	// There's definitely more to it, but ultimately it seems games should expect it to delay randomly.
	return hleDelayResult(result, "mpeg get avc", 100);
}

u32 sceMpegFinish()
{
	if (!isMpegInit)
	{
		WARN_LOG(HLE, "sceMpegFinish(...): not initialized");
		// TODO: Need to properly hook module load/unload for this to work right.
		//return ERROR_MPEG_NOT_YET_INIT;
	} else {
		INFO_LOG(HLE, "sceMpegFinish(...)");
	}
	isMpegInit = false;
	//__MpegFinish();
	return hleDelayResult(0, "mpeg finish", 250);
}

u32 sceMpegQueryMemSize()
{
	DEBUG_LOG(HLE, "sceMpegQueryMemSize()");
	return 0x10000;	// 64K
}

int sceMpegGetAtracAu(u32 mpeg, u32 streamId, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegGetAtracAu(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, streamId, auAddr, attrAddr);
		return -1;
	}

	SceMpegRingBuffer mpegRingbuffer;
	Memory::ReadStruct(ctx->mpegRingbufferAddr, &mpegRingbuffer);

	SceMpegAu sceAu;
	sceAu.read(auAddr);

	auto streamInfo = ctx->streamMap.find(streamId);
	if (streamInfo != ctx->streamMap.end() && streamInfo->second.needsReset)
	{
		sceAu.pts = 0;
		streamInfo->second.needsReset = false;
	}

	// The audio can end earlier than the video does.
	if (mpegRingbuffer.packetsFree == mpegRingbuffer.packets || (ctx->mediaengine->IsAudioEnd() && !ctx->mediaengine->IsVideoEnd())) {
		DEBUG_LOG(HLE, "PSP_ERROR_MPEG_NO_DATA=sceMpegGetAtracAu(%08x, %08x, %08x, %08x)", mpeg, streamId, auAddr, attrAddr);
		// TODO: Does this really delay?
		return hleDelayResult(PSP_ERROR_MPEG_NO_DATA, "mpeg get atrac", mpegDecodeErrorDelayMs);
	}

	int result = 0;

	sceAu.pts = ctx->mediaengine->getAudioTimeStamp() + ctx->mpegFirstTimestamp;
	if (ctx->mediaengine->IsVideoEnd()) {
		INFO_LOG(HLE, "video end reach. pts: %i dts: %i", (int)sceAu.pts, (int)ctx->mediaengine->getLastTimeStamp());
		mpegRingbuffer.packetsFree = mpegRingbuffer.packets;
		Memory::WriteStruct(ctx->mpegRingbufferAddr, &mpegRingbuffer);

		result = PSP_ERROR_MPEG_NO_DATA;
	}
	sceAu.write(auAddr);


	if (Memory::IsValidAddress(attrAddr)) {
		Memory::Write_U32(0, attrAddr);
	}

	DEBUG_LOG(HLE, "%x=sceMpegGetAtracAu(%08x, %08x, %08x, %08x)", result, mpeg, streamId, auAddr, attrAddr);
	// TODO: Not clear on exactly when this delays.
	return hleDelayResult(result, "mpeg get atrac", 100);
}

int sceMpegQueryPcmEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (Memory::IsValidAddress(esSizeAddr) && Memory::IsValidAddress(outSizeAddr)) {
		DEBUG_LOG(HLE, "sceMpegQueryPcmEsSize(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);
		Memory::Write_U32(MPEG_PCM_ES_SIZE, esSizeAddr);
		Memory::Write_U32(MPEG_PCM_ES_OUTPUT_SIZE, outSizeAddr);
		return 0;
	}

	ERROR_LOG(HLE, "sceMpegQueryPcmEsSize - bad pointers(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);
	return -1;
}


u32 sceMpegChangeGetAuMode(u32 mpeg, int streamUid, int mode)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegChangeGetAuMode(%08x, %i, %i): bad mpeg handle", mpeg, streamUid, mode);
		return -1;
	}

	// NOTE: Where is the info supposed to come from?
	StreamInfo info = {0};
	info.sid = streamUid;
	if (info.sid) {
		switch (info.type) {
		case MPEG_AVC_STREAM:
			if(mode == MPEG_AU_MODE_DECODE) {
				ctx->ignoreAvc = false;
			} else if (mode == MPEG_AU_MODE_SKIP) {
				ctx->ignoreAvc = true;
			}
			break;
		case MPEG_AUDIO_STREAM:
		case MPEG_ATRAC_STREAM:
			if(mode == MPEG_AU_MODE_DECODE) {
				ctx->ignoreAtrac = false;
			} else if (mode == MPEG_AU_MODE_SKIP) {
				ctx->ignoreAtrac = true;
			}
			break;
		case MPEG_PCM_STREAM:
			if(mode == MPEG_AU_MODE_DECODE) {
				ctx->ignorePcm = false;
			} else if (mode == MPEG_AU_MODE_SKIP) {
				ctx->ignorePcm = true;
			}
			break;
		default:
			ERROR_LOG(HLE, "UNIMPL sceMpegChangeGetAuMode(%08x, %i): unkown streamID", mpeg, streamUid);
			break;
		}
	} else {
			ERROR_LOG(HLE, "UNIMPL sceMpegChangeGetAuMode(%08x, %i): unkown streamID", mpeg, streamUid);
	}
	return 0;
}

u32 sceMpegChangeGetAvcAuMode(u32 mpeg, u32 stream_addr, int mode)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegChangeGetAvcAuMode(%08x, %08x, %i)", mpeg, stream_addr, mode);
	return 0;
}

u32 sceMpegGetPcmAu(u32 mpeg, int streamUid, u32 auAddr, u32 attrAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegGetPcmAu(%08x, %i, %08x, %08x)", mpeg, streamUid, auAddr, attrAddr);
	return 0;
}

u32 sceMpegRingbufferQueryPackNum(int memorySize)
{
	ERROR_LOG(HLE, "sceMpegRingbufferQueryPackNum(%i)", memorySize);
	int packets = memorySize / (2048 + 104);
	return packets;
}

u32 sceMpegFlushAllStream(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegFlushAllStream(%08x): bad mpeg handle", mpeg);
		return -1;
	}
	WARN_LOG(HLE, "UNIMPL sceMpegFlushAllStream(%08x)", mpeg);

	ctx->isAnalyzed = false;

	if (Memory::IsValidAddress(ctx->mpegRingbufferAddr))
	{
		auto ringbuffer = Memory::GetStruct<SceMpegRingBuffer>(ctx->mpegRingbufferAddr);

		ringbuffer->packetsFree = ringbuffer->packets;
		ringbuffer->packetsRead = 0;
		ringbuffer->packetsWritten = 0;
	}

	return 0;
}

u32 sceMpegFlushStream(u32 mpeg, int stream_addr)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegFlushStream(%08x, %i)", mpeg , stream_addr);
	//__MpegFinish();
	return 0;
}

u32 sceMpegAvcCopyYCbCr(u32 mpeg, u32 sourceAddr, u32 YCbCrAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcCopyYCbCr(%08x, %08x, %08x)", mpeg, sourceAddr, YCbCrAddr);
	return 0;
}

u32 sceMpegAtracDecode(u32 mpeg, u32 auAddr, u32 bufferAddr, int init)
{
	DEBUG_LOG(HLE, "UNIMPL sceMpegAtracDecode(%08x, %08x, %08x, %i)", mpeg, auAddr, bufferAddr, init);

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return 0;
	}

	if (!Memory::IsValidAddress(auAddr) || !Memory::IsValidAddress(bufferAddr)) {
		ERROR_LOG(HLE, "sceMpegAtracDecode: bad addresses");
		return 0;
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);

	Memory::Memset(bufferAddr, 0, MPEG_ATRAC_ES_OUTPUT_SIZE);
	ctx->mediaengine->getAudioSamples(Memory::GetPointer(bufferAddr));
	avcAu.pts = ctx->mediaengine->getAudioTimeStamp() + ctx->mpegFirstTimestamp;

	avcAu.write(auAddr);


	return hleDelayResult(0, "mpeg atrac decode", atracDecodeDelayMs);
	//hleEatMicro(4000);
	//return hleDelayResult(0, "mpeg atrac decode", 200);
}

// YCbCr -> RGB color space conversion
u32 sceMpegAvcCsc(u32 mpeg, u32 sourceAddr, u32 rangeAddr, int frameWidth, u32 destAddr)
{
	DEBUG_LOG(HLE, "sceMpegAvcCsc(%08x, %08x, %08x, %i, %08x)", mpeg, sourceAddr, rangeAddr, frameWidth, destAddr);
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx)
		return -1;
	if ((!Memory::IsValidAddress(rangeAddr)) || (!Memory::IsValidAddress(destAddr)))
		return -1;
	int x  = Memory::Read_U32(rangeAddr);
	int y = Memory::Read_U32(rangeAddr + 4);
	int width    = Memory::Read_U32(rangeAddr + 8);
	int height   = Memory::Read_U32(rangeAddr + 12);
	int destSize = ctx->mediaengine->writeVideoImageWithRange(Memory::GetPointer(destAddr), frameWidth, ctx->videoPixelMode, 
		x, y, width, height);

	gpu->InvalidateCache(destAddr, destSize, GPU_INVALIDATE_SAFE);
	return 0;
}

u32 sceMpegRingbufferDestruct(u32 ringbufferAddr)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferDestruct(%08x)", ringbufferAddr);

	if (Memory::IsValidAddress(ringbufferAddr))
	{
		auto ringbuffer = Memory::GetStruct<SceMpegRingBuffer>(ringbufferAddr);

		ringbuffer->packetsFree = ringbuffer->packets;
		ringbuffer->packetsRead = 0;
		ringbuffer->packetsWritten = 0;
	}
	return 0;
}

u32 sceMpegAvcInitYCbCr(u32 mpeg, int mode, int width, int height, u32 ycbcr_addr)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcInitYCbCr(%08x, %i, %i, %i, %08x)", mpeg, mode, width, height, ycbcr_addr);
	return 0;
}

int sceMpegAvcQueryYCbCrSize(u32 mpeg, u32 mode, u32 width, u32 height, u32 resultAddr)
{
	if ((width & 15) != 0 || (height & 15) != 0 || height > 272 || width > 480)
	{
		ERROR_LOG(HLE, "sceMpegAvcQueryYCbCrSize: bad w/h %i x %i", width, height);
		return ERROR_MPEG_INVALID_VALUE;
	}
	DEBUG_LOG(HLE, "sceMpegAvcQueryYCbCrSize(%08x, %i, %i, %i, %08x)", mpeg, mode, width, height, resultAddr);

	int size = (width / 2) * (height / 2) * 6 + 128;
	Memory::Write_U32(size, resultAddr);
	return 0;
}

u32 sceMpegQueryUserdataEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (Memory::IsValidAddress(esSizeAddr) && Memory::IsValidAddress(outSizeAddr)) {
		DEBUG_LOG(HLE, "sceMpegQueryUserdataEsSize(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);
		Memory::Write_U32(MPEG_DATA_ES_SIZE, esSizeAddr);
		Memory::Write_U32(MPEG_DATA_ES_OUTPUT_SIZE, outSizeAddr);
		return 0;
	}

	ERROR_LOG(HLE, "sceMpegQueryUserdataEsSize - bad pointers(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);
	return -1;
}

u32 sceMpegAvcResourceGetAvcDecTopAddr(u32 mpeg)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcResourceGetAvcDecTopAddr(%08x)", mpeg);
// it's just a random address
	return 0x12345678;
}

u32 sceMpegAvcResourceFinish(u32 mpeg)
{
	DEBUG_LOG(HLE,"sceMpegAvcResourceFinish(%08x)", mpeg);
	return 0;
}

u32 sceMpegAvcResourceGetAvcEsBuf(u32 mpeg)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcResourceGetAvcEsBuf(%08x)", mpeg);
	return 0;
}

u32 sceMpegAvcResourceInit(u32 mpeg)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcResourceInit(%08x)", mpeg);
    if (mpeg != 1) {
      	return ERROR_MPEG_INVALID_VALUE;
	}
	return 0;
}


int sceMpegAvcConvertToYuv420(u32 mpeg, u32 bufferOutput, u32 unknown1, int unknown2)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcConvertToYuv420(%08x, %08x, %08x, %08x)", mpeg, bufferOutput, unknown1, unknown2);
	return 0;
}

int sceMpegGetUserdataAu(u32 mpeg, u32 streamUid, u32 auAddr, u32 resultAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegGetUserdataAu(%08x, %08x, %08x, %08x)", mpeg, streamUid, auAddr, resultAddr);

	// TODO: Are these at all right?  Seen in Phantasy Star Portable 2.
	Memory::Write_U32(0, resultAddr);
	Memory::Write_U32(0, resultAddr + 4);

	// We currently can't demux userdata so this seems like the best thing to return in the meantime..
	// Then we probably shouldn't do the above writes? but it works...
	return ERROR_MPEG_NO_DATA;
}

const HLEFunction sceMpeg[] =
{
	{0xe1ce83a7,WrapI_UUUU<sceMpegGetAtracAu>,"sceMpegGetAtracAu"},
	{0xfe246728,WrapI_UUUU<sceMpegGetAvcAu>,"sceMpegGetAvcAu"},
	{0xd8c5f121,WrapU_UUUUUUU<sceMpegCreate>,"sceMpegCreate"},
	{0xf8dcb679,WrapI_UUU<sceMpegQueryAtracEsSize>,"sceMpegQueryAtracEsSize"},
	{0xc132e22f,WrapU_V<sceMpegQueryMemSize>,"sceMpegQueryMemSize"},
	{0x21ff80e4,WrapI_UUU<sceMpegQueryStreamOffset>,"sceMpegQueryStreamOffset"},
	{0x611e9e11,WrapU_UU<sceMpegQueryStreamSize>,"sceMpegQueryStreamSize"},
	{0x42560f23,WrapI_UUU<sceMpegRegistStream>,"sceMpegRegistStream"},
	{0x591a4aa2,WrapU_UI<sceMpegUnRegistStream>,"sceMpegUnRegistStream"},
	{0x707b7629,WrapU_U<sceMpegFlushAllStream>,"sceMpegFlushAllStream"},
	{0x500F0429,WrapU_UI<sceMpegFlushStream>,"sceMpegFlushStream"},
	{0xa780cf7e,WrapI_U<sceMpegMallocAvcEsBuf>,"sceMpegMallocAvcEsBuf"},
	{0xceb870b1,WrapI_UI<sceMpegFreeAvcEsBuf>,"sceMpegFreeAvcEsBuf"},
	{0x167afd9e,WrapI_UUU<sceMpegInitAu>,"sceMpegInitAu"},
	{0x682a619b,WrapU_V<sceMpegInit>,"sceMpegInit"},
	{0x606a4649,WrapI_U<sceMpegDelete>,"sceMpegDelete"},
	{0x874624d6,WrapU_V<sceMpegFinish>,"sceMpegFinish"},
	{0x800c44df,WrapU_UUUI<sceMpegAtracDecode>,"sceMpegAtracDecode"},
	{0x0e3c2e9d,&WrapU_UUUUU<sceMpegAvcDecode>,"sceMpegAvcDecode"},
	{0x740fccd1,&WrapU_UUUU<sceMpegAvcDecodeStop>,"sceMpegAvcDecodeStop"},
	{0x4571cc64,&WrapU_U<sceMpegAvcDecodeFlush>,"sceMpegAvcDecodeFlush"},
	{0x0f6c18d7,&WrapI_UU<sceMpegAvcDecodeDetail>,"sceMpegAvcDecodeDetail"},
	{0xa11c7026,WrapI_UU<sceMpegAvcDecodeMode>,"sceMpegAvcDecodeMode"},
	{0x37295ed8,WrapU_UUUUUU<sceMpegRingbufferConstruct>,"sceMpegRingbufferConstruct"},
	{0x13407f13,WrapU_U<sceMpegRingbufferDestruct>,"sceMpegRingbufferDestruct"},
	{0xb240a59e,WrapU_UUU<sceMpegRingbufferPut>,"sceMpegRingbufferPut"},
	{0xb5f6dc87,WrapI_U<sceMpegRingbufferAvailableSize>,"sceMpegRingbufferAvailableSize"},
	{0xd7a29f46,WrapU_I<sceMpegRingbufferQueryMemSize>,"sceMpegRingbufferQueryMemSize"},
	{0x769BEBB6,WrapU_I<sceMpegRingbufferQueryPackNum>,"sceMpegRingbufferQueryPackNum"},
	{0x211a057c,WrapI_UUUUU<sceMpegAvcQueryYCbCrSize>,"sceMpegAvcQueryYCbCrSize"},
	{0xf0eb1125,WrapI_UUUU<sceMpegAvcDecodeYCbCr>,"sceMpegAvcDecodeYCbCr"},
	{0xf2930c9c,WrapU_UUU<sceMpegAvcDecodeStopYCbCr>,"sceMpegAvcDecodeStopYCbCr"},
	{0x67179b1b,WrapU_UIIIU<sceMpegAvcInitYCbCr>,"sceMpegAvcInitYCbCr"},
	{0x0558B075,WrapU_UUU<sceMpegAvcCopyYCbCr>,"sceMpegAvcCopyYCbCr"},
	{0x31bd0272,WrapU_UUUIU<sceMpegAvcCsc>,"sceMpegAvcCsc"},
	{0x9DCFB7EA,WrapU_UII<sceMpegChangeGetAuMode>,"sceMpegChangeGetAuMode"},
	{0x8C1E027D,WrapU_UIUU<sceMpegGetPcmAu>,"sceMpegGetPcmAu"},
	{0xC02CF6B5,WrapI_UUU<sceMpegQueryPcmEsSize>,"sceMpegQueryPcmEsSize"},
	{0xC45C99CC,WrapU_UUU<sceMpegQueryUserdataEsSize>,"sceMpegQueryUserdataEsSize"},
	{0x234586AE,WrapU_UUI<sceMpegChangeGetAvcAuMode>,"sceMpegChangeGetAvcAuMode"},
	{0x63B9536A,WrapU_U<sceMpegAvcResourceGetAvcDecTopAddr>,"sceMpegAvcResourceGetAvcDecTopAddr"},
	{0x8160a2fe,WrapU_U<sceMpegAvcResourceFinish>,"sceMpegAvcResourceFinish"},
	{0xaf26bb01,WrapU_U<sceMpegAvcResourceGetAvcEsBuf>,"sceMpegAvcResourceGetAvcEsBuf"},
	{0xfcbdb5ad,WrapU_U<sceMpegAvcResourceInit>,"sceMpegAvcResourceInit"},
	{0xF5E7EA31,WrapI_UUUI<sceMpegAvcConvertToYuv420>,"sceMpegAvcConvertToYuv420"},
	{0x01977054,WrapI_UUUU<sceMpegGetUserdataAu>,"sceMpegGetUserdataAu"},
	{0x3c37a7a6,0,"sceMpegNextAvcRpAu"},
	{0x11f95cf1,0,"sceMpegGetAvcNalAu"},
	{0xab0e9556,0,"sceMpegAvcDecodeDetailIndex"},
	{0xcf3547a2,0,"sceMpegAvcDecodeDetail2"},
	{0x921fcccf,0,"sceMpegGetAvcEsAu"},
};

void Register_sceMpeg()
{
	RegisterModule("sceMpeg", ARRAY_SIZE(sceMpeg), sceMpeg);
}
