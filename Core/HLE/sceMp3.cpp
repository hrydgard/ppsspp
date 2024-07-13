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

// Games known to support custom music and almost certainly use sceMp3:
//
// * ATV Offroad Fury: Blazin' Trails
// * Beats (/PSP/MUSIC)
// * Crazy Taxi : Fare Wars  (/MUSIC)
// * Dead or Alive Paradise
// * Gran Turismo - You must first clear all driving challenges up to C to unlock this feature, then it will be available through the options menu.
// * Grand Theft Auto : Liberty City Stories
// * Grand Theft Auto : Vice City Stories
// * Heroes' VS (#5866 ?)
// * MLB 08 : The Show
// * MotorStorm : Artic Edge
// * NBA Live 09
// * Need for Speed Carbon
// * Need for Speed Pro Street
// * Pro Evolution Soccer 2014
// * SD Gundam G Generation Overworld
// * TOCA Race Driver 2
// * Untold Legends II
// * Wipeout Pulse (/MUSIC/WIPEOUT)
//
// Games known to use LowLevelDecode:
//
// * Gundam G (custom BGM)
// * Heroes' VS (custom BGM)
//
// Games that use sceMp3 internally
//
// * Kirameki School Life SP
// * Breakquest (mini)
// * Orbit (mini)
// * SWAT Target Liberty ULES00927
// * Geometry Wars (homebrew)
// * Hanayaka Nari Wa ga Ichizoku
// * Velocity (mini)
// * N+ (mini) (#9379)
// * Mighty Flip Champs DX (mini)
// * EDGE (mini)
// * Stellar Attack (mini)
// * Hungry Giraffe (mini)
// * OMG - Z (mini)
// ...probably lots more minis...
//
// BUGS
//
// Custom music plays but starts stuttering:
// * Beats
//
// Custom music just repeats a small section:
// * Crazy Taxi

#include <map>
#include <algorithm>

#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceMp3.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"

static const u32 ERROR_MP3_INVALID_HANDLE = 0x80671001;
static const u32 ERROR_MP3_UNRESERVED_HANDLE = 0x80671102;
static const u32 ERROR_MP3_NOT_YET_INIT_HANDLE = 0x80671103;
static const u32 ERROR_MP3_NO_RESOURCE_AVAIL = 0x80671201;
static const u32 ERROR_MP3_BAD_SAMPLE_RATE = 0x80671302;
static const u32 ERROR_MP3_BAD_RESET_FRAME = 0x80671501;
static const u32 ERROR_MP3_BAD_ADDR = 0x80671002;
static const u32 ERROR_MP3_BAD_SIZE = 0x80671003;
static const u32 ERROR_AVCODEC_INVALID_DATA = 0x807f00fd;
static const int AU_BUF_MIN_SIZE = 8192;
static const int PCM_BUF_MIN_SIZE = 9216;
static const size_t MP3_MAX_HANDLES = 2;

// This one is only used for save state upgrading.
struct Mp3ContextOld {
public:
	int mp3StreamStart;
	int mp3StreamEnd;
	u32 mp3Buf;
	int mp3BufSize;
	u32 mp3PcmBuf;
	int mp3PcmBufSize;

	int readPosition;

	int bufferRead;
	int bufferWrite;
	int bufferAvailable;

	int mp3DecodedBytes;
	int mp3LoopNum;
	int mp3MaxSamples;
	int mp3SumDecodedSamples;

	int mp3Channels;
	int mp3Bitrate;
	int mp3SamplingRate;
	int mp3Version;

	void DoState(PointerWrap &p) {
		auto s = p.Section("Mp3Context", 1);
		if (!s)
			return;

		Do(p, mp3StreamStart);
		Do(p, mp3StreamEnd);
		Do(p, mp3Buf);
		Do(p, mp3BufSize);
		Do(p, mp3PcmBuf);
		Do(p, mp3PcmBufSize);
		Do(p, readPosition);
		Do(p, bufferRead);
		Do(p, bufferWrite);
		Do(p, bufferAvailable);
		Do(p, mp3DecodedBytes);
		Do(p, mp3LoopNum);
		Do(p, mp3MaxSamples);
		Do(p, mp3SumDecodedSamples);
		Do(p, mp3Channels);
		Do(p, mp3Bitrate);
		Do(p, mp3SamplingRate);
		Do(p, mp3Version);
	};
};

static std::map<u32, AuCtx *> mp3Map;
static const int mp3DecodeDelay = 2400;
static bool resourceInited = false;

static AuCtx *getMp3Ctx(u32 mp3) {
	if (mp3Map.find(mp3) == mp3Map.end())
		return NULL;
	return mp3Map[mp3];
}

void __Mp3Init() {
	resourceInited = false;
}

void __Mp3Shutdown() {
	for (auto it = mp3Map.begin(), end = mp3Map.end(); it != end; ++it) {
		delete it->second;
	}
	mp3Map.clear();
}

void __Mp3DoState(PointerWrap &p) {
	auto s = p.Section("sceMp3", 0, 3);
	if (!s)
		return;

	if (s >= 2) {
		Do(p, mp3Map);
	} else {
		std::map<u32, Mp3ContextOld *> mp3Map_old;
		Do(p, mp3Map_old); // read old map
		for (auto it = mp3Map_old.begin(), end = mp3Map_old.end(); it != end; ++it) {
			auto mp3 = new AuCtx;
			u32 id = it->first;
			auto mp3_old = it->second;
			mp3->AuBuf = mp3_old->mp3Buf;
			mp3->AuBufSize = mp3_old->mp3BufSize;
			mp3->PCMBuf = mp3_old->mp3PcmBuf;
			mp3->PCMBufSize = mp3_old->mp3PcmBufSize;
			mp3->BitRate = mp3_old->mp3Bitrate;
			mp3->Channels = mp3_old->mp3Channels;
			mp3->endPos = mp3_old->mp3StreamEnd;
			mp3->startPos = mp3_old->mp3StreamStart;
			mp3->LoopNum = mp3_old->mp3LoopNum;
			mp3->SamplingRate = mp3_old->mp3SamplingRate;
			mp3->freq = mp3->SamplingRate;
			mp3->SumDecodedSamples = mp3_old->mp3SumDecodedSamples;
			mp3->Version = mp3_old->mp3Version;
			mp3->MaxOutputSample = mp3_old->mp3MaxSamples;
			mp3->SetReadPos(mp3_old->readPosition);

			mp3->decoder = CreateAudioDecoder(PSP_CODEC_MP3);
			mp3Map[id] = mp3;
		}
	}

	if (s >= 3) {
		Do(p, resourceInited);
	} else {
		// Previous behavior acted as if it was already inited.
		resourceInited = true;
	}
}

static int sceMp3Decode(u32 mp3, u32 outPcmPtr) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0 || ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	}

	int pcmBytes = ctx->AuDecode(outPcmPtr);
	if (pcmBytes > 0) {
		// decode data successfully, delay thread
		return hleDelayResult(hleLogSuccessI(Log::ME, pcmBytes), "mp3 decode", mp3DecodeDelay);
	} else if (pcmBytes == 0) {
		return hleLogSuccessI(Log::ME, pcmBytes);
	}
	// Should already have logged.
	return pcmBytes;
}

static int sceMp3ResetPlayPosition(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0 || ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	}

	return hleLogSuccessI(Log::ME, ctx->AuResetPlayPosition());
}

static int sceMp3CheckStreamDataNeeded(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "incorrect handle type");
	}

	return hleLogSuccessI(Log::ME, ctx->AuCheckStreamDataNeeded());
}

static u32 sceMp3ReserveMp3Handle(u32 mp3Addr) {
	if (!resourceInited) {
		return hleLogError(Log::ME, ERROR_MP3_NO_RESOURCE_AVAIL, "sceMp3InitResource must be called first");
	}
	if (mp3Map.size() >= MP3_MAX_HANDLES) {
		return hleLogError(Log::ME, ERROR_MP3_NO_RESOURCE_AVAIL, "no free handles");
	}
	if (mp3Addr != 0 && !Memory::IsValidRange(mp3Addr, 32)) {
		// The PSP would crash, but might as well return a proper error.
		return hleLogError(Log::ME, SCE_KERNEL_ERROR_INVALID_POINTER, "bad mp3 pointer");
	}

	AuCtx *Au = new AuCtx;
	if (mp3Addr) {
		Au->startPos = Memory::Read_U64(mp3Addr); // AUDIO stream start position.
		Au->endPos = Memory::Read_U64(mp3Addr + 8); // AUDIO stream end position.
		Au->AuBuf = Memory::Read_U32(mp3Addr + 16); // Input Au data buffer.
		Au->AuBufSize = Memory::Read_U32(mp3Addr + 20); // Input Au data buffer size.
		Au->PCMBuf = Memory::Read_U32(mp3Addr + 24); // Output PCM data buffer.
		Au->PCMBufSize = Memory::Read_U32(mp3Addr + 28); // Output PCM data buffer size.

		if (Au->startPos >= Au->endPos) {
			delete Au;
			return hleLogError(Log::ME, ERROR_MP3_BAD_SIZE, "start must be before end");
		}
		if (!Au->AuBuf || !Au->PCMBuf) {
			delete Au;
			return hleLogError(Log::ME, ERROR_MP3_BAD_ADDR, "invalid buffer addresses");
		}
		if ((int)Au->AuBufSize < AU_BUF_MIN_SIZE || (int)Au->PCMBufSize < PCM_BUF_MIN_SIZE) {
			delete Au;
			return hleLogError(Log::ME, ERROR_MP3_BAD_SIZE, "buffers too small");
		}

		DEBUG_LOG(Log::ME, "startPos %llx endPos %llx mp3buf %08x mp3bufSize %08x PCMbuf %08x PCMbufSize %08x",
			Au->startPos, Au->endPos, Au->AuBuf, Au->AuBufSize, Au->PCMBuf, Au->PCMBufSize);
	} else {
		Au->startPos = 0;
		Au->endPos = 0;
		Au->AuBuf = 0;
		Au->AuBufSize = 0;
		Au->PCMBuf = 0;
		Au->PCMBufSize = 0;
	}

	Au->SetReadPos(Au->startPos);
	Au->decoder = CreateAudioDecoder(PSP_CODEC_MP3);

	int handle = (int)mp3Map.size();
	mp3Map[handle] = Au;

	return hleLogSuccessI(Log::ME, handle);
}

static int sceMp3InitResource() {
	// TODO: Could validate the utility modules have been loaded?
	if (resourceInited) {
		return hleLogSuccessI(Log::ME, 0);
	}
	resourceInited = true;
	return hleLogSuccessI(Log::ME, hleDelayResult(0, "mp3 resource init", 200));
}

static int sceMp3TermResource() {
	if (!resourceInited) {
		return hleLogSuccessI(Log::ME, 0);
	}

	// Free any handles that are still open.
	for (auto au : mp3Map) {
		delete au.second;
	}
	mp3Map.clear();

	resourceInited = false;
	return hleLogSuccessI(Log::ME, hleDelayResult(0, "mp3 resource term", 100));
}

static int __CalculateMp3Channels(int bitval) {
	if (bitval == 0 || bitval == 1 || bitval == 2) { // Stereo / Joint Stereo / Dual Channel.
		return 2;
	}
	else if (bitval == 3) { // Mono.
		return 1;
	}
	else {
		return -1;
	}
}

static int __CalculateMp3SampleRates(int bitval, int mp3version) {
	if (mp3version == 3) { // MPEG Version 1
		int valuemapping[] = { 44100, 48000, 32000, -1 };
		return valuemapping[bitval];
	}
	else if (mp3version == 2) { // MPEG Version 2
		int valuemapping[] = { 22050, 24000, 16000, -1 };
		return valuemapping[bitval];
	}
	else if (mp3version == 0) { // MPEG Version 2.5
		int valuemapping[] = { 11025, 12000, 8000, -1 };
		return valuemapping[bitval];
	}
	else {
		return -1;
	}
}

static int __CalculateMp3Bitrates(int bitval, int mp3version, int mp3layer) {
	if (mp3version == 3) { // MPEG Version 1
		if (mp3layer == 3) { // Layer I
			int valuemapping[] = { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1 };
			return valuemapping[bitval];
		}
		else if (mp3layer == 2) { // Layer II
			int valuemapping[] = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, -1 };
			return valuemapping[bitval];
		}
		else if (mp3layer == 1) { // Layer III
			int valuemapping[] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1 };
			return valuemapping[bitval];
		}
		else {
			return -1;
		}
	}
	else if (mp3version == 2 || mp3version == 0) { // MPEG Version 2 or 2.5
		if (mp3layer == 3) { // Layer I
			int valuemapping[] = { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, -1 };
			return valuemapping[bitval];
		}
		else if (mp3layer == 1 || mp3layer == 2) { // Layer II or III
			int valuemapping[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1 };
			return valuemapping[bitval];
		}
		else {
			return -1;
		}
	}
	else {
		return -1;
	}
}

static int CalculateMp3SamplesPerFrame(int versionBits, int layerBits) {
	if (versionBits == 1 || layerBits == 0) {
		return -1;
	} else if (layerBits == 3) {
		return 384;
	} else if (layerBits == 2 || versionBits == 3) {
		return 1152;
	} else {
		return 576;
	}
}

static int FindMp3Header(AuCtx *ctx, int &header, int end) {
	u32 addr = ctx->AuBuf + ctx->AuStreamWorkareaSize();
	if (Memory::IsValidRange(addr, end)) {
		const u8 *ptr = Memory::GetPointerUnchecked(addr);
		for (int offset = 0; offset < end; ++offset) {
			// If we hit valid sync bits, then we've found a header.
			if (ptr[offset] == 0xFF && (ptr[offset + 1] & 0xC0) == 0xC0) {
				header = bswap32(Memory::Read_U32(addr + offset));
				return offset;
			}
		}
	}

	return -1;
}

static int sceMp3Init(u32 mp3) {
	int sdkver = sceKernelGetCompiledSdkVersion();
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "incorrect handle type");
	}

	static const int PARSE_DELAY_MS = 500;

	// First, let's search for the MP3 header.  It can be offset by at most 1439 bytes.
	// If we have an ID3 tag, we'll get past it based on frame sync.  Don't modify startPos.
	int header = 0;
	if (FindMp3Header(ctx, header, 1440) < 0)
		return hleDelayResult(hleLogWarning(Log::ME, ERROR_AVCODEC_INVALID_DATA, "no header found"), "mp3 init", PARSE_DELAY_MS);

	// Parse the Mp3 header
	int layerBits = (header >> 17) & 0x3;
	int versionBits = (header >> 19) & 0x3;
	int bitrate = __CalculateMp3Bitrates((header >> 12) & 0xF, versionBits, layerBits);
	int samplerate = __CalculateMp3SampleRates((header >> 10) & 0x3, versionBits);;
	int channels = __CalculateMp3Channels((header >> 6) & 0x3);

	DEBUG_LOG(Log::ME, "sceMp3Init(): channels=%i, samplerate=%iHz, bitrate=%ikbps, layerBits=%d ,versionBits=%d,HEADER: %08x", channels, samplerate, bitrate, layerBits, versionBits, header);

	if (layerBits != 1) {
		// TODO: Should return ERROR_AVCODEC_INVALID_DATA.
		WARN_LOG_REPORT(Log::ME, "sceMp3Init: invalid data: not layer 3");
	}
	if (bitrate == 0 || bitrate == -1) {
		return hleDelayResult(hleReportError(Log::ME, ERROR_AVCODEC_INVALID_DATA, "invalid bitrate v%d l%d rate %04x", versionBits, layerBits, (header >> 12) & 0xF), "mp3 init", PARSE_DELAY_MS);
	}
	if (samplerate == -1) {
		return hleDelayResult(hleReportError(Log::ME, ERROR_AVCODEC_INVALID_DATA, "invalid sample rate v%d l%d rate %02x", versionBits, layerBits, (header >> 10) & 0x3), "mp3 init", PARSE_DELAY_MS);
	}

	// Before we allow init, newer SDK versions next require at least 156 bytes.
	// That happens to be the size of the first frame header for VBR.
	if (sdkver >= 0x06000000 && ctx->ReadPos() < 156) {
		return hleDelayResult(hleLogError(Log::ME, SCE_KERNEL_ERROR_INVALID_VALUE, "insufficient mp3 data for init"), "mp3 init", PARSE_DELAY_MS);
	}

	ctx->SamplingRate = samplerate;
	ctx->Channels = channels;
	ctx->BitRate = bitrate;
	ctx->MaxOutputSample = CalculateMp3SamplesPerFrame(versionBits, layerBits);
	ctx->freq = ctx->SamplingRate;

	if (versionBits != 3) {
		// TODO: Should return 0x80671301 (unsupported version?)
		WARN_LOG_REPORT(Log::ME, "sceMp3Init: invalid data: not MPEG v1");
	}
	if (samplerate != 44100 && sdkver < 3090500) {
		return hleDelayResult(hleLogError(Log::ME, ERROR_MP3_BAD_SAMPLE_RATE, "invalid data: not 44.1kHz"), "mp3 init", PARSE_DELAY_MS);
	}

	// Based on bitrate, we can calculate the frame size in bytes.
	// Note: this doesn't correctly handle padding or slot size, but the PSP doesn't either.
	uint32_t bytesPerSecond = (ctx->MaxOutputSample / 8) * ctx->BitRate * 1000;
	// The frame count ignores the upper bits of these sizes, although they are used in cases.
	uint64_t totalBytes = (ctx->endPos & 0xFFFFFFFF) - (ctx->startPos & 0xFFFFFFFF);
	ctx->FrameNum = (int)((totalBytes * ctx->SamplingRate) / bytesPerSecond);

	ctx->Version = versionBits;

	return hleDelayResult(hleLogSuccessI(Log::ME, 0), "mp3 init", PARSE_DELAY_MS);
}

static int sceMp3GetLoopNum(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "incorrect handle type");
	}

	return hleLogSuccessI(Log::ME, ctx->LoopNum);
}

static int sceMp3GetMaxOutputSample(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	} else if (ctx->AuBuf == 0) {
		return hleLogWarning(Log::ME, 0, "no channel available for low level");
	}

	return hleLogSuccessI(Log::ME, ctx->MaxOutputSample);
}

static int sceMp3GetSumDecodedSample(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "incorrect handle type");
	}

	return hleLogSuccessI(Log::ME, ctx->SumDecodedSamples);
}

static int sceMp3SetLoopNum(u32 mp3, int loop) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "incorrect handle type");
	}

	if (loop < 0)
		loop = -1;

	ctx->LoopNum = loop;
	return hleLogSuccessI(Log::ME, 0);
}

static int sceMp3GetMp3ChannelNum(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	} else if (ctx->AuBuf == 0) {
		return hleLogWarning(Log::ME, 0, "no channel available for low level");
	}

	return hleLogSuccessI(Log::ME, ctx->Channels);
}

static int sceMp3GetBitRate(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	} else if (ctx->AuBuf == 0) {
		return hleLogWarning(Log::ME, 0, "no bitrate available for low level");
	}

	return hleLogSuccessI(Log::ME, ctx->BitRate);
}

static int sceMp3GetSamplingRate(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	} else if (ctx->AuBuf == 0) {
		return hleLogWarning(Log::ME, 0, "no sample rate available for low level");
	}

	return hleLogSuccessI(Log::ME, ctx->SamplingRate);
}

static int sceMp3GetInfoToAddStreamData(u32 mp3, u32 dstPtr, u32 towritePtr, u32 srcposPtr) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "incorrect handle type");
	}

	return hleLogSuccessI(Log::ME, ctx->AuGetInfoToAddStreamData(dstPtr, towritePtr, srcposPtr));
}

static int sceMp3NotifyAddStreamData(u32 mp3, int size) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "incorrect handle type");
	}

	return hleLogSuccessI(Log::ME, ctx->AuNotifyAddStreamData(size));
}

static int sceMp3ReleaseMp3Handle(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (ctx) {
		delete ctx;
		mp3Map.erase(mp3);
		return hleLogSuccessI(Log::ME, 0);
	} else if (mp3 >= MP3_MAX_HANDLES) {
		return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
	}

	// Intentionally a zero result.
	return hleLogDebug(Log::ME, 0, "double free ignored");
}

static u32 sceMp3EndEntry() {
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceMp3EndEntry(...)");
	return 0;
}

static u32 sceMp3StartEntry() {
	ERROR_LOG_REPORT(Log::ME, "UNIMPL sceMp3StartEntry(...)");
	return 0;
}

static u32 sceMp3GetFrameNum(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0 || ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	}

	return hleLogSuccessI(Log::ME, ctx->FrameNum);
}

static u32 sceMp3GetMPEGVersion(u32 mp3) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0) {
		// Seems to be the wrong error code.
		return hleLogError(Log::ME, ERROR_MP3_UNRESERVED_HANDLE, "not yet init");
	} else if (ctx->AuBuf == 0) {
		return hleLogWarning(Log::ME, 0, "no MPEG version available for low level");
	}

	// Tests have not revealed how to expose more than "3" here as a result.
	return hleReportDebug(Log::ME, ctx->Version);
}

static u32 sceMp3ResetPlayPositionByFrame(u32 mp3, u32 frame) {
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		if (mp3 >= MP3_MAX_HANDLES)
			return hleLogError(Log::ME, ERROR_MP3_INVALID_HANDLE, "invalid handle");
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "unreserved handle");
	} else if (ctx->Version < 0 || ctx->AuBuf == 0) {
		return hleLogError(Log::ME, ERROR_MP3_NOT_YET_INIT_HANDLE, "not yet init");
	}

	if (frame >= (u32)ctx->FrameNum) {
		return hleLogError(Log::ME, ERROR_MP3_BAD_RESET_FRAME, "bad frame position");
	}

	return hleLogSuccessI(Log::ME, ctx->AuResetPlayPositionByFrame(frame));
}

static u32 sceMp3LowLevelInit(u32 mp3, u32 unk) {
	auto ctx = new AuCtx();

	// create mp3 decoder
	ctx->decoder = CreateAudioDecoder(PSP_CODEC_MP3);

	// close the audio if mp3 already exists.
	if (mp3Map.find(mp3) != mp3Map.end()) {
		delete mp3Map[mp3];
		mp3Map.erase(mp3);
	}
	mp3Map[mp3] = ctx;

	// Indicate that we've run low level init by setting version to 1.
	ctx->Version = 1;

	return hleLogSuccessInfoI(Log::ME, hleDelayResult(0, "mp3 low level", 600));
}

static u32 sceMp3LowLevelDecode(u32 mp3, u32 sourceAddr, u32 sourceBytesConsumedAddr, u32 samplesAddr, u32 sampleBytesAddr) {
	// sourceAddr: input mp3 stream buffer
	// sourceBytesConsumedAddr: consumed bytes decoded in source
	// samplesAddr: output pcm buffer
	// sampleBytesAddr: output pcm size
	DEBUG_LOG(Log::ME, "sceMp3LowLevelDecode(%08x, %08x, %08x, %08x, %08x)", mp3, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(Log::ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	if (!Memory::IsValidAddress(sourceAddr) || !Memory::IsValidAddress(sourceBytesConsumedAddr) ||
		!Memory::IsValidAddress(samplesAddr) || !Memory::IsValidAddress(sampleBytesAddr)) {
		ERROR_LOG(Log::ME, "sceMp3LowLevelDecode(%08x, %08x, %08x, %08x, %08x) : invalid address in args", mp3, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);
		return -1;
	}

	const u8 *inbuff = Memory::GetPointerWriteUnchecked(sourceAddr);
	int16_t *outbuf = (int16_t *)Memory::GetPointerWriteUnchecked(samplesAddr);
	
	int outSamples = 0;
	int inbytesConsumed = 0;
	ctx->decoder->Decode(inbuff, 4096, &inbytesConsumed, 2, outbuf, &outSamples);
	int outBytes = outSamples * sizeof(int16_t) * 2;
	NotifyMemInfo(MemBlockFlags::WRITE, samplesAddr, outBytes, "Mp3LowLevelDecode");
	
	Memory::Write_U32(inbytesConsumed, sourceBytesConsumedAddr);
	Memory::Write_U32(outBytes, sampleBytesAddr);
	return 0;
}

const HLEFunction sceMp3[] = {
	{0X07EC321A, &WrapU_U<sceMp3ReserveMp3Handle>,          "sceMp3ReserveMp3Handle",         'x', "x"    },
	{0X0DB149F4, &WrapI_UI<sceMp3NotifyAddStreamData>,      "sceMp3NotifyAddStreamData",      'i', "xi"   },
	{0X2A368661, &WrapI_U<sceMp3ResetPlayPosition>,         "sceMp3ResetPlayPosition",        'i', "x"    },
	{0X354D27EA, &WrapI_U<sceMp3GetSumDecodedSample>,       "sceMp3GetSumDecodedSample",      'i', "x"    },
	{0X35750070, &WrapI_V<sceMp3InitResource>,              "sceMp3InitResource",             'i', ""     },
	{0X3C2FA058, &WrapI_V<sceMp3TermResource>,              "sceMp3TermResource",             'i', ""     },
	{0X3CEF484F, &WrapI_UI<sceMp3SetLoopNum>,               "sceMp3SetLoopNum",               'i', "xi"   },
	{0X44E07129, &WrapI_U<sceMp3Init>,                      "sceMp3Init",                     'i', "x"    },
	{0X732B042A, &WrapU_V<sceMp3EndEntry>,                  "sceMp3EndEntry",                 'x', ""     },
	{0X7F696782, &WrapI_U<sceMp3GetMp3ChannelNum>,          "sceMp3GetMp3ChannelNum",         'i', "x"    },
	{0X87677E40, &WrapI_U<sceMp3GetBitRate>,                "sceMp3GetBitRate",               'i', "x"    },
	{0X87C263D1, &WrapI_U<sceMp3GetMaxOutputSample>,        "sceMp3GetMaxOutputSample",       'i', "x"    },
	{0X8AB81558, &WrapU_V<sceMp3StartEntry>,                "sceMp3StartEntry",               'x', ""     },
	{0X8F450998, &WrapI_U<sceMp3GetSamplingRate>,           "sceMp3GetSamplingRate",          'i', "x"    },
	{0XA703FE0F, &WrapI_UUUU<sceMp3GetInfoToAddStreamData>, "sceMp3GetInfoToAddStreamData",   'i', "xppp" },
	{0XD021C0FB, &WrapI_UU<sceMp3Decode>,                   "sceMp3Decode",                   'i', "xp"   },
	{0XD0A56296, &WrapI_U<sceMp3CheckStreamDataNeeded>,     "sceMp3CheckStreamDataNeeded",    'i', "x"    },
	{0XD8F54A51, &WrapI_U<sceMp3GetLoopNum>,                "sceMp3GetLoopNum",               'i', "x"    },
	{0XF5478233, &WrapI_U<sceMp3ReleaseMp3Handle>,          "sceMp3ReleaseMp3Handle",         'i', "x"    },
	{0XAE6D2027, &WrapU_U<sceMp3GetMPEGVersion>,            "sceMp3GetMPEGVersion",           'x', "x"    },
	{0X3548AEC8, &WrapU_U<sceMp3GetFrameNum>,               "sceMp3GetFrameNum",              'i', "x"    },
	{0X0840E808, &WrapU_UU<sceMp3ResetPlayPositionByFrame>, "sceMp3ResetPlayPositionByFrame", 'i', "xi"   },
	{0X1B839B83, &WrapU_UU<sceMp3LowLevelInit>,             "sceMp3LowLevelInit",             'x', "xx"   },
	{0XE3EE2C81, &WrapU_UUUUU<sceMp3LowLevelDecode>,        "sceMp3LowLevelDecode",           'x', "xxxxx"}
};

void Register_sceMp3() {
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
