// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

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
#ifdef _WIN32
#include "GPU/Directx9/GPU_DX9.h"
#endif
#include "GPU/GLES/GPU_GLES.h"

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
	snprintf(s_format_context->filename, sizeof(s_format_context->filename), "%s", (GetSysDirectory(DIRECTORY_VIDEO_DUMP) + "framedump" + s_file_index_str.str() + ".avi").c_str());
	// Make sure that the path exists
	if (!File::Exists(GetSysDirectory(DIRECTORY_VIDEO_DUMP)))
		File::CreateDir(GetSysDirectory(DIRECTORY_VIDEO_DUMP));

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

static const u8 *ConvertBufferTo888RGB(const GPUDebugBuffer &buf, u8 *&temp, u32 &w, u32 &h) {
	// The temp buffer will be freed by the caller if set, and can be the return value.
	temp = nullptr;

	w = std::min(w, buf.GetStride());
	h = std::min(h, buf.GetHeight());

	const u8 *buffer = buf.GetData();
	if (buf.GetFlipped() && buf.GetFormat() == GPU_DBG_FORMAT_888_RGB) {
		// Silly OpenGL reads upside down, we flip to another buffer for simplicity.
		temp = new u8[3 * w * h];
		for (u32 y = 0; y < h; y++) {
			memcpy(temp + y * w * 3, buffer + (buf.GetHeight() - y - 1) * buf.GetStride() * 3, w * 3);
		}
		buffer = temp;
	}
	else if (buf.GetFormat() != GPU_DBG_FORMAT_888_RGB) {
		// Let's boil it down to how we need to interpret the bits.
		int baseFmt = buf.GetFormat() & ~(GPU_DBG_FORMAT_REVERSE_FLAG | GPU_DBG_FORMAT_BRSWAP_FLAG);
		bool rev = (buf.GetFormat() & GPU_DBG_FORMAT_REVERSE_FLAG) != 0;
		bool brswap = (buf.GetFormat() & GPU_DBG_FORMAT_BRSWAP_FLAG) != 0;
		bool flip = buf.GetFlipped();

		temp = new u8[3 * w * h];

		// This is pretty inefficient.
		const u16 *buf16 = (const u16 *)buffer;
		const u32 *buf32 = (const u32 *)buffer;
		for (u32 y = 0; y < h; y++) {
			for (u32 x = 0; x < w; x++) {
				u8 *dst;
				if (flip) {
					dst = &temp[(h - y - 1) * w * 3 + x * 3];
				}
				else {
					dst = &temp[y * w * 3 + x * 3];
				}

				u8 &r = brswap ? dst[2] : dst[0];
				u8 &g = dst[1];
				u8 &b = brswap ? dst[0] : dst[2];

				u32 src;
				switch (baseFmt) {
				case GPU_DBG_FORMAT_565:
					src = buf16[y * buf.GetStride() + x];
					if (rev) {
						src = bswap16(src);
					}
					r = Convert5To8((src >> 0) & 0x1F);
					g = Convert6To8((src >> 5) & 0x3F);
					b = Convert5To8((src >> 11) & 0x1F);
					break;
				case GPU_DBG_FORMAT_5551:
					src = buf16[y * buf.GetStride() + x];
					if (rev) {
						src = bswap16(src);
					}
					r = Convert5To8((src >> 0) & 0x1F);
					g = Convert5To8((src >> 5) & 0x1F);
					b = Convert5To8((src >> 10) & 0x1F);
					break;
				case GPU_DBG_FORMAT_4444:
					src = buf16[y * buf.GetStride() + x];
					if (rev) {
						src = bswap16(src);
					}
					r = Convert4To8((src >> 0) & 0xF);
					g = Convert4To8((src >> 4) & 0xF);
					b = Convert4To8((src >> 8) & 0xF);
					break;
				case GPU_DBG_FORMAT_8888:
					src = buf32[y * buf.GetStride() + x];
					if (rev) {
						src = bswap32(src);
					}
					r = (src >> 0) & 0xFF;
					g = (src >> 8) & 0xFF;
					b = (src >> 16) & 0xFF;
					break;
				default:
					ERROR_LOG(COMMON, "Unsupported framebuffer format for screenshot: %d", buf.GetFormat());
					return nullptr;
				}
			}
		}
		buffer = temp;
	}

	return buffer;
}

void AVIDump::AddFrame()
{
	GPUDebugBuffer buf;
	gpuDebug->GetCurrentFramebuffer(buf);
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

	// Convert image from BGR24 to desired pixel format, and scale to initial
	// width and height
	if ((s_sws_context =
           sws_getCachedContext(s_sws_context, w, h, AV_PIX_FMT_RGB24, s_width, s_height,
                                s_stream->codec->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr)))
	{
		sws_scale(s_sws_context, s_src_frame->data, s_src_frame->linesize, 0, h,
              s_scaled_frame->data, s_scaled_frame->linesize);
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
	// was dumped, then create a new file accordingly. However, is it possible for the height
	// (possibly width as well, but no examples known) to have a value of zero. This can occur as the
	// VI is able to be set to a zero value for height/width to disable output. If this is the case,
	// simply keep the last known resolution of the video for the added frame.
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
