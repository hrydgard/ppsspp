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
#include "Core/HLE/sceMp3.h"
#include "Core/HW/MediaEngine.h"
#include "Core/Reporting.h"
#include "../HW/MediaEngine.h"

#ifdef USE_FFMPEG
#ifndef PRId64
#define PRId64  "%llu" 
#endif

// Urgh! Why is this needed?
#ifdef ANDROID
#ifndef UINT64_C
#define UINT64_C(c) (c ## ULL)
#endif
#endif
extern "C" {
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
//#include <libavutil/timestamp.h>     // iOS build is not happy with this one.
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}
#endif

static const int MP3_BITRATES[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};

struct Mp3Context {
	void DoState(PointerWrap &p) {
		p.Do(mp3StreamStart);
		p.Do(mp3StreamEnd);
		p.Do(mp3Buf);
		p.Do(mp3BufSize);
		p.Do(mp3PcmBuf);
		p.Do(mp3PcmBufSize);
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
	MediaEngine *mediaengine;

	int mp3Channels;
	int mp3Bitrate;
	int mp3SamplingRate;
	int mp3Version;
#ifdef USE_FFMPEG
	AVFormatContext *avformat_context;
	AVIOContext	  *avio_context;
	AVCodecContext  *decoder_context;
	SwrContext      *resampler_context;
	int audio_stream_index;
#endif
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

/* MP3 */
int sceMp3Decode(u32 mp3, u32 outPcmPtr) {
	DEBUG_LOG(HLE, "sceMp3Decode(%08x,%08x)", mp3, outPcmPtr);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	// Nothing to decode
	if (ctx->bufferAvailable == 0 || ctx->readPosition >= ctx->mp3StreamEnd) {
		return 0;
	}
	int bytesdecoded = 0;

#ifndef USE_FFMPEG
	Memory::Memset(ctx->mp3PcmBuf, 0, ctx->mp3PcmBufSize);
	Memory::Write_U32(ctx->mp3PcmBuf, outPcmPtr);
#else

	AVFrame frame;
	memset(&frame, 0, sizeof(frame));
	AVPacket packet;
	memset(&packet, 0, sizeof(packet));
	int got_frame = 0, ret;
	static int audio_frame_count = 0;

	while (!got_frame) {
		if ((ret = av_read_frame(ctx->avformat_context, &packet)) < 0)
			break;

		if (packet.stream_index == ctx->audio_stream_index) {
			avcodec_get_frame_defaults(&frame);
			got_frame = 0;
			ret = avcodec_decode_audio4(ctx->decoder_context, &frame, &got_frame, &packet);
			if (ret < 0) {
				ERROR_LOG(HLE, "avcodec_decode_audio4: Error decoding audio %d", ret);
				continue;
			}
			if (got_frame) {
				//char buf[1024] = "";
				//av_ts_make_time_string(buf, frame.pts, &ctx->decoder_context->time_base);
				//DEBUG_LOG(HLE, "audio_frame n:%d nb_samples:%d pts:%s", audio_frame_count++, frame.nb_samples, buf);

				/*
				u8 *audio_dst_data;
				int audio_dst_linesize;

				ret = av_samples_alloc(&audio_dst_data, &audio_dst_linesize, frame.channels, frame.nb_samples, (AVSampleFormat)frame.format, 1);
				if (ret < 0) {
					ERROR_LOG(HLE, "av_samples_alloc: Could not allocate audio buffer %d", ret);
					return -1;
				}
				*/

				int decoded = av_samples_get_buffer_size(NULL, frame.channels, frame.nb_samples, (AVSampleFormat)frame.format, 1);

				u8* out = Memory::GetPointer(ctx->mp3PcmBuf + bytesdecoded);
				ret = swr_convert(ctx->resampler_context, &out, frame.nb_samples, (const u8**)frame.extended_data, frame.nb_samples);
				if (ret < 0) {
					ERROR_LOG(HLE, "swr_convert: Error while converting %d", ret);
					return -1;
				}

				//av_samples_copy(&audio_dst_data, frame.data, 0, 0, frame.nb_samples, frame.channels, (AVSampleFormat)frame.format);

				//memcpy(Memory::GetPointer(ctx->mp3PcmBuf + bytesdecoded), audio_dst_data, decoded);
				bytesdecoded += decoded;
				// av_freep(&audio_dst_data[0]);
			}
		}
		av_free_packet(&packet);
	}
	Memory::Write_U32(ctx->mp3PcmBuf, outPcmPtr);
#endif

	#if 0 && defined(_DEBUG)
	char fileName[256];
	sprintf(fileName, "out.wav", mp3);

	FILE * file = fopen(fileName, "a+b");
	if (file) {
		if (!Memory::IsValidAddress(ctx->mp3Buf)) {
			ERROR_LOG(HLE, "sceMp3Decode mp3Buf %08X is not a valid address!", ctx->mp3Buf);
		}

		//u8 * ptr = Memory::GetPointer(ctx->mp3Buf);
		fwrite(Memory::GetPointer(ctx->mp3PcmBuf), 1, bytesdecoded, file);

		fclose(file);
	}
	#endif

	return bytesdecoded;
}

int sceMp3ResetPlayPosition(u32 mp3) {
	DEBUG_LOG(HLE, "SceMp3ResetPlayPosition(%08x)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	ctx->readPosition = ctx->mp3StreamStart;
	return 0;
}

int sceMp3CheckStreamDataNeeded(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3CheckStreamDataNeeded(%08x)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->bufferAvailable != ctx->mp3BufSize && ctx->readPosition < ctx->mp3StreamEnd;
}

int readFunc(void *opaque, uint8_t *buf, int buf_size) {
	Mp3Context *ctx = static_cast<Mp3Context*>(opaque);

	int res = 0;
	while (ctx->bufferAvailable && buf_size) {
		// Maximum bytes we can read
		int to_read = std::min(ctx->bufferAvailable, buf_size);

		// Don't read past the end if the buffer loops
		to_read = std::min(ctx->mp3BufSize - ctx->bufferRead, to_read);
		memcpy(buf + res, Memory::GetCharPointer(ctx->mp3Buf + ctx->bufferRead), to_read);

		ctx->bufferRead += to_read;
		if (ctx->bufferRead == ctx->mp3BufSize)
			ctx->bufferRead = 0;
		ctx->bufferAvailable -= to_read;
		buf_size -= to_read;
		res += to_read;
	}

	if (ctx->bufferAvailable == 0) {
		ctx->bufferRead = 0;
		ctx->bufferWrite = 0;
	}

#if 0 && defined(_DEBUG)
	char fileName[256];
	sprintf(fileName, "out.mp3");

	FILE * file = fopen(fileName, "a+b");
	if (file) {
		if (!Memory::IsValidAddress(ctx->mp3Buf)) {
			ERROR_LOG(HLE, "sceMp3Decode mp3Buf %08X is not a valid address!", ctx->mp3Buf);
		}

		fwrite(buf, 1, res, file);

		fclose(file);
	}
#endif

	return res;
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

	ctx->readPosition = ctx->mp3StreamStart;
	ctx->mp3MaxSamples = ctx->mp3PcmBufSize / 4 ;

	ctx->mp3Channels = 2;
	ctx->mp3Bitrate = 128;
	ctx->mp3SamplingRate = 44100;

#ifdef USE_FFMPEG
	ctx->avformat_context = NULL;
	ctx->avio_context = NULL;
#endif

	mp3Map[mp3Addr] = ctx;
	return mp3Addr;
}

int sceMp3InitResource() {
	WARN_LOG(HLE, "UNIMPL: sceMp3InitResource");
	// Do nothing here 
	return 0;
}

int sceMp3TermResource() {
	WARN_LOG(HLE, "UNIMPL: sceMp3TermResource");
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

	ctx->mp3Version = ((header >> 19) & 0x3);

#ifdef USE_FFMPEG
	u8* avio_buffer = static_cast<u8*>(av_malloc(ctx->mp3BufSize));
	ctx->avio_context = avio_alloc_context(avio_buffer, ctx->mp3BufSize, 0, ctx, readFunc, NULL, NULL);
	ctx->avformat_context = avformat_alloc_context();
	ctx->avformat_context->pb = ctx->avio_context;

	int ret;
	if ((ret = avformat_open_input(&ctx->avformat_context, NULL, av_find_input_format("mp3"), NULL)) < 0) {
		ERROR_LOG(HLE, "avformat_open_input: Cannot open input %d", ret);
		return -1;
	}

	if ((ret = avformat_find_stream_info(ctx->avformat_context, NULL)) < 0) {
		ERROR_LOG(HLE, "avformat_find_stream_info: Cannot find stream information %d", ret);
		return -1;
	}

	AVCodec *dec;

	/* select the audio stream */
	ret = av_find_best_stream(ctx->avformat_context, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
	if (ret < 0) {
		ERROR_LOG(HLE, "av_find_best_stream: Cannot find a audio stream in the input file %d", ret);
		return -1;
	}
	ctx->audio_stream_index = ret;
	ctx->decoder_context = ctx->avformat_context->streams[ctx->audio_stream_index]->codec;

	/* init the audio decoder */
	if ((ret = avcodec_open2(ctx->decoder_context, dec, NULL)) < 0) {
		ERROR_LOG(HLE, "avcodec_open2: Cannot open audio decoder %d", ret);
		return -1;
	}

	ctx->resampler_context = swr_alloc_set_opts(NULL,
		ctx->decoder_context->channel_layout,
		AV_SAMPLE_FMT_S16,
		ctx->decoder_context->sample_rate,
		ctx->decoder_context->channel_layout,
		ctx->decoder_context->sample_fmt,
		ctx->decoder_context->sample_rate,
		0, NULL);

	// Let's just grab this info from FFMPEG, it seems more reliable than the code above.

	ctx->mp3SamplingRate = ctx->decoder_context->sample_rate;
	ctx->mp3Channels = ctx->decoder_context->channels;
	ctx->mp3Bitrate = ctx->decoder_context->bit_rate;

	if (!ctx->resampler_context) {
		ERROR_LOG(HLE, "Could not allocate resampler context %d", ret);
		return -1;
	}

	if ((ret = swr_init(ctx->resampler_context)) < 0) {
		ERROR_LOG(HLE, "Failed to initialize the resampling context %d", ret);
		return -1;
	}

	av_dump_format(ctx->avformat_context, 0, "mp3", 0);
#endif

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

int sceMp3GetMaxOutputSample(u32 mp3)
{
	DEBUG_LOG(HLE, "sceMp3GetMaxOutputSample(%08x)", mp3);
	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	return ctx->mp3MaxSamples;
}

int sceMp3GetSumDecodedSample(u32 mp3) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceMp3GetSumDecodedSample(%08X)", mp3);
	return 0;
}

int sceMp3SetLoopNum(u32 mp3, int loop) {
	INFO_LOG(HLE, "sceMp3SetLoopNum(%08X, %i)", mp3, loop);

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
	INFO_LOG(HLE, "sceMp3GetInfoToAddStreamData(%08X, %08X, %08X, %08X)", mp3, dstPtr, towritePtr, srcposPtr);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	u32 buf, max_write;
	if (ctx->readPosition < ctx->mp3StreamEnd) {
		buf = ctx->mp3Buf + ctx->bufferWrite;
		max_write = std::min(ctx->mp3BufSize - ctx->bufferWrite, ctx->mp3BufSize - ctx->bufferAvailable);
	} else {
		buf = 0;
		max_write = 0;
	}

	if (Memory::IsValidAddress(dstPtr))
		Memory::Write_U32(buf, dstPtr);
	if (Memory::IsValidAddress(towritePtr))
		Memory::Write_U32(max_write, towritePtr);
	if (Memory::IsValidAddress(srcposPtr))
		Memory::Write_U32(ctx->readPosition, srcposPtr);

	return 0;
}

int sceMp3NotifyAddStreamData(u32 mp3, int size) {
	INFO_LOG(HLE, "sceMp3NotifyAddStreamData(%08X, %i)", mp3, size);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

	ctx->readPosition += size;
	ctx->bufferAvailable += size;
	ctx->bufferWrite += size;

	if (ctx->bufferWrite == ctx->mp3BufSize)
		ctx->bufferWrite = 0;

	if (ctx->readPosition >= ctx->mp3StreamEnd && ctx->mp3LoopNum != 0) {
		ctx->readPosition = ctx->mp3StreamStart;
		if (ctx->mp3LoopNum > 0)
			ctx->mp3LoopNum--;
	}
	return 0;
}

int sceMp3ReleaseMp3Handle(u32 mp3) {
	DEBUG_LOG(HLE, "sceMp3ReleaseMp3Handle(%08X)", mp3);

	Mp3Context *ctx = getMp3Ctx(mp3);
	if (!ctx) {
		ERROR_LOG(HLE, "%s: bad mp3 handle %08x", __FUNCTION__, mp3);
		return -1;
	}

#ifdef USE_FFMPEG
	av_free(ctx->avio_context->buffer);
	av_free(ctx->avio_context);
#endif
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
	{0xAE6D2027,WrapU_U<sceMp3GetVersion>,"sceMp3GetVersion"}, // Name is wrong.
	{0x3548AEC8,WrapU_U<sceMp3GetFrameNum>,"sceMp3GetFrameNum"},
	{0x0840e808,0,"sceMp3_0840E808"},
	{0x1b839b83,0,"sceMp3_1B839B83"},
	{0xe3ee2c81,0,"sceMp3_E3EE2C81"},
};

void Register_sceMp3() {
	RegisterModule("sceMp3", ARRAY_SIZE(sceMp3), sceMp3);
}
