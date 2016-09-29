// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#ifndef MOBILE_DEVICE
#if defined(__FreeBSD__)
#define __STDC_CONSTANT_MACROS 1
#endif

#include <string>
#include <sstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

#include "Common/FileUtil.h"
#include "Common/MsgHandler.h"
#include "Common/ColorConv.h"

#include "Core/Config.h"
#include "Core/AVIDump.h"
#include "Core/System.h"
#include "Core/Screenshot.h"

#include "GPU/Common/GPUDebugInterface.h"

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55, 28, 1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

static AVFormatContext* s_format_context = nullptr;
static AVStream* s_stream = nullptr;
static AVFrame* s_src_frame = nullptr;
static AVFrame* s_scaled_frame = nullptr;
static int s_bytes_per_pixel;
static SwsContext* s_sws_context = nullptr;
static int s_width;
static int s_height;
static bool s_start_dumping = false;
static int s_current_width;
static int s_current_height;
static int s_file_index = 0;
static GPUDebugBuffer buf;

static void InitAVCodec()
{
	static bool first_run = true;
	if (first_run)
	{
		av_register_all();
		first_run = false;
	}
}

bool AVIDump::Start(int w, int h)
{
	s_width = w;
	s_height = h;
	s_current_width = w;
	s_current_height = h;

	InitAVCodec();
	bool success = CreateAVI();
	if (!success)
		CloseFile();
	return success;
}

bool AVIDump::CreateAVI()
{
	AVCodec* codec = nullptr;

	s_format_context = avformat_alloc_context();
	std::stringstream s_file_index_str;
	s_file_index_str << s_file_index;
	snprintf(s_format_context->filename, sizeof(s_format_context->filename), "%s", (GetSysDirectory(DIRECTORY_VIDEO) + "framedump" + s_file_index_str.str() + ".avi").c_str());
	// Make sure that the path exists
	if (!File::Exists(GetSysDirectory(DIRECTORY_VIDEO)))
		File::CreateDir(GetSysDirectory(DIRECTORY_VIDEO));

	if (File::Exists(s_format_context->filename))
		File::Delete(s_format_context->filename);

	if (!(s_format_context->oformat = av_guess_format("avi", nullptr, nullptr)) || !(s_stream = avformat_new_stream(s_format_context, codec)))
	{
		return false;
	}

	s_stream->codec->codec_id = g_Config.bUseFFV1 ? AV_CODEC_ID_FFV1 : s_format_context->oformat->video_codec;
	if (!g_Config.bUseFFV1)
		s_stream->codec->codec_tag = MKTAG('X', 'V', 'I', 'D');  // Force XVID FourCC for better compatibility
	s_stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
	s_stream->codec->bit_rate = 400000;
	s_stream->codec->width = s_width;
	s_stream->codec->height = s_height;
	s_stream->codec->time_base.num = 1001;
	s_stream->codec->time_base.den = 60000;
	s_stream->codec->gop_size = 12;
	s_stream->codec->pix_fmt = g_Config.bUseFFV1 ? AV_PIX_FMT_BGRA : AV_PIX_FMT_YUV420P;

	if (!(codec = avcodec_find_encoder(s_stream->codec->codec_id)) || (avcodec_open2(s_stream->codec, codec, nullptr) < 0))
	{
		return false;
	}

	s_src_frame = av_frame_alloc();
	s_scaled_frame = av_frame_alloc();

	s_scaled_frame->format = s_stream->codec->pix_fmt;
	s_scaled_frame->width = s_width;
	s_scaled_frame->height = s_height;

#if LIBAVCODEC_VERSION_MAJOR >= 55
	if (av_frame_get_buffer(s_scaled_frame, 1))
		return false;
#else
	if (avcodec_default_get_buffer(s_stream->codec, s_scaled_frame))
		return false;
#endif

	NOTICE_LOG(G3D, "Opening file %s for dumping", s_format_context->filename);
	if (avio_open(&s_format_context->pb, s_format_context->filename, AVIO_FLAG_WRITE) < 0 || avformat_write_header(s_format_context, nullptr))
	{
		WARN_LOG(G3D, "Could not open %s", s_format_context->filename);
		return false;
	}

	return true;
}

static void PreparePacket(AVPacket* pkt)
{
	av_init_packet(pkt);
	pkt->data = nullptr;
	pkt->size = 0;
}

void AVIDump::AddFrame()
{
	gpuDebug->GetCurrentFramebuffer(buf, GPU_DBG_FRAMEBUF_DISPLAY);
	u32 w = buf.GetStride();
	u32 h = buf.GetHeight();
	CheckResolution(w, h);
	u8 *flipbuffer = nullptr;
	const u8 *buffer = ConvertBufferTo888RGB(buf, flipbuffer, w, h);
	s_src_frame->data[0] = const_cast<u8*>(buffer);
	s_src_frame->linesize[0] = w * 3;
	s_src_frame->format = AV_PIX_FMT_RGB24;
	s_src_frame->width = s_width;
	s_src_frame->height = s_height;

	// Convert image from BGR24 to desired pixel format, and scale to initial width and height
	if ((s_sws_context = sws_getCachedContext(s_sws_context, w, h, AV_PIX_FMT_RGB24, s_width, s_height, s_stream->codec->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr)))
	{
		sws_scale(s_sws_context, s_src_frame->data, s_src_frame->linesize, 0, h, s_scaled_frame->data, s_scaled_frame->linesize);
	}

	s_scaled_frame->format = s_stream->codec->pix_fmt;
	s_scaled_frame->width = s_width;
	s_scaled_frame->height = s_height;

	// Encode and write the image.
	AVPacket pkt;
	PreparePacket(&pkt);
	int got_packet;
	int error = avcodec_encode_video2(s_stream->codec, &pkt, s_scaled_frame, &got_packet);
	while (!error && got_packet)
	{
		// Write the compressed frame in the media file.
		if (pkt.pts != (s64)AV_NOPTS_VALUE)
		{
			pkt.pts = av_rescale_q(pkt.pts, s_stream->codec->time_base, s_stream->time_base);
		}
		if (pkt.dts != (s64)AV_NOPTS_VALUE)
		{
			pkt.dts = av_rescale_q(pkt.dts, s_stream->codec->time_base, s_stream->time_base);
		}
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(56, 60, 100)
		if (s_stream->codec->coded_frame->key_frame)
			pkt.flags |= AV_PKT_FLAG_KEY;
#endif
		pkt.stream_index = s_stream->index;
		av_interleaved_write_frame(s_format_context, &pkt);

		// Handle delayed frames.
		PreparePacket(&pkt);
		error = avcodec_encode_video2(s_stream->codec, &pkt, nullptr, &got_packet);
	}
	if (error)
		ERROR_LOG(G3D, "Error while encoding video: %d", error);
}

void AVIDump::Stop()
{
	av_write_trailer(s_format_context);
	CloseFile();
	s_file_index = 0;
	NOTICE_LOG(G3D, "Stopping frame dump");
}

void AVIDump::CloseFile()
{
	if (s_stream)
	{
		if (s_stream->codec)
		{
#if LIBAVCODEC_VERSION_MAJOR < 55
			avcodec_default_release_buffer(s_stream->codec, s_src_frame);
#endif
			avcodec_close(s_stream->codec);
		}
		av_freep(&s_stream);
	}

	av_frame_free(&s_src_frame);
	av_frame_free(&s_scaled_frame);

	if (s_format_context)
	{
		if (s_format_context->pb)
			avio_close(s_format_context->pb);
		av_freep(&s_format_context);
	}

	if (s_sws_context)
	{
		sws_freeContext(s_sws_context);
		s_sws_context = nullptr;
	}
}

void AVIDump::CheckResolution(int width, int height)
{
	// We check here to see if the requested width and height have changed since the last frame which
	// was dumped, then create a new file accordingly. However, is it possible for the width and height
	// to have a value of zero. If this is the case, simply keep the last known resolution of the video
	// for the added frame.
	if ((width != s_current_width || height != s_current_height) && (width > 0 && height > 0))
	{
		int temp_file_index = s_file_index;
		Stop();
		s_file_index = temp_file_index + 1;
		Start(width, height);
		s_current_width = width;
		s_current_height = height;
	}
}
#endif
