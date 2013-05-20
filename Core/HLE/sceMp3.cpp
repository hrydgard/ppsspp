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
#include "Core/HLE/HLE.h"
#include "Core/HW/MediaEngine.h"
#include "Core/Reporting.h"

static const int MP3_BITRATES[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};

struct Mp3Context {	
	Mp3Context() : mediaengine(NULL) {}
	~Mp3Context() {
		if (mediaengine != NULL) {
			delete mediaengine;
		}
	}

	void DoState(PointerWrap &p) {
		p.Do(mp3StreamStart);
		p.Do(mp3StreamEnd);
		p.Do(mp3Buf);
		p.Do(mp3BufSize);
		p.Do(mp3PcmBuf);
		p.Do(mp3BufPendingSize);
		p.Do(mp3PcmBufSize);
		p.Do(mp3InputFileReadPos);
		p.Do(mp3InputBufWritePos);
		p.Do(mp3InputBufSize);
		p.Do(mp3InputFileSize);
		p.Do(mp3DecodedBytes);
		p.Do(mp3LoopNum);
		p.Do(mp3MaxSamples);
		p.Do(mp3Bitrate);
		p.Do(mp3Channels);
		p.Do(mp3SamplingRate);
		p.Do(mp3Version);
		p.DoClass(mediaengine);
		p.DoMarker("Mp3Context");
	}

	u64 mp3StreamStart;
	u64 mp3StreamEnd;
	u64 mp3StreamPosition;
	u32 mp3Buf;
	int mp3BufSize;
	int mp3BufPendingSize;
	u32 mp3PcmBuf;
	int mp3PcmBufSize;

	int mp3InputFileReadPos;
	int mp3InputBufWritePos;
	int mp3InputBufSize;
	int mp3InputFileSize;
	int mp3DecodedBytes;
	int mp3LoopNum;
	int mp3MaxSamples;

	int mp3Channels;
	int mp3Bitrate;
	int mp3SamplingRate;
	int mp3Version;

	MediaEngine *mediaengine;
};


static std::map<u32, Mp3Context *> mp3Map;
static u32 lastMp3Handle = 0;

Mp3Context *getMp3Ctx(u32 mp3) {
	if (mp3Map.find(mp3) == mp3Map.end()) {
		ERROR_LOG(HLE, "Bad mp3 handle %08x - using last one (%08x) instead", mp3, lastMp3Handle);
		mp3 = lastMp3Handle;
	}
	if (mp3Map.find(mp3) == mp3Map.end())
		return NULL;
	return mp3Map[mp3];
}

int sceMp3Decode(u32 mp3, u32 outPcmPtr) {
	DEBUG_LOG(HLE, "sceMp3Decode(%08x,%08x)", mp3, outPcmPtr);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	// Nothing to decode
	if(ctx->mp3BufPendingSize == 0 || ctx->mp3StreamPosition >= ctx->mp3StreamEnd) {
		if (ctx->mp3LoopNum == 0) {
			return 0;
		} else if (ctx->mp3LoopNum > 0) {
			--ctx->mp3LoopNum;
		}
	}

	Memory::Memset(ctx->mp3PcmBuf, 0, ctx->mp3PcmBufSize);
	Memory::Write_U32(ctx->mp3PcmBuf, outPcmPtr);

	// TODO: Actually decode the data
#ifdef _DEBUG
	char fileName[256];
	sprintf(fileName, "%lli.mp3", ctx->mp3StreamPosition);

	FILE * file = fopen(fileName, "wb");
	if(file) {
		if(!Memory::IsValidAddress(ctx->mp3Buf)) {
			ERROR_LOG(HLE, "sceMp3Decode mp3Buf %08X is not a valid address!", ctx->mp3Buf);
		}

		u8 * ptr = Memory::GetPointer(ctx->mp3Buf);
		fwrite(ptr, 1, ctx->mp3BufPendingSize, file);
		
		fclose(file);
	}
#endif

	ctx->mp3StreamPosition += ctx->mp3BufPendingSize;
	if(ctx->mp3StreamPosition > ctx->mp3StreamEnd)
		ctx->mp3StreamPosition = ctx->mp3StreamEnd;

	// Reset the pending buffer size so the program will know that we need to buffer more data
	ctx->mp3BufPendingSize = (ctx->mp3StreamPosition < ctx->mp3StreamEnd)?-1:0;

	return ctx->mp3PcmBufSize;
}

int sceMp3ResetPlayPosition(u32 mp3) {
	DEBUG_LOG(HLE, "SceMp3ResetPlayPosition(%08x)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	ctx->mp3StreamPosition = 0;
	ctx->mp3BufPendingSize = -1;
	return 0;
}

int sceMp3CheckStreamDataNeeded(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3CheckStreamDataNeeded(%08x)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return (ctx->mp3BufPendingSize < 0) && (ctx->mp3StreamPosition < ctx->mp3StreamEnd);
}

u32 sceMp3ReserveMp3Handle(u32 mp3Addr) {
	DEBUG_LOG(HLE, "sceMp3ReserveMp3Handle(%08x)", mp3Addr);
	Mp3Context *ctx = new Mp3Context;

	memset(ctx, 0, sizeof(Mp3Context));

	ctx->mp3StreamStart = Memory::Read_U64(mp3Addr);
	ctx->mp3StreamEnd = Memory::Read_U64(mp3Addr+8);
	ctx->mp3Buf = Memory::Read_U32(mp3Addr+16);
	ctx->mp3BufSize = Memory::Read_U32(mp3Addr+20);
	ctx->mp3PcmBuf = Memory::Read_U32(mp3Addr+24);
	ctx->mp3PcmBufSize = Memory::Read_U32(mp3Addr+28);

	ctx->mp3StreamPosition = ctx->mp3StreamStart;
	ctx->mp3BufPendingSize = -1;
	ctx->mp3MaxSamples = ctx->mp3PcmBufSize / 4 ;

	/*ctx->mp3Channels = 2;
	ctx->mp3Bitrate = 128;
	ctx->mp3SamplingRate = 44100;*/

	mp3Map[mp3Addr] = ctx;
	return mp3Addr;
}

int sceMp3InitResource() {
	WARN_LOG(HLE, "UNIML: sceMp3InitResource");
	// Do nothing here 
	return 0;
}

int sceMp3TermResource() {
	WARN_LOG(HLE, "UNIML: sceMp3TermResource");
	// Do nothing here 
	return 0;
}

int sceMp3Init(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3Init(%08x)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	// Read in the header and swap the endian
	int header = Memory::Read_U32(ctx->mp3Buf);
	header = (header >> 24) |
         ((header<<8) & 0x00FF0000) |
         ((header>>8) & 0x0000FF00) |
         (header << 24);

	int channels = ((header >> 6) & 0x3);
	if(channels == 0 || channels == 1 || channels == 2)
		ctx->mp3Channels = 2;
	else if(channels == 3)
		ctx->mp3Channels = 1;
	else 
		ctx->mp3Channels = 0;

	// 0 == VBR
	int bitrate = ((header >> 10) & 0x3);
	if(bitrate < (int)ARRAY_SIZE(MP3_BITRATES))
		ctx->mp3Bitrate = MP3_BITRATES[bitrate];
	else
		ctx->mp3Bitrate = -1;

	int samplerate = ((header >> 12) & 0x3);
	if (samplerate == 0) 
		ctx->mp3SamplingRate = 44100;
	else if (samplerate == 1) 
		ctx->mp3SamplingRate = 48000;
	else if (samplerate == 2) 
		ctx->mp3SamplingRate = 32000;
	else 
		ctx->mp3SamplingRate = 0;
	
	ctx->mp3Version = ((header >> 19) & 0x3);
	return 0;
}

int sceMp3GetLoopNum(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3GetLoopNum(%08x)", mp3);
	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return ctx->mp3LoopNum;
}

int sceMp3GetMaxOutputSample(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3GetMaxOutputSample(%08x)", mp3);
	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return ctx->mp3MaxSamples;
}

int sceMp3NotifyAddStreamData(u32 mp3, int size) {
	DEBUG_LOG(HLE, "sceMp3NotifyAddStreamData(%08X, %i)", mp3, size);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	ctx->mp3BufPendingSize = size;
	return 0;
}

int sceMp3GetSumDecodedSample(u32 mp3) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceMp3GetSumDecodedSample(%08X)", mp3);
	return 0;
}

int sceMp3SetLoopNum(u32 mp3, int loop) {
	DEBUG_LOG(HLE, "sceMp3SetLoopNum(%08X, %i)", mp3, loop);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	ctx->mp3LoopNum = loop;
	return 0;
}

int sceMp3GetMp3ChannelNum(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3GetMp3ChannelNum(%08X)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return ctx->mp3Channels;
}

int sceMp3GetBitRate(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3GetBitRate(%08X)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return ctx->mp3Bitrate;
}

int sceMp3GetSamplingRate(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3GetSamplingRate(%08X)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return ctx->mp3SamplingRate;
}

int sceMp3GetInfoToAddStreamData(u32 mp3, u32 dstPtr, u32 towritePtr, u32 srcposPtr) {
	DEBUG_LOG(HLE, "HACK: sceMp3GetInfoToAddStreamData(%08X, %08X, %08X, %08X)", mp3, dstPtr, towritePtr, srcposPtr);
	
	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	if(Memory::IsValidAddress(dstPtr))
		Memory::Write_U32(ctx->mp3Buf, dstPtr);
	if(Memory::IsValidAddress(towritePtr))
		Memory::Write_U32(ctx->mp3BufSize, towritePtr);
	if(Memory::IsValidAddress(srcposPtr))
		Memory::Write_U32((u32)ctx->mp3StreamPosition, srcposPtr);
	return 0;
}

int sceMp3ReleaseMp3Handle(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3ReleaseMp3Handle(%08X)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	mp3Map.erase(mp3Map.find(mp3));
	delete ctx;
	return 0;
}

u32 sceMp3EndEntry() {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceMp3EndEntry(...)");
	return 0;
}

u32 sceMp3StartEntry() {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceMp3StartEntry(...)");
	return 0;
}

u32 sceMp3GetFrameNum(u32 mp3) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceMp3GetFrameNum(%08x)", mp3);
	return 0;
}

u32 sceMp3GetVersion(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3GetVersion(%08x)", mp3);
	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}
	return ctx->mp3Version;
}

const HLEFunction sceMp3[] =
{
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
	{0xAE6D2027,WrapU_U<sceMp3GetVersion>,"sceMp3GetVersion"}, // Name is wrong.
	{0x3548AEC8,WrapU_U<sceMp3GetFrameNum>,"sceMp3GetFrameNum"},
	{0x0840e808,0,"sceMp3_0840e808"},
};

void Register_sceMp3()
{
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
