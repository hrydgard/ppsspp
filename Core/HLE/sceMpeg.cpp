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
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Swap.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HW/MediaEngine.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "Core/System.h"

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

// MPEG AVC Resource Management
static const int MPEG_AVC_RESOURCE_SIZE = 0x20000;
static const int MPEG_AVC_RESOURCE_FLAG = 0x4;

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
static const int MPEG_WARMUP_FRAMES = 1;

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
static int pmp_nBlocks = 0; //number of blocks received in the last sceMpegBasePESpacketCopy call
#ifdef USE_FFMPEG
static std::list<AVFrame*> pmp_queue; //list of pmp video frames have been decoded and will be played
#endif
static std::list<u32> pmp_ContextList; //list of pmp media contexts
static bool pmp_oldStateLoaded = false; // for dostate

// Calculate the number of total packets added to the ringbuffer by calling the sceMpegRingbufferPut() once.
static int ringbufferPutPacketsAdded = 0;
static bool useRingbufferPutCallbackMulti = true;

#ifdef USE_FFMPEG

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
}
#include "Core/FFMPEGCompat.h"
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
	Memory::Memcpy(this, addr, sizeof(*this), "SceMpegAu");
	pts = (pts & 0xFFFFFFFFULL) << 32 | (((u64)pts) >> 32);
	dts = (dts & 0xFFFFFFFFULL) << 32 | (((u64)dts) >> 32);
}

void SceMpegAu::write(u32 addr) {
	pts = (pts & 0xFFFFFFFFULL) << 32 | (((u64)pts) >> 32);
	dts = (dts & 0xFFFFFFFFULL) << 32 | (((u64)dts) >> 32);
	Memory::Memcpy(addr, this, sizeof(*this), "SceMpegAu");
}

/*
// Currently unused
static int getMaxAheadTimestamp(const SceMpegRingBuffer &ringbuf) {
	return std::max(maxAheadTimestamp, 700 * ringbuf.packets);  // empiric value from JPCSP, thanks!
}
*/

const u8 defaultMpegheader[2048] = {
	0x50,0x53,0x4d,0x46,0x30,0x30,0x31,0x35,0x00,0x00,0x08,0x00,0x00,
	0x10,0xc8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x4e,0x00,
	0x00,0x00,0x01,0x5f,0x90,0x00,0x00,0x00,0x0d,0xbe,0xca,0x00,0x00,0x61,0xa8,0x00,0x01,0x5f,
	0x90,0x02,0x01,0x00,0x00,0x00,0x34,0x00,0x00,0x00,0x01,0x5f,0x90,0x00,0x00,0x00,0x0d,0xbe,
	0xca,0x00,0x01,0x00,0x00,0x00,0x22,0x00,0x02,0xe0,0x00,0x20,0xfb,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x1e,0x11,0x00,0x00,0xbd,0x00,0x20,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x02,0x02
};

static bool isMpegInit;
static int mpegLibVersion = 0;
static u32 mpegLibCrc = 0;
static u32 streamIdGen;
static int actionPostPut;
std::map<u32, MpegContext *> g_mpegCtxs;
static std::map<u32, u32> videocodecEdramAllocations;
static int mpegBasePixelMode;
static int mpegBaseDefaultBufferWidth;
static u32 mpegBaseCscCalls;
static u32 vshMeMemoryBase;
static u32 mpegPESCopyCalls;
static u32 vshVideoOpenCalls;
static u32 vshVideoDecodeCalls;
static u32 vshVideoDecodedFrames;
static u32 vshVideoDeleteCalls;

struct VSHVideoCodecContext {
	int frameCount = 0;
	int decodeCalls = 0;
	int width = 0;
	int height = 0;
	u32 imageAllocation = 0;
	u32 imageAllocationSize = 0;
	u32 buffers[3][8]{};
	u32 auxiliary1 = 0;
	u32 auxiliary2 = 0;

#ifdef USE_FFMPEG
	AVCodecContext *codec = nullptr;
	AVFrame *frame = nullptr;
	SwsContext *sws = nullptr;
#endif

	VSHVideoCodecContext() = default;
	~VSHVideoCodecContext() {
		ResetHostDecoder();
	}

	void ResetHostDecoder() {
#ifdef USE_FFMPEG
		if (frame) {
			av_frame_free(&frame);
		}
		if (codec) {
			avcodec_free_context(&codec);
		}
		if (sws) {
			sws_freeContext(sws);
			sws = nullptr;
		}
#endif
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("VSHVideoCodecContext", 1, 2);
		if (!s) {
			return;
		}
		if (p.mode == PointerWrap::MODE_READ) {
			ResetHostDecoder();
		}
		Do(p, frameCount);
		if (s >= 2) {
			Do(p, decodeCalls);
		} else if (p.mode == PointerWrap::MODE_READ) {
			decodeCalls = 0;
		}
		Do(p, width);
		Do(p, height);
		Do(p, imageAllocation);
		Do(p, imageAllocationSize);
		DoArray(p, &buffers[0][0], 24);
		Do(p, auxiliary1);
		Do(p, auxiliary2);
	}
};

static std::map<u32, VSHVideoCodecContext *> vshVideoCodecContexts;

// MPEG AVC Resource Management
static u32 sceMpegAvcResourceAddr = 0;
static u32 sceMpegAvcResourceDataAddr = 0;
static int sceMpegAvcResourceFlags = 0;

MpegContext::MpegContext() {
	memcpy(mpegheader, defaultMpegheader, 2048);
}
MpegContext::~MpegContext() {
	delete mediaengine;
}
void MpegContext::DoState(PointerWrap &p) {
	auto s = p.Section("MpegContext", 1, 4);
	if (!s)
		return;
	if (s >= 3)
		Do(p, mpegwarmUp);
	else
		mpegwarmUp = 1000;
	DoArray(p, mpegheader, 2048);
	Do(p, defaultFrameWidth);
	Do(p, videoFrameCount);
	Do(p, audioFrameCount);
	Do(p, endOfAudioReached);
	Do(p, endOfVideoReached);
	Do(p, videoPixelMode);
	Do(p, mpegMagic);
	Do(p, mpegVersion);
	Do(p, mpegRawVersion);
	Do(p, mpegOffset);
	Do(p, mpegStreamSize);
	Do(p, mpegFirstTimestamp);
	Do(p, mpegLastTimestamp);
	Do(p, mpegFirstDate);
	Do(p, mpegLastDate);
	Do(p, mpegRingbufferAddr);
	DoArray(p, esBuffers, MPEG_DATA_ES_BUFFERS);
	Do(p, avc);
	Do(p, avcRegistered);
	Do(p, atracRegistered);
	Do(p, pcmRegistered);
	Do(p, dataRegistered);
	Do(p, ignoreAtrac);
	Do(p, ignorePcm);
	Do(p, ignoreAvc);
	Do(p, isAnalyzed);
	Do<u32, StreamInfo>(p, streamMap);
	if (s >= 4) {
		Do(p, streamIgnoreMap);
	} else if (p.mode == PointerWrap::MODE_READ) {
		streamIgnoreMap.clear();
		for (auto const &it : streamMap) {
			if (it.second.type == MPEG_AVC_STREAM)
				streamIgnoreMap[it.first] = ignoreAvc;
			else if (it.second.type == MPEG_ATRAC_STREAM || it.second.type == MPEG_AUDIO_STREAM)
				streamIgnoreMap[it.first] = ignoreAtrac;
			else if (it.second.type == MPEG_PCM_STREAM)
				streamIgnoreMap[it.first] = ignorePcm;
			else
				streamIgnoreMap[it.first] = false;
		}
	}
	DoClass(p, mediaengine);
	ringbufferNeedsReverse = s < 2;
}

static MpegContext *getMpegCtx(u32 mpegAddr) {
	if (!Memory::IsValidAddress(mpegAddr))
		return nullptr;

	u32 mpeg = Memory::ReadUnchecked_U32(mpegAddr);
	auto found = g_mpegCtxs.find(mpeg);
	if (found == g_mpegCtxs.end())
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

const std::map<u32, MpegContext *> &__MpegGetContexts() {
	return g_mpegCtxs;
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

static void AnalyzeMpeg(const u8 *buffer, u32 validSize, MpegContext *ctx) {
	ctx->mpegMagic = *(u32_le*)buffer;
	ctx->mpegRawVersion = *(u32_le*)(buffer + PSMF_STREAM_VERSION_OFFSET);
	ctx->mpegVersion = getMpegVersion(ctx->mpegRawVersion);
	ctx->mpegOffset = bswap32(*(u32_le*)(buffer + PSMF_STREAM_OFFSET_OFFSET));
	ctx->mpegStreamSize = bswap32(*(u32_le*)(buffer + PSMF_STREAM_SIZE_OFFSET));
	ctx->mpegFirstTimestamp = getMpegTimeStamp(buffer + PSMF_FIRST_TIMESTAMP_OFFSET);
	ctx->mpegLastTimestamp = getMpegTimeStamp(buffer + PSMF_LAST_TIMESTAMP_OFFSET);
	ctx->mpegFirstDate = convertTimestampToDate(ctx->mpegFirstTimestamp);
	ctx->mpegLastDate = convertTimestampToDate(ctx->mpegLastTimestamp);
	ctx->mpegwarmUp = 0;
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
		WARN_LOG_REPORT(Log::Mpeg, "Unexpected mpeg first timestamp: %llx / %lld", ctx->mpegFirstTimestamp, ctx->mpegFirstTimestamp);
	}

	if (ctx->mpegMagic != PSMF_MAGIC || ctx->mpegVersion < 0 ||
		(ctx->mpegOffset & 2047) != 0 || ctx->mpegOffset == 0) {
		// mpeg header is invalid!
		return;
	}

	if (!ctx->isAnalyzed && ctx->mediaengine && ctx->mpegStreamSize > 0 && validSize >= ctx->mpegOffset) {
		// init mediaEngine
		auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
		if (ringbuffer.IsValid()) {
			ctx->mediaengine->loadStream(buffer, ctx->mpegOffset, ringbuffer->packets * ringbuffer->packetSize);
		} else {
			// TODO: Does this make any sense?
			ctx->mediaengine->loadStream(buffer, ctx->mpegOffset, 0);
		}

		// When used with scePsmf, some applications attempt to use sceMpegQueryStreamOffset
		// and sceMpegQueryStreamSize, which forces a packet overwrite in the Media Engine and in
		// the MPEG ringbuffer.
		// Mark the current MPEG as analyzed to filter this, and restore it at sceMpegFinish.
		ctx->isAnalyzed = true;
	}

	// copy header struct to mpeg header.
	memcpy(ctx->mpegheader, buffer, validSize >= 2048 ? 2048 : validSize);
	*(u32_le*)(ctx->mpegheader + PSMF_STREAM_OFFSET_OFFSET) = 0x80000;

	INFO_LOG(Log::Mpeg, "Stream offset: %d, Stream size: 0x%X", ctx->mpegOffset, ctx->mpegStreamSize);
	INFO_LOG(Log::Mpeg, "First timestamp: %lld, Last timestamp: %lld", ctx->mpegFirstTimestamp, ctx->mpegLastTimestamp);
}

class PostPutAction : public PSPAction {
public:
	PostPutAction() {}
	void setRingAddr(u32 ringAddr) { ringAddr_ = ringAddr; }
	void setRemainingPackets(u32 remainingPackets) { remainingPackets_ = remainingPackets; }
	static PSPAction *Create() { return new PostPutAction; }
	void DoState(PointerWrap &p) override {
		auto s = p.Section("PostPutAction", 1, 2);
		if (!s)
			return;

		Do(p, ringAddr_);
		if (s >= 2) {
			Do(p, remainingPackets_);
		}
	}
	void run(MipsCall &call) override;
private:
	u32 ringAddr_ = 0;
	s32 remainingPackets_ = 0;
};

void __MpegInit() {
	isMpegInit = false;
	mpegLibVersion = 0x010A;
	streamIdGen = 1;
	actionPostPut = __KernelRegisterActionType(PostPutAction::Create);
	videocodecEdramAllocations.clear();
	vshVideoCodecContexts.clear();
	mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;
	mpegBaseDefaultBufferWidth = 0;
	mpegBaseCscCalls = 0;
	vshMeMemoryBase = 0;
	mpegPESCopyCalls = 0;
	vshVideoOpenCalls = 0;
	vshVideoDecodeCalls = 0;
	vshVideoDecodedFrames = 0;
	vshVideoDeleteCalls = 0;

#ifdef USE_FFMPEG
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 18, 100)
	avcodec_register_all();
#endif
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 12, 100)
	av_register_all();
#endif
#endif
}

void __MpegDoState(PointerWrap &p) {
	auto s = p.Section("sceMpeg", 1, 10);
	if (!s)
		return;

	if (s < 2) {
		int oldLastMpeg = -1;
		bool oldIsMpegAnalyzed = false;
		Do(p, oldLastMpeg);
		Do(p, streamIdGen);
		Do(p, oldIsMpegAnalyzed);
		// Let's assume the oldest version.
		mpegLibVersion = 0x0101;
	} else {
		if (s < 3) {
			useRingbufferPutCallbackMulti = false;
			ringbufferPutPacketsAdded = 0;
		} else {
			Do(p, ringbufferPutPacketsAdded);
		}
		if (s < 4) {
			mpegLibCrc = 0;
		}
		else {
			Do(p, mpegLibCrc);
		}

		Do(p, streamIdGen);
		Do(p, mpegLibVersion);
	}
	Do(p, isMpegInit);
	Do(p, actionPostPut);
	__KernelRestoreActionType(actionPostPut, PostPutAction::Create);

	Do(p, g_mpegCtxs);
	if (s >= 5) {
		Do(p, videocodecEdramAllocations);
	} else if (p.mode == PointerWrap::MODE_READ) {
		videocodecEdramAllocations.clear();
	}
	if (s >= 6) {
		Do(p, vshVideoCodecContexts);
		Do(p, mpegBasePixelMode);
		Do(p, mpegBaseDefaultBufferWidth);
		if (s >= 7) {
			Do(p, mpegBaseCscCalls);
		} else if (p.mode == PointerWrap::MODE_READ) {
			mpegBaseCscCalls = 0;
		}
		if (s >= 8) {
			Do(p, vshMeMemoryBase);
		} else if (p.mode == PointerWrap::MODE_READ) {
			vshMeMemoryBase = 0;
		}
		if (s >= 9) {
			Do(p, mpegPESCopyCalls);
		} else if (p.mode == PointerWrap::MODE_READ) {
			mpegPESCopyCalls = 0;
		}
		if (s >= 10) {
			Do(p, vshVideoOpenCalls);
			Do(p, vshVideoDecodeCalls);
			Do(p, vshVideoDecodedFrames);
			Do(p, vshVideoDeleteCalls);
		} else if (p.mode == PointerWrap::MODE_READ) {
			vshVideoOpenCalls = 0;
			vshVideoDecodeCalls = 0;
			vshVideoDecodedFrames = 0;
			vshVideoDeleteCalls = 0;
		}
	} else if (p.mode == PointerWrap::MODE_READ) {
		vshVideoCodecContexts.clear();
		mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;
		mpegBaseDefaultBufferWidth = 0;
		mpegBaseCscCalls = 0;
		vshMeMemoryBase = 0;
		mpegPESCopyCalls = 0;
		vshVideoOpenCalls = 0;
		vshVideoDecodeCalls = 0;
		vshVideoDecodedFrames = 0;
		vshVideoDeleteCalls = 0;
	}
}

void __MpegShutdown() {
	std::map<u32, MpegContext *>::iterator it, end;
	for (it = g_mpegCtxs.begin(), end = g_mpegCtxs.end(); it != end; ++it) {
		delete it->second;
	}
	g_mpegCtxs.clear();
	for (const auto &[_, allocation] : videocodecEdramAllocations) {
		if (allocation != 0) {
			userMemory.Free(allocation);
		}
	}
	videocodecEdramAllocations.clear();
	for (const auto &[_, context] : vshVideoCodecContexts) {
		if (context->imageAllocation != 0) {
			userMemory.Free(context->imageAllocation);
		}
		delete context;
	}
	vshVideoCodecContexts.clear();
}

void __MpegLoadModule(int version,u32 crc) {
	mpegLibVersion = version;
	mpegLibCrc = crc;
}

static u32 sceMpegInit() {
	if (isMpegInit) {
		WARN_LOG(Log::Mpeg, "sceMpegInit(): already initialized");
		// TODO: Need to properly hook module load/unload for this to work right.
		//return SCE_MPEG_ERROR_ALREADY_INIT;
	} else {
		INFO_LOG(Log::Mpeg, "sceMpegInit(), mpegLibVersion 0x%0x, mpegLibcrc %x", mpegLibVersion, mpegLibCrc);
	}
	isMpegInit = true;
	return hleDelayResult(hleNoLog(0), "mpeg init", 750);
}

static u32 __MpegRingbufferQueryMemSize(int packets) {
	return packets * (104 + 2048);
}

static u32 sceMpegRingbufferQueryMemSize(int packets) {
	u32 size = __MpegRingbufferQueryMemSize(packets);
	return hleLogDebug(Log::Mpeg, size);
}

static u32 sceMpegRingbufferConstruct(u32 ringbufferAddr, u32 numPackets, u32 data, u32 size, u32 callbackAddr, u32 callbackArg) {
	if (!Memory::IsValidAddress(ringbufferAddr)) {
		ERROR_LOG_REPORT(Log::Mpeg, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): bad ringbuffer, should crash", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
		return hleNoLog(SCE_KERNEL_ERROR_ILLEGAL_ADDRESS);
	}

	if ((int)size < 0) {
		ERROR_LOG_REPORT(Log::Mpeg, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): invalid size", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
		return hleNoLog(SCE_MPEG_ERROR_NO_MEMORY);
	}

	if (__MpegRingbufferQueryMemSize(numPackets) > size) {
		if (numPackets < 0x00100000) {
			ERROR_LOG_REPORT(Log::Mpeg, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): too many packets for buffer", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
			return hleNoLog(SCE_MPEG_ERROR_NO_MEMORY);
		} else {
			// The PSP's firmware allows some cases here, due to a bug in its validation.
			ERROR_LOG_REPORT(Log::Mpeg, "sceMpegRingbufferConstruct(%08x, %i, %08x, %08x, %08x, %08x): too many packets for buffer, bogus size", ringbufferAddr, numPackets, data, size, callbackAddr, callbackArg);
		}
	}

	auto ring = PSPPointer<SceMpegRingBuffer>::Create(ringbufferAddr);

	ring->packets = numPackets;
	ring->packetsRead = 0;
	ring->packetsWritePos = 0;
	ring->packetsAvail = 0;
	ring->packetSize = 2048;
	ring->data = data;
	ring->callback_addr = callbackAddr;
	ring->callback_args = callbackArg;
	ring->dataUpperBound = data + numPackets * 2048;
	if (ring->semaID != 0) {
		// I'm starting to think this is just padding or something.
		// It's not written by this function.
		WARN_LOG(Log::Mpeg, "Detected 'semaID' (might not be) %d (%08x)", ring->semaID, ring->semaID);
	}
	ring->mpeg = 0;
	// This isn't in ver 0104, but it is in 0105.
	if (mpegLibVersion >= 0x0105)
		ring->gp = __KernelGetModuleGP(__KernelGetCurThreadModuleId());
	return hleLogDebug(Log::Mpeg, 0);
}

static u32 MpegRequiredMem() {
	if (mpegLibVersion < 0x0105) {
		return MPEG_MEMSIZE_0104;
	}
	return MPEG_MEMSIZE_0105;
}

// ddrTop is currently ignored.
static u32 sceMpegCreate(u32 mpegAddr, u32 dataPtr, u32 size, u32 ringbufferAddr, u32 frameWidth, u32 mode, u32 ddrTop) {
	if (!Memory::IsValidAddress(mpegAddr)) {
		return hleLogWarning(Log::Mpeg, -1, "invalid addresses");
	}

	if (size < MpegRequiredMem()) {
		return hleLogWarning(Log::Mpeg, SCE_MPEG_ERROR_NO_MEMORY);
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
	Memory::WriteUnchecked_U32(mpegHandle, mpegAddr);

	// Initialize fake mpeg struct.
	Memory::Memcpy(mpegHandle, "LIBMPEG\0", 8, "Mpeg");
	Memory::Memcpy(mpegHandle + 8, "001\0", 4, "Mpeg");
	Memory::WriteUnchecked_U32(-1, mpegHandle + 12);
	if (ringbuffer.IsValid()) {
		Memory::WriteUnchecked_U32(ringbufferAddr, mpegHandle + 16);
		Memory::WriteUnchecked_U32(ringbuffer->dataUpperBound, mpegHandle + 20);
	}
	MpegContext *ctx = new MpegContext();
	if (g_mpegCtxs.find(mpegHandle) != g_mpegCtxs.end()) {
		WARN_LOG_REPORT(Log::HLE, "Replacing existing mpeg context at %08x", mpegAddr);
		// Otherwise, it would leak.
		delete g_mpegCtxs[mpegHandle];
	}
	g_mpegCtxs[mpegHandle] = ctx;

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
	ctx->mpegStreamSize = 0;
	ctx->mpegOffset = 0;
	for (int i = 0; i < MPEG_DATA_ES_BUFFERS; i++) {
		ctx->esBuffers[i] = false;
	}

	// Detailed "analysis" is done later in Query* for some reason.
	ctx->isAnalyzed = false;
	ctx->mediaengine = new MediaEngine();

	return hleDelayResult(hleLogInfo(Log::Mpeg, 0), "mpeg create", 29000);
}

static int sceMpegDelete(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	delete ctx;
	g_mpegCtxs.erase(Memory::ReadUnchecked_U32(mpeg));

	return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg delete", 40000);
}


static int sceMpegAvcDecodeMode(u32 mpeg, u32 modeAddr)
{
	if (!Memory::IsValidRange(modeAddr, 8)) {
		return hleLogWarning(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	DEBUG_LOG(Log::Mpeg, "sceMpegAvcDecodeMode(%08x, %08x)", mpeg, modeAddr);

	int mode = Memory::ReadUnchecked_U32(modeAddr);
	int pixelMode = Memory::ReadUnchecked_U32(modeAddr + 4);
	if (pixelMode >= GE_CMODE_16BIT_BGR5650 && pixelMode <= GE_CMODE_32BIT_ABGR8888) {
		ctx->videoPixelMode = pixelMode;
	} else {
		ERROR_LOG(Log::Mpeg, "sceMpegAvcDecodeMode(%i, %i): unknown pixelMode ", mode, pixelMode);
	}
	return hleNoLog(0);
}

static int sceMpegQueryStreamOffset(u32 mpeg, u32 bufferAddr, u32 offsetAddr)
{
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(offsetAddr)) {
		return hleLogWarning(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	// Kinda destructive, no? Shouldn't this just do what sceMpegQueryStreamSize does?
	AnalyzeMpeg(Memory::GetPointerWriteUnchecked(bufferAddr), Memory::ClampValidSizeAt(bufferAddr, 32768), ctx);

	if (ctx->mpegMagic != PSMF_MAGIC) {
		Memory::WriteUnchecked_U32(0, offsetAddr);
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "Bad PSMF magic");
	} else if (ctx->mpegVersion < 0) {
		Memory::WriteUnchecked_U32(0, offsetAddr);
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_BAD_VERSION, "Bad version");
	} else if ((ctx->mpegOffset & 2047) != 0 || ctx->mpegOffset == 0) {
		Memory::WriteUnchecked_U32(0, offsetAddr);
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "Bad offset");
	}

	Memory::WriteUnchecked_U32(ctx->mpegOffset, offsetAddr);
	return hleLogDebug(Log::Mpeg, 0);
}

static u32 sceMpegQueryStreamSize(u32 bufferAddr, u32 sizeAddr)
{
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(sizeAddr)) {
		return hleLogWarning(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext ctx;
	ctx.mediaengine = nullptr;  // makes sure we don't actually load the stream.
	ctx.isAnalyzed = false;

	AnalyzeMpeg(Memory::GetPointerWriteUnchecked(bufferAddr), Memory::ClampValidSizeAt(bufferAddr, 32768), &ctx);

	if (ctx.mpegMagic != PSMF_MAGIC) {
		Memory::WriteUnchecked_U32(0, sizeAddr);
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "Bad PSMF magic");
	} else if ((ctx.mpegOffset & 2047) != 0 ) {
		Memory::WriteUnchecked_U32(0, sizeAddr);
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "Bad offset %08x", ctx.mpegOffset);
	}

	Memory::WriteUnchecked_U32(ctx.mpegStreamSize, sizeAddr);
	return hleLogDebug(Log::Mpeg, 0);
}

static int sceMpegRegistStream(u32 mpeg, u32 streamType, u32 streamNum)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	switch (streamType) {
	case MPEG_AVC_STREAM:
		ctx->avcRegistered = true;
		ctx->mediaengine->addVideoStream(streamNum);
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
		DEBUG_LOG(Log::Mpeg, "sceMpegRegistStream(%i) : unknown stream type", streamType);
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
	ctx->streamIgnoreMap[sid] = false;
	return hleLogInfo(Log::Mpeg, sid);
}

static int sceMpegMallocAvcEsBuf(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	// Doesn't actually malloc, just keeps track of a couple of flags
	for (int i = 0; i < MPEG_DATA_ES_BUFFERS; i++) {
		if (!ctx->esBuffers[i]) {
			ctx->esBuffers[i] = true;
			return hleLogDebug(Log::Mpeg, i + 1);
		}
	}

	// No es buffer
	return hleLogDebug(Log::Mpeg, 0);
}

static int sceMpegFreeAvcEsBuf(u32 mpeg, int esBuf)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	if (esBuf == 0) {
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE);
	}

	if (esBuf >= 1 && esBuf <= MPEG_DATA_ES_BUFFERS) {
		// TODO: Check if it's already been free'd?
		ctx->esBuffers[esBuf - 1] = false;
	}
	return hleLogDebug(Log::Mpeg, 0);
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
		ERROR_LOG(Log::Mpeg, "Can not find H264 codec, please update ffmpeg");
		return false;
	}

	// Create CodecContext
	AVCodecContext * pmp_CodecCtx = avcodec_alloc_context3(pmp_Codec);
	if (pmp_CodecCtx == NULL){
		ERROR_LOG(Log::Mpeg, "Can not allocate pmp Codec Context");
		return false;
	}

	pmp_CodecCtx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT | AV_CODEC_FLAG_LOW_DELAY;

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
		ERROR_LOG(Log::Mpeg, "Can not open pmp video codec");
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

	void add(const H264Frames *p) {
		add(p->stream, p->size);
	};

	void add(const u8 *str, int sz) {
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
				ERROR_LOG(Log::Mpeg, "Pmp video initialization failed");
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
		for (int i = 0; i < pmp_nBlocks; i++){
			auto lli = PSPPointer<SceMpegLLI>::Create(pmp_videoSource);
			// add source block into pmpframes
			const uint8_t *ptr = Memory::GetPointerRangeOrException(lli->pSrc, lli->iSize);
			if (ptr)
				pmpframes->add(ptr, lli->iSize);
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
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
		if (packet.size != 0)
			avcodec_send_packet(pCodecCtx, &packet);
		int len = avcodec_receive_frame(pCodecCtx, pFrame);
		if (len == 0) {
			got_picture = 1;
		} else if (len == AVERROR(EAGAIN)) {
			got_picture = 0;
		} else {
			got_picture = 0;
		}
#else
		avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, &packet);
#endif
		DEBUG_LOG(Log::Mpeg, "got_picture %d", got_picture);
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
				ERROR_LOG(Log::Mpeg, "Cannot initialize sws conversion context");
				return false;
			}

			// Convert to RGBA
			int swsRet = sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data,
				pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
			if (swsRet < 0){
				ERROR_LOG(Log::Mpeg, "sws_scale: Error while converting %d", swsRet);
				return false;
			}
			// free sws context
			sws_freeContext(img_convert_ctx);

			// update timestamp
#if LIBAVUTIL_VERSION_MAJOR >= 59
			int64_t bestPts = mediaengine->m_pFrame->best_effort_timestamp;
			int64_t ptsDuration = mediaengine->m_pFrame->duration;
#elif LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(55, 58, 100)
			int64_t bestPts = mediaengine->m_pFrame->best_effort_timestamp;
			int64_t ptsDuration = mediaengine->m_pFrame->pkt_duration;
#else
			int64_t bestPts = av_frame_get_best_effort_timestamp(mediaengine->m_pFrame);
			int64_t ptsDuration = av_frame_get_pkt_duration(mediaengine->m_pFrame);
#endif
			if (bestPts != AV_NOPTS_VALUE)
				mediaengine->m_videopts = bestPts + ptsDuration - mediaengine->m_firstTimeStamp;
			else
				mediaengine->m_videopts += ptsDuration;

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

	Do(p, pmp_videoSource);
	Do(p, pmp_nBlocks);
	if (p.mode == PointerWrap::MODE_READ){
		// for loadstate, we will reinitialize the pmp codec
		__VideoPmpShutdown();
	}
}

static u32 sceMpegAvcDecode(u32 mpeg, u32 auAddr, u32 frameWidth, u32 bufferAddr, u32 initAddr) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
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
		return hleLogError(Log::Mpeg, -1, "Bogus mpegringbufferaddr");
	}

	if (!Memory::IsValidRange(bufferAddr, 4) || !Memory::IsValidRange(initAddr, 4)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	u32 buffer = Memory::ReadUnchecked_U32(bufferAddr);
	u32 init = Memory::ReadUnchecked_U32(initAddr);
	DEBUG_LOG(Log::Mpeg, "video: bufferAddr = %08x, *buffer = %08x, *init = %08x", bufferAddr, buffer, init);

	// check and decode pmp video
	bool ispmp = false;
	if (decodePmpVideo(ringbuffer, mpeg)){
		DEBUG_LOG(Log::Mpeg, "Using ffmpeg to decode pmp video");
		ispmp = true;
	}

	if (ringbuffer->packetsRead == 0 || ctx->mediaengine->IsVideoEnd()) {
		return hleDelayResult(hleLogWarning(Log::Mpeg, SCE_MPEG_ERROR_AVC_DECODE_FATAL, "mpeg buffer empty"), "mpeg buffer empty", avcEmptyDelayMs);
	}

	s32 beforeAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;

	// We stored the video stream id here in sceMpegGetAvcAu().
	ctx->mediaengine->setVideoStream(avcAu.esBuffer);

	int accumDelay = 0;

	if (ispmp){
#ifdef USE_FFMPEG
		while (pmp_queue.size() != 0){
			// playing all pmp_queue frames
			ctx->mediaengine->m_pFrameRGB = pmp_queue.front();
			int bufferSize = ctx->mediaengine->writeVideoImage(buffer, frameWidth, ctx->videoPixelMode);
			gpu->PerformWriteFormattedFromMemory(buffer, bufferSize, frameWidth, (GEBufferFormat)ctx->videoPixelMode);
			ctx->avc.avcFrameStatus = 1;
			ctx->videoFrameCount++;

			// free front frame
			accumDelay += 30;
			pmp_queue.pop_front();
		}
#endif
	} else if (ctx->mediaengine->stepVideo(ctx->videoPixelMode)) {
		int bufferSize = ctx->mediaengine->writeVideoImage(buffer, frameWidth, ctx->videoPixelMode);
		gpu->PerformWriteFormattedFromMemory(buffer, bufferSize, frameWidth, (GEBufferFormat)ctx->videoPixelMode);
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

	if (mpegLibVersion >= 0x0105 && mpegLibVersion < 0x010a) {
		//Killzone - Liberation expect , issue #16727
		Memory::WriteOrException_U32(1, initAddr);
	}
	else {
		// Save the current frame's status to initAddr
		Memory::WriteOrException_U32(ctx->avc.avcFrameStatus, initAddr);
	}
	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;

	if (ctx->videoFrameCount <= 1) {
		return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg decode", accumDelay + avcFirstDelayMs);
	} else {
		return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg decode", accumDelay + avcDecodeDelayMs);
	}
	//hleEatMicro(3300);
	//return hleDelayResult(0, "mpeg decode", 200);
}

static u32 sceMpegAvcDecodeStop(u32 mpeg, u32 frameWidth, u32 bufferAddr, u32 statusAddr) {
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(statusAddr)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	// No last frame generated
	Memory::WriteOrException_U32(0, statusAddr);
	return hleLogDebug(Log::Mpeg, 0);
}

static u32 sceMpegUnRegistStream(u32 mpeg, int streamUid) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
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
		DEBUG_LOG(Log::Mpeg, "sceMpegUnRegistStream(%i) : unknown streamID ", streamUid);
		break;
	}
	ctx->streamMap[streamUid] = info;
	info.type = -1;
	info.sid = -1 ;
	info.needsReset = true;
	ctx->isAnalyzed = false;
	return hleNoLog(0);
}

static int sceMpegAvcDecodeDetail(u32 mpeg, u32 detailAddr) {
	if (!Memory::IsValidRange(detailAddr, 36)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	Memory::WriteUnchecked_U32(ctx->avc.avcDecodeResult, detailAddr + 0);
	Memory::WriteUnchecked_U32(ctx->videoFrameCount, detailAddr + 4);
	Memory::WriteUnchecked_U32(ctx->avc.avcDetailFrameWidth, detailAddr + 8);
	Memory::WriteUnchecked_U32(ctx->avc.avcDetailFrameHeight, detailAddr + 12);
	Memory::WriteUnchecked_U32(0, detailAddr + 16);
	Memory::WriteUnchecked_U32(0, detailAddr + 20);
	Memory::WriteUnchecked_U32(0, detailAddr + 24);
	Memory::WriteUnchecked_U32(0, detailAddr + 28);
	Memory::WriteUnchecked_U32(ctx->avc.avcFrameStatus, detailAddr + 32);
	return hleLogDebug(Log::Mpeg, 0);
}

static u32 sceMpegAvcDecodeStopYCbCr(u32 mpeg, u32 bufferAddr, u32 statusAddr) {
	if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValid4AlignedAddress(statusAddr)) {
		return hleLogError(Log::Mpeg, -1, "UNIMPL + invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL + bad mpeg handle");
	}

	ERROR_LOG(Log::Mpeg, "UNIMPL sceMpegAvcDecodeStopYCbCr(%08x, %08x, %08x)", mpeg, bufferAddr, statusAddr);
	Memory::WriteUnchecked_U32(0, statusAddr);
	return hleNoLog(0);
}

static int sceMpegAvcDecodeYCbCr(u32 mpeg, u32 auAddr, u32 bufferAddr, u32 initAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "Bogus mpegringbufferaddr");
	}

	if (ringbuffer->packetsRead == 0 || ctx->mediaengine->IsVideoEnd()) {
		return hleDelayResult(hleLogWarning(Log::Mpeg, SCE_MPEG_ERROR_AVC_DECODE_FATAL, "mpeg buffer empty"), "mpeg buffer empty", avcEmptyDelayMs);
	}

	s32 beforeAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;

	// We stored the video stream id here in sceMpegGetAvcAu().
	ctx->mediaengine->setVideoStream(avcAu.esBuffer);

	if (!Memory::IsValidRange(bufferAddr, 4) || !Memory::IsValidRange(initAddr, 4)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	u32 buffer = Memory::ReadUnchecked_U32(bufferAddr);
	u32 init = Memory::ReadUnchecked_U32(initAddr);
	DEBUG_LOG(Log::Mpeg, "*buffer = %08x, *init = %08x", buffer, init);

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

	if (mpegLibVersion >= 0x010A) {
		// Sunday Vs Magazine Shuuketsu! Choujou Daikessen expect, issue #11060
		Memory::WriteUnchecked_U32(1, initAddr);
	} else {
		// Save the current frame's status to initAddr
		Memory::WriteUnchecked_U32(ctx->avc.avcFrameStatus, initAddr);
	}
	ctx->avc.avcDecodeResult = MPEG_AVC_DECODE_SUCCESS;

	if (ctx->videoFrameCount <= 1)
		return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg decode", avcFirstDelayMs);
	else
		return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg decode", avcDecodeDelayMs);

	//hleEatMicro(3300);
	//return hleDelayResult(0, "mpeg decode", 200);
}

static u32 sceMpegAvcDecodeFlush(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL + bad mpeg handle");
	}

	if ( ctx->videoFrameCount > 0 || ctx->audioFrameCount > 0) {
		//__MpegFinish();
	}
	return hleLogWarning(Log::Mpeg, 0, "UNIMPL");
}

static int sceMpegInitAu(u32 mpeg, u32 bufferAddr, u32 auPointer) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

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
	return hleLogDebug(Log::Mpeg, 0);
}

static int sceMpegQueryAtracEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr) {
	if (!Memory::IsValid4AlignedAddress(esSizeAddr) || !Memory::IsValid4AlignedAddress(outSizeAddr)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	Memory::WriteUnchecked_U32(MPEG_ATRAC_ES_SIZE, esSizeAddr);
	Memory::WriteUnchecked_U32(MPEG_ATRAC_ES_OUTPUT_SIZE, outSizeAddr);
	return hleLogDebug(Log::Mpeg, 0);
}

static int sceMpegRingbufferAvailableSize(u32 ringbufferAddr) {
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ringbufferAddr);
	if (!ringbuffer.IsValid()) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDRESS, "invalid ringbuffer, should crash");
	}

	MpegContext *ctx = getMpegCtx(ringbuffer->mpeg);
	if (!ctx) {
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_NOT_YET_INIT, "bad mpeg handle");
	}

	ctx->mpegRingbufferAddr = ringbufferAddr;

	// PPSSPP doesn't have a fully asynchronous Media Engine thread to constantly drain the buffer.
	// Games like Ridge Racer 2 rely on this function to implicitly update the available size.
	// However, we must NOT overwrite packetsAvail if the stream hasn't been initialized/analyzed yet.
	// This protects manual struct modifications (like in the 'avail' test) prior to stream playback.
	if (ctx->mediaengine && ctx->isAnalyzed) {
		ringbuffer->packetsAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;
	}

	hleEatCycles(2020);
	hleReSchedule("mpeg ringbuffer avail");

	static int lastAvail = 0;
	if (lastAvail != ringbuffer->packetsAvail) {
		DEBUG_LOG(Log::Mpeg, "%i=sceMpegRingbufferAvailableSize(%08x)", ringbuffer->packets - ringbuffer->packetsAvail, ringbufferAddr);
		lastAvail = ringbuffer->packetsAvail;
	} else {
		VERBOSE_LOG(Log::Mpeg, "%i=sceMpegRingbufferAvailableSize(%08x)", ringbuffer->packets - ringbuffer->packetsAvail, ringbufferAddr);
	}
	return hleNoLog(ringbuffer->packets - ringbuffer->packetsAvail);
}

void PostPutAction::run(MipsCall &call) {
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ringAddr_);

	MpegContext *ctx = getMpegCtx(ringbuffer->mpeg);
	int writeOffset = ringbuffer->packetsWritePos % (s32)ringbuffer->packets;

	int packetsAddedThisRound = currentMIPS->r[MIPS_REG_V0];
	if (packetsAddedThisRound > 0) {
		ringbufferPutPacketsAdded += packetsAddedThisRound;
	}

	if (!ctx) {
		_dbg_assert_(false);
		ERROR_LOG(Log::Mpeg, "sceMpegRingbufferPut: bad mpeg handle %08x", ringbuffer->mpeg);
		return;
	}

	// It seems validation is done only by older mpeg libs.
	if (mpegLibVersion < 0x0105 && packetsAddedThisRound > 0) {
		// TODO: Faster / less wasteful validation.
		auto demuxer = std::make_unique<MpegDemux>(packetsAddedThisRound * 2048, 0);
		int readOffset = ringbuffer->packetsRead % (s32)ringbuffer->packets;
		uint32_t bufSize = Memory::ClampValidSizeAt(ringbuffer->data + readOffset * 2048, packetsAddedThisRound * 2048);
		const u8 *buf = Memory::GetPointerOrException(ringbuffer->data + readOffset * 2048);
		bool invalid = false;
		for (uint32_t i = 0; i < bufSize / 2048; ++i) {
			demuxer->addStreamData(buf, 2048);
			buf += 2048;

			if (!demuxer->demux(0xFFFF)) {
				invalid = true;
			}
		}
		if (invalid) {
			// Bail out early - don't accept any of the packets, even the good ones.
			// This happens during legit playback at the Burnout Legends menu!
			ERROR_LOG(Log::Mpeg, "sceMpegRingbufferPut(): invalid mpeg data");
			call.setReturnValue(SCE_MPEG_ERROR_INVALID_VALUE);

			if (mpegLibVersion <= 0x0103) {
				// Act like they were actually added, but don't increment read pos.
				ringbuffer->packetsWritePos += packetsAddedThisRound;
				ringbuffer->packetsAvail += packetsAddedThisRound;
			}
			return;
		}
	}

	if (ringbuffer->packetsRead == 0 && ctx->mediaengine && packetsAddedThisRound > 0) {
		// init mediaEngine
		AnalyzeMpeg(ctx->mpegheader, 2048, ctx);
		ctx->mediaengine->loadStream(ctx->mpegheader, 2048, ringbuffer->packets * ringbuffer->packetSize);
	}
	if (packetsAddedThisRound > 0) {
		if (packetsAddedThisRound > ringbuffer->packets - ringbuffer->packetsAvail) {
			WARN_LOG(Log::Mpeg, "sceMpegRingbufferPut clamping packetsAdded old=%i new=%i", packetsAddedThisRound, ringbuffer->packets - ringbuffer->packetsAvail);
			packetsAddedThisRound = ringbuffer->packets - ringbuffer->packetsAvail;
		}
		const u8 *data = Memory::GetPointerOrException(ringbuffer->data + writeOffset * 2048);
		uint32_t dataSize = Memory::ClampValidSizeAt(ringbuffer->data + writeOffset * 2048, packetsAddedThisRound * 2048);
		int actuallyAdded = ctx->mediaengine == NULL ? 8 : ctx->mediaengine->addStreamData(data, dataSize) / 2048;
		if (actuallyAdded != packetsAddedThisRound) {
			WARN_LOG(Log::Mpeg, "sceMpegRingbufferPut(): unable to enqueue all added packets, going to overwrite some frames (%d != %d)", actuallyAdded, packetsAddedThisRound);
		}
		ringbuffer->packetsRead += packetsAddedThisRound;
		ringbuffer->packetsWritePos += packetsAddedThisRound;
		ringbuffer->packetsAvail += packetsAddedThisRound;

		if (remainingPackets_ > 0) {
			if (packetsAddedThisRound > remainingPackets_) {
				WARN_LOG(Log::Mpeg, "Wrote more packets than expected (added %d, remaining %d).", packetsAddedThisRound, remainingPackets_);
			}
			else if (packetsAddedThisRound < remainingPackets_) {
				// Call the callback again to get more packets.
				writeOffset = ringbuffer->packetsWritePos % (s32)ringbuffer->packets;
				u32 desiredPacketsThisRound = std::min(remainingPackets_, (s32)ringbuffer->packets - writeOffset);

				PostPutAction* action = (PostPutAction*)__KernelCreateAction(actionPostPut);
				action->setRingAddr(ringAddr_);
				action->setRemainingPackets(remainingPackets_ - desiredPacketsThisRound);
				u32 args[3] = { (u32)ringbuffer->data + (u32)writeOffset * 2048, desiredPacketsThisRound, (u32)ringbuffer->callback_args };
				hleEnqueueCall(ringbuffer->callback_addr, 3, args, action); //Calling the callback
			}
		}
	}
	DEBUG_LOG(Log::Mpeg, "packetAdded: %i packetsRead: %i packetsTotal: %i", packetsAddedThisRound, ringbuffer->packetsRead, ringbuffer->packets);

	if (packetsAddedThisRound < 0 && ringbufferPutPacketsAdded == 0) {
		// Return an error.
		call.setReturnValue(packetsAddedThisRound);
	} else {
		call.setReturnValue(ringbufferPutPacketsAdded);
	}
}


// Program signals that it has written data to the ringbuffer and gets a callback ?
static u32 sceMpegRingbufferPut(u32 ringbufferAddr, int numPackets, int available)
{
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ringbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior.
		return hleLogError(Log::Mpeg, -1, "invalid ringbuffer address");
	}

	numPackets = std::min(numPackets, available);
	// Generally, program will call sceMpegRingbufferAvailableSize() before this func.
	// Seems still need to check actual available, Patapon 3 for example.
	numPackets = std::min(numPackets, ringbuffer->packets - ringbuffer->packetsAvail);
	if (numPackets <= 0) {
		return hleLogDebug(Log::Mpeg, 0, "no packets to enqueue");
	}

	MpegContext *ctx = getMpegCtx(ringbuffer->mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle %08x", ringbuffer->mpeg);
	}
	ringbufferPutPacketsAdded = 0;
	// Execute callback function as a direct MipsCall, no blocking here so no messing around with wait states etc
	if (ringbuffer->callback_addr != 0) {
		DEBUG_LOG(Log::Mpeg, "sceMpegRingbufferPut(%08x, %i, %i)", ringbufferAddr, numPackets, available);

		// Call this multiple times until we get numPackets.
		// Normally this would be if it did not read enough, but also if available > packets.
		// Should ultimately return the TOTAL number of returned packets.
		int writeOffset = ringbuffer->packetsWritePos % (s32)ringbuffer->packets;
		if (numPackets > 0) {
			u32 desiredPacketsThisRound = std::min(numPackets, (s32)ringbuffer->packets - writeOffset); //min(How many avail, how many will fit)

			PostPutAction *action = (PostPutAction *)__KernelCreateAction(actionPostPut);
			action->setRingAddr(ringbufferAddr);
			// Old savestate don't use this feature (useRingbufferPutCallbackMulti), just for compatibility.
			action->setRemainingPackets(useRingbufferPutCallbackMulti ? numPackets - desiredPacketsThisRound : 0);

			u32 writePosBefore = ringbuffer->packetsWritePos;
			u32 args[3] = { (u32)ringbuffer->data + (u32)writeOffset * 2048, desiredPacketsThisRound, (u32)ringbuffer->callback_args };
			hleEnqueueCall(ringbuffer->callback_addr, 3, args, action);
		}
	} else {
		ERROR_LOG_REPORT(Log::Mpeg, "sceMpegRingbufferPut: callback_addr zero");
	}
	return hleNoLog(0);
}

static int sceMpegGetAvcAu(u32 mpeg, u32 streamId, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogError(Log::Mpeg, -1, "bad mpeg handle");
	}

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior.
		return hleLogError(Log::Mpeg, -1, "invalid ringbuffer address");
	}

	if (PSP_CoreParameter().compat.flags().MpegAvcWarmUp) {
		if (ctx->mpegwarmUp == 0) {
			ctx->mpegwarmUp++;
			return hleLogDebug(Log::Mpeg, SCE_MPEG_ERROR_NO_DATA, "warming up (%d)", ctx->mpegwarmUp);
		}
	}

	SceMpegAu avcAu;
	avcAu.read(auAddr);

	if (ringbuffer->packetsRead == 0 || ringbuffer->packetsAvail == 0) {
		avcAu.pts = 0;
		avcAu.dts = 0;
		avcAu.write(auAddr);
		// TODO: Does this really reschedule?
		return hleDelayResult(hleLogDebug(Log::Mpeg, SCE_MPEG_ERROR_NO_DATA), "mpeg get avc", 2000);
	}

	auto streamInfo = ctx->streamMap.find(streamId);
	if (streamInfo == ctx->streamMap.end())	{
		return hleLogWarning(Log::Mpeg, -1, "invalid video stream %08x", streamId);
	}

	if (ctx->streamIgnoreMap[streamId]) {
		avcAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;
		avcAu.dts = avcAu.pts - videoTimestampStep;
		avcAu.esBuffer = streamInfo->second.num;
		avcAu.write(auAddr);
		if (Memory::IsValid4AlignedAddress(attrAddr)) {
			Memory::WriteUnchecked_U32(1, attrAddr);
		}
		return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg get avc ignore", 100);
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
		ERROR_LOG(Log::Mpeg, "sceMpegGetAvcAu - video too much ahead");
		// TODO: Does this really reschedule?
		return hleDelayResult(SCE_MPEG_ERROR_NO_DATA, "mpeg get avc", mpegDecodeErrorDelayMs);
	}*/

	int result = 0;

	avcAu.pts = ctx->mediaengine->getVideoTimeStamp() + ctx->mpegFirstTimestamp;
	avcAu.dts = avcAu.pts - videoTimestampStep;

	if (ctx->mediaengine->IsVideoEnd()) {
		INFO_LOG(Log::Mpeg, "video end reach. pts: %i dts: %i", (int)avcAu.pts, (int)ctx->mediaengine->getLastTimeStamp());
		avcAu.dts = -1;
		result = SCE_MPEG_ERROR_NO_DATA;
	}

	// The avcau struct may have been modified by mediaengine, write it back.
	avcAu.write(auAddr);

	if (result == 0) {
		// Jeanne d'Arc return 00000000 as attrAddr here and cause WriteMemoryOrRaiseException error
		if (Memory::IsValidAddress(attrAddr)) {
			Memory::WriteOrException_U32(1, attrAddr);
		}
	}

	// TODO: sceMpegGetAvcAu seems to modify esSize, and delay when it's > 1000 or something.
	// There's definitely more to it, but ultimately it seems games should expect it to delay randomly.
	return hleDelayResult(hleLogDebug(Log::Mpeg, result), "mpeg get avc", 100);
}

static u32 sceMpegFinish()
{
	if (!isMpegInit) {
		WARN_LOG(Log::Mpeg, "sceMpegFinish(...): not initialized");
		// TODO: Need to properly hook module load/unload for this to work right.
		//return SCE_MPEG_ERROR_NOT_YET_INIT;
	} else {
		INFO_LOG(Log::Mpeg, "sceMpegFinish()");
		__VideoPmpShutdown();
	}
	isMpegInit = false;
	//__MpegFinish();
	return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg finish", 250);
}

static u32 sceMpegQueryMemSize() {
	return hleLogDebug(Log::Mpeg, MpegRequiredMem());
}

static int sceMpegGetAtracAu(u32 mpeg, u32 streamId, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior.
		return hleLogWarning(Log::Mpeg, -1, "invalid ringbuffer address");
	}

	SceMpegAu atracAu;
	atracAu.read(auAddr);

	auto streamInfo = ctx->streamMap.find(streamId);
	if (streamInfo != ctx->streamMap.end() && streamInfo->second.needsReset) {
		atracAu.pts = 0;
		streamInfo->second.needsReset = false;
	}
	if (streamInfo == ctx->streamMap.end()) {
		WARN_LOG_REPORT(Log::Mpeg, "sceMpegGetAtracAu: invalid audio stream %08x", streamId);
		// TODO: Why was this changed to not return an error?
	}

	if (streamInfo != ctx->streamMap.end() && ctx->streamIgnoreMap[streamId]) {
		atracAu.pts = ctx->mediaengine->getAudioTimeStamp() + ctx->mpegFirstTimestamp;
		atracAu.dts = atracAu.pts;
		atracAu.esBuffer = streamInfo->second.num;
		atracAu.write(auAddr);
		if (Memory::IsValid4AlignedAddress(attrAddr)) {
			Memory::WriteUnchecked_U32(0, attrAddr);
		}
		return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg get atrac ignore", 100);
	}

	// The audio can end earlier than the video does.
	if (ringbuffer->packetsAvail == 0) {
		atracAu.pts = 0;
		atracAu.dts = 0;
		atracAu.write(auAddr);
		// TODO: Does this really delay?
		return hleDelayResult(hleLogDebug(Log::Mpeg, SCE_MPEG_ERROR_NO_DATA), "mpeg get atrac", mpegDecodeErrorDelayMs);
	}

	// esBuffer is the memory where this au data goes.  We don't write the data to memory.
	// Instead, let's abuse it to keep track of the stream number.
	if (streamInfo != ctx->streamMap.end()) {
		atracAu.esBuffer = streamInfo->second.num;
		ctx->mediaengine->setAudioStream(streamInfo->second.num);
	}

	int result = 0;
	atracAu.pts = ctx->mediaengine->getAudioTimeStamp() + ctx->mpegFirstTimestamp;
	atracAu.dts = atracAu.pts;

	if (ctx->mediaengine->IsNoAudioData()) {
		atracAu.dts = -1;
		result = SCE_MPEG_ERROR_NO_DATA;
	}

	if (ctx->atracRegistered && ctx->mediaengine->IsNoAudioData() && !ctx->endOfAudioReached) {
		WARN_LOG(Log::Mpeg, "Audio end reach. pts: %i dts: %i", (int)atracAu.pts, (int)ctx->mediaengine->getLastTimeStamp());
		ctx->endOfAudioReached = true;
	}

	atracAu.write(auAddr);

	if (result == 0) {
		// 3rd birthday return 00000000 as attrAddr here and cause WriteMemoryOrRaiseException error
		if (Memory::IsValid4AlignedAddress(attrAddr)) {
			Memory::WriteUnchecked_U32(0, attrAddr);
		}
	}

	// TODO: Not clear on exactly when this delays.
	return hleDelayResult(hleLogDebug(Log::Mpeg, result), "mpeg get atrac", 100);
}

static int sceMpegQueryPcmEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (!Memory::IsValid4AlignedAddress(esSizeAddr) || !Memory::IsValid4AlignedAddress(outSizeAddr)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	Memory::WriteUnchecked_U32(MPEG_PCM_ES_SIZE, esSizeAddr);
	Memory::WriteUnchecked_U32(MPEG_PCM_ES_OUTPUT_SIZE, outSizeAddr);
	return hleLogError(Log::Mpeg, 0, "UNIMPL");
}


static u32 sceMpegChangeGetAuMode(u32 mpeg, int streamUid, int mode)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "bad mpeg handle");
	}
	if (mode != MPEG_AU_MODE_DECODE && mode != MPEG_AU_MODE_SKIP) {
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "UNIMPL / bad mode");
	}

	auto stream = ctx->streamMap.find(streamUid);
	if (stream == ctx->streamMap.end()) {
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "UNIMPL / unknown streamID");
	} else {
		StreamInfo &info = stream->second;
		DEBUG_LOG(Log::Mpeg, "sceMpegChangeGetAuMode(%08x, %i, %i): changing type=%d", mpeg, streamUid, mode, info.type);
		ctx->streamIgnoreMap[streamUid] = (mode == MPEG_AU_MODE_SKIP);
	}
	return hleNoLog(0);
}

static u32 sceMpegChangeGetAvcAuMode(u32 mpeg, u32 stream_addr, int mode) {
	if (!Memory::IsValidAddress(stream_addr)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL + bad mpeg handle");
	}

	ERROR_LOG_REPORT_ONCE(mpegChangeAvcAu, Log::Mpeg, "UNIMPL sceMpegChangeGetAvcAuMode(%08x, %08x, %i)", mpeg, stream_addr, mode);
	return 0;
}

static u32 sceMpegGetPcmAu(u32 mpeg, int streamUid, u32 auAddr, u32 attrAddr)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}
	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (!ringbuffer.IsValid()) {
		// Would have crashed before, TODO test behavior
		return hleLogWarning(Log::Mpeg, -1, "invalid ringbuffer address");
	}
	if (!Memory::IsValidAddress(streamUid)) {
		return hleLogWarning(Log::Mpeg, SCE_MPEG_ERROR_INVALID_ADDR, "didn't get a fake stream, bad streamUid address");
	}

	SceMpegAu atracAu;
	atracAu.read(auAddr);
	auto streamInfo = ctx->streamMap.find(streamUid);
	if (streamInfo == ctx->streamMap.end()) {
		return hleLogWarning(Log::Mpeg, -1, "bad streamUid ");
	}

	atracAu.write(auAddr);
	u32 attr = 1 << 7; // Sampling rate (1 = 44.1kHz).
	attr |= 2;         // Number of channels (1 - MONO / 2 - STEREO).
	if (Memory::IsValidAddress(attrAddr))
		Memory::WriteUnchecked_U32(attr, attrAddr);

	ERROR_LOG_REPORT_ONCE(mpegPcmAu, Log::Mpeg, "UNIMPL sceMpegGetPcmAu(%08x, %i, %08x, %08x)", mpeg, streamUid, auAddr, attrAddr);
	return hleNoLog(0);
}

static int __MpegRingbufferQueryPackNum(u32 memorySize) {
	return memorySize / (2048 + 104);
}

static int sceMpegRingbufferQueryPackNum(u32 memorySize) {
	DEBUG_LOG(Log::Mpeg, "sceMpegRingbufferQueryPackNum(%i)", memorySize);
	return __MpegRingbufferQueryPackNum(memorySize);
}

static u32 sceMpegFlushAllStream(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL + bad mpeg handle");
	}

	ctx->isAnalyzed = false;

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (ringbuffer.IsValid()) {
		ringbuffer->packetsAvail = 0;
		ringbuffer->packetsRead = 0;
		ringbuffer->packetsWritePos = 0;
	}

	return hleLogInfo(Log::Mpeg, 0, "UNIMPL");
}

static u32 sceMpegFlushStream(u32 mpeg, int stream_addr) {
	if (!Memory::IsValidAddress(stream_addr)) {
		return hleLogError(Log::Mpeg, -1, "UNIMPL / invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
		return -1;
	}

	//__MpegFinish();
	return hleLogError(Log::Mpeg, 0, "UNIMPL");
}

static u32 sceMpegAvcCopyYCbCr(u32 mpeg, u32 sourceAddr, u32 YCbCrAddr) {
	if (!Memory::IsValidAddress(sourceAddr) || !Memory::IsValidAddress(YCbCrAddr)) {
		return hleLogError(Log::Mpeg, -1, "UNIMPL / invalid address(es)");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogError(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	// This is very common.
	return hleLogDebug(Log::Mpeg, 0, "UNIMPL");
}

static u32 sceMpegAtracDecode(u32 mpeg, u32 auAddr, u32 bufferAddr, int init)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	if (!Memory::IsValidAddress(bufferAddr)) {
		return hleLogWarning(Log::Mpeg, -1, "invalid addresses");
	}

	SceMpegAu atracAu;
	atracAu.read(auAddr);

	// We kept track of the stream number here in sceMpegGetAtracAu().
	ctx->mediaengine->setAudioStream(atracAu.esBuffer);

	Memory::Memset(bufferAddr, 0, MPEG_ATRAC_ES_OUTPUT_SIZE, "MpegAtracClear");
	ctx->mediaengine->getAudioSamples(bufferAddr);
	atracAu.pts = ctx->mediaengine->getAudioTimeStamp() + ctx->mpegFirstTimestamp;

	atracAu.write(auAddr);

	auto ringbuffer = PSPPointer<SceMpegRingBuffer>::Create(ctx->mpegRingbufferAddr);
	if (ringbuffer.IsValid()) {
		ringbuffer->packetsAvail = ringbuffer->packets - ctx->mediaengine->getRemainSize() / 2048;
	}

	return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg atrac decode", atracDecodeDelayMs);
	//hleEatMicro(4000);
	//return hleDelayResult(0, "mpeg atrac decode", 200);
}

// YCbCr -> RGB color space conversion
static u32 sceMpegAvcCsc(u32 mpeg, u32 sourceAddr, u32 rangeAddr, int frameWidth, u32 destAddr)
{
	if (!Memory::IsValidAddress(sourceAddr) || !Memory::IsValidRange(rangeAddr, 16) || !Memory::IsValidAddress(destAddr)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	if (frameWidth == 0) {
		if (!ctx->defaultFrameWidth) {
			frameWidth = ctx->avc.avcDetailFrameWidth;
		} else {
			frameWidth = ctx->defaultFrameWidth;
		}
	}

	int x = Memory::ReadUnchecked_U32(rangeAddr);
	int y = Memory::ReadUnchecked_U32(rangeAddr + 4);
	int width = Memory::ReadUnchecked_U32(rangeAddr + 8);
	int height = Memory::ReadUnchecked_U32(rangeAddr + 12);

	if (x < 0 || y < 0 || width < 0 || height < 0) {
		WARN_LOG(Log::Mpeg, "sceMpegAvcCsc(%08x, %08x, %08x, %i, %08x) returning ERROR_INVALID_VALUE", mpeg, sourceAddr, rangeAddr, frameWidth, destAddr);
		return SCE_KERNEL_ERROR_INVALID_VALUE;
	}

	int destSize = ctx->mediaengine->writeVideoImageWithRange(destAddr, frameWidth, ctx->videoPixelMode, x, y, width, height);
	gpu->PerformWriteFormattedFromMemory(destAddr, destSize, frameWidth, (GEBufferFormat)ctx->videoPixelMode);

	// Do not use avcDecodeDelayMs 's value
	// Will cause video 's screen dislocation in Bleach heat of soul 6
	// https://github.com/hrydgard/ppsspp/issues/5535
	// If do not use DelayResult,Wil cause flickering in Dengeki no Pilot: Tenkuu no Kizuna
	// https://github.com/hrydgard/ppsspp/issues/7549

	return hleDelayResult(hleLogDebug(Log::Mpeg, 0), "mpeg avc csc", avcCscDelayMs);
}

static u32 sceMpegRingbufferDestruct(u32 ringbufferAddr) {
	// Apparently, does nothing.
	return hleLogDebug(Log::Mpeg, 0);
}

static u32 sceMpegAvcInitYCbCr(u32 mpeg, int mode, int width, int height, u32 ycbcr_addr)
{
	if (!Memory::IsValidAddress(ycbcr_addr)) {
		return hleLogError(Log::Mpeg, -1, "invalid addresses");
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	WARN_LOG_ONCE(sceMpegAvcInitYCbCr, Log::Mpeg, "UNIMPL sceMpegAvcInitYCbCr(%08x, %i, %i, %i, %08x)", mpeg, mode, width, height, ycbcr_addr);
	return hleNoLog(0);
}

static int sceMpegAvcQueryYCbCrSize(u32 mpeg, u32 mode, u32 width, u32 height, u32 resultAddr)
{
	if ((width & 15) != 0 || (height & 15) != 0 || height > 272 || width > 480)	{
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "sceMpegAvcQueryYCbCrSize: bad w/h %i x %i", width, height);
	}

	int size = (width / 2) * (height / 2) * 6 + 128;
	if (Memory::IsValidAddress(resultAddr)) {
		Memory::WriteUnchecked_U32(size, resultAddr);
	}
	return hleLogDebug(Log::Mpeg, 0);
}

static u32 sceMpegQueryUserdataEsSize(u32 mpeg, u32 esSizeAddr, u32 outSizeAddr)
{
	if (!Memory::IsValidAddress(esSizeAddr) || !Memory::IsValidAddress(outSizeAddr)) {
		ERROR_LOG(Log::Mpeg, "sceMpegQueryUserdataEsSize(%08x, %08x, %08x): invalid addresses", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		WARN_LOG(Log::Mpeg, "sceMpegQueryUserdataEsSize(%08x, %08x, %08x): bad mpeg handle", mpeg, esSizeAddr, outSizeAddr);
		return -1;
	}

	// Checked above.
	Memory::WriteUnchecked_U32(MPEG_DATA_ES_SIZE, esSizeAddr);
	Memory::WriteUnchecked_U32(MPEG_DATA_ES_OUTPUT_SIZE, outSizeAddr);

	return hleLogDebug(Log::Mpeg, 0);
}

static u32 sceMpegAvcResourceGetAvcDecTopAddr(u32 mpeg) {
	if ((sceMpegAvcResourceFlags & MPEG_AVC_RESOURCE_FLAG) == 0) {
		return hleLogDebug(Log::Mpeg, 0);
	}

	u32 addr = sceMpegAvcResourceDataAddr & 0xFFC00000;
	return hleLogDebug(Log::Mpeg, addr);
}

static u32 sceMpegAvcResourceFinish(u32 mpeg) {
	return hleLogInfo(Log::Mpeg, 0, "UNIMPL");
}

static u32 sceMpegAvcResourceGetAvcEsBuf(u32 mpeg) {
	if ((sceMpegAvcResourceFlags & MPEG_AVC_RESOURCE_FLAG) == 0) {
		return hleLogDebug(Log::Mpeg, 0);
	}

	return hleLogDebug(Log::Mpeg, sceMpegAvcResourceDataAddr);
}

static u32 sceMpegAvcResourceInit(u32 mpeg) {
	if (mpeg != 1) {
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE);
	}

	if ((sceMpegAvcResourceFlags & MPEG_AVC_RESOURCE_FLAG) != 0) {
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_ALREADY_INIT);
	}

	// Allocate memory for the AVC resource
	// In a real implementation, this would use sceKernelCreateFpl and sceKernelAllocateFpl
	// For PPSSPP, we'll simulate the allocation
	sceMpegAvcResourceAddr = 0x10000000; // Simulated base address
	sceMpegAvcResourceDataAddr = sceMpegAvcResourceAddr + 8; // Offset for data
	sceMpegAvcResourceFlags |= MPEG_AVC_RESOURCE_FLAG;

	return hleLogDebug(Log::Mpeg, 0);
}

// TODO: SIMD this (or rather, the caller).
static u32 convertABGRToYCbCr(u32 abgr) {
	//see http://en.wikipedia.org/wiki/Yuv#Y.27UV444_to_RGB888_conversion for more information.
	u8  r = (abgr >>  0) & 0xFF;
	u8  g = (abgr >>  8) & 0xFF;
	u8  b = (abgr >> 16) & 0xFF;
	int  y = 0.299f * r + 0.587f * g + 0.114f * b + 0;
	int cb = -0.169f * r - 0.331f * g + 0.499f * b + 128.0f;
	int cr = 0.499f * r - 0.418f * g - 0.0813f * b + 128.0f;

	// clamp values before packing.
	if (y > 0xFF) {
		y = 0xFF;
	} else if ( y < 0) {
		y = 0;
	}
	if (cb > 0xFF) {
		cb = 0xFF;
	} else if (cb < 0) {
		cb = 0;
	}
	if (cr > 0xFF) {
		cr = 0xFF;
	} else if (cr < 0) {
		cr = 0;
	}

	return (y << 16) | (cb << 8) | cr;
}

// bufferOutputAddr is checked in the caller.
static int __MpegAvcConvertToYuv420(const void *data, u32 bufferOutputAddr, int width, int height) {
	u32 *imageBuffer = (u32*)data;
	int sizeY = width * height;
	int sizeCb = sizeY >> 2;
	u8 *Y = Memory::GetPointerWriteRangeOrException(bufferOutputAddr, sizeY + sizeCb + sizeCb);
	u8 *Cb = Y + sizeY;
	u8 *Cr = Cb + sizeCb;

	if (!Y)
		return hleLogError(Log::Mpeg, 0, "Bad output buffer pointer for yuv conv: %08x", bufferOutputAddr);

	for (int y = 0; y < height; y += 2) {
		for (int x = 0; x < width; x += 2) {
			u32 abgr0 = imageBuffer[width * (y + 0) + x + 0];
			u32 abgr1 = imageBuffer[width * (y + 0) + x + 1];
			u32 abgr2 = imageBuffer[width * (y + 1) + x + 0];
			u32 abgr3 = imageBuffer[width * (y + 1) + x + 1];

			u32 yCbCr0 = convertABGRToYCbCr(abgr0);
			u32 yCbCr1 = convertABGRToYCbCr(abgr1);
			u32 yCbCr2 = convertABGRToYCbCr(abgr2);
			u32 yCbCr3 = convertABGRToYCbCr(abgr3);

			Y[width * (y + 0) + x + 0] = (yCbCr0 >> 16) & 0xFF;
			Y[width * (y + 0) + x + 1] = (yCbCr1 >> 16) & 0xFF;
			Y[width * (y + 1) + x + 0] = (yCbCr2 >> 16) & 0xFF;
			Y[width * (y + 1) + x + 1] = (yCbCr3 >> 16) & 0xFF;

			Cb[(width >> 1) * (y >> 1) + (x >> 1)] = (yCbCr0 >> 8) & 0xFF;
			Cr[(width >> 1) * (y >> 1) + (x >> 1)] = yCbCr0 & 0xFF;
		}
	}
	return (width << 16) | height;
}

static int sceMpegAvcConvertToYuv420(u32 mpeg, u32 bufferOutputAddr, u32 bufferAddr, int unknown2) {
	if (!Memory::IsValidAddress(bufferOutputAddr))
		return hleLogError(Log::Mpeg, SCE_MPEG_ERROR_INVALID_VALUE, "invalid addresses");

	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx)
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");

	if (ctx->mediaengine->m_buffer == 0)
		return hleLogWarning(Log::Mpeg, SCE_MPEG_ERROR_AVC_INVALID_VALUE, "m_buffer is zero");

	const u8 *data = ctx->mediaengine->getFrameImage();
	int width = ctx->mediaengine->m_desWidth;
	int height = ctx->mediaengine->m_desHeight;

	if (data) {
		__MpegAvcConvertToYuv420(data, bufferOutputAddr, width, height);
	}
	return hleLogDebug(Log::Mpeg, (width << 16) | height);
}

static int sceMpegGetUserdataAu(u32 mpeg, u32 streamUid, u32 auAddr, u32 resultAddr) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "bad mpeg handle");
	}

	if (Memory::IsValidRange(resultAddr, 8)) {
		// TODO: Are these at all right?  Seen in Phantasy Star Portable 2.
		Memory::WriteUnchecked_U32(0, resultAddr);
		Memory::WriteUnchecked_U32(0, resultAddr + 4);
	}

	// We currently can't demux userdata so this seems like the best thing to return in the meantime..
	// Then we probably shouldn't do the above writes? but it works...
	return hleLogDebug(Log::Mpeg, SCE_MPEG_ERROR_NO_DATA);
}

static u32 sceMpegNextAvcRpAu(u32 mpeg, u32 streamUid) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegNextAvcRpAu(%08x, %08x)", mpeg, streamUid);
	return 0;
}

static u32 sceMpegGetAvcNalAu(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegGetAvcNalAu(%08x)", mpeg);
	return 0;
}

static u32 sceMpegAvcDecodeDetailIndex(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegAvcDecodeDetailIndex(%08x)", mpeg);
	return 0;
}

static u32 sceMpegAvcDecodeDetail2(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegAvcDecodeDetail2(%08x)", mpeg);
	return 0;
}

static u32 sceMpegGetAvcEsAu(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegGetAvcEsAu(%08x)", mpeg);
	return 0;
}

static u32 sceMpegAvcCscInfo(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegAvcCscInfo(%08x)", mpeg);
	return 0;
}

static u32 sceMpegAvcCscMode(u32 mpeg) {
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegAvcCscMode(%08x)", mpeg);
	return 0;
}

static u32 sceMpegFlushAu(u32 mpeg)
{
	MpegContext *ctx = getMpegCtx(mpeg);
	if (!ctx) {
		return hleLogWarning(Log::Mpeg, -1, "UNIMPL / bad mpeg handle");
	}

	ERROR_LOG_REPORT(Log::Mpeg, "UNIMPL sceMpegFlushAu(%08x)", mpeg);

	return 0;
}

const HLEFunction sceMpeg[] =
{
	{0XE1CE83A7, &WrapI_UUUU<sceMpegGetAtracAu>,               "sceMpegGetAtracAu",                  'i', "xxxx"   },
	{0XFE246728, &WrapI_UUUU<sceMpegGetAvcAu>,                 "sceMpegGetAvcAu",                    'i', "xxxx"   },
	{0XD8C5F121, &WrapU_UUUUUUU<sceMpegCreate>,                "sceMpegCreate",                      'x', "xxxxxxx" ,HLE_CLEAR_STACK_BYTES, 0xA8},
	{0XF8DCB679, &WrapI_UUU<sceMpegQueryAtracEsSize>,          "sceMpegQueryAtracEsSize",            'i', "xxx"    },
	{0XC132E22F, &WrapU_V<sceMpegQueryMemSize>,                "sceMpegQueryMemSize",                'x', "" ,HLE_CLEAR_STACK_BYTES, 0x18},
	{0X21FF80E4, &WrapI_UUU<sceMpegQueryStreamOffset>,         "sceMpegQueryStreamOffset",           'i', "xxx",HLE_CLEAR_STACK_BYTES, 0x18},
	{0X611E9E11, &WrapU_UU<sceMpegQueryStreamSize>,            "sceMpegQueryStreamSize",             'x', "xx",HLE_CLEAR_STACK_BYTES, 0x8},
	{0X42560F23, &WrapI_UUU<sceMpegRegistStream>,              "sceMpegRegistStream",                'i', "xxx" ,HLE_CLEAR_STACK_BYTES, 0x48},
	{0X591A4AA2, &WrapU_UI<sceMpegUnRegistStream>,             "sceMpegUnRegistStream",              'x', "xi" ,HLE_CLEAR_STACK_BYTES, 0x18 },
	{0X707B7629, &WrapU_U<sceMpegFlushAllStream>,              "sceMpegFlushAllStream",              'x', "x"      },
	{0X500F0429, &WrapU_UI<sceMpegFlushStream>,                "sceMpegFlushStream",                 'x', "xi"     },
	{0XA780CF7E, &WrapI_U<sceMpegMallocAvcEsBuf>,              "sceMpegMallocAvcEsBuf",              'i', "x"      },
	{0XCEB870B1, &WrapI_UI<sceMpegFreeAvcEsBuf>,               "sceMpegFreeAvcEsBuf",                'i', "xi"     },
	{0X167AFD9E, &WrapI_UUU<sceMpegInitAu>,                    "sceMpegInitAu",                      'i', "xxx"    },
	{0X682A619B, &WrapU_V<sceMpegInit>,                        "sceMpegInit",                        'x', "" ,HLE_CLEAR_STACK_BYTES, 0x48},
	{0X606A4649, &WrapI_U<sceMpegDelete>,                      "sceMpegDelete",                      'i', "x",HLE_CLEAR_STACK_BYTES, 0x18},
	{0X874624D6, &WrapU_V<sceMpegFinish>,                      "sceMpegFinish",                      'x', "" ,HLE_CLEAR_STACK_BYTES, 0x18},
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
	{0XF5E7EA31, &WrapI_UUUI<sceMpegAvcConvertToYuv420>,       "sceMpegAvcConvertToYuv420",          'x', "xxxi"   },
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
	RegisterHLEModule("sceMpeg", ARRAY_SIZE(sceMpeg), sceMpeg);
}

// This function is currently only been used for PMP videos
// p pointing to a SceMpegLLI structure consists of video frame blocks.
static u32 sceMpegBasePESpacketCopy(u32 p)
{
	pmp_videoSource = p;
	pmp_nBlocks = 0;
	u32 current = p;
	while (current != 0 && pmp_nBlocks < 4096) {
		auto lli = PSPPointer<SceMpegLLI>::Create(current);
		if (!lli.IsValid()) {
			return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "lli=%08x", current);
		}
		const u32 source = lli->pSrc;
		const u32 size = (u32)lli->iSize & 0xFFF;
		u32 destination = lli->pDst;
		if (size == 0 || !Memory::IsValidRange(source, size)) {
			return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_SIZE,
				"source=%08x size=%u", source, size);
		}
		if (!Memory::IsValidRange(destination, size) &&
			Memory::IsValidRange(vshMeMemoryBase + destination, size)) {
			destination += vshMeMemoryBase;
		}
		if (!Memory::IsValidRange(destination, size)) {
			return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR,
				"destination=%08x meBase=%08x size=%u", lli->pDst, vshMeMemoryBase, size);
		}
		Memory::Memcpy(destination, source, size, "VSHMpegPESCopy");
		NotifyMemInfo(MemBlockFlags::WRITE, destination, size, "VSHMpegPESCopy");
		pmp_nBlocks++;
		current = lli->Next;
	}
	if (current != 0) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_SIZE, "linked list too long");
	}
	mpegPESCopyCalls++;
	if (mpegPESCopyCalls <= 3) {
		NOTICE_LOG(Log::Mpeg, "Direct VSH copied %d MPEG PES block(s) through ME base %08x", pmp_nBlocks, vshMeMemoryBase);
	}
	return hleLogDebug(Log::Mpeg, 0, "source=%08x blocks=%d", p, pmp_nBlocks);
}

static bool UntileVSHVideoFrame(u32 cscStructAddr, int width, int height,
	std::vector<u8> *luma, std::vector<u8> *cb, std::vector<u8> *cr) {
	if (!Memory::IsValidRange(cscStructAddr, 48) || width <= 0 || height <= 0 ||
		(width & 15) != 0 || (height & 1) != 0) {
		return false;
	}
	u32 sourceAddr[8];
	for (int i = 0; i < 8; ++i) {
		sourceAddr[i] = Memory::ReadUnchecked_U32(cscStructAddr + 16 + i * 4);
	}
	const int width2 = width / 2;
	const int height2 = height / 2;
	const u32 sizeY1 = ((width + 16) >> 5) * (height >> 1) * 16;
	const u32 sizeY2 = (width >> 5) * (height >> 1) * 16;
	const u32 sizeC1 = sizeY1 / 2;
	const u32 sizeC2 = sizeY2 / 2;
	const u32 sizes[8] = {sizeY1, sizeY2, sizeY1, sizeY2, sizeC1, sizeC2, sizeC1, sizeC2};
	for (int i = 0; i < 8; ++i) {
		if (!Memory::IsValidRange(sourceAddr[i], sizes[i])) {
			return false;
		}
	}
	luma->assign((size_t)width * height, 0);
	cb->assign((size_t)width2 * height2, 0x80);
	cr->assign((size_t)width2 * height2, 0x80);
	const u8 *source[8];
	for (int i = 0; i < 8; ++i) {
		source[i] = Memory::GetPointerUnchecked(sourceAddr[i]);
	}

	size_t n = 0;
	for (int x = 0; x < width; x += 32) {
		for (int y = 0; y < height; y += 2) {
			memcpy(&(*luma)[(size_t)y * width + x], source[0] + n, 16);
			n += 16;
		}
	}
	n = 0;
	for (int x = 16; x < width; x += 32) {
		for (int y = 0; y < height; y += 2) {
			memcpy(&(*luma)[(size_t)y * width + x], source[1] + n, 16);
			n += 16;
		}
	}
	n = 0;
	for (int x = 0; x < width; x += 32) {
		for (int y = 1; y < height; y += 2) {
			memcpy(&(*luma)[(size_t)y * width + x], source[2] + n, 16);
			n += 16;
		}
	}
	n = 0;
	for (int x = 16; x < width; x += 32) {
		for (int y = 1; y < height; y += 2) {
			memcpy(&(*luma)[(size_t)y * width + x], source[3] + n, 16);
			n += 16;
		}
	}
	for (int section = 0; section < 4; ++section) {
		const int xStart = section >= 2 ? 8 : 0;
		const int yStart = section & 1;
		n = 0;
		for (int x = xStart; x < width2; x += 16) {
			for (int y = yStart; y < height2; y += 2) {
				for (int xx = 0; xx < 8; ++xx) {
					const size_t dest = (size_t)y * width2 + x + xx;
					(*cb)[dest] = source[4 + section][n++];
					(*cr)[dest] = source[4 + section][n++];
				}
			}
		}
	}
	return true;
}

static inline u32 VSHYUVToRGBA(u8 y, u8 cb, u8 cr) {
	const int c = std::max(0, (int)y - 16);
	const int d = (int)cb - 128;
	const int e = (int)cr - 128;
	const u8 r = (u8)std::clamp((298 * c + 409 * e + 128) >> 8, 0, 255);
	const u8 g = (u8)std::clamp((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255);
	const u8 b = (u8)std::clamp((298 * c + 516 * d + 128) >> 8, 0, 255);
	return r | ((u32)g << 8) | ((u32)b << 16) | 0xFF000000;
}

static int ConvertVSHVideoFrame(u32 destAddr, int bufferWidth, u32 cscStructAddr,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	if (!Memory::IsValidRange(cscStructAddr, 48)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "csc=%08x", cscStructAddr);
	}
	const int width = (int)Memory::ReadUnchecked_U32(cscStructAddr + 4) << 4;
	const int height = (int)Memory::ReadUnchecked_U32(cscStructAddr + 0) << 4;
	if (bufferWidth == 0) {
		bufferWidth = mpegBaseDefaultBufferWidth != 0 ? mpegBaseDefaultBufferWidth : width;
	}
	rangeX = std::clamp(rangeX, 0, width);
	rangeY = std::clamp(rangeY, 0, height);
	rangeWidth = std::clamp(rangeWidth, 0, width - rangeX);
	rangeHeight = std::clamp(rangeHeight, 0, height - rangeY);
	const int bytesPerPixel = mpegBasePixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	if (bufferWidth < rangeWidth || bufferWidth > 2048 ||
		!Memory::IsValidRange(destAddr, (u32)bufferWidth * std::max(1, rangeHeight) * bytesPerPixel)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR,
			"dest=%08x stride=%d range=%dx%d", destAddr, bufferWidth, rangeWidth, rangeHeight);
	}

	std::vector<u8> luma;
	std::vector<u8> cb;
	std::vector<u8> cr;
	if (!UntileVSHVideoFrame(cscStructAddr, width, height, &luma, &cb, &cr)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "invalid tiled YCbCr frame");
	}
	u8 *dest = Memory::GetPointerWriteUnchecked(destAddr);
	for (int y = 0; y < rangeHeight; ++y) {
		for (int x = 0; x < rangeWidth; ++x) {
			const int sourceX = rangeX + x;
			const int sourceY = rangeY + y;
			const size_t yIndex = (size_t)sourceY * width + sourceX;
			const size_t cIndex = (size_t)(sourceY / 2) * (width / 2) + sourceX / 2;
			const u32 rgba = VSHYUVToRGBA(luma[yIndex], cb[cIndex], cr[cIndex]);
			if (bytesPerPixel == 4) {
				((u32 *)dest)[(size_t)y * bufferWidth + x] = rgba;
			} else {
				u16 value = 0;
				switch (mpegBasePixelMode) {
				case GE_CMODE_16BIT_BGR5650: value = RGBA8888ToRGB565(rgba); break;
				case GE_CMODE_16BIT_ABGR5551: value = RGBA8888ToRGBA5551(rgba); break;
				case GE_CMODE_16BIT_ABGR4444: value = RGBA8888ToRGBA4444(rgba); break;
				default: break;
				}
				((u16 *)dest)[(size_t)y * bufferWidth + x] = value;
			}
		}
	}
	NotifyMemInfo(MemBlockFlags::WRITE, destAddr, (u32)bufferWidth * rangeHeight * bytesPerPixel, "VSHVideoCSC");
	mpegBaseCscCalls++;
	if (mpegBaseCscCalls <= 3) {
		NOTICE_LOG(Log::Mpeg, "Direct VSH CSC #%u: dest=%08x stride=%d image=%dx%d range=%d,%d %dx%d mode=%d",
			mpegBaseCscCalls, destAddr, bufferWidth, width, height, rangeX, rangeY, rangeWidth, rangeHeight, mpegBasePixelMode);
	}
	return hleDelayResult(hleLogDebug(Log::Mpeg, 0, "dest=%08x %dx%d", destAddr, rangeWidth, rangeHeight),
		"VSH video CSC", 4000);
}

static int sceMpegBaseCscInit(int bufferWidth) {
	// The PSP routine selects the default AVC YCbCr-to-RGB coefficients and
	// clears alpha. PPSSPP's host conversion already uses those defaults; no
	// persistent guest buffer is returned by this call.
	mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;
	mpegBaseDefaultBufferWidth = bufferWidth;
	return hleLogDebug(Log::Mpeg, 0, "bufferWidth=%d", bufferWidth);
}

static int sceMpegbaseSetPixelMode(int internalPixelMode) {
	switch (internalPixelMode) {
	case 0: mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888; break;
	case 1: mpegBasePixelMode = GE_CMODE_16BIT_BGR5650; break;
	case 2: mpegBasePixelMode = GE_CMODE_16BIT_ABGR5551; break;
	case 3: mpegBasePixelMode = GE_CMODE_16BIT_ABGR4444; break;
	default: return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "mode=%d", internalPixelMode);
	}
	return hleLogDebug(Log::Mpeg, 0, "mode=%d", internalPixelMode);
}

static int sceMpegBaseSetDefaultBufferWidth(int bufferWidth, u32 coefficientsAddr) {
	mpegBaseDefaultBufferWidth = bufferWidth;
	return hleLogDebug(Log::Mpeg, 0, "bufferWidth=%d coefficients=%08x", bufferWidth, coefficientsAddr);
}

static int sceMpegBaseCscAvc(u32 destAddr, u32 unknownAddr, int bufferWidth, u32 cscStructAddr) {
	if (!Memory::IsValidRange(cscStructAddr, 48)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR);
	}
	const int width = (int)Memory::ReadUnchecked_U32(cscStructAddr + 4) << 4;
	const int height = (int)Memory::ReadUnchecked_U32(cscStructAddr + 0) << 4;
	return ConvertVSHVideoFrame(destAddr, bufferWidth, cscStructAddr, 0, 0, width, height);
}

static int sceMpegBaseCscAvcRange(u32 destAddr, u32 unknownAddr, u32 rangeAddr, u32 bufferWidth, u32 cscStructAddr) {
	if (!Memory::IsValidRange(rangeAddr, 16)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "range=%08x", rangeAddr);
	}
	return ConvertVSHVideoFrame(destAddr, bufferWidth, cscStructAddr,
		(int)Memory::ReadUnchecked_U32(rangeAddr + 0) << 4,
		(int)Memory::ReadUnchecked_U32(rangeAddr + 4) << 4,
		(int)Memory::ReadUnchecked_U32(rangeAddr + 8) << 4,
		(int)Memory::ReadUnchecked_U32(rangeAddr + 12) << 4);
}

static int sceMpegBaseYCrCbCopy(u32 destStructAddr, u32 sourceStructAddr, int flags) {
	if (!Memory::IsValidRange(destStructAddr, 48) || !Memory::IsValidRange(sourceStructAddr, 48)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR,
			"dest=%08x source=%08x", destStructAddr, sourceStructAddr);
	}
	const u32 width = Memory::ReadUnchecked_U32(sourceStructAddr + 4);
	const u32 height = Memory::ReadUnchecked_U32(sourceStructAddr + 0);
	const u32 size1 = ((width + 16) >> 5) * (height >> 1);
	const u32 size2 = (width >> 5) * (height >> 1);
	auto copySection = [&](int section, u32 blocks) -> bool {
		const u32 source = Memory::ReadUnchecked_U32(sourceStructAddr + 16 + section * 4);
		const u32 dest = Memory::ReadUnchecked_U32(destStructAddr + 16 + section * 4);
		const u32 size = blocks << 4;
		if (!Memory::IsValidRange(source, size) || !Memory::IsValidRange(dest, size)) {
			return false;
		}
		Memory::Memcpy(dest, source, size, "VSHMpegYCbCrCopy");
		NotifyMemInfo(MemBlockFlags::WRITE, dest, size, "VSHMpegYCbCrCopy");
		return true;
	};
	if ((flags & 1) != 0 &&
		(!copySection(0, size1) || !copySection(1, size2) ||
		 !copySection(4, size1 >> 1) || !copySection(5, size2 >> 1))) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid even-field buffers");
	}
	if ((flags & 2) != 0 &&
		(!copySection(2, size1) || !copySection(3, size2) ||
		 !copySection(6, size1 >> 1) || !copySection(7, size2 >> 1))) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "invalid odd-field buffers");
	}
	return hleLogDebug(Log::Mpeg, 0, "dest=%08x source=%08x flags=%d %ux%u",
		destStructAddr, sourceStructAddr, flags, width, height);
}

const HLEFunction sceMpegbase[] =
{
	{0XBEA18F91, &WrapU_U<sceMpegBasePESpacketCopy>,           "sceMpegBasePESpacketCopy",           'x', "x"      },
	{0X492B5E4B, &WrapI_I<sceMpegBaseCscInit>,                 "sceMpegBaseCscInit",                 'i', "i"      },
	{0X0530BE4E, &WrapI_I<sceMpegbaseSetPixelMode>,             "sceMpegbase_0530BE4E",               'i', "i"      },
	{0X91929A21, &WrapI_UUIU<sceMpegBaseCscAvc>,                "sceMpegBaseCscAvc",                  'i', "xxix"   },
	{0X304882E1, &WrapI_UUUUU<sceMpegBaseCscAvcRange>,          "sceMpegBaseCscAvcRange",             'i', "xxxxx"  },
	{0X7AC0321A, &WrapI_UUI<sceMpegBaseYCrCbCopy>,              "sceMpegBaseYCrCbCopy",               'i', "xxi"    },
	{0XAC9E717E, &WrapI_IU<sceMpegBaseSetDefaultBufferWidth>,  "sceMpegbase_AC9E717E",               'i', "ix"     },
};

void Register_sceMpegbase()
{
	RegisterHLEModule("sceMpegbase", ARRAY_SIZE(sceMpegbase), sceMpegbase);
};

static u32 AlignVideoSize(u32 value, u32 alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

static void FreeVSHVideoCodecContext(u32 bufferAddr) {
	auto it = vshVideoCodecContexts.find(bufferAddr);
	if (it == vshVideoCodecContexts.end()) {
		return;
	}
	if (it->second->imageAllocation != 0) {
		userMemory.Free(it->second->imageAllocation);
	}
	delete it->second;
	vshVideoCodecContexts.erase(it);
}

static VSHVideoCodecContext *GetVSHVideoCodecContext(u32 bufferAddr, bool create) {
	auto it = vshVideoCodecContexts.find(bufferAddr);
	if (it != vshVideoCodecContexts.end()) {
		return it->second;
	}
	if (!create) {
		return nullptr;
	}
	auto *context = new VSHVideoCodecContext();
	vshVideoCodecContexts[bufferAddr] = context;
	return context;
}

#ifdef USE_FFMPEG
static bool EnsureVSHH264Decoder(VSHVideoCodecContext *context) {
	if (context->codec && context->frame) {
		return true;
	}
	InitFFmpeg();
	AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!decoder) {
		ERROR_LOG(Log::Mpeg, "Direct VSH could not find the FFmpeg H.264 decoder");
		return false;
	}
	context->codec = avcodec_alloc_context3(decoder);
	context->frame = av_frame_alloc();
	if (!context->codec || !context->frame) {
		context->ResetHostDecoder();
		return false;
	}
	// Let FFmpeg choose a bounded worker count for frame/slice parallelism.
	context->codec->thread_count = 0;
	context->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
	context->codec->flags2 |= AV_CODEC_FLAG2_CHUNKS;
	if (avcodec_open2(context->codec, decoder, nullptr) < 0) {
		context->ResetHostDecoder();
		return false;
	}
	return true;
}

static std::vector<u8> NormalizeVSHH264Packet(const u8 *source, size_t size) {
	std::vector<u8> packet;
	if (size >= 3 && source[0] == 0 && source[1] == 0 &&
		(source[2] == 1 || (size >= 4 && source[2] == 0 && source[3] == 1))) {
		packet.assign(source, source + size);
		return packet;
	}

	// MP4 AVC samples normally prefix each NAL with a big-endian 32-bit size.
	// Convert the complete sample to Annex B for FFmpeg. If it is not a valid
	// length-prefixed sample, retain the original bytes for parser tolerance.
	size_t offset = 0;
	while (offset + 4 <= size) {
		const u32 nalSize = ((u32)source[offset] << 24) | ((u32)source[offset + 1] << 16) |
			((u32)source[offset + 2] << 8) | source[offset + 3];
		offset += 4;
		if (nalSize == 0 || nalSize > size - offset) {
			packet.clear();
			break;
		}
		packet.insert(packet.end(), {0, 0, 0, 1});
		packet.insert(packet.end(), source + offset, source + offset + nalSize);
		offset += nalSize;
	}
	if (offset != size || packet.empty()) {
		packet.assign(source, source + size);
	}
	return packet;
}

static bool DecodeVSHH264(VSHVideoCodecContext *context, const u8 *source, size_t size,
	std::vector<u8> *luma, std::vector<u8> *cb, std::vector<u8> *cr, bool *hasImage) {
	*hasImage = false;
	if (!EnsureVSHH264Decoder(context)) {
		return false;
	}

	std::vector<u8> packetData = NormalizeVSHH264Packet(source, size);
	packetData.resize(packetData.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
	AVPacket *packet = av_packet_alloc();
	if (!packet) {
		return false;
	}
	packet->data = packetData.data();
	packet->size = (int)(packetData.size() - AV_INPUT_BUFFER_PADDING_SIZE);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
	const int sendResult = avcodec_send_packet(context->codec, packet);
	if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
		av_packet_free(&packet);
		WARN_LOG(Log::Mpeg, "Direct VSH H.264 packet rejected by FFmpeg: %d", sendResult);
		return false;
	}

	int receiveResult = AVERROR(EAGAIN);
	do {
		receiveResult = avcodec_receive_frame(context->codec, context->frame);
		if (receiveResult == 0) {
			*hasImage = true;
		}
	} while (receiveResult == 0);
	av_packet_free(&packet);
	if (!*hasImage) {
		return receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF;
	}
#else
	int frameFinished = 0;
	const int decodeResult = avcodec_decode_video2(context->codec, context->frame, &frameFinished, packet);
	av_packet_free(&packet);
	if (decodeResult < 0) {
		return false;
	}
	*hasImage = frameFinished != 0;
	if (!*hasImage) {
		return true;
	}
#endif

	context->width = context->frame->width;
	context->height = context->frame->height;
	if (context->width <= 0 || context->height <= 0 || context->width > 720 || context->height > 576) {
		return false;
	}
	const int chromaWidth = (context->width + 1) / 2;
	const int chromaHeight = (context->height + 1) / 2;
	luma->resize((size_t)context->width * context->height);
	cb->resize((size_t)chromaWidth * chromaHeight);
	cr->resize((size_t)chromaWidth * chromaHeight);
	u8 *dest[] = {luma->data(), cb->data(), cr->data(), nullptr};
	int destStride[] = {context->width, chromaWidth, chromaWidth, 0};
	context->sws = sws_getCachedContext(context->sws,
		context->width, context->height, (AVPixelFormat)context->frame->format,
		context->width, context->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!context->sws) {
		return false;
	}
	return sws_scale(context->sws, context->frame->data, context->frame->linesize,
		0, context->height, dest, destStride) == context->height;
}
#endif

static bool AllocateVSHVideoBuffers(VSHVideoCodecContext *context) {
	if (context->imageAllocation != 0) {
		return true;
	}
	const int width = context->width;
	const int height = context->height;
	const u32 sizeY1 = AlignVideoSize(((width + 16) >> 5) * (height >> 1) * 16, 0x200);
	const u32 sizeY2 = AlignVideoSize((width >> 5) * (height >> 1) * 16, 0x200);
	const u32 sizeC1 = AlignVideoSize(((width + 16) >> 5) * (height >> 1) * 8, 0x200);
	const u32 sizeC2 = AlignVideoSize((width >> 5) * (height >> 1) * 8, 0x200);
	const u32 perFrame = 2 * (sizeY1 + sizeY2 + sizeC1 + sizeC2);
	context->imageAllocationSize = 0x100 + perFrame * 3;
	context->imageAllocation = userMemory.Alloc(context->imageAllocationSize, true, "VSHVideoYCbCr");
	if (context->imageAllocation == (u32)-1) {
		context->imageAllocation = 0;
		context->imageAllocationSize = 0;
		return false;
	}
	Memory::Memset(context->imageAllocation, 0, context->imageAllocationSize, "VSHVideoYCbCrInit");
	context->auxiliary1 = context->imageAllocation;
	context->auxiliary2 = context->imageAllocation + 0x40;
	u32 cursor = context->imageAllocation + 0x100;
	for (int frame = 0; frame < 3; ++frame) {
		const u32 sizes[8] = {sizeY1, sizeY1, sizeY2, sizeY2, sizeC1, sizeC1, sizeC2, sizeC2};
		for (int section = 0; section < 8; ++section) {
			context->buffers[frame][section] = cursor;
			if (section >= 4) {
				Memory::Memset(cursor, 0x80, sizes[section], "VSHVideoChromaInit");
			}
			cursor += sizes[section];
		}
	}
	return true;
}

static void TileVSHVideoFrame(VSHVideoCodecContext *context, int frameIndex,
	const std::vector<u8> &luma, const std::vector<u8> &cb, const std::vector<u8> &cr) {
	const int width = context->width;
	const int height = context->height;
	const int width2 = width / 2;
	const int height2 = height / 2;
	u8 *output[8];
	for (int i = 0; i < 8; ++i) {
		output[i] = Memory::GetPointerWriteUnchecked(context->buffers[frameIndex][i]);
	}

	size_t n = 0;
	for (int x = 0; x < width; x += 32) {
		for (int y = 0; y < height; y += 2) {
			memcpy(output[0] + n, &luma[(size_t)y * width + x], 16);
			n += 16;
		}
	}
	n = 0;
	for (int x = 16; x < width; x += 32) {
		for (int y = 0; y < height; y += 2) {
			memcpy(output[1] + n, &luma[(size_t)y * width + x], 16);
			n += 16;
		}
	}
	n = 0;
	for (int x = 0; x < width; x += 32) {
		for (int y = 1; y < height; y += 2) {
			memcpy(output[2] + n, &luma[(size_t)y * width + x], 16);
			n += 16;
		}
	}
	n = 0;
	for (int x = 16; x < width; x += 32) {
		for (int y = 1; y < height; y += 2) {
			memcpy(output[3] + n, &luma[(size_t)y * width + x], 16);
			n += 16;
		}
	}

	for (int section = 0; section < 4; ++section) {
		const int xStart = (section >= 2) ? 8 : 0;
		const int yStart = section & 1;
		n = 0;
		for (int x = xStart; x < width2; x += 16) {
			for (int y = yStart; y < height2; y += 2) {
				for (int xx = 0; xx < 8; ++xx) {
					const size_t source = (size_t)y * width2 + x + xx;
					output[4 + section][n++] = cb[source];
					output[4 + section][n++] = cr[source];
				}
			}
		}
	}
}

static int sceVideocodecOpen(u32 bufferAddr, int type) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	FreeVSHVideoCodecContext(bufferAddr);
	GetVSHVideoCodecContext(bufferAddr, true);
	vshVideoOpenCalls++;
	Memory::WriteUnchecked_U32(0x05100601, bufferAddr + 0);
	switch (type) {
	case 0: {
		Memory::WriteUnchecked_U32(1, bufferAddr + 8);
		Memory::WriteUnchecked_U32(0x3C2C, bufferAddr + 24);
		Memory::WriteUnchecked_U32(0x15C00, bufferAddr + 32);
		const u32 secondary = Memory::ReadUnchecked_U32(bufferAddr + 16);
		if (!Memory::IsValidRange(secondary, 8)) {
			return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "secondary=%08x", secondary);
		}
		Memory::WriteUnchecked_U32(0x1F6400, secondary + 0);
		Memory::WriteUnchecked_U32(0x15C00, secondary + 4);
		break;
	}
	case 1:
		Memory::WriteUnchecked_U32(0, bufferAddr + 8);
		Memory::WriteUnchecked_U32(0x264C, bufferAddr + 24);
		Memory::WriteUnchecked_U32(0xB69E3, bufferAddr + 32);
		break;
	default:
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x type=%d", bufferAddr, type);
}

static int sceVideocodecReleaseEDRAM(u32 bufferAddr) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	auto allocation = videocodecEdramAllocations.find(bufferAddr);
	if (allocation != videocodecEdramAllocations.end()) {
		userMemory.Free(allocation->second);
		videocodecEdramAllocations.erase(allocation);
	}
	Memory::WriteUnchecked_U32(0, bufferAddr + 20);
	Memory::WriteUnchecked_U32(0, bufferAddr + 92);
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x", bufferAddr);
}

static int sceVideocodecGetEDRAM(u32 bufferAddr, int type) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	if (type != 0 && type != 1) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}

	auto previous = videocodecEdramAllocations.find(bufferAddr);
	if (previous != videocodecEdramAllocations.end()) {
		userMemory.Free(previous->second);
		videocodecEdramAllocations.erase(previous);
	}
	u32 size = (Memory::ReadUnchecked_U32(bufferAddr + 24) + 63) & ~63U;
	u32 allocation = userMemory.Alloc(size, true, "sceVideocodecEDRAM");
	if (allocation == (u32)-1) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_NO_MEMORY, "size=%u", size);
	}
	videocodecEdramAllocations[bufferAddr] = allocation;
	Memory::WriteUnchecked_U32(allocation, bufferAddr + 20);
	Memory::WriteUnchecked_U32(allocation, bufferAddr + 92);
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x type=%d allocation=%08x size=%u", bufferAddr, type, allocation, size);
}

static int sceVideocodecInit(u32 bufferAddr, int type) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	if (type != 0 && type != 1 && type != 3) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}
	Memory::WriteUnchecked_U32(Memory::ReadUnchecked_U32(bufferAddr + 20) + 8, bufferAddr + 12);
	if (type == 0) {
		vshMeMemoryBase = Memory::ReadUnchecked_U32(bufferAddr + 28);
	}
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x type=%d", bufferAddr, type);
}

static int sceVideocodecGetVersion(u32 bufferAddr, int type) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	if (type != 0 && type != 1) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}
	Memory::WriteUnchecked_U32(0x78, bufferAddr + 4);
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x type=%d", bufferAddr, type);
}

static int sceVideocodecSetMemory(u32 bufferAddr, int type) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	if (type != 0) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}
	Memory::WriteUnchecked_U32(0, bufferAddr + 8);
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x memory=%08x/%08x/%08x/%08x", bufferAddr,
		Memory::ReadUnchecked_U32(bufferAddr + 64), Memory::ReadUnchecked_U32(bufferAddr + 68),
		Memory::ReadUnchecked_U32(bufferAddr + 72), Memory::ReadUnchecked_U32(bufferAddr + 76));
}

static int sceVideocodec_893B32B1() {
	return hleLogDebug(Log::Mpeg, 0);
}

static int sceVideocodecDecode(u32 bufferAddr, int type) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	if (type != 0) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}
	auto *context = GetVSHVideoCodecContext(bufferAddr, true);
	context->decodeCalls++;
	vshVideoDecodeCalls++;
	const u32 meSourceOffset = Memory::ReadUnchecked_U32(bufferAddr + 36);
	u32 sourceAddr = meSourceOffset;
	const u32 sourceSize = Memory::ReadUnchecked_U32(bufferAddr + 40);
	if (!Memory::IsValidRange(sourceAddr, sourceSize)) {
		// mpeg_vsh passes an address in the Media Engine's view plus the
		// CPU-visible base at work-area offset 0x1C. This is the mapping used by
		// the native type-0 init command; the EDRAM work allocation is separate.
		const u32 cpuSourceBase = Memory::ReadUnchecked_U32(bufferAddr + 28);
		if (meSourceOffset <= 0x02000000 && Memory::IsValidRange(cpuSourceBase + meSourceOffset, sourceSize)) {
			sourceAddr = cpuSourceBase + meSourceOffset;
		}
	}
	if (!Memory::IsValidRange(sourceAddr, sourceSize)) {
		auto allocation = videocodecEdramAllocations.find(bufferAddr);
		if (allocation != videocodecEdramAllocations.end()) {
			const u32 allocationSize = userMemory.GetBlockSizeFromAddress(allocation->second);
			if (sourceAddr <= allocationSize && sourceSize <= allocationSize - sourceAddr) {
				sourceAddr += allocation->second;
			}
		}
	}
	if (sourceSize == 0 || !Memory::IsValidRange(sourceAddr, sourceSize)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR,
			"source=%08x size=%u", sourceAddr, sourceSize);
	}
	if (context->decodeCalls <= 3) {
		NOTICE_LOG(Log::Mpeg, "Direct VSH video decode #%d: context=%08x source=%08x size=%u",
			context->decodeCalls, bufferAddr, sourceAddr, sourceSize);
	}
	std::vector<u8> luma;
	std::vector<u8> cb;
	std::vector<u8> cr;
	bool hasImage = false;
#ifdef USE_FFMPEG
	if (!DecodeVSHH264(context, Memory::GetPointerUnchecked(sourceAddr), sourceSize, &luma, &cb, &cr, &hasImage)) {
		Memory::WriteUnchecked_U32(0x80618009, bufferAddr + 8);
		return hleLogError(Log::Mpeg, -1, "H.264 decode failed source=%08x size=%u", sourceAddr, sourceSize);
	}
#else
	return hleLogError(Log::Mpeg, -1, "FFmpeg is unavailable");
#endif

	const u32 secondary = Memory::ReadUnchecked_U32(bufferAddr + 16);
	const u32 yuvStruct = Memory::ReadUnchecked_U32(bufferAddr + 44);
	const u32 detail = Memory::ReadUnchecked_U32(bufferAddr + 48);
	const u32 decodeSEI = Memory::ReadUnchecked_U32(bufferAddr + 80);
	if (!Memory::IsValidRange(secondary, 40) || !Memory::IsValidRange(yuvStruct, 44)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR,
			"secondary=%08x yuv=%08x", secondary, yuvStruct);
	}

	Memory::WriteUnchecked_U32(context->width, secondary + 8);
	Memory::WriteUnchecked_U32(context->height, secondary + 12);
	Memory::WriteUnchecked_U32(1, secondary + 28);
	Memory::WriteUnchecked_U32(hasImage ? 1 : 0, secondary + 32);
	Memory::WriteUnchecked_U32(hasImage ? 0 : 1, secondary + 36);
	Memory::WriteUnchecked_U32(hasImage ? 1 : 0, yuvStruct + 32);
	Memory::WriteUnchecked_U32(0, bufferAddr + 8);

	if (hasImage) {
		if (!AllocateVSHVideoBuffers(context)) {
			return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_NO_MEMORY);
		}
		const int frameIndex = context->frameCount % 3;
		TileVSHVideoFrame(context, frameIndex, luma, cb, cr);
		for (int i = 0; i < 8; ++i) {
			Memory::WriteUnchecked_U32(context->buffers[frameIndex][i], yuvStruct + i * 4);
		}
		if (!Memory::IsValidRange(Memory::ReadUnchecked_U32(yuvStruct + 36), 36)) {
			Memory::WriteUnchecked_U32(context->auxiliary1, yuvStruct + 36);
		}
		if (!Memory::IsValidRange(Memory::ReadUnchecked_U32(yuvStruct + 40), 32)) {
			Memory::WriteUnchecked_U32(context->auxiliary2, yuvStruct + 40);
		}
		Memory::WriteUnchecked_U8(0x02, context->auxiliary1 + 0);
		Memory::WriteUnchecked_U32(90000, context->auxiliary1 + 8);
		Memory::WriteUnchecked_U32(90000, context->auxiliary1 + 16);
		Memory::WriteUnchecked_U32(context->frameCount * 2, context->auxiliary1 + 24);
		Memory::WriteUnchecked_U32(2, context->auxiliary1 + 28);
		Memory::WriteUnchecked_U8(1, context->auxiliary1 + 33);
		if (Memory::IsValidRange(detail, 40)) {
			Memory::WriteUnchecked_U8(1, detail + 0);
			Memory::WriteUnchecked_U8(0xFF, detail + 1);
			Memory::WriteUnchecked_U32(3, detail + 4);
			Memory::WriteUnchecked_U32(4, detail + 8);
			Memory::WriteUnchecked_U32(1, detail + 12);
			Memory::WriteUnchecked_U32(0x10000, detail + 20);
			Memory::WriteUnchecked_U32(4004, detail + 32);
			Memory::WriteUnchecked_U32(240000, detail + 36);
		}
		if (Memory::IsValidRange(decodeSEI, 36)) {
			Memory::WriteUnchecked_U8(0x02, decodeSEI + 0);
			Memory::WriteUnchecked_U32(90000, decodeSEI + 8);
			Memory::WriteUnchecked_U32(90000, decodeSEI + 16);
			Memory::WriteUnchecked_U32(context->frameCount * 2, decodeSEI + 24);
			Memory::WriteUnchecked_U32(2, decodeSEI + 28);
			Memory::WriteUnchecked_U8(1, decodeSEI + 33);
		}
		context->frameCount++;
		vshVideoDecodedFrames++;
		if (context->frameCount <= 3) {
			NOTICE_LOG(Log::Mpeg, "Direct VSH decoded frame #%d: %dx%d", context->frameCount, context->width, context->height);
		}
		NotifyMemInfo(MemBlockFlags::WRITE, context->imageAllocation, context->imageAllocationSize, "VSHVideoDecode");
	}

	return hleDelayResult(hleLogDebug(Log::Mpeg, 0,
		"buffer=%08x source=%08x size=%u image=%d %dx%d", bufferAddr, sourceAddr, sourceSize,
		hasImage, context->width, context->height), "VSH video decode", 4000);
}

static int sceVideocodecStop(u32 bufferAddr, int type) {
	auto *context = GetVSHVideoCodecContext(bufferAddr, false);
	if (type != 0 && type != 1) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}
#ifdef USE_FFMPEG
	if (context && context->codec) {
		avcodec_flush_buffers(context->codec);
	}
#endif
	if (context) {
		NOTICE_LOG(Log::Mpeg, "Direct VSH video codec stop: calls=%d frames=%d csc=%u",
			context->decodeCalls, context->frameCount, mpegBaseCscCalls);
	}
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x type=%d", bufferAddr, type);
}

static int sceVideocodecDelete(u32 bufferAddr, int type) {
	if (type != 0 && type != 1) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_INVALID_VALUE, "type=%d", type);
	}
	FreeVSHVideoCodecContext(bufferAddr);
	vshVideoDeleteCalls++;
	return hleDelayResult(hleLogDebug(Log::Mpeg, 0, "buffer=%08x type=%d", bufferAddr, type),
		"VSH video codec delete", 40000);
}

static int sceVideocodecGetSEI(u32 bufferAddr, int type) {
	if (!Memory::IsValidRange(bufferAddr, 96)) {
		return hleLogError(Log::Mpeg, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "buffer=%08x", bufferAddr);
	}
	const u32 decodeSEI = Memory::ReadUnchecked_U32(bufferAddr + 80);
	if (Memory::IsValidRange(decodeSEI, 32)) {
		Memory::WriteUnchecked_U32(0, decodeSEI + 28);
	}
	return hleLogDebug(Log::Mpeg, 0, "buffer=%08x type=%d", bufferAddr, type);
}

const HLEFunction sceVideocodec[] = {
	{0XC01EC829, &WrapI_UI<sceVideocodecOpen>,        "sceVideocodecOpen",         'i', "xi"},
	{0X2D31F5B1, &WrapI_UI<sceVideocodecGetEDRAM>,    "sceVideocodecGetEDRAM",     'i', "xi"},
	{0X17099F0A, &WrapI_UI<sceVideocodecInit>,        "sceVideocodecInit",         'i', "xi"},
	{0X26927D19, &WrapI_UI<sceVideocodecGetVersion>,  "sceVideocodecGetVersion",   'i', "xi"},
	{0X745A7B7A, &WrapI_UI<sceVideocodecSetMemory>,   "sceVideocodecSetMemory",    'i', "xi"},
	{0X893B32B1, &WrapI_V<sceVideocodec_893B32B1>,   "sceVideocodec_893B32B1",    'i', ""  },
	{0XDBA273FA, &WrapI_UI<sceVideocodecDecode>,      "sceVideocodecDecode",       'i', "xi"},
	{0XA2F0564E, &WrapI_UI<sceVideocodecStop>,        "sceVideocodecStop",         'i', "xi"},
	{0X307E6E1C, &WrapI_UI<sceVideocodecDelete>,      "sceVideocodecDelete",       'i', "xi"},
	{0X627B7D42, &WrapI_UI<sceVideocodecGetSEI>,      "sceVideocodecGetSEI",       'i', "xi"},
	{0X4F160BF4, &WrapI_U<sceVideocodecReleaseEDRAM>, "sceVideocodecReleaseEDRAM", 'i', "x" },
};

void Register_sceVideocodec() {
	RegisterHLEModule("sceVideocodec", ARRAY_SIZE(sceVideocodec), sceVideocodec);
}

VSHVideoDebugStatus __MpegGetVSHVideoDebugStatus() {
	VSHVideoDebugStatus status;
	status.openCalls = vshVideoOpenCalls;
	status.decodeCalls = vshVideoDecodeCalls;
	status.decodedFrames = vshVideoDecodedFrames;
	status.deleteCalls = vshVideoDeleteCalls;
	status.cscCalls = mpegBaseCscCalls;
	status.pesCopyCalls = mpegPESCopyCalls;
	status.activeContexts = (u32)vshVideoCodecContexts.size();
	status.edramAllocations = (u32)videocodecEdramAllocations.size();
	for (const auto &[_, context] : vshVideoCodecContexts) {
		status.imageAllocationBytes += context->imageAllocationSize;
		if (context->frameCount > 0) {
			status.width = context->width;
			status.height = context->height;
		}
	}
	return status;
}
