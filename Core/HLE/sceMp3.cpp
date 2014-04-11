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

#include <map>
#include <algorithm>

#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceMp3.h"
#include "Core/HW/MediaEngine.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/HW/SimpleAudioDec.h"

static std::map<u32, AuCtx *> mp3Map;

AuCtx *getMp3Ctx(u32 mp3) {
	if (mp3Map.find(mp3) == mp3Map.end())
		return NULL;
	return mp3Map[mp3];
}

void __Mp3Shutdown() {
	for (auto it = mp3Map.begin(), end = mp3Map.end(); it != end; ++it) {
		delete it->second;
	}
	mp3Map.clear();
}

void __Mp3DoState(PointerWrap &p) {
	auto s = p.Section("sceMp3", 0, 1);
	if (!s)
		return;

	p.Do(mp3Map);
}

int sceMp3Decode(u32 mp3, u32 outPcmPtr) {
	DEBUG_LOG(ME, "sceMp3Decode(%08x,%08x)", mp3, outPcmPtr);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuDecode(outPcmPtr);
}

int sceMp3ResetPlayPosition(u32 mp3) {
	DEBUG_LOG(ME, "SceMp3ResetPlayPosition(%08x)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuResetPlayPosition();
}

int sceMp3CheckStreamDataNeeded(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3CheckStreamDataNeeded(%08x)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuCheckStreamDataNeeded();
}


u32 sceMp3ReserveMp3Handle(u32 mp3Addr) {
	DEBUG_LOG(ME, "sceMp3ReserveMp3Handle(%08x)", mp3Addr);
	AuCtx *Au = new AuCtx;

	if (!Memory::IsValidAddress(mp3Addr)){
		ERROR_LOG(ME, "sceMp3ReserveMp3Handle(%08x) invalid address %08x", mp3Addr);
		return -1;
	}
	Au->startPos = Memory::Read_U64(mp3Addr);				// Audio stream start position.
	Au->endPos = Memory::Read_U32(mp3Addr + 8);				// Audio stream end position.
	Au->AuBuf = Memory::Read_U32(mp3Addr + 16);            // Input Au data buffer.	
	Au->AuBufSize = Memory::Read_U32(mp3Addr + 20);        // Input Au data buffer size.
	Au->PCMBuf = Memory::Read_U32(mp3Addr + 24);            // Output PCM data buffer.
	Au->PCMBufSize = Memory::Read_U32(mp3Addr + 28);        // Output PCM data buffer size.

	DEBUG_LOG(ME, "startPos %x endPos %x mp3buf %08x mp3bufSize %08x PCMbuf %08x PCMbufSize %08x",
		Au->startPos, Au->endPos, Au->AuBuf, Au->AuBufSize, Au->PCMBuf, Au->PCMBufSize);

	Au->audioType = PSP_CODEC_MP3;
	Au->Channels = 2;
	Au->SumDecodedSamples = 0;
	Au->MaxOutputSample = Au->PCMBufSize / 4;
	Au->LoopNum = -1;
	Au->AuBufAvailable = 0;
	Au->MaxOutputSample = 0;
	Au->readPos = Au->startPos;

	// create Au decoder
	Au->decoder = new SimpleAudio(Au->audioType);

	// close the audio if mp3Addr already exist.
	if (mp3Map.find(mp3Addr) != mp3Map.end()) {
		delete mp3Map[mp3Addr];
		mp3Map.erase(mp3Addr);
	}

	mp3Map[mp3Addr] = Au;

	return mp3Addr;
}

int sceMp3InitResource() {
	WARN_LOG(ME, "UNIMPL: sceMp3InitResource");
	// Do nothing here 
	return 0;
}

int sceMp3TermResource() {
	WARN_LOG(ME, "UNIMPL: sceMp3TermResource");
	// Do nothing here 
	return 0;
}

int __CalculateMp3Channels(int bitval) {
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

int __CalculateMp3SampleRates(int bitval, int mp3version) {
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

int __CalculateMp3Bitrates(int bitval, int mp3version, int mp3layer) {
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

int __ParseMp3Header(AuCtx *ctx, bool *isID3) {
	int header = bswap32(Memory::Read_U32(ctx->AuBuf));
	// ID3 tag , can be seen in Hanayaka Nari Wa ga Ichizoku.
	static const int ID3 = 0x49443300;
	if ((header & 0xFFFFFF00) == ID3) {
		*isID3 = true;
		int size = bswap32(Memory::Read_U32(ctx->AuBuf + ctx->startPos + 6));
		// Highest bit of each byte has to be ignored (format: 0x7F7F7F7F)
		size = (size & 0x7F) | ((size & 0x7F00) >> 1) | ((size & 0x7F0000) >> 2) | ((size & 0x7F000000) >> 3);
		header = bswap32(Memory::Read_U32(ctx->AuBuf + ctx->startPos + 10 + size));
	}
	return header;
}

int sceMp3Init(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3Init(%08x)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	// Parse the Mp3 header
	bool isID3 = false;
	int header = __ParseMp3Header(ctx, &isID3);
	int layer = (header >> 17) & 0x3;
	ctx->Version = ((header >> 19) & 0x3);
	ctx->SamplingRate = __CalculateMp3SampleRates((header >> 10) & 0x3, ctx->Version);
	ctx->Channels = __CalculateMp3Channels((header >> 6) & 0x3);
	ctx->BitRate = __CalculateMp3Bitrates((header >> 12) & 0xF, ctx->Version, layer);
	ctx->freq = ctx->SamplingRate;

	INFO_LOG(ME, "sceMp3Init(): channels=%i, samplerate=%iHz, bitrate=%ikbps", ctx->Channels, ctx->SamplingRate, ctx->BitRate);

	// for mp3, if required freq is 48000, reset resampling Frequency to 48000 seems get better sound quality (e.g. Miku Custom BGM)
	if (ctx->freq == 48000){
		ctx->decoder->setResampleFrequency(ctx->freq);
	}

	// For mp3 file, if ID3 tag is detected, we must move startPos and writePos to 0x400 (stream start position), and reduce the available buffer size by 0x400
	// this is very important for ID3 tag mp3, since our universal audio decoder is for decoding stream part only.
	if (isID3){
		// if get ID3 tage, we will decode from 0x400
		ctx->startPos = 0x400;
		ctx->writePos = 0x400;
		ctx->AuBufAvailable -= 0x400;
	}
	else{
		// if no ID3 tag, we will decode from the begining of the file
		ctx->startPos = 0;
		ctx->writePos = 0;
	}

	return 0;
}

int sceMp3GetLoopNum(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3GetLoopNum(%08x)", mp3);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetLoopNum();
}

int sceMp3GetMaxOutputSample(u32 mp3)
{
	DEBUG_LOG(ME, "sceMp3GetMaxOutputSample(%08x)", mp3);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetMaxOutputSample();
}

int sceMp3GetSumDecodedSample(u32 mp3) {
	DEBUG_LOG_REPORT(ME, "sceMp3GetSumDecodedSample(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetSumDecodedSample();
}

int sceMp3SetLoopNum(u32 mp3, int loop) {
	INFO_LOG(ME, "sceMp3SetLoopNum(%08X, %i)", mp3, loop);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuSetLoopNum(loop);
}
int sceMp3GetMp3ChannelNum(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3GetMp3ChannelNum(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetChannelNum();
}
int sceMp3GetBitRate(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3GetBitRate(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetBitRate();
}
int sceMp3GetSamplingRate(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3GetSamplingRate(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetSamplingRate();
}

int sceMp3GetInfoToAddStreamData(u32 mp3, u32 dstPtr, u32 towritePtr, u32 srcposPtr) {
	DEBUG_LOG(ME, "sceMp3GetInfoToAddStreamData(%08X, %08X, %08X, %08X)", mp3, dstPtr, towritePtr, srcposPtr);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetInfoToAddStreamData(dstPtr, towritePtr, srcposPtr);
}

int sceMp3NotifyAddStreamData(u32 mp3, int size) {
	DEBUG_LOG(ME, "sceMp3NotifyAddStreamData(%08X, %i)", mp3, size);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuNotifyAddStreamData(size);
}

int sceMp3ReleaseMp3Handle(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3ReleaseMp3Handle(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	delete ctx;
	mp3Map.erase(mp3);

	return 0;
}

u32 sceMp3EndEntry() {
	ERROR_LOG_REPORT(ME, "UNIMPL sceMp3EndEntry(...)");
	return 0;
}

u32 sceMp3StartEntry() {
	ERROR_LOG_REPORT(ME, "UNIMPL sceMp3StartEntry(...)");
	return 0;
}

u32 sceMp3GetFrameNum(u32 mp3) {
	ERROR_LOG_REPORT(ME, "UNIMPL sceMp3GetFrameNum(%08x)", mp3);
	return 0;
}

u32 sceMp3GetMPEGVersion(u32 mp3) {
	INFO_LOG(ME, "sceMp3GetMPEGVersion(%08x)", mp3);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuGetVersion();
}

u32 sceMp3ResetPlayPositionByFrame(u32 mp3, int position) {
	DEBUG_LOG(ME, "sceMp3ResetPlayPositionByFrame(%08x, %i)", mp3, position);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->sceAuResetPlayPositionByFrame(position);
}

u32 sceMp3LowLevelInit() {
	ERROR_LOG_REPORT(ME, "UNIMPL sceMp3LowLevelInit(...)");
	return 0;
}

u32 sceMp3LowLevelDecode() {
	ERROR_LOG_REPORT(ME, "UNIMPL sceMp3LowLevelDecode(...)");
	return 0;
}

const HLEFunction sceMp3[] = {
	{0x07EC321A,WrapU_U<sceMp3ReserveMp3Handle>,"sceMp3ReserveMp3Handle"},
	{0x0DB149F4,WrapI_UI<sceMp3NotifyAddStreamData>,"sceMp3NotifyAddStreamData"},
	{0x2A368661,WrapI_U<sceMp3ResetPlayPosition>,"sceMp3ResetPlayPosition"},
	{0x354D27EA,WrapI_U<sceMp3GetSumDecodedSample>,"sceMp3GetSumDecodedSample"},
	{0x35750070,WrapI_V<sceMp3InitResource>,"sceMp3InitResource"},
	{0x3C2FA058,WrapI_V<sceMp3TermResource>,"sceMp3TermResource"},
	{0x3CEF484F,WrapI_UI<sceMp3SetLoopNum>,"sceMp3SetLoopNum"},
	{0x44E07129,WrapI_U<sceMp3Init>,"sceMp3Init"},
	{0x732B042A,WrapU_V<sceMp3EndEntry>,"sceMp3EndEntry"},
	{0x7F696782,WrapI_U<sceMp3GetMp3ChannelNum>,"sceMp3GetMp3ChannelNum"},
	{0x87677E40,WrapI_U<sceMp3GetBitRate>,"sceMp3GetBitRate"},
	{0x87C263D1,WrapI_U<sceMp3GetMaxOutputSample>,"sceMp3GetMaxOutputSample"},
	{0x8AB81558,WrapU_V<sceMp3StartEntry>,"sceMp3StartEntry"},
	{0x8F450998,WrapI_U<sceMp3GetSamplingRate>,"sceMp3GetSamplingRate"},
	{0xA703FE0F,WrapI_UUUU<sceMp3GetInfoToAddStreamData>,"sceMp3GetInfoToAddStreamData"},
	{0xD021C0FB,WrapI_UU<sceMp3Decode>,"sceMp3Decode"},
	{0xD0A56296,WrapI_U<sceMp3CheckStreamDataNeeded>,"sceMp3CheckStreamDataNeeded"},
	{0xD8F54A51,WrapI_U<sceMp3GetLoopNum>,"sceMp3GetLoopNum"},
	{0xF5478233,WrapI_U<sceMp3ReleaseMp3Handle>,"sceMp3ReleaseMp3Handle"},
	{0xAE6D2027,WrapU_U<sceMp3GetMPEGVersion>,"sceMp3GetMPEGVersion"},
	{0x3548AEC8,WrapU_U<sceMp3GetFrameNum>,"sceMp3GetFrameNum"},
	{0x0840e808,WrapU_UI<sceMp3ResetPlayPositionByFrame>,"sceMp3ResetPlayPositionByFrame"},
	{0x1b839b83,WrapU_V<sceMp3LowLevelInit>,"sceMp3LowLevelInit"},
	{0xe3ee2c81,WrapU_V<sceMp3LowLevelDecode>,"sceMp3LowLevelDecode"}
};

void Register_sceMp3() {
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
