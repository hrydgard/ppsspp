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
#include "../../Common/Action.h"

static bool useMediaEngine;

// MPEG AVC elementary stream.
static const int MPEG_AVC_ES_SIZE = 2048;          // MPEG packet size.
// MPEG ATRAC elementary stream.
static const int MPEG_ATRAC_ES_SIZE = 2112;
static const int MPEG_ATRAC_ES_OUTPUT_SIZE = 8192;
// MPEG PCM elementary stream.
static const int MPEG_PCM_ES_SIZE = 320;
static const int MPEG_PCM_ES_OUTPUT_SIZE = 320;
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
static const int MPEG_MEMSIZE = 0x10000;          // 64k.

static const int MPEG_AVC_DECODE_SUCCESS = 1;       // Internal value.
static const int MPEG_AVC_DECODE_ERROR_FATAL = -8;

static const int atracDecodeDelay = 3000;         // Microseconds
static const int avcDecodeDelay = 5400;           // Microseconds
static const int mpegDecodeErrorDelay = 100;      // Delay in Microseconds in case of decode error
static const int mpegTimestampPerSecond = 90000; // How many MPEG Timestamp units in a second.
//static const int videoTimestampStep = 3003;      // Value based on pmfplayer (mpegTimestampPerSecond / 29.970 (fps)).
static const int audioTimestampStep = 4180;      // For audio play at 44100 Hz (2048 samples / 44100 * mpegTimestampPerSecond == 4180)
//static const int audioFirstTimestamp = 89249;    // The first MPEG audio AU has always this timestamp
static const int audioFirstTimestamp = 90000;    // The first MPEG audio AU has always this timestamp
static const s64 UNKNOWN_TIMESTAMP = -1;

// At least 2048 bytes of MPEG data is provided when analysing the MPEG header
static const int MPEG_HEADER_BUFFER_MINIMUM_SIZE = 2048;

static const int NUM_ES_BUFFERS = 2;

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

struct StreamInfo
{
	int type;
	int num;
};

typedef std::map<u32, StreamInfo> StreamInfoMap;

// Internal structure
struct MpegContext {
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
		p.Do(isAnalyzed);
		p.Do<StreamInfo>(streamMap);
		mediaengine->DoState(p);
		p.DoMarker("MpegContext");
	}

	u32 defaultFrameWidth;
	int videoFrameCount;
	int audioFrameCount;
	bool endOfAudioReached;
	bool endOfVideoReached;
	int videoPixelMode;
	u32 mpegMagic;
	u32 mpegVersion;
	u32 mpegRawVersion;
	u32 mpegOffset;
	u32 mpegStreamSize;
	u32 mpegFirstTimestamp;
	u32 mpegLastTimestamp;
	u32 mpegFirstDate;
	u32 mpegLastDate;
	u32 mpegRingbufferAddr;
	u32 mpegStreamAddr;
	bool esBuffers[NUM_ES_BUFFERS];
	AvcContext avc;

	bool avcRegistered;
	bool atracRegistered;
	bool pcmRegistered;

	bool isAnalyzed;

	StreamInfoMap streamMap;
	MediaEngine *mediaengine;
};

static u32 streamIdGen;
static bool isCurrentMpegAnalyzed;
static bool fakeMode;
static int actionPostPut;
static std::map<u32, MpegContext *> mpegMap;
// TODO: Remove.
static u32 lastMpegHandle = 0;

MpegContext *getMpegCtx(u32 mpegAddr) {
	u32 mpeg = Memory::Read_U32(mpegAddr);

	// TODO: Remove.
	if (mpegMap.find(mpeg) == mpegMap.end())
	{
		ERROR_LOG(HLE, "Bad mpeg handle %08x - using last one (%08x) instead", mpeg, lastMpegHandle);
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
	buf->packetsFree = 0; // set later
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
	ctx->mpegFirstTimestamp = bswap32(Memory::Read_U32(buffer_addr + PSMF_FIRST_TIMESTAMP_OFFSET));
	ctx->mpegLastTimestamp = bswap32(Memory::Read_U32(buffer_addr + PSMF_LAST_TIMESTAMP_OFFSET));
	ctx->mpegFirstDate = convertTimestampToDate(ctx->mpegFirstTimestamp);
	ctx->mpegLastDate = convertTimestampToDate(ctx->mpegLastTimestamp);
	ctx->avc.avcDetailFrameWidth = (Memory::Read_U8(buffer_addr + 142) * 0x10);
	ctx->avc.avcDetailFrameHeight = (Memory::Read_U8(buffer_addr + 143) * 0x10);
	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;
	ctx->avc.avcFrameStatus = 0;

	//if (!isCurrentMpegAnalyzed) {
	//SceMpegRingBuffer ringbuffer;
	//InitRingbuffer(&ringbuffer, 0, 0, 0, 0, 0);
	// ????
	//Memory::WriteStruct(ctx->mpegRingbufferAddr, &ringbuffer);
	//}

	ctx->videoFrameCount = 0;
	ctx->audioFrameCount = 0;
	ctx->endOfAudioReached = false;
	ctx->endOfVideoReached = false;

	if ((ctx->mpegStreamSize > 0) && !ctx->isAnalyzed) {
		ctx->mediaengine->setFakeMode(fakeMode);
		ctx->mediaengine->init(buffer_addr, ctx->mpegStreamSize, ctx->mpegOffset);
		ctx->mediaengine->setVideoDim(ctx->avc.avcDetailFrameWidth, ctx->avc.avcDetailFrameHeight);
		// mysterious?
		//meChannel = new PacketChannel();
		//meChannel.write(buffer_addr, mpegOffset);
	}
	// When used with scePsmf, some applications attempt to use sceMpegQueryStreamOffset
	// and sceMpegQueryStreamSize, which forces a packet overwrite in the Media Engine and in
	// the MPEG ringbuffer.
	// Mark the current MPEG as analyzed to filter this, and restore it at sceMpegFinish.
	ctx->isAnalyzed = true;

	INFO_LOG(ME, "Stream offset: %d, Stream size: 0x%X", ctx->mpegOffset, ctx->mpegStreamSize);
	INFO_LOG(ME, "First timestamp: %d, Last timestamp: %d", ctx->mpegFirstTimestamp, ctx->mpegLastTimestamp);
}

class PostPutAction : public Action {
public:
	PostPutAction() {}
	void setRingAddr(u32 ringAddr) { ringAddr_ = ringAddr; }
	static Action *Create() { return new PostPutAction; }
	void DoState(PointerWrap &p) { p.Do(ringAddr_); p.DoMarker("PostPutAction"); }
	void run();
private:
	u32 ringAddr_;
};

void __MpegInit(bool useMediaEngine_) {
	lastMpegHandle = 0;
	streamIdGen = 1;
	fakeMode = !useMediaEngine_;
	isCurrentMpegAnalyzed = false;
	actionPostPut = __KernelRegisterActionType(PostPutAction::Create);
}

void __MpegDoState(PointerWrap &p) {
	p.Do(lastMpegHandle);
	p.Do(streamIdGen);
	p.Do(fakeMode);
	p.Do(isCurrentMpegAnalyzed);
	p.Do(actionPostPut);
	__KernelRestoreActionType(actionPostPut, PostPutAction::Create);

	int n = (int) mpegMap.size();
	p.Do(n);
	if (p.mode == p.MODE_READ) {
		std::map<u32, MpegContext *>::iterator it, end;
		for (it = mpegMap.begin(), end = mpegMap.end(); it != end; ++it) {
			delete it->second->mediaengine;
			delete it->second;
		}
		mpegMap.clear();

		for (int i = 0; i < n; ++i) {
			u32 key;
			p.Do(key);
			MpegContext *ctx = new MpegContext;
			ctx->mediaengine = new MediaEngine;
			ctx->DoState(p);
			mpegMap[key] = ctx;
		}
	} else {
		std::map<u32, MpegContext *>::iterator it, end;
		for (it = mpegMap.begin(), end = mpegMap.end(); it != end; ++it) {
			p.Do(it->first);
			it->second->DoState(p);
		}
	}

	p.DoMarker("sceMpeg");
}

void __MpegShutdown() {
	std::map<u32, MpegContext *>::iterator it, end;
	for (it = mpegMap.begin(), end = mpegMap.end(); it != end; ++it) {
		delete it->second->mediaengine;
		delete it->second;
	}
	mpegMap.clear();
}

void sceMpegInit()
{
	WARN_LOG(HLE, "sceMpegInit()");

	RETURN(0);
}

u32 sceMpegRingbufferQueryMemSize(int packets)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferQueryMemSize(%i)", packets);
	return packets * (104 + 2048);
}

u32 sceMpegRingbufferConstruct(u32 ringbufferAddr, u32 numPackets, u32 data, u32 size, u32 callbackAddr, u32 callbackArg)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferConstruct(%08x, %i, %08x, %i, %08x, %i)",
		ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
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
	Memory::ReadStruct(ringbufferAddr, &ringbuffer);
	if (ringbuffer.packetSize == 0) {
		ringbuffer.packetsFree = 0;
	} else {
		ringbuffer.packetsFree = (ringbuffer.dataUpperBound - ringbuffer.data) / ringbuffer.packetSize;
	}
	ringbuffer.mpeg = mpegAddr;
	Memory::WriteStruct(ringbufferAddr, &ringbuffer);

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
	ctx->videoPixelMode = 0;
	ctx->avcRegistered = false;
	ctx->atracRegistered = false;
	ctx->pcmRegistered = false;
	ctx->defaultFrameWidth = frameWidth;
	for (int i = 0; i < NUM_ES_BUFFERS; i++) {
		ctx->esBuffers[i] = false;
	}

	// Detailed "analysis" is done later in Query* for some reason.
	ctx->isAnalyzed = false;
	ctx->mediaengine = new MediaEngine();

	INFO_LOG(HLE, "%08x=sceMpegCreate(%08x, %08x, %i, %08x, %i, %i, %i)",
		mpegHandle, mpegAddr, dataPtr, size, ringbufferAddr, frameWidth, mode, ddrTop);
	return 0;
}

int sceMpegDelete(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegDelete(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	DEBUG_LOG(HLE, "sceMpegDelete(%08x)", mpeg);

	delete ctx->mediaengine;
	delete ctx;
	mpegMap.erase(mpeg);

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
		ctx->videoPixelMode = pixelMode;
		return 0;
	} else {
		WARN_LOG(HLE, "sceMpegAvcDecodeMode(%08x, %08x): invalid modeAddr", mpeg, modeAddr);
		return -1;
	}
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
		return -1; //ERROR_MPEG_INVALID_VALUE
	} else if (ctx->mpegVersion < 0) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad version");
		return -1; //ERROR_MPEG_BAD_VERSION
	} else if ((ctx->mpegOffset & 2047) != 0 || ctx->mpegOffset == 0) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad offset");
		return -1; //ERROR_MPEG_INVALID_VALUE
	}
	Memory::Write_U32(ctx->mpegOffset, offsetAddr);
	return 0;
}

u32 sceMpegQueryStreamSize(u32 bufferAddr, u32 sizeAddr)
{
	DEBUG_LOG(HLE, "sceMpegQueryStreamSize(%08x, %08x)", bufferAddr, sizeAddr);

	MpegContext temp;
	temp.mediaengine = new MediaEngine();

	AnalyzeMpeg(bufferAddr, &temp);

	delete temp.mediaengine;

	if (temp.mpegMagic != PSMF_MAGIC) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad PSMF magic");
		return ERROR_MPEG_INVALID_VALUE;
	} else if (temp.mpegVersion < 0) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad version");
		return ERROR_MPEG_BAD_VERSION;
	} else if ((temp.mpegOffset & 2047) != 0 || temp.mpegOffset == 0) {
		ERROR_LOG(HLE, "sceMpegQueryStreamOffset: Bad offset");
		return ERROR_MPEG_INVALID_VALUE;
	}
	Memory::Write_U32(temp.mpegStreamSize, sizeAddr);
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

	DEBUG_LOG(HLE, "sceMpegRegistStream(%08x, %i, %i)", mpeg, streamType, streamNum);

	switch (streamType) {
	case MPEG_AVC_STREAM:
		ctx->avcRegistered = true;
		break;
	case MPEG_AUDIO_STREAM:
	case MPEG_ATRAC_STREAM:
		ctx->atracRegistered = true;
		break;
	case MPEG_PCM_STREAM:
		ctx->pcmRegistered = true;
		break;
	}
	// ...
	u32 sid = streamIdGen++;
	StreamInfo info;
	info.type = streamType;
	info.num = streamNum;
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
	Memory::ReadStruct(auAddr, &avcAu);

	SceMpegRingBuffer ringbuffer;
	Memory::ReadStruct(ctx->mpegRingbufferAddr, &ringbuffer);

	if (ringbuffer.packetsRead == 0) {
		// empty!
		return MPEG_AVC_DECODE_ERROR_FATAL;
	}

	u32 buffer = Memory::Read_U32(bufferAddr);
	u32 init = Memory::Read_U32(initAddr);
	DEBUG_LOG(HLE, "*buffer = %08x, *init = %08x", buffer, init);

	const int width = std::min((int)frameWidth, 480);
	const int height = ctx->avc.avcDetailFrameHeight;

	int packetsInRingBuffer = ringbuffer.packets - ringbuffer.packetsFree;
	int processedPackets = ringbuffer.packetsRead - packetsInRingBuffer;
	int processedSize = processedPackets * ringbuffer.packetSize;

	int packetsConsumed = 3;
	if (ctx->mpegStreamSize > 0 && ctx->mpegLastTimestamp > 0) {
		// Try a better approximation of the packets consumed based on the timestamp
		int processedSizeBasedOnTimestamp = (int) ((((float) avcAu.pts) / ctx->mpegLastTimestamp) * ctx->mpegStreamSize);
		if (processedSizeBasedOnTimestamp < processedSize) {
			packetsConsumed = 0;
		} else {
			packetsConsumed = (processedSizeBasedOnTimestamp - processedSize) / ringbuffer.packetSize;
			if (packetsConsumed > 10) {
				packetsConsumed = 10;
			}
		}
		DEBUG_LOG(HLE, "sceMpegAvcDecode consumed %d %d/%d %d", processedSizeBasedOnTimestamp, processedSize, ctx->mpegStreamSize, packetsConsumed);
	}

	if (ctx->mediaengine->stepVideo()) {
		ctx->mediaengine->writeVideoImage(buffer, frameWidth, ctx->videoPixelMode);
		packetsConsumed += ctx->mediaengine->readLength() / ringbuffer.packetSize;

		// The MediaEngine is already consuming all the remaining
		// packets when approaching the end of the video. The PSP
		// is only consuming the last packet when reaching the end,
		// not before.
		// Consuming all the remaining packets?
		if (ringbuffer.packetsFree + packetsConsumed >= ringbuffer.packets) {
			// Having not yet reached the last timestamp?
			if (ctx->mpegLastTimestamp > 0 && avcAu.pts < ctx->mpegLastTimestamp) {
				// Do not yet consume all the remaining packets.
				packetsConsumed = 0;
			}
		}
		ctx->mediaengine->setReadLength(ctx->mediaengine->readLength() - packetsConsumed * ringbuffer.packetSize);
	} else {
		// Consume all remaining packets
		packetsConsumed = ringbuffer.packets - ringbuffer.packetsFree;
	}
	ctx->avc.avcFrameStatus = 1;
	ctx->videoFrameCount++;

	// Update the ringbuffer with the consumed packets
	if (ringbuffer.packetsFree < ringbuffer.packets && packetsConsumed > 0) {
		ringbuffer.packetsFree = std::min(ringbuffer.packets, ringbuffer.packetsFree + packetsConsumed);
		DEBUG_LOG(HLE, "sceMpegAvcDecode consumed %d packets, remaining %d packets", packetsConsumed, ringbuffer.packets - ringbuffer.packetsFree);
	}

	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;

	// Flush structs back to memory
	Memory::WriteStruct(auAddr, &avcAu);
	Memory::WriteStruct(ctx->mpegRingbufferAddr, &ringbuffer);

	Memory::Write_U32(ctx->avc.avcFrameStatus, initAddr);  // 1 = showing, 0 = not showing

	DEBUG_LOG(HLE, "sceMpegAvcDecode(%08x, %08x, %i, %08x, %08x)", mpeg, auAddr, frameWidth, bufferAddr, initAddr);

	return 0;
}

u32 sceMpegAvcDecodeStop(u32 mpeg, u32 frameWidth, u32 bufferAddr, u32 statusAddr)
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeStop(%08x, %08x, %08x, statusAddr=%08x)",
		mpeg, frameWidth, bufferAddr, statusAddr);

	return 0;
}

void sceMpegUnRegistStream()
{
	WARN_LOG(HLE, "HACK sceMpegUnRegistStream(...)");
	RETURN(0);
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

void sceMpegAvcDecodeStopYCbCr()
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeStopYCbCr(...)");
	RETURN(0);
}

int sceMpegAvcDecodeYCbCr(u32 mpeg, u32 auAddr, u32 bufferAddr, u32 initAddr)
{
	WARN_LOG(HLE, "HACK sceMpegAvcDecodeYCbCr(%08x, %08x, %08x, %08x)", mpeg, auAddr, bufferAddr, initAddr);
	return 0;
}

u32 sceMpegAvcDecodeFlush(u32 mpeg)
{
	ERROR_LOG(HLE, "UNIMPL sceMpegAvcDecodeFlush(%08x)", mpeg);
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
	Memory::ReadStruct(auPointer, &sceAu);

	if (bufferAddr >= 1 && bufferAddr <= NUM_ES_BUFFERS && ctx->esBuffers[bufferAddr - 1]) {
		// This esbuffer has been allocated for Avc.
		sceAu.esBuffer = bufferAddr;   // Can this be right??? not much of a buffer pointer..
		sceAu.esSize = MPEG_AVC_ES_SIZE;
		sceAu.dts = 0;
		sceAu.pts = 0;

		Memory::WriteStruct(auPointer, &sceAu);
	} else {
		// This esbuffer has been left as Atrac.
		sceAu.esBuffer = bufferAddr;
		sceAu.esSize = MPEG_ATRAC_ES_SIZE;
		sceAu.pts = 0;
		sceAu.dts = UNKNOWN_TIMESTAMP;

		Memory::WriteStruct(auPointer, &sceAu);
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
	if (!Memory::IsValidAddress(ringbufferAddr)) {
		ERROR_LOG(HLE, "sceMpegRingbufferAvailableSize(%08x) - bad address", ringbufferAddr);
		return -1;
	}

	SceMpegRingBuffer ringbuffer;
	Memory::ReadStruct(ringbufferAddr, &ringbuffer);
	DEBUG_LOG(HLE, "%i=sceMpegRingbufferAvailableSize(%08x)", ringbuffer.packetsFree, ringbufferAddr);
	return ringbuffer.packetsFree;
}




void PostPutAction::run() {
	SceMpegRingBuffer ringbuffer;
	Memory::ReadStruct(ringAddr_, &ringbuffer);

	MpegContext *ctx = getMpegCtx(ringbuffer.mpeg);

	int packetsAdded = currentMIPS->r[2];
	if (packetsAdded > 0) {
		if (ctx)
			ctx->mediaengine->feedPacketData(ringbuffer.data, packetsAdded * ringbuffer.packetSize);
		if (packetsAdded > ringbuffer.packetsFree) {
			WARN_LOG(HLE, "sceMpegRingbufferPut clamping packetsAdded old=%i new=%i", packetsAdded, ringbuffer.packetsFree);
			packetsAdded = ringbuffer.packetsFree;
		}
		ringbuffer.packetsRead += packetsAdded;
		ringbuffer.packetsWritten += packetsAdded;
		ringbuffer.packetsFree -= packetsAdded;
	}

	Memory::WriteStruct(ringAddr_, &ringbuffer);
}


// Program signals that it has written data to the ringbuffer and gets a callback ?
u32 sceMpegRingbufferPut(u32 ringbufferAddr, u32 numPackets, u32 available)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferPut(%08x, %i, %i)", ringbufferAddr, numPackets, available);
	if (numPackets < 0) {
		ERROR_LOG(HLE, "sub-zero number of packets put");
		return 0;
	}

	SceMpegRingBuffer ringbuffer;
	Memory::ReadStruct(ringbufferAddr, &ringbuffer);

	numPackets = std::min(numPackets, available);

	MpegContext *ctx = getMpegCtx(ringbuffer.mpeg);
	if (!ctx) {
		WARN_LOG(HLE, "sceMpegRingbufferPut(%08x, %i, %i): bad mpeg handle %08x", ringbufferAddr, numPackets, available, ringbuffer.mpeg);
		return 0;
	}

	// Clamp to length of mpeg stream - this seems like a hack as we don't have access to the context here really
	int mpegStreamPackets = (ctx->mpegStreamSize + ringbuffer.packetSize - 1) / ringbuffer.packetSize;
	int remainingPackets = mpegStreamPackets - ringbuffer.packetsRead;
	if (remainingPackets < 0) {
		remainingPackets = 0;
	}
	numPackets = std::min(numPackets, (u32)remainingPackets);

	// Execute callback function as a direct MipsCall, no blocking here so no messing around with wait states etc
	if (ringbuffer.callback_addr) {
		PostPutAction *action = (PostPutAction *) __KernelCreateAction(actionPostPut);
		action->setRingAddr(ringbufferAddr);
		u32 args[3] = {ringbuffer.data, numPackets, ringbuffer.callback_args};
		__KernelDirectMipsCall(ringbuffer.callback_addr, action, false, args, 3, false);
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
	DEBUG_LOG(HLE, "sceMpegGetAvcAu(%08x, %08x, %08x, %08x)", mpeg, streamId, auAddr, attrAddr);

	SceMpegRingBuffer mpegRingbuffer;
	Memory::ReadStruct(ctx->mpegRingbufferAddr, &mpegRingbuffer);

	SceMpegAu sceAu;
	Memory::ReadStruct(auAddr, &sceAu);

	if (mpegRingbuffer.packetsRead == 0) {
		// delayThread(mpegErrorDecodeDelay)
		return -1;   // ERROR_MPEG_NO_DATA
	}

	if (ctx->streamMap.find(streamId) == ctx->streamMap.end())
	{
		ERROR_LOG(HLE, "sceMpegGetAvcAu - bad stream id %i", streamId);
		return -1;
	}

	// Wait for audio if too much ahead
	if (ctx->atracRegistered && (sceAu.pts > sceAu.pts + getMaxAheadTimestamp(mpegRingbuffer)))
	{
		ERROR_LOG(HLE, "sceMpegGetAvcAu - video too much ahead");
		return -1;  // MPEG_NO_DATA
	}

	int result = 0;
	// read the au struct from ram
	if (!ctx->mediaengine->readVideoAu(&sceAu)) {
		if (ctx->mpegLastTimestamp < 0 || sceAu.pts >= ctx->mpegLastTimestamp) {
			DEBUG_LOG(HLE, "End of video reached");
			ctx->endOfVideoReached = true;
		} else {
			ctx->endOfAudioReached = false;
		}

		// The avcau struct may have been modified by mediaengine, write it back.
		Memory::WriteStruct(auAddr, &sceAu);
	}

	if (Memory::IsValidAddress(attrAddr)) {
		Memory::Write_U32(1, attrAddr);
	}

	return result;
}

void sceMpegFinish()
{
	WARN_LOG(HLE, "sceMpegFinish(...)");
	//__MpegFinish();
	RETURN(0);
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
	DEBUG_LOG(HLE, "sceMpegGetAtracAu(%08x, %08x, %08x, %08x)", mpeg, streamId, auAddr, attrAddr);

	SceMpegRingBuffer mpegRingbuffer;
	Memory::ReadStruct(ctx->mpegRingbufferAddr, &mpegRingbuffer);

	SceMpegAu sceAu;
	Memory::ReadStruct(auAddr, &sceAu);


	//...

	if (Memory::IsValidAddress(attrAddr)) {
		Memory::Write_U32(0, attrAddr);
	}

	return 0;
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


void sceMpegChangeGetAuMode()
{
	WARN_LOG(HLE, "HACK sceMpegChangeGetAuMode(...)");
	RETURN(0);
}

void sceMpegGetPcmAu()
{
	WARN_LOG(HLE, "HACK sceMpegGetPcmAu(...)");
	RETURN(0);
}

void sceMpegRingbufferQueryPackNum()
{
	WARN_LOG(HLE, "HACK sceMpegRingbufferQueryPackNum(...)");
	RETURN(0);
}

void sceMpegFlushAllStream()
{
	WARN_LOG(HLE, "HACK sceMpegFlushAllStream(...)");
	RETURN(0);
}

void sceMpegAvcCopyYCbCr()
{
	WARN_LOG(HLE, "HACK sceMpegAvcCopyYCbCr(...)");
	RETURN(0);
}

void sceMpegAtracDecode()
{
	WARN_LOG(HLE, "HACK sceMpegAtracDecode(...)");
	RETURN(0);
}

// YCbCr -> RGB color space conversion
void sceMpegAvcCsc()
{
	WARN_LOG(HLE, "HACK sceMpegAvcCsc(...)");
	RETURN(0);
}

u32 sceMpegRingbufferDestruct(u32 ringbufferAddr)
{
	DEBUG_LOG(HLE, "sceMpegRingbufferDestruct(%08x)", ringbufferAddr);
	// Don't need to do anything here
	return 0;
}

void sceMpegAvcInitYCbCr()
{
	WARN_LOG(HLE, "HACK sceMpegAvcInitYCbCr(...)");
	RETURN(0);
}

int sceMpegAvcQueryYCbCrSize(u32 mpeg, u32 mode, u32 width, u32 height, u32 resultAddr)
{
	if ((width & 15) != 0 || (height & 15) != 0 || height > 272 || width > 480)
	{
		ERROR_LOG(HLE, "sceMpegAvcQueryYCbCrSize: bad w/h %i x %i", width, height);
		return -1;
	}
	DEBUG_LOG(HLE, "sceMpegAvcQueryYCbCrSize(%08x, %i, %i, %i, %08x)", mpeg, mode, width, height, resultAddr);

	int size = (width / 2) * (height / 2) * 6 + 128;
	Memory::Write_U32(size, resultAddr);
	return 0;
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
	{0x591a4aa2,sceMpegUnRegistStream,"sceMpegUnRegistStream"},
	{0x707b7629,sceMpegFlushAllStream,"sceMpegFlushAllStream"},
	{0xa780cf7e,WrapI_U<sceMpegMallocAvcEsBuf>,"sceMpegMallocAvcEsBuf"},
	{0xceb870b1,WrapI_UI<sceMpegFreeAvcEsBuf>,"sceMpegFreeAvcEsBuf"},
	{0x167afd9e,WrapI_UUU<sceMpegInitAu>,"sceMpegInitAu"},
	{0x682a619b,sceMpegInit,"sceMpegInit"},
	{0x606a4649,WrapI_U<sceMpegDelete>,"sceMpegDelete"},
	{0x874624d6,sceMpegFinish,"sceMpegFinish"},
	{0x800c44df,sceMpegAtracDecode,"sceMpegAtracDecode"},
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
	{0x769BEBB6,sceMpegRingbufferQueryPackNum,"sceMpegRingbufferQueryPackNum"},
	{0x211a057c,WrapI_UUUUU<sceMpegAvcQueryYCbCrSize>,"sceMpegAvcQueryYCbCrSize"},
	{0xf0eb1125,WrapI_UUUU<sceMpegAvcDecodeYCbCr>,"sceMpegAvcDecodeYCbCr"},
	{0xf2930c9c,sceMpegAvcDecodeStopYCbCr,"sceMpegAvcDecodeStopYCbCr"},
	{0x67179b1b,sceMpegAvcInitYCbCr,"sceMpegAvcInitYCbCr"},
	{0x0558B075,sceMpegAvcCopyYCbCr,"sceMpegAvcCopyYCbCr"},
	{0x31bd0272,sceMpegAvcCsc,"sceMpegAvcCsc"},
	{0x9DCFB7EA,sceMpegChangeGetAuMode,"sceMpegChangeGetAuMode"},
	{0x8C1E027D,sceMpegGetPcmAu,"sceMpegGetPcmAu"},
	{0xC02CF6B5,WrapI_UUU<sceMpegQueryPcmEsSize>,"sceMpegQueryPcmEsSize"},
};

const HLEFunction sceMp3[] =
{
	{0x07EC321A,0,"sceMp3ReserveMp3Handle"},
	{0x0DB149F4,0,"sceMp3NotifyAddStreamData"},
	{0x2A368661,0,"sceMp3ResetPlayPosition"},
	{0x354D27EA,0,"sceMp3GetSumDecodedSample"},
	{0x35750070,0,"sceMp3InitResource"},
	{0x3C2FA058,0,"sceMp3TermResource"},
	{0x3CEF484F,0,"sceMp3SetLoopNum"},
	{0x44E07129,0,"sceMp3Init"},
	{0x732B042A,0,"sceMp3EndEntry"},
	{0x7F696782,0,"sceMp3GetMp3ChannelNum"},
	{0x87677E40,0,"sceMp3GetBitRate"},
	{0x87C263D1,0,"sceMp3GetMaxOutputSample"},
	{0x8AB81558,0,"sceMp3StartEntry"},
	{0x8F450998,0,"sceMp3GetSamplingRate"},
	{0xA703FE0F,0,"sceMp3GetInfoToAddStreamData"},
	{0xD021C0FB,0,"sceMp3Decode"},
	{0xD0A56296,0,"sceMp3CheckStreamDataNeeded"},
	{0xD8F54A51,0,"sceMp3GetLoopNum"},
	{0xF5478233,0,"sceMp3ReleaseMp3Handle"},
};

void Register_sceMpeg()
{
	RegisterModule("sceMpeg", ARRAY_SIZE(sceMpeg), sceMpeg);
}

void Register_sceMp3()
{
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
