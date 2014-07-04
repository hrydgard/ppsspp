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


struct Mp3Context {
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

		p.Do(mp3StreamStart);
		p.Do(mp3StreamEnd);
		p.Do(mp3Buf);
		p.Do(mp3BufSize);
		p.Do(mp3PcmBuf);
		p.Do(mp3PcmBufSize);
		p.Do(readPosition);
		p.Do(bufferRead);
		p.Do(bufferWrite);
		p.Do(bufferAvailable);
		p.Do(mp3DecodedBytes);
		p.Do(mp3LoopNum);
		p.Do(mp3MaxSamples);
		p.Do(mp3SumDecodedSamples);
		p.Do(mp3Channels);
		p.Do(mp3Bitrate);
		p.Do(mp3SamplingRate);
		p.Do(mp3Version);
	};
};

static std::map<u32, Mp3Context *> mp3Map_old;
static std::map<u32, AuCtx *> mp3Map;
static const int mp3DecodeDelay = 4000;

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
	auto s = p.Section("sceMp3", 0, 2);
	if (!s)
		return;

	if (s >= 2){
		p.Do(mp3Map);
	}
	if (s <= 1 && p.mode == p.MODE_READ){
		p.Do(mp3Map_old); // read old map
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
			mp3->readPos = mp3_old->readPosition;
			mp3->AuBufAvailable = 0; // reset to read from file
			mp3->askedReadSize = 0;
			mp3->realReadSize = 0;

			mp3->audioType = PSP_CODEC_MP3;
			mp3->decoder = new SimpleAudio(mp3->audioType);
			mp3Map[id] = mp3;
		}
	}
}

int sceMp3Decode(u32 mp3, u32 outPcmPtr) {
	DEBUG_LOG(ME, "sceMp3Decode(%08x,%08x)", mp3, outPcmPtr);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
		
	int pcmBytes = ctx->AuDecode(outPcmPtr);
	if (!pcmBytes) {
		// decode data successfully, delay thread
		hleDelayResult(pcmBytes, "mp3 decode", mp3DecodeDelay);
	}
	return pcmBytes;
}

int sceMp3ResetPlayPosition(u32 mp3) {
	DEBUG_LOG(ME, "SceMp3ResetPlayPosition(%08x)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuResetPlayPosition();
}

int sceMp3CheckStreamDataNeeded(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3CheckStreamDataNeeded(%08x)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuCheckStreamDataNeeded();
}

u32 sceMp3ReserveMp3Handle(u32 mp3Addr) {
	INFO_LOG(ME, "sceMp3ReserveMp3Handle(%08x)", mp3Addr);
	if (!Memory::IsValidAddress(mp3Addr)){
		ERROR_LOG(ME, "sceMp3ReserveMp3Handle(%08x) invalid address %08x", mp3Addr, mp3Addr);
		return -1;
	}

	AuCtx *Au = new AuCtx;
	Au->startPos = Memory::Read_U64(mp3Addr);				// Audio stream start position.
	Au->endPos = Memory::Read_U32(mp3Addr + 8);				// Audio stream end position.
	Au->AuBuf = Memory::Read_U32(mp3Addr + 16);            // Input Au data buffer.	
	Au->AuBufSize = Memory::Read_U32(mp3Addr + 20);        // Input Au data buffer size.
	Au->PCMBuf = Memory::Read_U32(mp3Addr + 24);            // Output PCM data buffer.
	Au->PCMBufSize = Memory::Read_U32(mp3Addr + 28);        // Output PCM data buffer size.

	DEBUG_LOG(ME, "startPos %llx endPos %llx mp3buf %08x mp3bufSize %08x PCMbuf %08x PCMbufSize %08x",
		Au->startPos, Au->endPos, Au->AuBuf, Au->AuBufSize, Au->PCMBuf, Au->PCMBufSize);

	Au->audioType = PSP_CODEC_MP3;
	Au->Channels = 2;
	Au->SumDecodedSamples = 0;
	Au->MaxOutputSample = Au->PCMBufSize / 4;
	Au->LoopNum = -1;
	Au->AuBufAvailable = 0;
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
	INFO_LOG(ME, "sceMp3Init(%08x)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	// Parse the Mp3 header
	bool hasID3Tag = false;
	int header = __ParseMp3Header(ctx, &hasID3Tag);
	int layer = (header >> 17) & 0x3;
	ctx->Version = ((header >> 19) & 0x3);
	ctx->SamplingRate = __CalculateMp3SampleRates((header >> 10) & 0x3, ctx->Version);
	ctx->Channels = __CalculateMp3Channels((header >> 6) & 0x3);
	ctx->BitRate = __CalculateMp3Bitrates((header >> 12) & 0xF, ctx->Version, layer);
	ctx->freq = ctx->SamplingRate;

	INFO_LOG(ME, "sceMp3Init(): channels=%i, samplerate=%iHz, bitrate=%ikbps", ctx->Channels, ctx->SamplingRate, ctx->BitRate);

	// for mp3, if required freq is 48000, reset resampling Frequency to 48000 seems get better sound quality (e.g. Miku Custom BGM)
	if (ctx->freq == 48000) {
		ctx->decoder->SetResampleFrequency(ctx->freq);
	}

	// For mp3 file, if ID3 tag is detected, we must move startPos to 0x400 (stream start position), remove 0x400 bytes of the sourcebuff, and reduce the available buffer size by 0x400
	// this is very important for ID3 tag mp3, since our universal audio decoder is for decoding stream part only.
	if (hasID3Tag) {
		// if get ID3 tage, we will decode from 0x400
		ctx->startPos = 0x400;
		ctx->EatSourceBuff(0x400);
	} else {
		// if no ID3 tag, we will decode from the begining of the file
		ctx->startPos = 0;
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

	return ctx->AuGetLoopNum();
}

int sceMp3GetMaxOutputSample(u32 mp3) {
	DEBUG_LOG(ME, "sceMp3GetMaxOutputSample(%08x)", mp3);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuGetMaxOutputSample();
}

int sceMp3GetSumDecodedSample(u32 mp3) {
	INFO_LOG(ME, "sceMp3GetSumDecodedSample(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuGetSumDecodedSample();
}

int sceMp3SetLoopNum(u32 mp3, int loop) {
	INFO_LOG(ME, "sceMp3SetLoopNum(%08X, %i)", mp3, loop);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuSetLoopNum(loop);
}

int sceMp3GetMp3ChannelNum(u32 mp3) {
	INFO_LOG(ME, "sceMp3GetMp3ChannelNum(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuGetChannelNum();
}

int sceMp3GetBitRate(u32 mp3) {
	INFO_LOG(ME, "sceMp3GetBitRate(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuGetBitRate();
}

int sceMp3GetSamplingRate(u32 mp3) {
	INFO_LOG(ME, "sceMp3GetSamplingRate(%08X)", mp3);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuGetSamplingRate();
}

int sceMp3GetInfoToAddStreamData(u32 mp3, u32 dstPtr, u32 towritePtr, u32 srcposPtr) {
	DEBUG_LOG(ME, "sceMp3GetInfoToAddStreamData(%08X, %08X, %08X, %08X)", mp3, dstPtr, towritePtr, srcposPtr);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuGetInfoToAddStreamData(dstPtr, towritePtr, srcposPtr);
}

int sceMp3NotifyAddStreamData(u32 mp3, int size) {
	DEBUG_LOG(ME, "sceMp3NotifyAddStreamData(%08X, %i)", mp3, size);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuNotifyAddStreamData(size);
}

int sceMp3ReleaseMp3Handle(u32 mp3) {
	INFO_LOG(ME, "sceMp3ReleaseMp3Handle(%08X)", mp3);

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
	INFO_LOG(ME, "sceMp3GetFrameNum(%08x)", mp3);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return ctx->AuGetFrameNum();
}

u32 sceMp3GetMPEGVersion(u32 mp3) {
	INFO_LOG(ME, "sceMp3GetMPEGVersion(%08x)", mp3);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuGetVersion();
}

u32 sceMp3ResetPlayPositionByFrame(u32 mp3, int position) {
	DEBUG_LOG(ME, "sceMp3ResetPlayPositionByFrame(%08x, %i)", mp3, position);
	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->AuResetPlayPositionByFrame(position);
}

u32 sceMp3LowLevelInit(u32 mp3) {
	INFO_LOG(ME, "sceMp3LowLevelInit(%i)", mp3);
	auto ctx = new AuCtx;

	ctx->audioType = PSP_CODEC_MP3;
	// create mp3 decoder
	ctx->decoder = new SimpleAudio(ctx->audioType);

	// close the audio if mp3 already exists.
	if (mp3Map.find(mp3) != mp3Map.end()) {
		delete mp3Map[mp3];
		mp3Map.erase(mp3);
	}

	mp3Map[mp3] = ctx;
	return 0;
}

u32 sceMp3LowLevelDecode(u32 mp3, u32 sourceAddr, u32 sourceBytesConsumedAddr, u32 samplesAddr, u32 sampleBytesAddr) {
	// sourceAddr: input mp3 stream buffer
	// sourceBytesConsumedAddr: consumed bytes decoded in source
	// samplesAddr: output pcm buffer
	// sampleBytesAddr: output pcm size
	DEBUG_LOG(ME, "sceMp3LowLevelDecode(%08x, %08x, %08x, %08x, %08x)", mp3, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);

	AuCtx *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(ME, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	if (!Memory::IsValidAddress(sourceAddr) || !Memory::IsValidAddress(sourceBytesConsumedAddr) ||
		!Memory::IsValidAddress(samplesAddr) || !Memory::IsValidAddress(sampleBytesAddr)) {
		ERROR_LOG(ME, "sceMp3LowLevelDecode(%08x, %08x, %08x, %08x, %08x) : invalid address in args", mp3, sourceAddr, sourceBytesConsumedAddr, samplesAddr, sampleBytesAddr);
		return -1;
	}

	auto inbuff = Memory::GetPointer(sourceAddr);
	auto outbuff = Memory::GetPointer(samplesAddr);
	
	int outpcmbytes = 0;
	ctx->decoder->Decode((void*)inbuff, 4096, outbuff, &outpcmbytes);
	
	Memory::Write_U32(ctx->decoder->GetSourcePos(), sourceBytesConsumedAddr);
	Memory::Write_U32(outpcmbytes, sampleBytesAddr);
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
	{0x1b839b83,WrapU_U<sceMp3LowLevelInit>,"sceMp3LowLevelInit"},
	{0xe3ee2c81,WrapU_UUUUU<sceMp3LowLevelDecode>,"sceMp3LowLevelDecode"}
};

void Register_sceMp3() {
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
