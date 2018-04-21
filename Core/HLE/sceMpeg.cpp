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
#include <algorithm>
#include <memory>

#include "Common/Swap.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HW/MediaEngine.h"
#include "Core/Config.h"
#include "Core/MemMapHelpers.h"
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
static const int MPEG_DATA_ES_BUFFERS = 2;

// MPEG analysis results.
static const int MPEG_VERSION_0012 = 0;
static const int MPEG_VERSION_0013 = 1;
static const int MPEG_VERSION_0014 = 2;
static const int MPEG_VERSION_0015 = 3;

// PSMF analysis results.
static const int PSMF_VERSION_0012 = 0x32313030;
static const int PSMF_VERSION_0013 = 0x33313030;
static const int PSMF_VERSION_0014 = 0x34313030;
static const int PSMF_VERSION_0015 = 0x35313030;

// MPEG streams.
static const int MPEG_AVC_STREAM = 0;
static const int MPEG_ATRAC_STREAM = 1;
static const int MPEG_PCM_STREAM = 2;
static const int MPEG_DATA_STREAM = 3;      // Arbitrary user defined type. Can represent audio or video.
static const int MPEG_AUDIO_STREAM = 15;
static const int MPEG_AU_MODE_DECODE = 0;
static const int MPEG_AU_MODE_SKIP = 1;
static const u32 MPEG_MEMSIZE_0104 = 0x0b3DB;
static const u32 MPEG_MEMSIZE_0105 = 0x10000;     // 64k.
static const int MPEG_AVC_DECODE_SUCCESS = 1;     // Internal value.

static const int atracDecodeDelayMs = 3000;
static const int avcFirstDelayMs = 3600;
static const int avcCscDelayMs = 4000;
static const int avcDecodeDelayMs = 5400;         // Varies between 4700 and 6000.
static const int avcEmptyDelayMs = 320;
static const int mpegDecodeErrorDelayMs = 100;
static const int mpegTimestampPerSecond = 90000;  // How many MPEG Timestamp units in a second.
static const int videoTimestampStep = 3003;       // Value based on pmfplayer (mpegTimestampPerSecond / 29.970 (fps)).
static const int audioTimestampStep = 4180;       // For audio play at 44100 Hz (2048 samples / 44100 * mpegTimestampPerSecond == 4180)
static const int audioFirstTimestamp = 90000;     // The first MPEG audio AU has always this timestamp
static const int maxAheadTimestamp = 40000;
static const s64 UNKNOWN_TIMESTAMP = -1;

// At least 2048 bytes of MPEG data is provided when analysing the MPEG header
static const int MPEG_HEADER_BUFFER_MINIMUM_SIZE = 2048;

// For PMP media
static u32 pmp_videoSource = 0; //pointer to the video source (SceMpegLLi structure) 
static int pmp_nBlocks = 0; //number of blocks received in the last sceMpegbase_BEA18F91 call
static std::list<AVFrame*> pmp_queue; //list of pmp video frames have been decoded and will be played
static std::list<u32> pmp_ContextList; //list of pmp media contexts
static bool pmp_oldStateLoaded = false; // for dostate

#ifdef USE_FFMPEG 

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}
static AVPixelFormat pmp_want_pix_fmt;

#endif

struct SceMpegLLI
{
	u32 pSrc;
	u32 pDst;
	u32 Next;
	int iSize;
};

void SceMpegAu::read(u32 addr) {
	Memory::ReadStruct(addr, this);
	pts = (pts & 0xFFFFFFFFULL) << 32 | (((u64)pts) >> 32);
	dts = (dts & 0xFFFFFFFFULL) << 32 | (((u64)dts) >> 32);
}

void SceMpegAu::write(u32 addr) {
	pts = (pts & 0xFFFFFFFFULL) << 32 | (((u64)pts) >> 32);
	dts = (dts & 0xFFFFFFFFULL) << 32 | (((u64)dts) >> 32);
	Memory::WriteStruct(addr, this);
}

/*
// Currently unused
static int getMaxAheadTimestamp(const SceMpegRingBuffer &ringbuf) {
	return std::max(maxAheadTimestamp, 700 * ringbuf.packets);  // empiric value from JPCSP, thanks!
}
*/

const u8 defaultMpegheader[2048] = {0x50,0x53,0x4d,0x46,0x30,0x30,0x31,0x35,0x00,0x00,0x08,0x00,0x00,
	0x10,0xc8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x4e,0x00,
	0x00,0x00,0x01,0x5f,0x90,0x00,0x00,0x00,0x0d,0xbe,0xca,0x00,0x00,0x61,0xa8,0x00,0x01,0x5f,
	0x90,0x02,0x01,0x00,0x00,0x00,0x34,0x00,0x00,0x00,0x01,0x5f,0x90,0x00,0x00,0x00,0x0d,0xbe,
	0xca,0x00,0x01,0x00,0x00,0x00,0x22,0x00,0x02,0xe0,0x00,0x20,0xfb,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x1e,0x11,0x00,0x00,0xbd,0x00,0x20,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x02,0x02};

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
	MpegContext() : ringbufferNeedsReverse(false), mediaengine(nullptr) {
		memcpy(mpegheader, defaultMpegheader, 2048);
	}
	~MpegContext() {
		delete mediaengine;
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("MpegContext", 1, 2);
		if (!s)
			return;

		p.DoArray(mpegheader, 2048);
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
		p.DoArray(esBuffers, MPEG_DATA_ES_BUFFERS);
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
		ringbufferNeedsReverse = s < 2;
	}

	u8 mpegheader[2048];
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
	bool esBuffers[MPEG_DATA_ES_BUFFERS];
	AvcContext avc;

	bool avcRegistered;
	bool atracRegistered;
	bool pcmRegistered;
	bool dataRegistered;

	bool ignoreAtrac;
	bool ignorePcm;
	bool ignoreAvc;

	bool isAnalyzed;
	bool ringbufferNeedsReverse;

	StreamInfoMap streamMap;
	MediaEngine *mediaengine;
};

static bool isMpegInit;
static int mpegLibVersion;
static u32 streamIdGen;
static int actionPostPut;
static std::map<u32, MpegContext *> mpegMap;

static MpegContext *getMpegCtx(u32 mpegAddr) {
	if (!Memory::IsValidAddress(mpegAddr))
		return nullptr;
		
	u32 mpeg = Memory::Read_U32(mpegAddr);
	auto found = mpegMap.find(mpeg);
	if (found == mpegMap.end())
		return nullptr;

	MpegContext *res = found->second;
	// Take this opportunity to upgrade savestates if necessary.
	if (res->ringbufferNeedsReverse) {
		auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(res->mpegRingbufferAddr);
		ringbuffer->packetsAvail = ringbuffer->packets - ringbuffer->packetsAvail;
		res->ringbufferNeedsReverse = false;
	}
	return res;
}

static void InitRingbuffer(SceMpegRingBuffer *buf, int packets, int data, int size, int callback_addr, int callback_args) {
	buf->packets = packets;
	buf->packetsRead = 0;
	buf->packetsWritePos = 0;
	buf->packetsAvail = 0;
	buf->packetSize = 2048;
	buf->data = data;
	buf->callback_addr = callback_addr;
	buf->callback_args = callback_args;
	buf->dataUpperBound = data + packets * 2048;
	buf->semaID = 0;
	buf->mpeg = 0;
	// This isn't in ver 0104, but it is in 0105.
	if (mpegLibVersion >= 0x0105)
		buf->gp = __KernelGetModuleGP(__KernelGetCurThreadModuleId());
}

static u32 convertTimestampToDate(u32 ts) {
	return ts;  // TODO
}

static u32 getMpegVersion(u32 mpegRawVersion) {
	switch (mpegRawVersion) {
		case PSMF_VERSION_0012: return MPEG_VERSION_0012;
		case PSMF_VERSION_0013: return MPEG_VERSION_0013;
		case PSMF_VERSION_0014: return MPEG_VERSION_0014;
		case PSMF_VERSION_0015: return MPEG_VERSION_0015;
		default: return -1;
	}
}
static void AnalyzeMpeg(u8 *buffer, MpegContext *ctx) {
	ctx->mpegMagic = *(u32_le*)buffer;
	ctx->mpegRawVersion = *(u32_le*)(buffer + PSMF_STREAM_VERSION_OFFSET);
	ctx->mpegVersion = getMpegVersion(ctx->mpegRawVersion);
	ctx->mpegOffset = bswap32(*(u32_le*)(buffer + PSMF_STREAM_OFFSET_OFFSET));
	ctx->mpegStreamSize = bswap32(*(u32_le*)(buffer + PSMF_STREAM_SIZE_OFFSET));
	ctx->mpegFirstTimestamp = getMpegTimeStamp(buffer + PSMF_FIRST_TIMESTAMP_OFFSET);
	ctx->mpegLastTimestamp = getMpegTimeStamp(buffer + PSMF_LAST_TIMESTAMP_OFFSET);
	ctx->mpegFirstDate = convertTimestampToDate(ctx->mpegFirstTimestamp);
	ctx->mpegLastDate = convertTimestampToDate(ctx->mpegLastTimestamp);
	ctx->avc.avcDetailFrameWidth = (*(u8*)(buffer + 142)) * 0x10;
	ctx->avc.avcDetailFrameHeight = (*(u8*)(buffer + 143)) * 0x10;
	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;
	ctx->avc.avcFrameStatus = 0;
	ctx->videoFrameCount = 0;
	ctx->audioFrameCount = 0;
	ctx->endOfAudioReached = false;
	ctx->endOfVideoReached = false;
	
	// Sanity Check ctx->mpegFirstTimestamp
	if (ctx->mpegFirstTimestamp != 90000) {
		WARN_LOG_REPORT(ME, "Unexpected mpeg first timestamp: %llx / %lld", ctx->mpegFirstTimestamp, ctx->mpegFirstTimestamp);
	}
	
	if (ctx->mpegMagic != PSMF_MAGIC || ctx->mpegVersion < 0 ||
		(ctx->mpegOffset & 2047) != 0 || ctx->mpegOffset == 0) {
		// mpeg header is invalid!
		return;
	}

	if (ctx->mediaengine && (ctx->mpegStreamSize > 0) && !ctx->isAnalyzed) {
		// init mediaEngine
		auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
		if (ringbuffer.IsValid()) {
			ctx->mediaengine->loadStream(buffer, ctx->mpegOffset, ringbuffer->packets * ringbuffer->packetSize);
		} else {
			// TODO: Does this make any sense?
			ctx->mediaengine->loadStream(buffer, ctx->mpegOffset, 0);
		}
	}
	
	// When used with scePsmf, some applications attempt to use sceMpegQueryStreamOffset
	// and sceMpegQueryStreamSize, which forces a packet overwrite in the Media Engine and in
	// the MPEG ringbuffer.
	// Mark the current MPEG as analyzed to filter this, and restore it at sceMpegFinish.
	ctx->isAnalyzed = true;

	// copy header struct to mpeg header.
	memcpy(ctx->mpegheader, buffer, 2048);
	*(u32_le*)(ctx->mpegheader + PSMF_STREAM_OFFSET_OFFSET) = 0x80000;

	INFO_LOG(ME, "Stream offset: %d, Stream size: 0x%X", ctx->mpegOffset, ctx->mpegStreamSize);
	INFO_LOG(ME, "First timestamp: %lld, Last timestamp: %lld", ctx->mpegFirstTimestamp, ctx->mpegLastTimestamp);
}

class PostPutAction : public Action {
public:
	PostPutAction() {}
	void setRingAddr(u32 ringAddr) { ringAddr_ = ringAddr; }
	static Action *Create() { return new PostPutAction; }
	void DoState(PointerWrap &p) override {
		auto s = p.Section("PostPutAction", 1);
		if (!s)
			return;

		p.Do(ringAddr_);
	}
	void run(MipsCall &call) override;
private:
	u32 ringAddr_;
};

void __MpegInit() {
	isMpegInit = false;
	mpegLibVersion = 0x010A;
	streamIdGen = 1;
	actionPostPut = __KernelRegisterActionType(PostPutAction::Create);

#ifdef USE_FFMPEG
	avcodec_register_all();
	av_register_all();
#endif
}

void __MpegDoState(PointerWrap &p) {
	auto s = p.Section("sceMpeg", 1, 2);
	if (!s)
		return;

	if (s < 2) {
		int oldLastMpeg = -1;
		bool oldIsMpegAnalyzed = false;
		p.Do(oldLastMpeg);
		p.Do(streamIdGen);
		p.Do(oldIsMpegAnalyzed);
		// Let's assume the oldest version.
		mpegLibVersion = 0x0101;
	} else {
		p.Do(streamIdGen);
		p.Do(mpegLibVersion);
	}
	p.Do(isMpegInit);
	p.Do(actionPostPut);
	__KernelRestoreActionType(actionPostPut, PostPutAction::Create);

	p.Do(mpegMap);
}

void __MpegShutdown() {
	std::map<u32, MpegContext *>::iterator it, end;
	for (it = mpegMap.begin(), end = mpegMap.end(); it != end; ++it) {
		delete it->second;
	}
	mpegMap.clear();
}

void __MpegLoadModule(int version) {
	mpegLibVersion = version;
}

static u32 sceMpegInit() {
	if (isMpegInit) {
		WARN_LOG(ME, "sceMpegInit(): already initialized");
		// TODO: Need to properly hook module load/unload for this to work right.
		//return ERROR_MPEG_ALREADY_INIT;
	} else {
		INFO_LOG(ME, "sceMpegInit()");
	}
	isMpegInit = true;
	return hleDelayResult(0, "mpeg init", 750);
}

static u32 __MpegRingbufferQueryMemSize(int packets) {
	return packets * (104 + 2048);
}

static u32 sceMpegRingbufferQueryMemSize(int packets) {
	u32 size = __MpegRingbufferQueryMemSize(packets);
	DEBUG_LOG(ME, "%i = sceMpegRingbufferQueryMemSize(%i)", size, packets);
	return size;
}


static u32 sceMpegRingbufferConstruct(u32 ringbufferAddr, u32 numPackets, u32 data, u32 size, u32 callbackAddr, u32 callbackArg) {
	if (!Memory::IsValidAddress(ringbufferAddr)) {
		ERROR_LOG_REPORT(ME, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): bad ringbuffer, should crash", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	if ((int)size < 0) {
		ERROR_LOG_REPORT(ME, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): invalid size", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
		return ERROR_MPEG_NO_MEMORY;
	}

	if (__MpegRingbufferQueryMemSize(numPackets) > size) {
		if (numPackets < 0x00100000) {
			ERROR_LOG_REPORT(ME, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): too many packets for buffer", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
			return ERROR_MPEG_NO_MEMORY;
		} else {
			// The PSP's firmware allows some cases here, due to a bug in its validation.
			ERROR_LOG_REPORT(ME, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): too many packets for buffer, bogus size", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
		}
	}

	DEBUG_LOG(ME, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x)", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
	auto ring = PSPPointer<SceMpegRingBuffer>::Create(ringbufferAddr);
	InitRingbuffer(ring, numPackets, data, size, callbackAddr, callbackArg);
	return 0;
}

static u32 MpegRequiredMem() {
	if (mpegLibVersion < 0x0105) {
		return MPEG_MEMSIZE_0104;
	}
	return MPEG_MEMSIZE_0105;
}

static u32 sceMpegCreate(u32 mpegAddr, u32 dataPtr, u32 size, u32 ringbufferAddr, u32 frameWidth, u32 mode, u32 ddrTop)
{
	if (!Memory::IsValidAddress(mpegAddr)) {
		WARN_LOG(ME, "sceMpegCreate(%08x, %08x, %i, %08x, %i, %i, %i): invalid addresses", mpegAddr, dataPtr, size, ringbufferAddr, frameWidth, mode, ddrTop);
		return -1;
	}

	if (size < MpegRequiredMem()) {
		WARN_LOG(ME, "ERROR_MPEG_NO_MEMORY=sceMpegCreate(%08x, %08x, %i, %08x, %i, %i, %i)", mpegAddr, dataPtr, size, ringbufferAddr, frameWidth, mode, ddrTop);
		return ERROR_MPEG_NO_MEMORY;
	}

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ringbufferAddr);
	if (ringbuffer.IsValid()) {
		if (ringbuffer->packetSize == 0) {
			ringbuffer->packetsAvail = 0;
		} else {
			ringbuffer->packetsAvail = ringbuffer->packets - (ringbuffer->dataUpperBound - ringbuffer->data) / ringbuffer->packetSize;
		}
		ringbuffer->mpeg = mpegAddr;
	}

	// Generate, and write mpeg handle into mpeg data, for some reason
	int mpegHandle = dataPtr + 0x30;
	Memory::Write_U32(mpegHandle, mpegAddr);

	// Initialize fake mpeg struct.
	Memory::Memcpy(mpegHandle, "LIBMPEG\0", 8);
	Memory::Memcpy(mpegHandle + 8, "001\0", 4);
	Memory::Write_U32(-1, mpegHandle + 12);
	if (ringbuffer.IsValid()) {
		Memory::Write_U32(ringbufferAddr, mpegHandle + 16);
		Memory::Write_U32(ringbuffer->dataUpperBound, mpegHandle + 20);
	}
	MpegContext *ctx = new MpegContext;
	if (mpegMap.find(mpegHandle) != mpegMap.end()) {
		WARN_LOG_REPORT(HLE, "Replacing existing mpeg context at %08x", mpegAddr);
		// Otherwise, it would leak.
		delete mpegMap[mpegHandle];
	}
	mpegMap[mpegHandle] = ctx;

	// Initialize mpeg values.
	ctx->mpegRingbufferAddr = ringbufferAddr;
	ctx->videoFrameCount = 0;
	ctx->audioFrameCount = 0;
	ctx->videoPixelMode = GE_CMODE_32BIT_ABGR8888; // TODO: What's the actual default?
	ctx->avcRegistered = false;
	ctx->atracRegistered = false;
	ctx->pcmRegistered = false;
	ctx->dataRegistered = false;
	ctx->ignoreAtrac = false;
	ctx->ignorePcm = false;
	ctx->ignoreAvc = false;
	ctx->defaultFrameWidth = frameWidth;
	for (int i = 0; i < MPEG_DATA_ES_BUFFERS; i++) {
		ctx->esBuffers[i] = false;
	}

	// Detailed "analysis" is done later in Query* for some reason.
	ctx->isAnalyzed = false;
	ctx->mediaengine = new MediaEngine();

	INFO_LOG(ME, "%08x=sceMpegCreate(%08x, %08x, %i, %08x, %i, %i, %i)", mpegHandle, mpegAddr, dataPtr, size, ringbufferAddr, frameWidth, mode, ddrTop);
	return hleDelayResult(0, "mpeg create", 29000);
}

static int sceMpegDelete(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegDelete(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegDelete(%08x)", mpeg);

	delete ctx;
	mpegMap.erase(Memory::Read_U32(mpeg));

	return hleDelayResult(0, "mpeg delete", 40000);
}


static int sceMpegAvcDecodeMode(u32 mpeg, u32 modeAddr)
{
	if (!Memory::IsValidAddress(modeAddr)) {
		WARN_LOG(ME, "sceMpegAvcDecodeMode(%08x, %08x): invalid addresses", mpeg, modeAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAvcDecodeMode(%08x, %08x): bad mpeg handle", mpeg, modeAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegAvcDecodeMode(%08x, %08x)", mpeg, modeAddr);

	int mode = Memory::Read_U32(modeAddr);
	int pixelMode = Memory::Read_U32(modeAddr + 4);
	if (pixelMode >= GE_CMODE_16BIT_BGR5650 && pixelMode <= GE_CMODE_32BIT_ABGR8888) {
		ctx->videoPixelMode = pixelMode;
	} else {
		ERROR_LOG(ME, "sceMpegAvcDecodeMode(%i, %i): unknown pixelMode ", mode, pixelMode);
	}
	return 0;
}

static int sceMpegQueryStreamOffset(u32 mpeg, u32 bufferAddr, u32 offsetAddr)
{
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(offsetAddr)) {
		ERROR_LOG(ME, "sceMpegQueryStreamOffset(%08x, %08x, %08x): invalid addresses", mpeg, bufferAddr, offsetAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegQueryStreamOffset(%08x, %08x, %08x): bad mpeg handle", mpeg, bufferAddr, offsetAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegQueryStreamOffset(%08x, %08x, %08x)", mpeg, bufferAddr, offsetAddr);

	// Kinda destructive, no?
	AnalyzeMpeg(Memory::GetPointer(bufferAddr), ctx);

	if (ctx->mpegMagic != PSMF_MAGIC) {
		ERROR_LOG(ME, "sceMpegQueryStreamOffset: Bad PSMF magic");
		Memory::Write_U32(0, offsetAddr);
		return ERROR_MPEG_INVALID_VALUE;
	} else if (ctx->mpegVersion < 0) {
		ERROR_LOG(ME, "sceMpegQueryStreamOffset: Bad version");
		Memory::Write_U32(0, offsetAddr);
		return ERROR_MPEG_BAD_VERSION;
	} else if ((ctx->mpegOffset & 2047) != 0 || ctx->mpegOffset == 0) {
		ERROR_LOG(ME, "sceMpegQueryStreamOffset: Bad offset");
		Memory::Write_U32(0, offsetAddr);
		return ERROR_MPEG_INVALID_VALUE;
	}

	Memory::Write_U32(ctx->mpegOffset, offsetAddr);
	return 0;
}

static u32 sceMpegQueryStreamSize(u32 bufferAddr, u32 sizeAddr)
{
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(sizeAddr)) {
		ERROR_LOG(ME, "sceMpegQueryStreamSize(%08x, %08x): invalid addresses", bufferAddr, sizeAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegQueryStreamSize(%08x, %08x)", bufferAddr, sizeAddr);

	MpegContext ctx;
	ctx.mediaengine = 0;

	AnalyzeMpeg(Memory::GetPointer(bufferAddr), &ctx);

	if (ctx.mpegMagic != PSMF_MAGIC) {
		ERROR_LOG(ME, "sceMpegQueryStreamSize: Bad PSMF magic");
		Memory::Write_U32(0, sizeAddr);
		return ERROR_MPEG_INVALID_VALUE;
	} else if ((ctx.mpegOffset & 2047) != 0 ) {
		ERROR_LOG(ME, "sceMpegQueryStreamSize: Bad offset");
		Memory::Write_U32(0, sizeAddr);
		return ERROR_MPEG_INVALID_VALUE;
	}

	Memory::Write_U32(ctx.mpegStreamSize, sizeAddr);
	return 0;
}

static int sceMpegRegistStream(u32 mpeg, u32 streamType, u32 streamNum)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegRegistStream(%08x, %i, %i): bad mpeg handle", mpeg, streamType, streamNum);
		return -1;
	}

	INFO_LOG(ME, "sceMpegRegistStream(%08x, %i, %i)", mpeg, streamType, streamNum);

	switch (streamType) {
	case MPEG_AVC_STREAM:
		ctx->avcRegistered = true;
		// TODO: Probably incorrect?
		ctx->mediaengine->setVideoStream(streamNum);
		break;
	case MPEG_AUDIO_STREAM:
	case MPEG_ATRAC_STREAM:
		ctx->atracRegistered = true;
		// TODO: Probably incorrect?
		ctx->mediaengine->setAudioStream(streamNum);
		break;
	case MPEG_PCM_STREAM:
		ctx->pcmRegistered = true;
		break;
	case MPEG_DATA_STREAM:
		ctx->dataRegistered = true;
		break;
	default : 
		DEBUG_LOG(ME, "sceMpegRegistStream(%i) : unknown stream type", streamType);
		break;
	}
	// ...
	u32 sid = streamIdGen++;
	StreamInfo info;
	info.type = streamType;
	info.num = streamNum;
	info.sid = sid;
	info.needsReset = true;
	ctx->streamMap[sid] = info;
	return sid;
}

static int sceMpegMallocAvcEsBuf(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegMallocAvcEsBuf(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegMallocAvcEsBuf(%08x)", mpeg);

	// Doesn't actually malloc, just keeps track of a couple of flags
	for (int i = 0; i < MPEG_DATA_ES_BUFFERS; i++) {
		if (!ctx->esBuffers[i]) {
			ctx->esBuffers[i] = true;
			return i + 1;
		}
	}
	// No es buffer
	return 0;
}

static int sceMpegFreeAvcEsBuf(u32 mpeg, int esBuf)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegFreeAvcEsBuf(%08x, %i): bad mpeg handle", mpeg, esBuf);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegFreeAvcEsBuf(%08x, %i)", mpeg, esBuf);

	if (esBuf == 0) {
		return ERROR_MPEG_INVALID_VALUE;
	}

	if (esBuf >= 1 && esBuf <= MPEG_DATA_ES_BUFFERS) {
		// TODO: Check if it's already been free'd?
		ctx->esBuffers[esBuf - 1] = false;
	}
	return 0;
}

// check the existence of pmp media context 
static bool isContextExist(u32 ctxAddr){
	for (auto it = pmp_ContextList.begin(); it != pmp_ContextList.end(); ++it){
		if (*it == ctxAddr){
			return true;
		}
	}
	return false;
}

// Initialize Pmp video parameters and decoder.
static bool InitPmp(MpegContext * ctx){
#ifdef USE_FFMPEG
	InitFFmpeg();
	auto mediaengine = ctx->mediaengine;
	mediaengine->m_isVideoEnd = false;
	mediaengine->m_firstTimeStamp = 0;
	mediaengine->m_lastTimeStamp = 0;
	ctx->mpegFirstTimestamp = 0;
	ctx->mpegLastTimestamp = 0;

	// wanted output pixel format
	// reference values for pix_fmt:
	// GE_CMODE_16BIT_BGR5650 <--> AV_PIX_FMT_BGR565LE 
	// GE_CMODE_16BIT_ABGR5551 <--> AV_PIX_FMT_BGR555LE;
	// GE_CMODE_16BIT_ABGR4444 <--> AV_PIX_FMT_BGR444LE;
	// GE_CMODE_32BIT_ABGR8888 <--> AV_PIX_FMT_RGBA;
	pmp_want_pix_fmt = AV_PIX_FMT_RGBA;

	// Create H264 video codec
	AVCodec * pmp_Codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (pmp_Codec == NULL){
		ERROR_LOG(ME, "Can not find H264 codec, please update ffmpeg");
		return false;
	}

	// Create CodecContext
	AVCodecContext * pmp_CodecCtx = avcodec_alloc_context3(pmp_Codec);
	if (pmp_CodecCtx == NULL){
		ERROR_LOG(ME, "Can not allocate pmp Codec Context");
		return false;
	}

	// each pmp video context is corresponding to one pmp video codec
	mediaengine->m_pCodecCtxs[0] = pmp_CodecCtx;

	// initialize H264 video parameters
	// set pmp video size. Better to get from pmp file directly if possible. Any idea?
	pmp_CodecCtx->width = 480;
	pmp_CodecCtx->height = 272;
	mediaengine->m_desHeight = pmp_CodecCtx->height;
	mediaengine->m_desWidth = pmp_CodecCtx->width;

	// Open pmp video codec
	if (avcodec_open2(pmp_CodecCtx, pmp_Codec, NULL) < 0){
		ERROR_LOG(ME, "Can not open pmp video codec");
		return false;
	}

	// initialize ctx->mediaengine->m_pFrame and ctx->mediaengine->m_pFrameRGB
	if (!mediaengine->m_pFrame){
		mediaengine->m_pFrame = av_frame_alloc();
	}
	if (!mediaengine->m_pFrameRGB){
		mediaengine->m_pFrameRGB = av_frame_alloc();
	}

	// get RGBA picture buffer
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
	mediaengine->m_bufSize = av_image_get_buffer_size(pmp_want_pix_fmt, pmp_CodecCtx->width, pmp_CodecCtx->height, 1);
#else
	mediaengine->m_bufSize = avpicture_get_size(pmp_want_pix_fmt, pmp_CodecCtx->width, pmp_CodecCtx->height);
#endif
	mediaengine->m_buffer = (u8*)av_malloc(mediaengine->m_bufSize);

	return true;
#else
	// we can not play pmp video without ffmpeg
	return false;
#endif
}

// This class H264Frames is used for collecting small pieces of frames into larger frames for ffmpeg to decode
// Basically, this will avoid incomplete frame decoding issue and improve much better the video quality. 
class H264Frames{
public:
	int size;
	u8* stream;
	
	H264Frames() :size(0), stream(NULL){};
	
	H264Frames(u8* str, int sz) :size(sz){
		stream = new u8[size];
		memcpy(stream, str, size);
	};

	H264Frames(H264Frames *frame){
		size = frame->size;
		stream = new u8[size];
		memcpy(stream, frame->stream, size);
	};

	~H264Frames(){
		size = 0;
		if (stream){
			delete[] stream;
			stream = NULL;
		}
	};
	
	void add(H264Frames *p){
		add(p->stream, p->size);
	};

	void add(u8* str, int sz){
		int newsize = size + sz;
		u8* newstream = new u8[newsize];
		// join two streams
		memcpy(newstream, stream, size);
		memcpy(newstream + size, str, sz);
		// delete old stream
		delete[] stream;
		// replace with new stream
		stream = newstream;
		size = newsize;
	};

	void remove(int pos){
		// remove stream from begining to pos
		if (pos == 0){
			// nothing to remove
		}
		else if (pos >= size){
			// we remove all
			size = 0;
			if (stream){
				delete[] stream;
				stream = NULL;
			}
		}
		else{
			// we remove the front part
			size -= pos;
			u8* str = new u8[size];
			memcpy(str, stream + pos, size);
			delete[] stream;
			stream = str;
		}
	};
#ifndef USE_FFMPEG
#define AV_INPUT_BUFFER_PADDING_SIZE 16
#endif
#ifndef AV_INPUT_BUFFER_PADDING_SIZE
#define AV_INPUT_BUFFER_PADDING_SIZE FF_INPUT_BUFFER_PADDING_SIZE
#endif
	void addpadding(){
		u8* str = new u8[size + AV_INPUT_BUFFER_PADDING_SIZE];
		memcpy(str, stream, size);
		memset(str + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
		size += AV_INPUT_BUFFER_PADDING_SIZE;
		delete[] stream;
		stream = str;
	}
};

// collect pmp video frames
static H264Frames *pmpframes;

// decode pmp video to RGBA format
static bool decodePmpVideo(PSPPointer<SceMpegRingBuffer> ringbuffer, u32 pmpctxAddr){

#ifdef USE_FFMPEG
	// the current video is pmp iff pmp_videoSource is a valid addresse
	MpegContext* ctx = getMpegCtx(pmpctxAddr);
	if (Memory::IsValidAddress(pmp_videoSource)){
		// We should initialize pmp codec for each pmp context
		if (isContextExist(pmpctxAddr) == false){
			bool ret = InitPmp(ctx);
			if (!ret){
				ERROR_LOG(ME, "Pmp video initialization failed");
				return false;
			}
			// add the initialized context into ContextList
			pmp_ContextList.push_front(pmpctxAddr);
		}

		ringbuffer->packetsRead = pmp_nBlocks;

		MediaEngine* mediaengine = ctx->mediaengine;
		AVFrame* pFrame = mediaengine->m_pFrame;
		AVFrame* pFrameRGB = mediaengine->m_pFrameRGB;
		auto pCodecCtx = mediaengine->m_pCodecCtxs[0];

		// pmpframes could be destroied when close a video to load another one 
		if (!pmpframes)
			pmpframes = new H264Frames;

		// joint all blocks into H264Frames
		SceMpegLLI lli;
		for (int i = 0; i < pmp_nBlocks; i++){
			Memory::ReadStructUnchecked(pmp_videoSource, &lli);
			// add source block into pmpframes
			pmpframes->add(Memory::GetPointer(lli.pSrc), lli.iSize);
			// get next block
			pmp_videoSource += sizeof(SceMpegLLI);
		}

		pmpframes->addpadding();

		// initialize packet
		AVPacket packet;
		av_new_packet(&packet, pCodecCtx->width*pCodecCtx->height);

		// set packet to source block
		packet.data = pmpframes->stream;
		packet.size = pmpframes->size;

		// reuse pFrame and pFrameRGB
		int got_picture = 0;
		av_frame_unref(pFrame);
		av_frame_unref(pFrameRGB);

		// hook pFrameRGB output to buffer
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
		av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, mediaengine->m_buffer, pmp_want_pix_fmt, pCodecCtx->width, pCodecCtx->height, 1);
#else
		avpicture_fill((AVPicture *)pFrameRGB, mediaengine->m_buffer, pmp_want_pix_fmt, pCodecCtx->width, pCodecCtx->height);
#endif


		// decode video frame
		int len = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, &packet);
		DEBUG_LOG(ME, "got_picture %d", got_picture);
		if (got_picture){
			SwsContext *img_convert_ctx = NULL;
			img_convert_ctx = sws_getContext(
				pCodecCtx->width,
				pCodecCtx->height,
				pCodecCtx->pix_fmt,
				pCodecCtx->width,
				pCodecCtx->height,
				pmp_want_pix_fmt,
				SWS_BILINEAR,
				NULL, NULL, NULL);

			if (!img_convert_ctx) {
				ERROR_LOG(ME, "Cannot initialize sws conversion context");
				return false;
			}

			// Convert to RGBA
			int swsRet = sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data,
				pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
			if (swsRet < 0){
				ERROR_LOG(ME, "sws_scale: Error while converting %d", swsRet);
				return false;
			}
			// free sws context 
			sws_freeContext(img_convert_ctx);

			// update timestamp
			if (av_frame_get_best_effort_timestamp(mediaengine->m_pFrame) != AV_NOPTS_VALUE)
				mediaengine->m_videopts = av_frame_get_best_effort_timestamp(mediaengine->m_pFrame) + av_frame_get_pkt_duration(mediaengine->m_pFrame) - mediaengine->m_firstTimeStamp;
			else
				mediaengine->m_videopts += av_frame_get_pkt_duration(mediaengine->m_pFrame);

			// push the decoded frame into pmp_queue
			pmp_queue.push_back(pFrameRGB);

			// write frame into ppm file
			// SaveFrame(pNewFrameRGB, pCodecCtx->width, pCodecCtx->height);
		}
		// free some pointers
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
		av_packet_unref(&packet);
#else
		av_free_packet(&packet);
#endif
		pmpframes->~H264Frames();
		// must reset pmp_VideoSource address to zero after decoding. 
		pmp_videoSource = 0;
		return true;
	}
	// not a pmp video, return false
	return false;
#else
	return false;
#endif
}


void __VideoPmpInit() {
	pmp_oldStateLoaded = false;
	pmpframes = new H264Frames();
}

void __VideoPmpShutdown() {
#ifdef USE_FFMPEG
	// We need to empty pmp_queue to not leak memory.
	for (auto it = pmp_queue.begin(); it != pmp_queue.end(); ++it){
		av_free(*it);
	}
	pmp_queue.clear();
	pmp_ContextList.clear();
	delete pmpframes;
	pmpframes = NULL;
#endif
}

void __VideoPmpDoState(PointerWrap &p){
	auto s = p.Section("PMPVideo", 0, 1);
	if (!s) {
		if (p.mode == PointerWrap::MODE_READ)
			pmp_oldStateLoaded = true;
		return;
	}

	p.Do(pmp_videoSource);
	p.Do(pmp_nBlocks);
	if (p.mode == PointerWrap::MODE_READ){
		// for loadstate, we will reinitialize the pmp codec
		__VideoPmpShutdown();
	}
}

static u32 sceMpegAvcDecode(u32 mpeg, u32 auAddr, u32 frameWidth, u32 bufferAddr, u32 initAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAvcDecode(%08x, %08x, %d, %08x, %08x): bad mpeg handle", mpeg, auAddr, frameWidth, bufferAddr, initAddr);
		return -1;
	}

	if (frameWidth == 0) {  // wtf, go sudoku passes in 0xcccccccc
		if (!ctx->defaultFrameWidth) {
			frameWidth = ctx->avc.avcDetailFrameWidth;
		} else {
			frameWidth = ctx->defaultFrameWidth;
		}
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);
	
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		ERROR_LOG(ME, "Bogus mpegringbufferaddr");
		return -1;
	}
	
	u32 buffer = Memory::Read_U32(bufferAddr);
	u32 init = Memory::Read_U32(initAddr);
	DEBUG_LOG(ME, "video: bufferAddr = %08x, *buffer = %08x, *init = %08x", bufferAddr, buffer, init);

	// check and decode pmp video
	bool ispmp = false;
	if (decodePmpVideo(ringbuffer, mpeg)){
		DEBUG_LOG(ME, "Using ffmpeg to decode pmp video");
		ispmp = true;
	}

	if (ringbuffer->packetsRead == 0 || ctx->mediaengine->IsVideoEnd()) {
		WARN_LOG(ME, "sceMpegAvcDecode(%08x, %08x, %d, %08x, %08x): mpeg buffer empty", mpeg, auAddr, frameWidth, bufferAddr, initAddr);
		return hleDelayResult(ERROR_MPEG_AVC_DECODE_FATAL, "mpeg buffer empty", avcEmptyDelayMs);
	}

	s32 beforeAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;

	// We stored the video stream id here in sceMpegGetAvcAu().
	ctx->mediaengine->setVideoStream(avcAu.esBuffer);

	if (ispmp){
#ifdef USE_FFMPEG
		while (pmp_queue.size() != 0){
			// playing all pmp_queue frames
			ctx->mediaengine->m_pFrameRGB = pmp_queue.front();
			int bufferSize = ctx->mediaengine->writeVideoImage(buffer, frameWidth, ctx->videoPixelMode);
			gpu->NotifyVideoUpload(buffer, bufferSize, frameWidth, ctx->videoPixelMode);
			ctx->avc.avcFrameStatus = 1;
			ctx->videoFrameCount++;
			
			// free front frame
			hleDelayResult(0, "pmp video decode", 30);
			pmp_queue.pop_front();
		}
#endif
	}
	else if(ctx->mediaengine->stepVideo(ctx->videoPixelMode)) {
		int bufferSize = ctx->mediaengine->writeVideoImage(buffer, frameWidth, ctx->videoPixelMode);
		gpu->NotifyVideoUpload(buffer, bufferSize, frameWidth, ctx->videoPixelMode);
		ctx->avc.avcFrameStatus = 1;
		ctx->videoFrameCount++;
	} else {
		ctx->avc.avcFrameStatus = 0;
	}
	s32 afterAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;
	// Don't actually reset avail, we only change it by what was decoded.
	// Garbage frames can cause this to be incorrect, but some games expect that.
	if (mpegLibVersion <= 0x0103) {
		ringbuffer->packetsAvail += afterAvail - beforeAvail;
	} else {
		ringbuffer->packetsAvail = afterAvail;
	}

	avcAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;

	// Flush structs back to memory
	avcAu.write(auAddr);

	// Save the current frame's status to initAddr 
	Memory::Write_U32(ctx->avc.avcFrameStatus, initAddr);
	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;

	DEBUG_LOG(ME, "sceMpegAvcDecode(%08x, %08x, %i, %08x, %08x)", mpeg, auAddr, frameWidth, bufferAddr, initAddr);

	if (ctx->videoFrameCount <= 1)
		return hleDelayResult(0, "mpeg decode", avcFirstDelayMs);
	else
		return hleDelayResult(0, "mpeg decode", avcDecodeDelayMs);
	//hleEatMicro(3300);
	//return hleDelayResult(0, "mpeg decode", 200);
}

static u32 sceMpegAvcDecodeStop(u32 mpeg, u32 frameWidth, u32 bufferAddr, u32 statusAddr)
{
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(statusAddr)){
		ERROR_LOG(ME, "sceMpegAvcDecodeStop(%08x, %08x, %08x, %08x): invalid addresses", mpeg, frameWidth, bufferAddr, statusAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAvcDecodeStop(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, frameWidth, bufferAddr, statusAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegAvcDecodeStop(%08x, %08x, %08x, %08x)", mpeg, frameWidth, bufferAddr, statusAddr);

	// No last frame generated
	Memory::Write_U32(0, statusAddr);
	return 0;
}

static u32 sceMpegUnRegistStream(u32 mpeg, int streamUid)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegUnRegistStream(%08x, %i): bad mpeg handle", mpeg, streamUid);
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
		DEBUG_LOG(ME, "sceMpegUnRegistStream(%i) : unknown streamID ", streamUid);
		break;
	}
	ctx->streamMap[streamUid] = info;
	info.type = -1;
	info.sid = -1 ;
	info.needsReset = true;
	ctx->isAnalyzed = false;
	return 0;
}

static int sceMpegAvcDecodeDetail(u32 mpeg, u32 detailAddr)
{
	if (!Memory::IsValidAddress(detailAddr)){
		WARN_LOG(ME, "sceMpegAvcDecodeDetail(%08x, %08x): invalid addresses", mpeg, detailAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAvcDecodeDetail(%08x, %08x): bad mpeg handle", mpeg, detailAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegAvcDecodeDetail(%08x, %08x)", mpeg, detailAddr);

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

static u32 sceMpegAvcDecodeStopYCbCr(u32 mpeg, u32 bufferAddr, u32 statusAddr)
{
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(statusAddr)) {
		ERROR_LOG(ME, "UNIMPL sceMpegAvcDecodeStopYCbCr(%08x, %08x, %08x): invalid addresses", mpeg, bufferAddr, statusAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcDecodeStopYCbCr(%08x, %08x, %08x): bad mpeg handle", mpeg, bufferAddr, statusAddr);
		return -1;
	}

	ERROR_LOG(ME, "UNIMPL sceMpegAvcDecodeStopYCbCr(%08x, %08x, %08x)", mpeg, bufferAddr, statusAddr);
	Memory::Write_U32(0, statusAddr);
	return 0;
}

static int sceMpegAvcDecodeYCbCr(u32 mpeg, u32 auAddr, u32 bufferAddr, u32 initAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAvcDecodeYCbCr(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, auAddr, bufferAddr, initAddr);
		return -1;
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		ERROR_LOG(ME, "Bogus mpegringbufferaddr");
		return -1;
	}

	if (ringbuffer->packetsRead == 0 || ctx->mediaengine->IsVideoEnd()) {
		WARN_LOG(ME, "sceMpegAvcDecodeYCbCr(%08x, %08x, %08x, %08x): mpeg buffer empty", mpeg, auAddr, bufferAddr, initAddr);
		return hleDelayResult(ERROR_MPEG_AVC_DECODE_FATAL, "mpeg buffer empty", avcEmptyDelayMs);
	}

	s32 beforeAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;

	// We stored the video stream id here in sceMpegGetAvcAu().
	ctx->mediaengine->setVideoStream(avcAu.esBuffer);

	u32 buffer = Memory::Read_U32(bufferAddr);
	u32 init = Memory::Read_U32(initAddr);
	DEBUG_LOG(ME, "*buffer = %08x, *init = %08x", buffer, init);

	if (ctx->mediaengine->stepVideo(ctx->videoPixelMode)) {
		// Don't draw here, we'll draw in the Csc func.
		ctx->avc.avcFrameStatus = 1;
		ctx->videoFrameCount++;
	} else {
		ctx->avc.avcFrameStatus = 0;
	}
	s32 afterAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;
	// Don't actually reset avail, we only change it by what was decoded.
	// Garbage frames can cause this to be incorrect, but some games expect that.
	if (mpegLibVersion <= 0x0103) {
		ringbuffer->packetsAvail += afterAvail - beforeAvail;
	} else {
		ringbuffer->packetsAvail = afterAvail;
	}

	avcAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;

	// Flush structs back to memory
	avcAu.write(auAddr);

	// Save the current frame's status to initAddr 
	Memory::Write_U32(ctx->avc.avcFrameStatus, initAddr);
	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;

	DEBUG_LOG(ME, "sceMpegAvcDecodeYCbCr(%08x, %08x, %08x, %08x)", mpeg, auAddr, bufferAddr, initAddr);

	if (ctx->videoFrameCount <= 1)
		return hleDelayResult(0, "mpeg decode", avcFirstDelayMs);
	else
		return hleDelayResult(0, "mpeg decode", avcDecodeDelayMs);
	//hleEatMicro(3300);
	//return hleDelayResult(0, "mpeg decode", 200);
}

static u32 sceMpegAvcDecodeFlush(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcDecodeFlush(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG(ME, "UNIMPL sceMpegAvcDecodeFlush(%08x)", mpeg);
	if ( ctx->videoFrameCount > 0 || ctx->audioFrameCount > 0) {
		//__MpegFinish();
	}
	return 0;
}

static int sceMpegInitAu(u32 mpeg, u32 bufferAddr, u32 auPointer)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegInitAu(%08x, %i, %08x): bad mpeg handle", mpeg, bufferAddr, auPointer);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegInitAu(%08x, %i, %08x)", mpeg, bufferAddr, auPointer);

	SceMpegAu sceAu;
	sceAu.read(auPointer);

	if (bufferAddr >= 1 && bufferAddr <= (u32)MPEG_DATA_ES_BUFFERS && ctx->esBuffers[bufferAddr - 1]) {
		// This esbuffer has been allocated for Avc.
		// Default to 0, since we stuff the stream id in here.  Technically, we shouldn't.
		// TODO: Do something better to track the AU data.  This used to be bufferAddr.
		sceAu.esBuffer = 0;
		sceAu.esSize = MPEG_AVC_ES_SIZE;
		sceAu.dts = 0;
		sceAu.pts = 0;

		sceAu.write(auPointer);
	} else {
		// This esbuffer has been left as Atrac.
		// Default to 0, since we stuff the stream id in here.  Technically, we shouldn't.
		// TODO: Do something better to track the AU data.  This used to be bufferAddr.
		sceAu.esBuffer = 0;
		sceAu.esSize = MPEG_ATRAC_ES_SIZE;
		sceAu.pts = 0;
		sceAu.dts = UNKNOWN_TIMESTAMP;

		sceAu.write(auPointer);
	}
	return 0;
}

static int sceMpegQueryAtracEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (!Memory::IsValidAddress(esSizeAddr) || !Memory::IsValidAddress(outSizeAddr)) {
		ERROR_LOG(ME, "sceMpegQueryAtracEsSize(%08x, %08x, %08x): invalid addresses", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegQueryAtracEsSize(%08x, %08x, %08x): bad mpeg handle", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegQueryAtracEsSize(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);

	Memory::Write_U32(MPEG_ATRAC_ES_SIZE, esSizeAddr);
	Memory::Write_U32(MPEG_ATRAC_ES_OUTPUT_SIZE, outSizeAddr);
	return 0;
}

static int sceMpegRingbufferAvailableSize(u32 ringbufferAddr)
{
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ringbufferAddr);

	if (!ringbuffer.IsValid()) {
		ERROR_LOG(ME, "sceMpegRingbufferAvailableSize(%08x): invalid ringbuffer, should crash", ringbufferAddr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDRESS;
	}

	MpegContext *ctx = getMpegCtx(ringbuffer->mpeg);
	if (!ctx) {
		ERROR_LOG(ME, "sceMpegRingbufferAvailableSize(%08x): bad mpeg handle", ringbufferAddr);
		return ERROR_MPEG_NOT_YET_INIT;
	}

	ctx->mpegRingbufferAddr = ringbufferAddr;
	hleEatCycles(2020);
	hleReSchedule("mpeg ringbuffer avail");

	static int lastAvail = 0;
	if (lastAvail != ringbuffer->packetsAvail) {
		DEBUG_LOG(ME, "%i=sceMpegRingbufferAvailableSize(%08x)", ringbuffer->packets - ringbuffer->packetsAvail, ringbufferAddr);
		lastAvail = ringbuffer->packetsAvail;
	} else {
		VERBOSE_LOG(ME, "%i=sceMpegRingbufferAvailableSize(%08x)", ringbuffer->packets - ringbuffer->packetsAvail, ringbufferAddr);
	}
	return ringbuffer->packets - ringbuffer->packetsAvail;
}

void PostPutAction::run(MipsCall &call) {
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ringAddr_);

	MpegContext *ctx = getMpegCtx(ringbuffer->mpeg);
	int writeOffset = ringbuffer->packetsWritePos % (s32)ringbuffer->packets;
	const u8 *data = Memory::GetPointer(ringbuffer->data + writeOffset * 2048);

	int packetsAdded = currentMIPS->r[MIPS_REG_V0];

	// It seems validation is done only by older mpeg libs.
	if (mpegLibVersion < 0x0105 && packetsAdded > 0) {
		// TODO: Faster / less wasteful validation.
		std::unique_ptr<MpegDemux> demuxer(new MpegDemux(packetsAdded * 2048, 0));
		int readOffset = ringbuffer->packetsRead % (s32)ringbuffer->packets;
		const u8 *buf = Memory::GetPointer(ringbuffer->data + readOffset * 2048);
		bool invalid = false;
		for (int i = 0; i < packetsAdded; ++i) {
			demuxer->addStreamData(buf, 2048);
			buf += 2048;

			if (!demuxer->demux(0xFFFF)) {
				invalid = true;
			}
		}
		if (invalid) {
			// Bail out early - don't accept any of the packets, even the good ones.
			ERROR_LOG_REPORT(ME, "sceMpegRingbufferPut(): invalid mpeg data");
			call.setReturnValue(ERROR_MPEG_INVALID_VALUE);

			if (mpegLibVersion <= 0x0103) {
				// Act like they were actually added, but don't increment read pos.
				ringbuffer->packetsWritePos += packetsAdded;
				ringbuffer->packetsAvail += packetsAdded;
			}
			return;
		}
	}

	if (ringbuffer->packetsRead == 0 && ctx->mediaengine && packetsAdded > 0) {
		// init mediaEngine
		AnalyzeMpeg(ctx->mpegheader, ctx);
		ctx->mediaengine->loadStream(ctx->mpegheader, 2048, ringbuffer->packets * ringbuffer->packetSize);
	}
	if (packetsAdded > 0) {
		if (packetsAdded > ringbuffer->packets - ringbuffer->packetsAvail) {
			WARN_LOG(ME, "sceMpegRingbufferPut clamping packetsAdded old=%i new=%i", packetsAdded, ringbuffer->packets - ringbuffer->packetsAvail);
			packetsAdded = ringbuffer->packets - ringbuffer->packetsAvail;
		}
		int actuallyAdded = ctx->mediaengine == NULL ? 8 : ctx->mediaengine->addStreamData(data, packetsAdded * 2048) / 2048;
		if (actuallyAdded != packetsAdded) {
			WARN_LOG_REPORT(ME, "sceMpegRingbufferPut(): unable to enqueue all added packets, going to overwrite some frames.");
		}
		ringbuffer->packetsRead += packetsAdded;
		ringbuffer->packetsWritePos += packetsAdded;
		ringbuffer->packetsAvail += packetsAdded;
	}
	DEBUG_LOG(ME, "packetAdded: %i packetsRead: %i packetsTotal: %i", packetsAdded, ringbuffer->packetsRead, ringbuffer->packets);

	call.setReturnValue(packetsAdded);
}


// Program signals that it has written data to the ringbuffer and gets a callback ?
static u32 sceMpegRingbufferPut(u32 ringbufferAddr, int numPackets, int available)
{
	numPackets = std::min(numPackets, available);
	if (numPackets <= 0) {
		DEBUG_LOG(ME, "sceMpegRingbufferPut(%08x, %i, %i): no packets to enqueue", ringbufferAddr, numPackets, available);
		return 0;
	}

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ringbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior.
		ERROR_LOG_REPORT(ME, "sceMpegRingbufferPut(%08x, %i, %i): invalid ringbuffer address", ringbufferAddr, numPackets, available);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(ringbuffer->mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegRingbufferPut(%08x, %i, %i): bad mpeg handle %08x", ringbufferAddr, numPackets, available, ringbuffer->mpeg);
		return -1;
	}

	// Execute callback function as a direct MipsCall, no blocking here so no messing around with wait states etc
	if (ringbuffer->callback_addr != 0) {
		DEBUG_LOG(ME, "sceMpegRingbufferPut(%08x, %i, %i)", ringbufferAddr, numPackets, available);

		PostPutAction *action = (PostPutAction *)__KernelCreateAction(actionPostPut);
		action->setRingAddr(ringbufferAddr);
		// TODO: Should call this multiple times until we get numPackets.
		// Normally this would be if it did not read enough, but also if available > packets.
		// Should ultimately return the TOTAL number of returned packets.
		int writeOffset = ringbuffer->packetsWritePos % (s32)ringbuffer->packets;
		u32 packetsThisRound = std::min(numPackets, (s32)ringbuffer->packets - writeOffset);
		u32 args[3] = {(u32)ringbuffer->data + (u32)writeOffset * 2048, packetsThisRound, (u32)ringbuffer->callback_args};
		__KernelDirectMipsCall(ringbuffer->callback_addr, action, args, 3, false);
	} else {
		ERROR_LOG_REPORT(ME, "sceMpegRingbufferPut: callback_addr zero");
	}
	return 0;
}

static int sceMpegGetAvcAu(u32 mpeg, u32 streamId, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegGetAvcAu(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, streamId, auAddr, attrAddr);
		return -1;
	}

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior.
		ERROR_LOG_REPORT(ME, "sceMpegGetAvcAu(%08x, %08x, %08x, %08x): invalid ringbuffer address", mpeg, streamId, auAddr, attrAddr);
		return -1;
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);

	if (ringbuffer->packetsRead == 0 || ringbuffer->packetsAvail == 0) {
		DEBUG_LOG(ME, "ERROR_MPEG_NO_DATA=sceMpegGetAvcAu(%08x, %08x, %08x, %08x)", mpeg, streamId, auAddr, attrAddr);
		avcAu.pts = -1;
		avcAu.dts = -1;
		avcAu.write(auAddr);
		// TODO: Does this really reschedule?
		return hleDelayResult(ERROR_MPEG_NO_DATA, "mpeg get avc", mpegDecodeErrorDelayMs);
	}

	auto streamInfo = ctx->streamMap.find(streamId);
	if (streamInfo == ctx->streamMap.end())	{
		WARN_LOG_REPORT(ME, "sceMpegGetAvcAu: invalid video stream %08x", streamId);
		return -1;
	}

	if (streamInfo->second.needsReset) {
		avcAu.pts = 0;
		streamInfo->second.needsReset = false;
	}

	// esBuffer is the memory where this au data goes.  We don't write the data to memory.
	// Instead, let's abuse it to keep track of the stream number.
	avcAu.esBuffer = streamInfo->second.num;

	/*// Wait for audio if too much ahead
	if (ctx->atracRegistered && (ctx->mediaengine->getVideoTimeStamp() > ctx->mediaengine->getAudioTimeStamp() + getMaxAheadTimestamp(mpegRingbuffer)))
	{
		ERROR_LOG(ME, "sceMpegGetAvcAu - video too much ahead");
		// TODO: Does this really reschedule?
		return hleDelayResult(ERROR_MPEG_NO_DATA, "mpeg get avc", mpegDecodeErrorDelayMs);
	}*/

	int result = 0;

	avcAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;
	avcAu.dts = avcAu.pts - videoTimestampStep;

	if (ctx->mediaengine->IsVideoEnd()) {
		INFO_LOG(ME, "video end reach. pts: %i dts: %i", (int)avcAu.pts, (int)ctx->mediaengine->getLastTimeStamp());
		ringbuffer->packetsAvail = 0;

		result = ERROR_MPEG_NO_DATA;
	}

	// The avcau struct may have been modified by mediaengine, write it back.
	avcAu.write(auAddr);

	// Jeanne d'Arc return 00000000 as attrAddr here and cause WriteToHardware error 
	if (Memory::IsValidAddress(attrAddr)) {
		Memory::Write_U32(1, attrAddr);
	}


	DEBUG_LOG(ME, "%x=sceMpegGetAvcAu(%08x, %08x, %08x, %08x)", result, mpeg, streamId, auAddr, attrAddr);
	// TODO: sceMpegGetAvcAu seems to modify esSize, and delay when it's > 1000 or something.
	// There's definitely more to it, but ultimately it seems games should expect it to delay randomly.
	return hleDelayResult(result, "mpeg get avc", 100);
}

static u32 sceMpegFinish()
{
	if (!isMpegInit) {
		WARN_LOG(ME, "sceMpegFinish(...): not initialized");
		// TODO: Need to properly hook module load/unload for this to work right.
		//return ERROR_MPEG_NOT_YET_INIT;
	} else {
		INFO_LOG(ME, "sceMpegFinish(...)");
		__VideoPmpShutdown();
	}
	isMpegInit = false;
	//__MpegFinish();
	return hleDelayResult(0, "mpeg finish", 250);
}

static u32 sceMpegQueryMemSize() {
	return hleLogSuccessX(ME, MpegRequiredMem());
}

static int sceMpegGetAtracAu(u32 mpeg, u32 streamId, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegGetAtracAu(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, streamId, auAddr, attrAddr);
		return -1;
	}

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior.
		WARN_LOG(ME, "sceMpegGetAtracAu(%08x, %08x, %08x, %08x): invalid ringbuffer address", mpeg, streamId, auAddr, attrAddr);
		return -1;
	}

	SceMpegAu atracAu;
	atracAu.read(auAddr);

	auto streamInfo = ctx->streamMap.find(streamId);
	if (streamInfo != ctx->streamMap.end() && streamInfo->second.needsReset) {
		atracAu.pts = 0;
		streamInfo->second.needsReset = false;
	}
	if (streamInfo == ctx->streamMap.end()) {
		WARN_LOG_REPORT(ME, "sceMpegGetAtracAu: invalid audio stream %08x", streamId);
		// TODO: Why was this changed to not return an error?
	}

	// The audio can end earlier than the video does.
	if (ringbuffer->packetsAvail == 0) {
		DEBUG_LOG(ME, "ERROR_MPEG_NO_DATA=sceMpegGetAtracAu(%08x, %08x, %08x, %08x)", mpeg, streamId, auAddr, attrAddr);
		// TODO: Does this really delay?
		return hleDelayResult(ERROR_MPEG_NO_DATA, "mpeg get atrac", mpegDecodeErrorDelayMs);
	}

	// esBuffer is the memory where this au data goes.  We don't write the data to memory.
	// Instead, let's abuse it to keep track of the stream number.
	if (streamInfo != ctx->streamMap.end()) {
		atracAu.esBuffer = streamInfo->second.num;
	}

	int result = 0;
	atracAu.pts = ctx->mediaengine->getAudioTimeStamp() + ctx->mpegFirstTimestamp;
	
	if (ctx->mediaengine->IsVideoEnd()) {
		INFO_LOG(ME, "video end reach. pts: %i dts: %i", (int)atracAu.pts, (int)ctx->mediaengine->getLastTimeStamp());
		ringbuffer->packetsAvail = 0;
		// TODO: Is this correct?
		if (!ctx->mediaengine->IsNoAudioData()) {
			WARN_LOG_REPORT(ME, "Video end without audio end, potentially skipping some audio?");
		}
		result = ERROR_MPEG_NO_DATA;
	}

	if (ctx->atracRegistered && ctx->mediaengine->IsNoAudioData() && !ctx->endOfAudioReached) {
		WARN_LOG(ME, "Audio end reach. pts: %i dts: %i", (int)atracAu.pts, (int)ctx->mediaengine->getLastTimeStamp());
		ctx->endOfAudioReached = true;
	}
	if (ctx->mediaengine->IsNoAudioData()) {
		result = ERROR_MPEG_NO_DATA;
	}

	atracAu.write(auAddr);

	// 3rd birthday return 00000000 as attrAddr here and cause WriteToHardware error 
	if (Memory::IsValidAddress(attrAddr)) {
		Memory::Write_U32(0, attrAddr);
	}

	DEBUG_LOG(ME, "%x=sceMpegGetAtracAu(%08x, %08x, %08x, %08x)", result, mpeg, streamId, auAddr, attrAddr);
	// TODO: Not clear on exactly when this delays.
	return hleDelayResult(result, "mpeg get atrac", 100);
}

static int sceMpegQueryPcmEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (!Memory::IsValidAddress(esSizeAddr) || !Memory::IsValidAddress(outSizeAddr)) {
		ERROR_LOG(ME, "sceMpegQueryPcmEsSize(%08x, %08x, %08x): invalid addresses", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegQueryPcmEsSize(%08x, %08x, %08x): bad mpeg handle", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	ERROR_LOG(ME, "sceMpegQueryPcmEsSize(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);

	Memory::Write_U32(MPEG_PCM_ES_SIZE, esSizeAddr);
	Memory::Write_U32(MPEG_PCM_ES_OUTPUT_SIZE, outSizeAddr);
	return 0;
}


static u32 sceMpegChangeGetAuMode(u32 mpeg, int streamUid, int mode)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegChangeGetAuMode(%08x, %i, %i): bad mpeg handle", mpeg, streamUid, mode);
		return ERROR_MPEG_INVALID_VALUE;
	}
	if (mode != MPEG_AU_MODE_DECODE && mode != MPEG_AU_MODE_SKIP) {
		ERROR_LOG(ME, "UNIMPL sceMpegChangeGetAuMode(%08x, %i, %i): bad mode", mpeg, streamUid, mode);
		return ERROR_MPEG_INVALID_VALUE;
	}

	auto stream = ctx->streamMap.find(streamUid);
	if (stream == ctx->streamMap.end()) {
		ERROR_LOG(ME, "UNIMPL sceMpegChangeGetAuMode(%08x, %i, %i): unknown streamID", mpeg, streamUid, mode);
		return ERROR_MPEG_INVALID_VALUE;
	} else {
		StreamInfo &info = stream->second;
		DEBUG_LOG(ME, "UNIMPL sceMpegChangeGetAuMode(%08x, %i, %i): changing type=%d", mpeg, streamUid, mode, info.type);
		switch (info.type) {
		case MPEG_AVC_STREAM:
			if (mode == MPEG_AU_MODE_DECODE) {
				ctx->ignoreAvc = false;
			} else if (mode == MPEG_AU_MODE_SKIP) {
				ctx->ignoreAvc = true;
			}
			break;
		case MPEG_AUDIO_STREAM:
		case MPEG_ATRAC_STREAM:
			if (mode == MPEG_AU_MODE_DECODE) {
				ctx->ignoreAtrac = false;
			} else if (mode == MPEG_AU_MODE_SKIP) {
				ctx->ignoreAtrac = true;
			}
			break;
		case MPEG_PCM_STREAM:
			if (mode == MPEG_AU_MODE_DECODE) {
				ctx->ignorePcm = false;
			} else if (mode == MPEG_AU_MODE_SKIP) {
				ctx->ignorePcm = true;
			}
			break;
		default:
			ERROR_LOG(ME, "UNIMPL sceMpegChangeGetAuMode(%08x, %i, %i): unknown streamID", mpeg, streamUid, mode);
			break;
		}
	}
	return 0;
}

static u32 sceMpegChangeGetAvcAuMode(u32 mpeg, u32 stream_addr, int mode)
{
	if (!Memory::IsValidAddress(stream_addr)) {
		ERROR_LOG(ME, "UNIMPL sceMpegChangeGetAvcAuMode(%08x, %08x, %i): invalid addresses", mpeg, stream_addr, mode);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegChangeGetAvcAuMode(%08x, %08x, %i): bad mpeg handle", mpeg, stream_addr, mode);
		return -1;
	}

	ERROR_LOG_REPORT_ONCE(mpegChangeAvcAu, ME, "UNIMPL sceMpegChangeGetAvcAuMode(%08x, %08x, %i)", mpeg, stream_addr, mode);
	return 0;
}

static u32 sceMpegGetPcmAu(u32 mpeg, int streamUid, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegGetPcmAu(%08x, %i, %08x, %08x): bad mpeg handle", mpeg, streamUid, auAddr, attrAddr);
		return -1;
	}
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior
		WARN_LOG(ME, "sceMpegGetPcmAu(%08x, %08x, %08x, %08x): invalid ringbuffer address", mpeg, streamUid, auAddr, attrAddr);
		return -1;
	}
	if (!Memory::IsValidAddress(streamUid)) {
		WARN_LOG(ME, "sceMpegGetPcmAu(%08x, %08x, %08x, %08x):  didn't get a fake stream", mpeg, streamUid, auAddr, attrAddr);
		return ERROR_MPEG_INVALID_ADDR;
	}
	SceMpegAu atracAu;
	atracAu.read(auAddr);
	auto streamInfo = ctx->streamMap.find(streamUid);
	if (streamInfo == ctx->streamMap.end()) {
		WARN_LOG(ME, "sceMpegGetPcmAu(%08x, %08x, %08x, %08x):  bad streamUid ", mpeg, streamUid, auAddr, attrAddr);
		return -1;
	}

	atracAu.write(auAddr);
	u32 attr = 1 << 7; // Sampling rate (1 = 44.1kHz).
	attr |= 2;         // Number of channels (1 - MONO / 2 - STEREO).
	if (Memory::IsValidAddress(attrAddr))
		Memory::Write_U32(attr, attrAddr);

	ERROR_LOG_REPORT_ONCE(mpegPcmAu, ME, "UNIMPL sceMpegGetPcmAu(%08x, %i, %08x, %08x)", mpeg, streamUid, auAddr, attrAddr);
	return 0;
}

static int __MpegRingbufferQueryPackNum(u32 memorySize) {
	return memorySize / (2048 + 104);
}

static int sceMpegRingbufferQueryPackNum(u32 memorySize) {
	DEBUG_LOG(ME, "sceMpegRingbufferQueryPackNum(%i)", memorySize);
	return __MpegRingbufferQueryPackNum(memorySize);
}

static u32 sceMpegFlushAllStream(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegFlushAllStream(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	WARN_LOG(ME, "UNIMPL sceMpegFlushAllStream(%08x)", mpeg);

	ctx->isAnalyzed = false;

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (ringbuffer.IsValid()) {
		ringbuffer->packetsAvail = 0;
		ringbuffer->packetsRead = 0;
		ringbuffer->packetsWritePos = 0;
	}

	return 0;
}

static u32 sceMpegFlushStream(u32 mpeg, int stream_addr)
{
	if (!Memory::IsValidAddress(stream_addr)) {
		ERROR_LOG(ME, "UNIMPL sceMpegFlushStream(%08x, %i): invalid addresses", mpeg , stream_addr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegFlushStream(%08x, %i): bad mpeg handle", mpeg , stream_addr);
		return -1;
	}

	ERROR_LOG(ME, "UNIMPL sceMpegFlushStream(%08x, %i)", mpeg , stream_addr);
	//__MpegFinish();
	return 0;
}

static u32 sceMpegAvcCopyYCbCr(u32 mpeg, u32 sourceAddr, u32 YCbCrAddr)
{
	if (!Memory::IsValidAddress(sourceAddr) || !Memory::IsValidAddress(YCbCrAddr)) {
		ERROR_LOG(ME, "UNIMPL sceMpegAvcCopyYCbCr(%08x, %08x, %08x): invalid addresses", mpeg, sourceAddr, YCbCrAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcCopyYCbCr(%08x, %08x, %08x): bad mpeg handle", mpeg, sourceAddr, YCbCrAddr);
		return -1;
	}

	ERROR_LOG(ME, "UNIMPL sceMpegAvcCopyYCbCr(%08x, %08x, %08x)", mpeg, sourceAddr, YCbCrAddr);
	return 0;
}

static u32 sceMpegAtracDecode(u32 mpeg, u32 auAddr, u32 bufferAddr, int init)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAtracDecode(%08x, %08x, %08x, %i): bad mpeg handle", mpeg, auAddr, bufferAddr, init);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegAtracDecode(%08x, %08x, %08x, %i)", mpeg, auAddr, bufferAddr, init);

	SceMpegAu atracAu;
	atracAu.read(auAddr);

	// We kept track of the stream number here in sceMpegGetAtracAu().
	ctx->mediaengine->setAudioStream(atracAu.esBuffer);

	Memory::Memset(bufferAddr, 0, MPEG_ATRAC_ES_OUTPUT_SIZE);
	ctx->mediaengine->getAudioSamples(bufferAddr);
	atracAu.pts = ctx->mediaengine->getAudioTimeStamp() + ctx->mpegFirstTimestamp;

	atracAu.write(auAddr);


	return hleDelayResult(0, "mpeg atrac decode", atracDecodeDelayMs);
	//hleEatMicro(4000);
	//return hleDelayResult(0, "mpeg atrac decode", 200);
}

// YCbCr -> RGB color space conversion
static u32 sceMpegAvcCsc(u32 mpeg, u32 sourceAddr, u32 rangeAddr, int frameWidth, u32 destAddr)
{
	if (!Memory::IsValidAddress(sourceAddr) || !Memory::IsValidAddress(rangeAddr) || !Memory::IsValidAddress(destAddr)) {
		ERROR_LOG(ME, "sceMpegAvcCsc(%08x, %08x, %08x, %i, %08x): invalid addresses", mpeg, sourceAddr, rangeAddr, frameWidth, destAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAvcCsc(%08x, %08x, %08x, %i, %08x): bad mpeg handle", mpeg, sourceAddr, rangeAddr, frameWidth, destAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegAvcCsc(%08x, %08x, %08x, %i, %08x)", mpeg, sourceAddr, rangeAddr, frameWidth, destAddr);

	int x = Memory::Read_U32(rangeAddr);
	int y = Memory::Read_U32(rangeAddr + 4);
	int width = Memory::Read_U32(rangeAddr + 8);
	int height = Memory::Read_U32(rangeAddr + 12);
	int destSize = ctx->mediaengine->writeVideoImageWithRange(destAddr, frameWidth, ctx->videoPixelMode, x, y, width, height);

	gpu->NotifyVideoUpload(destAddr, destSize, frameWidth, ctx->videoPixelMode);

	// Do not use avcDecodeDelayMs 's value
	// Will cause video 's screen dislocation in Bleach heat of soul 6
	// https://github.com/hrydgard/ppsspp/issues/5535
	// If do not use DelayResult,Wil cause flickering in Dengeki no Pilot: Tenkuu no Kizuna
	// https://github.com/hrydgard/ppsspp/issues/7549

	return hleDelayResult(0, "mpeg avc csc", avcCscDelayMs);
}

static u32 sceMpegRingbufferDestruct(u32 ringbufferAddr)
{
	DEBUG_LOG(ME, "sceMpegRingbufferDestruct(%08x)", ringbufferAddr);
	// Apparently, does nothing.
	return 0;
}

static u32 sceMpegAvcInitYCbCr(u32 mpeg, int mode, int width, int height, u32 ycbcr_addr)
{
	if (!Memory::IsValidAddress(ycbcr_addr)) {
		ERROR_LOG(ME, "UNIMPL sceMpegAvcInitYCbCr(%08x, %i, %i, %i, %08x): invalid addresses", mpeg, mode, width, height, ycbcr_addr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcInitYCbCr(%08x, %i, %i, %i, %08x): bad mpeg handle", mpeg, mode, width, height, ycbcr_addr);
		return -1;
	}

	ERROR_LOG(ME, "UNIMPL sceMpegAvcInitYCbCr(%08x, %i, %i, %i, %08x)", mpeg, mode, width, height, ycbcr_addr);
	return 0;
}

static int sceMpegAvcQueryYCbCrSize(u32 mpeg, u32 mode, u32 width, u32 height, u32 resultAddr)
{
	if ((width & 15) != 0 || (height & 15) != 0 || height > 272 || width > 480)	{
		ERROR_LOG(ME, "sceMpegAvcQueryYCbCrSize: bad w/h %i x %i", width, height);
		return ERROR_MPEG_INVALID_VALUE;
	}

	DEBUG_LOG(ME, "sceMpegAvcQueryYCbCrSize(%08x, %i, %i, %i, %08x)", mpeg, mode, width, height, resultAddr);

	int size = (width / 2) * (height / 2) * 6 + 128;
	Memory::Write_U32(size, resultAddr);
	return 0;
}

static u32 sceMpegQueryUserdataEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (!Memory::IsValidAddress(esSizeAddr) || !Memory::IsValidAddress(outSizeAddr)) {
		ERROR_LOG(ME, "sceMpegQueryUserdataEsSize(%08x, %08x, %08x): invalid addresses", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegQueryUserdataEsSize(%08x, %08x, %08x): bad mpeg handle", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegQueryUserdataEsSize(%08x, %08x, %08x)", mpeg, esSizeAddr, outSizeAddr);

	Memory::Write_U32(MPEG_DATA_ES_SIZE, esSizeAddr);
	Memory::Write_U32(MPEG_DATA_ES_OUTPUT_SIZE, outSizeAddr);
	return 0;
}

static u32 sceMpegAvcResourceGetAvcDecTopAddr(u32 mpeg)
{
	ERROR_LOG(ME, "UNIMPL sceMpegAvcResourceGetAvcDecTopAddr(%08x)", mpeg);
	// it's just a random address
	return 0x12345678;
}

static u32 sceMpegAvcResourceFinish(u32 mpeg)
{
	DEBUG_LOG(ME,"UNIMPL sceMpegAvcResourceFinish(%08x)", mpeg);
	return 0;
}

static u32 sceMpegAvcResourceGetAvcEsBuf(u32 mpeg)
{
	ERROR_LOG_REPORT_ONCE(mpegResourceEsBuf, ME, "UNIMPL sceMpegAvcResourceGetAvcEsBuf(%08x)", mpeg);
	return 0;
}

static u32 sceMpegAvcResourceInit(u32 mpeg)
{
	if (mpeg != 1) {
		return ERROR_MPEG_INVALID_VALUE;
	}

	ERROR_LOG(ME, "UNIMPL sceMpegAvcResourceInit(%08x)", mpeg);
	return 0;
}

static u32 convertABGRToYCbCr(u32 abgr) {
	//see http://en.wikipedia.org/wiki/Yuv#Y.27UV444_to_RGB888_conversion for more information.
	u8  r = (abgr >>  0) & 0xFF;
	u8  g = (abgr >>  8) & 0xFF;
	u8  b = (abgr >> 16) & 0xFF;
	int  y = 0.299f * r + 0.587f * g + 0.114f * b + 0;
	int cb = -0.169f * r - 0.331f * g + 0.499f * b + 128.0f;
	int cr = 0.499f * r - 0.418f * g - 0.0813f * b + 128.0f;

	// check yCbCr value
	if ( y > 0xFF)  y = 0xFF; if ( y < 0)  y = 0;
	if (cb > 0xFF) cb = 0xFF; if (cb < 0) cb = 0;
	if (cr > 0xFF) cr = 0xFF; if (cr < 0) cr = 0;

	return (y << 16) | (cb << 8) | cr;
}

static int __MpegAvcConvertToYuv420(const void *data, u32 bufferOutputAddr, int width, int height) {
	u32 *imageBuffer = (u32*)data;
	int sizeY = width * height;
	int sizeCb = sizeY >> 2;
	u8 *Y = (u8*)Memory::GetPointer(bufferOutputAddr);
	u8 *Cb = Y + sizeY;
	u8 *Cr = Cb + sizeCb;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 4) {
			u32 abgr0 = imageBuffer[x + 0];
			u32 abgr1 = imageBuffer[x + 1];
			u32 abgr2 = imageBuffer[x + 2];
			u32 abgr3 = imageBuffer[x + 3];

			u32 yCbCr0 = convertABGRToYCbCr(abgr0);
			u32 yCbCr1 = convertABGRToYCbCr(abgr1);
			u32 yCbCr2 = convertABGRToYCbCr(abgr2);
			u32 yCbCr3 = convertABGRToYCbCr(abgr3);
			
			Y[x + 0] = (yCbCr0 >> 16) & 0xFF;
			Y[x + 1] = (yCbCr1 >> 16) & 0xFF;
			Y[x + 2] = (yCbCr2 >> 16) & 0xFF;
			Y[x + 3] = (yCbCr3 >> 16) & 0xFF;

			*Cb++ = (yCbCr0 >> 8) & 0xFF;
			*Cr++ = yCbCr0 & 0xFF;
		}
		imageBuffer += width;
		Y += width ;
	}
	return (width << 16) | height;
}

static int sceMpegAvcConvertToYuv420(u32 mpeg, u32 bufferOutputAddr, u32 unknown1, int unknown2)
{
	if (!Memory::IsValidAddress(bufferOutputAddr)) {
		ERROR_LOG(ME, "sceMpegAvcConvertToYuv420(%08x, %08x, %08x, %08x): invalid addresses", mpeg, bufferOutputAddr, unknown1, unknown2);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegAvcConvertToYuv420(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, bufferOutputAddr, unknown1, unknown2);
		return -1;
	}

	if (ctx->mediaengine->m_buffer == 0){
		WARN_LOG(ME, "sceMpegAvcConvertToYuv420(%08x, %08x, %08x, %08x): m_buffer is zero ", mpeg, bufferOutputAddr, unknown1, unknown2);
		return ERROR_MPEG_AVC_INVALID_VALUE;
	}

	DEBUG_LOG(ME, "sceMpegAvcConvertToYuv420(%08x, %08x, %08x, %08x)", mpeg, bufferOutputAddr, unknown1, unknown2);
	const u8 *data = ctx->mediaengine->getFrameImage();
	int width = ctx->mediaengine->m_desWidth;
	int height = ctx->mediaengine->m_desHeight;

	if (data) {
		__MpegAvcConvertToYuv420(data, bufferOutputAddr, width, height);
	}
	return 0;
}

static int sceMpegGetUserdataAu(u32 mpeg, u32 streamUid, u32 auAddr, u32 resultAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "sceMpegGetUserdataAu(%08x, %08x, %08x, %08x): bad mpeg handle", mpeg, streamUid, auAddr, resultAddr);
		return -1;
	}

	DEBUG_LOG(ME, "sceMpegGetUserdataAu(%08x, %08x, %08x, %08x)", mpeg, streamUid, auAddr, resultAddr);

	// TODO: Are these at all right?  Seen in Phantasy Star Portable 2.
	Memory::Write_U32(0, resultAddr);
	Memory::Write_U32(0, resultAddr + 4);

	// We currently can't demux userdata so this seems like the best thing to return in the meantime..
	// Then we probably shouldn't do the above writes? but it works...
	return ERROR_MPEG_NO_DATA;
}

static u32 sceMpegNextAvcRpAu(u32 mpeg, u32 streamUid)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegNextAvcRpAu(%08x, %08x): bad mpeg handle", mpeg, streamUid);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegNextAvcRpAu(%08x, %08x)", mpeg, streamUid);

	return 0;
}

static u32 sceMpegGetAvcNalAu(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegGetAvcNalAu(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegGetAvcNalAu(%08x)", mpeg);

	return 0;
}

static u32 sceMpegAvcDecodeDetailIndex(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcDecodeDetailIndex(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegAvcDecodeDetailIndex(%08x)", mpeg);

	return 0;
}

static u32 sceMpegAvcDecodeDetail2(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcDecodeDetail2(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegAvcDecodeDetail2(%08x)", mpeg);

	return 0;
}

static u32 sceMpegGetAvcEsAu(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegGetAvcEsAu(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegGetAvcEsAu(%08x)", mpeg);

	return 0;
}

static u32 sceMpegAvcCscInfo(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcCscInfo(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegAvcCscInfo(%08x)", mpeg);

	return 0;
}

static u32 sceMpegAvcCscMode(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegAvcCscMode(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegAvcCscMode(%08x)", mpeg);

	return 0;
}

static u32 sceMpegFlushAu(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(ME, "UNIMPL sceMpegFlushAu(%08x): bad mpeg handle", mpeg);
		return -1;
	}

	ERROR_LOG_REPORT(ME, "UNIMPL sceMpegFlushAu(%08x)", mpeg);

	return 0;
}

const HLEFunction sceMpeg[] =
{
	{0XE1CE83A7, &WrapI_UUUU<sceMpegGetAtracAu>,               "sceMpegGetAtracAu",                  'i', "xxxx"   },
	{0XFE246728, &WrapI_UUUU<sceMpegGetAvcAu>,                 "sceMpegGetAvcAu",                    'i', "xxxx"   },
	{0XD8C5F121, &WrapU_UUUUUUU<sceMpegCreate>,                "sceMpegCreate",                      'x', "xxxxxxx"},
	{0XF8DCB679, &WrapI_UUU<sceMpegQueryAtracEsSize>,          "sceMpegQueryAtracEsSize",            'i', "xxx"    },
	{0XC132E22F, &WrapU_V<sceMpegQueryMemSize>,                "sceMpegQueryMemSize",                'x', ""       },
	{0X21FF80E4, &WrapI_UUU<sceMpegQueryStreamOffset>,         "sceMpegQueryStreamOffset",           'i', "xxx"    },
	{0X611E9E11, &WrapU_UU<sceMpegQueryStreamSize>,            "sceMpegQueryStreamSize",             'x', "xx"     },
	{0X42560F23, &WrapI_UUU<sceMpegRegistStream>,              "sceMpegRegistStream",                'i', "xxx"    },
	{0X591A4AA2, &WrapU_UI<sceMpegUnRegistStream>,             "sceMpegUnRegistStream",              'x', "xi"     },
	{0X707B7629, &WrapU_U<sceMpegFlushAllStream>,              "sceMpegFlushAllStream",              'x', "x"      },
	{0X500F0429, &WrapU_UI<sceMpegFlushStream>,                "sceMpegFlushStream",                 'x', "xi"     },
	{0XA780CF7E, &WrapI_U<sceMpegMallocAvcEsBuf>,              "sceMpegMallocAvcEsBuf",              'i', "x"      },
	{0XCEB870B1, &WrapI_UI<sceMpegFreeAvcEsBuf>,               "sceMpegFreeAvcEsBuf",                'i', "xi"     },
	{0X167AFD9E, &WrapI_UUU<sceMpegInitAu>,                    "sceMpegInitAu",                      'i', "xxx"    },
	{0X682A619B, &WrapU_V<sceMpegInit>,                        "sceMpegInit",                        'x', ""       },
	{0X606A4649, &WrapI_U<sceMpegDelete>,                      "sceMpegDelete",                      'i', "x"      },
	{0X874624D6, &WrapU_V<sceMpegFinish>,                      "sceMpegFinish",                      'x', ""       },
	{0X800C44DF, &WrapU_UUUI<sceMpegAtracDecode>,              "sceMpegAtracDecode",                 'x', "xxxi"   },
	{0X0E3C2E9D, &WrapU_UUUUU<sceMpegAvcDecode>,               "sceMpegAvcDecode",                   'x', "xxxxx"  },
	{0X740FCCD1, &WrapU_UUUU<sceMpegAvcDecodeStop>,            "sceMpegAvcDecodeStop",               'x', "xxxx"   },
	{0X4571CC64, &WrapU_U<sceMpegAvcDecodeFlush>,              "sceMpegAvcDecodeFlush",              'x', "x"      },
	{0X0F6C18D7, &WrapI_UU<sceMpegAvcDecodeDetail>,            "sceMpegAvcDecodeDetail",             'i', "xx"     },
	{0XA11C7026, &WrapI_UU<sceMpegAvcDecodeMode>,              "sceMpegAvcDecodeMode",               'i', "xx"     },
	{0X37295ED8, &WrapU_UUUUUU<sceMpegRingbufferConstruct>,    "sceMpegRingbufferConstruct",         'x', "xxxxxx" },
	{0X13407F13, &WrapU_U<sceMpegRingbufferDestruct>,          "sceMpegRingbufferDestruct",          'x', "x"      },
	{0XB240A59E, &WrapU_UII<sceMpegRingbufferPut>,             "sceMpegRingbufferPut",               'x', "xxx"    },
	{0XB5F6DC87, &WrapI_U<sceMpegRingbufferAvailableSize>,     "sceMpegRingbufferAvailableSize",     'i', "x"      },
	{0XD7A29F46, &WrapU_I<sceMpegRingbufferQueryMemSize>,      "sceMpegRingbufferQueryMemSize",      'x', "i"      },
	{0X769BEBB6, &WrapI_U<sceMpegRingbufferQueryPackNum>,      "sceMpegRingbufferQueryPackNum",      'i', "x"      },
	{0X211A057C, &WrapI_UUUUU<sceMpegAvcQueryYCbCrSize>,       "sceMpegAvcQueryYCbCrSize",           'i', "xxxxx"  },
	{0XF0EB1125, &WrapI_UUUU<sceMpegAvcDecodeYCbCr>,           "sceMpegAvcDecodeYCbCr",              'i', "xxxx"   },
	{0XF2930C9C, &WrapU_UUU<sceMpegAvcDecodeStopYCbCr>,        "sceMpegAvcDecodeStopYCbCr",          'x', "xxx"    },
	{0X67179B1B, &WrapU_UIIIU<sceMpegAvcInitYCbCr>,            "sceMpegAvcInitYCbCr",                'x', "xiiix"  },
	{0X0558B075, &WrapU_UUU<sceMpegAvcCopyYCbCr>,              "sceMpegAvcCopyYCbCr",                'x', "xxx"    },
	{0X31BD0272, &WrapU_UUUIU<sceMpegAvcCsc>,                  "sceMpegAvcCsc",                      'x', "xxxix"  },
	{0X9DCFB7EA, &WrapU_UII<sceMpegChangeGetAuMode>,           "sceMpegChangeGetAuMode",             'x', "xii"    },
	{0X8C1E027D, &WrapU_UIUU<sceMpegGetPcmAu>,                 "sceMpegGetPcmAu",                    'x', "xixx"   },
	{0XC02CF6B5, &WrapI_UUU<sceMpegQueryPcmEsSize>,            "sceMpegQueryPcmEsSize",              'i', "xxx"    },
	{0XC45C99CC, &WrapU_UUU<sceMpegQueryUserdataEsSize>,       "sceMpegQueryUserdataEsSize",         'x', "xxx"    },
	{0X234586AE, &WrapU_UUI<sceMpegChangeGetAvcAuMode>,        "sceMpegChangeGetAvcAuMode",          'x', "xxi"    },
	{0X63B9536A, &WrapU_U<sceMpegAvcResourceGetAvcDecTopAddr>, "sceMpegAvcResourceGetAvcDecTopAddr", 'x', "x"      },
	{0X8160A2FE, &WrapU_U<sceMpegAvcResourceFinish>,           "sceMpegAvcResourceFinish",           'x', "x"      },
	{0XAF26BB01, &WrapU_U<sceMpegAvcResourceGetAvcEsBuf>,      "sceMpegAvcResourceGetAvcEsBuf",      'x', "x"      },
	{0XFCBDB5AD, &WrapU_U<sceMpegAvcResourceInit>,             "sceMpegAvcResourceInit",             'x', "x"      },
	{0XF5E7EA31, &WrapI_UUUI<sceMpegAvcConvertToYuv420>,       "sceMpegAvcConvertToYuv420",          'i', "xxxi"   },
	{0X01977054, &WrapI_UUUU<sceMpegGetUserdataAu>,            "sceMpegGetUserdataAu",               'i', "xxxx"   },
	{0X3C37A7A6, &WrapU_UU<sceMpegNextAvcRpAu>,                "sceMpegNextAvcRpAu",                 'x', "xx"     },
	{0X11F95CF1, &WrapU_U<sceMpegGetAvcNalAu>,                 "sceMpegGetAvcNalAu",                 'x', "x"      },
	{0XAB0E9556, &WrapU_U<sceMpegAvcDecodeDetailIndex>,        "sceMpegAvcDecodeDetailIndex",        'x', "x"      },
	{0XCF3547A2, &WrapU_U<sceMpegAvcDecodeDetail2>,            "sceMpegAvcDecodeDetail2",            'x', "x"      },
	{0X921FCCCF, &WrapU_U<sceMpegGetAvcEsAu>,                  "sceMpegGetAvcEsAu",                  'x', "x"      },
	{0XE95838F6, &WrapU_U<sceMpegAvcCscInfo>,                  "sceMpegAvcCscInfo",                  'x', "x"      },
	{0XD1CE4950, &WrapU_U<sceMpegAvcCscMode>,                  "sceMpegAvcCscMode",                  'x', "x"      },
	{0XDBB60658, &WrapU_U<sceMpegFlushAu>,                     "sceMpegFlushAu",                     'x', "x"      },
	{0XD4DD6E75, nullptr,                                      "sceMpeg_D4DD6E75",                   '?', ""       },
	{0X11CAB459, nullptr,                                      "sceMpeg_11CAB459",                   '?', ""       },
	{0XC345DED2, nullptr,                                      "sceMpeg_C345DED2",                   '?', ""       },
	{0XB27711A8, nullptr,                                      "sceMpeg_B27711A8",                   '?', ""       },
	{0X988E9E12, nullptr,                                      "sceMpeg_988E9E12",                   '?', ""       },
};

void Register_sceMpeg()
{
	RegisterModule("sceMpeg", ARRAY_SIZE(sceMpeg), sceMpeg);
}

// This function is currently only been used for PMP videos
// p pointing to a SceMpegLLI structure consists of video frame blocks.
static u32 sceMpegbase_BEA18F91(u32 p)
{
	pmp_videoSource = p;
	pmp_nBlocks = 0;
	SceMpegLLI lli;
	while (1){
		Memory::ReadStruct(p, &lli);
		pmp_nBlocks++;
		// lli.Next ==0 for last block
		if (lli.Next == 0){
			break;
		}
		p = p + sizeof(SceMpegLLI);
	}
	
	DEBUG_LOG(ME, "sceMpegbase_BEA18F91(%08x), received %d block(s)", pmp_videoSource, pmp_nBlocks);
	return 0;
}

const HLEFunction sceMpegbase[] =
{
	{0XBEA18F91, &WrapU_U<sceMpegbase_BEA18F91>,               "sceMpegbase_BEA18F91",               'x', "x"      },
	{0X492B5E4B, nullptr,                                      "sceMpegBaseCscInit",                 '?', ""       },
	{0X0530BE4E, nullptr,                                      "sceMpegbase_0530BE4E",               '?', ""       },
	{0X91929A21, nullptr,                                      "sceMpegBaseCscAvc",                  '?', ""       },
	{0X304882E1, nullptr,                                      "sceMpegBaseCscAvcRange",             '?', ""       },
	{0X7AC0321A, nullptr,                                      "sceMpegBaseYCrCbCopy",               '?', ""       }
};

void Register_sceMpegbase()
{
	RegisterModule("sceMpegbase", ARRAY_SIZE(sceMpegbase), sceMpegbase);
};
