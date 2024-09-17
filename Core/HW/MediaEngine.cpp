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

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Math/CrossSIMD.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HW/MediaEngine.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "GPU/GPUState.h"  // Used by TextureDecoder.h when templates get instanced
#include "GPU/Common/TextureDecoder.h"
#include "GPU/GPUInterface.h"
#include "Core/HW/SimpleAudioDec.h"

#include <algorithm>

#ifdef _M_SSE
#include <emmintrin.h>
#endif

#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

#ifdef USE_FFMPEG

extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"

}
#endif // USE_FFMPEG

#ifdef USE_FFMPEG

#include "Core/FFMPEGCompat.h"

static AVPixelFormat getSwsFormat(int pspFormat)
{
	switch (pspFormat)
	{
	case GE_CMODE_16BIT_BGR5650:
		return AV_PIX_FMT_BGR565LE;
	case GE_CMODE_16BIT_ABGR5551:
		return AV_PIX_FMT_BGR555LE;
	case GE_CMODE_16BIT_ABGR4444:
		return AV_PIX_FMT_BGR444LE;
	case GE_CMODE_32BIT_ABGR8888:
		return AV_PIX_FMT_RGBA;
	default:
		ERROR_LOG(Log::ME, "Unknown pixel format");
		return (AVPixelFormat)0;
	}
}

void ffmpeg_logger(void *, int level, const char *format, va_list va_args) {
	// We're still called even if the level doesn't match.
	if (level > av_log_get_level())
		return;

	char tmp[1024];
	vsnprintf(tmp, sizeof(tmp), format, va_args);
	tmp[sizeof(tmp) - 1] = '\0';

	// Strip off any trailing newline.
	size_t len = strlen(tmp);
	if (tmp[len - 1] == '\n')
		tmp[len - 1] = '\0';

	if (!strcmp(tmp, "GHA Phase shifting")) {
		Reporting::ReportMessage("Atrac3+: GHA phase shifting");
	}

	// Let's color the log line appropriately.
	if (level <= AV_LOG_PANIC) {
		ERROR_LOG(Log::ME, "FF: %s", tmp);
	} else if (level >= AV_LOG_VERBOSE) {
		DEBUG_LOG(Log::ME, "FF: %s", tmp);
	} else {
		INFO_LOG(Log::ME, "FF: %s", tmp);
	}
}

bool InitFFmpeg() {
#ifdef _DEBUG
	av_log_set_level(AV_LOG_VERBOSE);
#else
	av_log_set_level(AV_LOG_WARNING);
#endif
	av_log_set_callback(&ffmpeg_logger);

	return true;
}
#endif

static int getPixelFormatBytes(int pspFormat)
{
	switch (pspFormat)
	{
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		return 2;
	case GE_CMODE_32BIT_ABGR8888:
		return 4;

	default:
		ERROR_LOG(Log::ME, "Unknown pixel format");
		return 4;
	}
}

MediaEngine::MediaEngine() {
	m_bufSize = 0x2000;

	m_mpegheaderSize = sizeof(m_mpegheader);
	m_audioType = PSP_CODEC_AT3PLUS; // in movie, we use only AT3+ audio
}

MediaEngine::~MediaEngine() {
	closeMedia();
}

void MediaEngine::closeMedia() {
	closeContext();
	delete m_pdata;
	delete m_demux;
	m_pdata = nullptr;
	m_demux = nullptr;
	AudioClose(&m_audioContext);
	m_isVideoEnd = false;
}

void MediaEngine::DoState(PointerWrap &p) {
	auto s = p.Section("MediaEngine", 1, 7);
	if (!s)
		return;

	Do(p, m_videoStream);
	Do(p, m_audioStream);

	DoArray(p, m_mpegheader, sizeof(m_mpegheader));
	if (s >= 4) {
		Do(p, m_mpegheaderSize);
	} else {
		m_mpegheaderSize = sizeof(m_mpegheader);
	}
	if (s >= 5) {
		Do(p, m_mpegheaderReadPos);
	} else {
		m_mpegheaderReadPos = m_mpegheaderSize;
	}
	if (s >= 6) {
		Do(p, m_expectedVideoStreams);
	} else {
		m_expectedVideoStreams = 0;
	}

	Do(p, m_ringbuffersize);

	u32 hasloadStream = m_pdata != nullptr;
	Do(p, hasloadStream);
	if (hasloadStream && p.mode == p.MODE_READ)
		reloadStream();
#ifdef USE_FFMPEG
	u32 hasopencontext = m_pFormatCtx != nullptr;
#else
	u32 hasopencontext = false;
#endif
	Do(p, hasopencontext);
	if (m_pdata)
		m_pdata->DoState(p);
	if (m_demux)
		m_demux->DoState(p);

	Do(p, m_videopts);
	if (s >= 7) {
		Do(p, m_lastPts);
	} else {
		m_lastPts = m_videopts;
	}
	Do(p, m_audiopts);

	if (s >= 2) {
		Do(p, m_firstTimeStamp);
		Do(p, m_lastTimeStamp);
	}

	if (hasopencontext && p.mode == p.MODE_READ) {
		openContext(true);
	}

	Do(p, m_isVideoEnd);
	bool noAudioDataRemoved;
	Do(p, noAudioDataRemoved);
	if (s >= 3) {
		Do(p, m_audioType);
	} else {
		m_audioType = PSP_CODEC_AT3PLUS;
	}
}

int MediaEngine::MpegReadbuffer(void *opaque, uint8_t *buf, int buf_size) {
	MediaEngine *mpeg = (MediaEngine *)opaque;

	int size = buf_size;
	if (mpeg->m_mpegheaderReadPos < mpeg->m_mpegheaderSize) {
		size = std::min(buf_size, mpeg->m_mpegheaderSize - mpeg->m_mpegheaderReadPos);
		memcpy(buf, mpeg->m_mpegheader + mpeg->m_mpegheaderReadPos, size);
		mpeg->m_mpegheaderReadPos += size;
	} else {
		size = mpeg->m_pdata->pop_front(buf, buf_size);
		if (size > 0)
			mpeg->m_decodingsize = size;
	}
	return size;
}

bool MediaEngine::SetupStreams() {
#ifdef USE_FFMPEG
	const u32 magic = *(u32_le *)&m_mpegheader[0];
	if (magic != PSMF_MAGIC) {
		WARN_LOG_REPORT(Log::ME, "Could not setup streams, bad magic: %08x", magic);
		return false;
	}
	int numStreams = *(u16_be *)&m_mpegheader[0x80];
	if (numStreams <= 0 || numStreams > 8) {
		// Looks crazy.  Let's bail out and let FFmpeg handle it.
		WARN_LOG_REPORT(Log::ME, "Could not setup streams, unexpected stream count: %d", numStreams);
		return false;
	}

	// Looking good.  Let's add those streams.
	int videoStreamNum = -1;
	for (int i = 0; i < numStreams; i++) {
		const u8 *const currentStreamAddr = m_mpegheader + 0x82 + i * 16;
		int streamId = currentStreamAddr[0];

		// We only set video streams.  We demux the audio stream separately.
		if ((streamId & PSMF_VIDEO_STREAM_ID) == PSMF_VIDEO_STREAM_ID) {
			++videoStreamNum;
			addVideoStream(videoStreamNum, streamId);
		}
	}
	// Add the streams to meet the expectation.
	for (int i = videoStreamNum + 1; i < m_expectedVideoStreams; i++) {
		addVideoStream(i);
	}
#endif

	return true;
}

bool MediaEngine::openContext(bool keepReadPos) {
#ifdef USE_FFMPEG
	InitFFmpeg();

	if (m_pFormatCtx || !m_pdata)
		return false;
	if (!keepReadPos) {
		m_mpegheaderReadPos = 0;
	}
	m_decodingsize = 0;

	m_bufSize = std::max(m_bufSize, m_mpegheaderSize);
	u8 *tempbuf = (u8*)av_malloc(m_bufSize);

	m_pFormatCtx = avformat_alloc_context();
	m_pIOContext = avio_alloc_context(tempbuf, m_bufSize, 0, (void*)this, &MpegReadbuffer, nullptr, nullptr);
	m_pFormatCtx->pb = m_pIOContext;

	// Open video file
    AVDictionary *open_opt = nullptr;
    av_dict_set_int(&open_opt, "probesize", m_mpegheaderSize, 0);
	if (avformat_open_input((AVFormatContext**)&m_pFormatCtx, nullptr, nullptr, &open_opt) != 0) {
		av_dict_free(&open_opt);
		return false;
	}
	av_dict_free(&open_opt);

	bool usedFFMPEGFindStreamInfo = false;
	if (!SetupStreams() || PSP_CoreParameter().compat.flags().UseFFMPEGFindStreamInfo) {
		// Fallback to old behavior.  Reads too much and corrupts when game doesn't read fast enough.
		// SetupStreams sometimes work for newer FFmpeg 3.1+ now, but sometimes framerate is missing.
		WARN_LOG_REPORT_ONCE(setupStreams, Log::ME, "Failed to read valid video stream data from header");
		if (avformat_find_stream_info(m_pFormatCtx, nullptr) < 0) {
			closeContext();
			return false;
		}
		usedFFMPEGFindStreamInfo = true;
	}

	if (m_videoStream >= (int)m_pFormatCtx->nb_streams) {
		WARN_LOG_REPORT(Log::ME, "Bad video stream %d", m_videoStream);
		m_videoStream = -1;
	}

	if (m_videoStream == -1) {
		// Find the first video stream
		for (int i = 0; i < (int)m_pFormatCtx->nb_streams; i++) {
			const AVStream *s = m_pFormatCtx->streams[i];
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 33, 100)
			AVMediaType type = s->codecpar->codec_type;
#else
			AVMediaType type = s->codec->codec_type;
#endif
			if (type == AVMEDIA_TYPE_VIDEO) {
				m_videoStream = i;
				break;
			}
		}
		if (m_videoStream == -1)
			return false;
	}

	if (!setVideoStream(m_videoStream, true))
		return false;

	setVideoDim();
	m_audioContext = CreateAudioDecoder((PSPAudioType)m_audioType);
	m_isVideoEnd = false;

	if (PSP_CoreParameter().compat.flags().UseFFMPEGFindStreamInfo && usedFFMPEGFindStreamInfo) {
		m_mpegheaderReadPos++;
		av_seek_frame(m_pFormatCtx, m_videoStream, 0, 0);
	}
#endif // USE_FFMPEG
	return true;
}

void MediaEngine::closeContext()
{
#ifdef USE_FFMPEG
	if (m_buffer)
		av_free(m_buffer);
	if (m_pFrameRGB)
		av_frame_free(&m_pFrameRGB);
	if (m_pFrame)
		av_frame_free(&m_pFrame);
	if (m_pIOContext && m_pIOContext->buffer)
		av_free(m_pIOContext->buffer);
	if (m_pIOContext)
		av_free(m_pIOContext);
	for (auto it : m_pCodecCtxs) {
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 33, 100)
		avcodec_free_context(&it.second);
#else
		avcodec_close(it.second);
#endif
	}
	m_pCodecCtxs.clear();
	// These are streams allocated from avformat_new_stream.
	for (auto it : m_codecsToClose) {
		avcodec_close(it);
	}
	m_codecsToClose.clear();
	if (m_pFormatCtx)
		avformat_close_input(&m_pFormatCtx);
	sws_freeContext(m_sws_ctx);
	m_sws_ctx = nullptr;
	m_pIOContext = nullptr;
#endif
	m_buffer = nullptr;
}

bool MediaEngine::loadStream(const u8 *buffer, int readSize, int RingbufferSize)
{
	closeMedia();

	m_videopts = 0;
	m_lastPts = -1;
	m_audiopts = 0;
	m_ringbuffersize = RingbufferSize;
	m_pdata = new BufferQueue(RingbufferSize + 2048);
	m_pdata->push(buffer, readSize);
	m_firstTimeStamp = getMpegTimeStamp(buffer + PSMF_FIRST_TIMESTAMP_OFFSET);
	m_lastTimeStamp = getMpegTimeStamp(buffer + PSMF_LAST_TIMESTAMP_OFFSET);
	int mpegoffset = (int)(*(s32_be*)(buffer + 8));
	m_demux = new MpegDemux(RingbufferSize + 2048, mpegoffset);
	m_demux->addStreamData(buffer, readSize);
	return true;
}

bool MediaEngine::reloadStream()
{
	return loadStream(m_mpegheader, 2048, m_ringbuffersize);
}

bool MediaEngine::addVideoStream(int streamNum, int streamId) {
#ifdef USE_FFMPEG
	if (m_pFormatCtx) {
		// no need to add an existing stream.
		if ((u32)streamNum < m_pFormatCtx->nb_streams)
			return true;
		AVCodec *h264_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		if (!h264_codec)
			return false;
		AVStream *stream = avformat_new_stream(m_pFormatCtx, h264_codec);
		if (stream) {
			// Reference ISO/IEC 13818-1.
			if (streamId == -1)
				streamId = PSMF_VIDEO_STREAM_ID | streamNum;

			stream->id = 0x00000100 | streamId;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 33, 100)
			stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
			stream->codecpar->codec_id = AV_CODEC_ID_H264;
#else
			stream->request_probe = 0;
			stream->need_parsing = AVSTREAM_PARSE_FULL;
#endif
			// We could set the width here, but we don't need to.
			if (streamNum >= m_expectedVideoStreams) {
				++m_expectedVideoStreams;
			}

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(59, 16, 100)
			AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
			AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
#else
			AVCodecContext *codecCtx = stream->codec;
#endif
			m_codecsToClose.push_back(codecCtx);
			return true;
		}
	}
#endif
	if (streamNum >= m_expectedVideoStreams) {
		++m_expectedVideoStreams;
	}
	return false;
}

int MediaEngine::addStreamData(const u8 *buffer, int addSize) {
	int size = addSize;
	if (size > 0 && m_pdata) {
		if (!m_pdata->push(buffer, size)) 
			size  = 0;
		if (m_demux) {
			m_demux->addStreamData(buffer, addSize);
		}
#ifdef USE_FFMPEG
		if (!m_pFormatCtx && m_pdata->getQueueSize() >= 2048) {
			m_mpegheaderSize = m_pdata->get_front(m_mpegheader, sizeof(m_mpegheader));
			int streamOffset = (int)(*(s32_be *)(m_mpegheader + 8));
			if (streamOffset <= m_mpegheaderSize) {
				m_mpegheaderSize = streamOffset;
				m_pdata->pop_front(0, m_mpegheaderSize);
				openContext();
			}
		}
#endif // USE_FFMPEG

		// We added data, so... not the end anymore?
		m_isVideoEnd = false;
	}
	return size;
}

bool MediaEngine::seekTo(s64 timestamp, int videoPixelMode) {
	if (timestamp <= 0) {
		return true;
	}

	// Just doing it the not so great way to be sure audio is in sync.
	int timeout = 1000;
	while (getVideoTimeStamp() < timestamp - 3003) {
		if (getAudioTimeStamp() < getVideoTimeStamp() - 4180 * 2) {
			getNextAudioFrame(NULL, NULL, NULL);
		}
		if (!stepVideo(videoPixelMode, true)) {
			return false;
		}
		if (--timeout <= 0) {
			return true;
		}
	}

	while (getAudioTimeStamp() < getVideoTimeStamp() - 4180 * 2) {
		if (getNextAudioFrame(NULL, NULL, NULL) == 0) {
			return false;
		}
		if (--timeout <= 0) {
			return true;
		}
	}

	return true;
}

bool MediaEngine::setVideoStream(int streamNum, bool force) {
	if (m_videoStream == streamNum && !force) {
		// Yay, nothing to do.
		return true;
	}

#ifdef USE_FFMPEG
	if (m_pFormatCtx && m_pCodecCtxs.find(streamNum) == m_pCodecCtxs.end()) {
		// Get a pointer to the codec context for the video stream
		if ((u32)streamNum >= m_pFormatCtx->nb_streams) {
			return false;
		}

		AVStream *stream = m_pFormatCtx->streams[streamNum];
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 33, 100)
		AVCodec *pCodec = avcodec_find_decoder(stream->codecpar->codec_id);
		if (!pCodec) {
			WARN_LOG_REPORT(Log::ME, "Could not find decoder for %d", (int)stream->codecpar->codec_id);
			return false;
		}
		AVCodecContext *m_pCodecCtx = avcodec_alloc_context3(pCodec);
		int paramResult = avcodec_parameters_to_context(m_pCodecCtx, stream->codecpar);
		if (paramResult < 0) {
			WARN_LOG_REPORT(Log::ME, "Failed to prepare context parameters: %08x", paramResult);
			return false;
		}
#else
		AVCodecContext *m_pCodecCtx = stream->codec;
		// Find the decoder for the video stream
		AVCodec *pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
		if (pCodec == nullptr) {
			return false;
		}
#endif

		m_pCodecCtx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT | AV_CODEC_FLAG_LOW_DELAY;

		AVDictionary *opt = nullptr;
		// Allow ffmpeg to use any number of threads it wants.  Without this, it doesn't use threads.
		av_dict_set(&opt, "threads", "0", 0);
		int openResult = avcodec_open2(m_pCodecCtx, pCodec, &opt);
		av_dict_free(&opt);
		if (openResult < 0) {
			return false;
		}

		m_pCodecCtxs[streamNum] = m_pCodecCtx;
	}
#endif
	m_videoStream = streamNum;

	return true;
}

bool MediaEngine::setVideoDim(int width, int height)
{
#ifdef USE_FFMPEG
	auto codecIter = m_pCodecCtxs.find(m_videoStream);
	if (codecIter == m_pCodecCtxs.end())
		return false;
	AVCodecContext *m_pCodecCtx = codecIter->second;

	if (width == 0 && height == 0)
	{
		// use the orignal video size
		m_desWidth = m_pCodecCtx->width;
		m_desHeight = m_pCodecCtx->height;
	}
	else
	{
		m_desWidth = width;
		m_desHeight = height;
	}

	// Allocate video frame
	if (!m_pFrame) {
		m_pFrame = av_frame_alloc();
	}

	sws_freeContext(m_sws_ctx);
	m_sws_ctx = nullptr;
	m_sws_fmt = -1;

	if (m_desWidth == 0 || m_desHeight == 0) {
		// Can't setup SWS yet, so stop for now.
		return false;
	}

	updateSwsFormat(GE_CMODE_32BIT_ABGR8888);

	// Allocate video frame for RGB24
	m_pFrameRGB = av_frame_alloc();
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
	int numBytes = av_image_get_buffer_size((AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight, 1);
#else
	int numBytes = avpicture_get_size((AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight);
#endif
	m_buffer = (u8*)av_malloc(numBytes * sizeof(uint8_t));

	// Assign appropriate parts of buffer to image planes in m_pFrameRGB
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
	av_image_fill_arrays(m_pFrameRGB->data, m_pFrameRGB->linesize, m_buffer, (AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight, 1);
#else
	avpicture_fill((AVPicture *)m_pFrameRGB, m_buffer, (AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight);
#endif
#endif // USE_FFMPEG
	return true;
}

void MediaEngine::updateSwsFormat(int videoPixelMode) {
#ifdef USE_FFMPEG
	auto codecIter = m_pCodecCtxs.find(m_videoStream);
	AVCodecContext *m_pCodecCtx = codecIter == m_pCodecCtxs.end() ? 0 : codecIter->second;

	AVPixelFormat swsDesired = getSwsFormat(videoPixelMode);
	if (swsDesired != m_sws_fmt && m_pCodecCtx != 0) {
		m_sws_fmt = swsDesired;
		m_sws_ctx = sws_getCachedContext
			(
				m_sws_ctx,
				m_pCodecCtx->width,
				m_pCodecCtx->height,
				m_pCodecCtx->pix_fmt,
				m_desWidth,
				m_desHeight,
				(AVPixelFormat)m_sws_fmt,
				SWS_BILINEAR,
				NULL,
				NULL,
				NULL
			);

		int *inv_coefficients;
		int *coefficients;
		int srcRange, dstRange;
		int brightness, contrast, saturation;

		if (sws_getColorspaceDetails(m_sws_ctx, &inv_coefficients, &srcRange, &coefficients, &dstRange, &brightness, &contrast, &saturation) != -1) {
			srcRange = 0;
			dstRange = 0;
			sws_setColorspaceDetails(m_sws_ctx, inv_coefficients, srcRange, coefficients, dstRange, brightness, contrast, saturation);
		}
	}
#endif
}

bool MediaEngine::stepVideo(int videoPixelMode, bool skipFrame) {
#ifdef USE_FFMPEG
	auto codecIter = m_pCodecCtxs.find(m_videoStream);
	AVCodecContext *m_pCodecCtx = codecIter == m_pCodecCtxs.end() ? 0 : codecIter->second;

	if (!m_pFormatCtx)
		return false;
	if (!m_pCodecCtx)
		return false;
	if (!m_pFrame)
		return false;

	AVPacket packet;
	av_init_packet(&packet);
	int frameFinished;
	bool bGetFrame = false;
	while (!bGetFrame) {
		bool dataEnd = av_read_frame(m_pFormatCtx, &packet) < 0;
		// Even if we've read all frames, some may have been re-ordered frames at the end.
		// Still need to decode those, so keep calling avcodec_decode_video2() / avcodec_receive_frame().
		if (dataEnd || packet.stream_index == m_videoStream) {
			// avcodec_decode_video2() / avcodec_send_packet() gives us the re-ordered frames with a NULL packet.
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
			if (dataEnd)
				av_packet_unref(&packet);
#else
			if (dataEnd)
				av_free_packet(&packet);
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 48, 101)
			if (packet.size != 0)
				avcodec_send_packet(m_pCodecCtx, &packet);
			int result = avcodec_receive_frame(m_pCodecCtx, m_pFrame);
			if (result == 0) {
				result = m_pFrame->pkt_size;
				frameFinished = 1;
			} else if (result == AVERROR(EAGAIN)) {
				result = 0;
				frameFinished = 0;
			} else {
				frameFinished = 0;
			}
#else
			int result = avcodec_decode_video2(m_pCodecCtx, m_pFrame, &frameFinished, &packet);
#endif
			if (frameFinished) {
				if (!m_pFrameRGB) {
					setVideoDim();
				}
				if (m_pFrameRGB && !skipFrame) {
					updateSwsFormat(videoPixelMode);
					// TODO: Technically we could set this to frameWidth instead of m_desWidth for better perf.
					// Update the linesize for the new format too.  We started with the largest size, so it should fit.
					m_pFrameRGB->linesize[0] = getPixelFormatBytes(videoPixelMode) * m_desWidth;

					sws_scale(m_sws_ctx, m_pFrame->data, m_pFrame->linesize, 0,
						m_pCodecCtx->height, m_pFrameRGB->data, m_pFrameRGB->linesize);
				}

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(55, 58, 100)
				int64_t bestPts = m_pFrame->best_effort_timestamp;
				int64_t ptsDuration = m_pFrame->pkt_duration;
#else
				int64_t bestPts = av_frame_get_best_effort_timestamp(m_pFrame);
				int64_t ptsDuration = av_frame_get_pkt_duration(m_pFrame);
#endif
				if (ptsDuration == 0) {
					if (m_lastPts == bestPts - m_firstTimeStamp || bestPts == AV_NOPTS_VALUE) {
						// TODO: Assuming 29.97 if missing.
						m_videopts += 3003;
					} else {
						m_videopts = bestPts - m_firstTimeStamp;
						m_lastPts = m_videopts;
					}
				} else if (bestPts != AV_NOPTS_VALUE) {
					m_videopts = bestPts + ptsDuration - m_firstTimeStamp;
					m_lastPts = m_videopts;
				} else {
					m_videopts += ptsDuration;
					m_lastPts = m_videopts;
				}
				bGetFrame = true;
			}
			if (result <= 0 && dataEnd) {
				// Sometimes, m_readSize is less than m_streamSize at the end, but not by much.
				// This is kinda a hack, but the ringbuffer would have to be prematurely empty too.
				m_isVideoEnd = !bGetFrame && (m_pdata->getQueueSize() == 0);
				if (m_isVideoEnd)
					m_decodingsize = 0;
				break;
			}
		}
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
		av_packet_unref(&packet);
#else
		av_free_packet(&packet);
#endif
	}
	return bGetFrame;
#else
	// If video engine is not available, just add to the timestamp at least.
	m_videopts += 3003;
	return true;
#endif // USE_FFMPEG
}

// Helpers that null out alpha (which seems to be the case on the PSP.)
// Some games depend on this, for example Sword Art Online (doesn't clear A's from buffer.)
inline void writeVideoLineRGBA(void *destp, const void *srcp, int width) {
	// TODO: Use SSE/NEON, investigate why AV_PIX_FMT_RGB0 does not work.
	u32_le *dest = (u32_le *)destp;
	const u32_le *src = (u32_le *)srcp;

	int count = width;

#if PPSSPP_ARCH(SSE2)
	__m128i mask = _mm_set1_epi32(0x00FFFFFF);
	while (count >= 8) {
		__m128i pixels1 = _mm_and_si128(_mm_loadu_si128((const __m128i *)src), mask);
		__m128i pixels2 = _mm_and_si128(_mm_loadu_si128((const __m128i *)src + 1), mask);
		_mm_storeu_si128((__m128i *)dest, pixels1);
		_mm_storeu_si128((__m128i *)dest + 1, pixels2);
		src += 8;
		dest += 8;
		count -= 8;
	}
#elif PPSSPP_ARCH(ARM_NEON)
	uint32x4_t mask = vdupq_n_u32(0x00FFFFFF);
	while (count >= 8) {
		uint32x4_t pixels1 = vandq_u32(vld1q_u32(src), mask);
		uint32x4_t pixels2 = vandq_u32(vld1q_u32(src + 4), mask);
		vst1q_u32(dest, pixels1);
		vst1q_u32(dest + 4, pixels2);
		src += 8;
		dest += 8;
		count -= 8;
	}
#endif
	const u32 mask32 = 0x00FFFFFF;
	DO_NOT_VECTORIZE_LOOP
	while (count--) {
		*dest++ = *src++ & mask32;
	}
}

inline void writeVideoLineABGR5650(void *destp, const void *srcp, int width) {
	memcpy(destp, srcp, width * sizeof(u16));
}

inline void writeVideoLineABGR5551(void *destp, const void *srcp, int width) {
	// TODO: Use SSE/NEON.
	u16_le *dest = (u16_le *)destp;
	const u16_le *src = (u16_le *)srcp;

	const u16 mask = 0x7FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

inline void writeVideoLineABGR4444(void *destp, const void *srcp, int width) {
	// TODO: Use SSE/NEON.
	u16_le *dest = (u16_le *)destp;
	const u16_le *src = (u16_le *)srcp;

	const u16 mask = 0x0FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

int MediaEngine::writeVideoImage(u32 bufferPtr, int frameWidth, int videoPixelMode) {
	int videoLineSize = 0;
	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		videoLineSize = frameWidth * sizeof(u32);
		break;
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		videoLineSize = frameWidth * sizeof(u16);
		break;
	}

	int videoImageSize = videoLineSize * m_desHeight;

	if (!Memory::IsValidRange(bufferPtr, videoImageSize) || frameWidth > 2048) {
		// Clearly invalid values.  Let's just not.
		ERROR_LOG_REPORT(Log::ME, "Ignoring invalid video decode address %08x/%x", bufferPtr, frameWidth);
		return 0;
	}

	u8 *buffer = Memory::GetPointerWriteUnchecked(bufferPtr);

#ifdef USE_FFMPEG
	if (!m_pFrame || !m_pFrameRGB)
		return 0;

	// lock the image size
	int height = m_desHeight;
	int width = m_desWidth;
	u8 *imgbuf = buffer;
	const u8 *data = m_pFrameRGB->data[0];

	bool swizzle = Memory::IsVRAMAddress(bufferPtr) && (bufferPtr & 0x00200000) == 0x00200000;
	if (swizzle) {
		imgbuf = new u8[videoImageSize];
	}

	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		for (int y = 0; y < height; y++) {
			writeVideoLineRGBA(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u32);
		}
		break;

	case GE_CMODE_16BIT_BGR5650:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u16);
		}
		break;

	case GE_CMODE_16BIT_ABGR5551:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u16);
		}
		break;

	case GE_CMODE_16BIT_ABGR4444:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u16);
		}
		break;

	default:
		ERROR_LOG_REPORT(Log::ME, "Unsupported video pixel format %d", videoPixelMode);
		break;
	}

	if (swizzle) {
		const int bxc = videoLineSize / 16;
		int byc = (height + 7) / 8;
		if (byc == 0)
			byc = 1;

		DoSwizzleTex16((const u32 *)imgbuf, buffer, bxc, byc, videoLineSize);
		delete [] imgbuf;
	}

	NotifyMemInfo(MemBlockFlags::WRITE, bufferPtr, videoImageSize, "VideoDecode");

	return videoImageSize;
#endif // USE_FFMPEG
	return 0;
}

int MediaEngine::writeVideoImageWithRange(u32 bufferPtr, int frameWidth, int videoPixelMode,
	                             int xpos, int ypos, int width, int height) {
	int videoLineSize = 0;
	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		videoLineSize = frameWidth * sizeof(u32);
		break;
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		videoLineSize = frameWidth * sizeof(u16);
		break;
	}
	int videoImageSize = videoLineSize * height;

	if (!Memory::IsValidRange(bufferPtr, videoImageSize) || frameWidth > 2048) {
		// Clearly invalid values.  Let's just not.
		ERROR_LOG_REPORT(Log::ME, "Ignoring invalid video decode address %08x/%x", bufferPtr, frameWidth);
		return 0;
	}

	u8 *buffer = Memory::GetPointerWriteUnchecked(bufferPtr);

#ifdef USE_FFMPEG
	if (!m_pFrame || !m_pFrameRGB)
		return 0;

	// lock the image size
	u8 *imgbuf = buffer;
	const u8 *data = m_pFrameRGB->data[0];

	bool swizzle = Memory::IsVRAMAddress(bufferPtr) && (bufferPtr & 0x00200000) == 0x00200000;
	if (swizzle) {
		imgbuf = new u8[videoImageSize];
	}

	if (width > m_desWidth - xpos)
		width = m_desWidth - xpos;
	if (height > m_desHeight - ypos)
		height = m_desHeight - ypos;

	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		data += (ypos * m_desWidth + xpos) * sizeof(u32);
		for (int y = 0; y < height; y++) {
			writeVideoLineRGBA(imgbuf, data, width);
			data += m_desWidth * sizeof(u32);
			imgbuf += videoLineSize;
		}
		break;

	case GE_CMODE_16BIT_BGR5650:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += videoLineSize;
		}
		break;

	case GE_CMODE_16BIT_ABGR5551:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += videoLineSize;
		}
		break;

	case GE_CMODE_16BIT_ABGR4444:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += videoLineSize;
		}
		break;

	default:
		ERROR_LOG_REPORT(Log::ME, "Unsupported video pixel format %d", videoPixelMode);
		break;
	}

	if (swizzle) {
		WARN_LOG_REPORT_ONCE(vidswizzle, Log::ME, "Swizzling Video with range");

		const int bxc = videoLineSize / 16;
		int byc = (height + 7) / 8;
		if (byc == 0)
			byc = 1;

		DoSwizzleTex16((const u32 *)imgbuf, buffer, bxc, byc, videoLineSize);
		delete [] imgbuf;
	}
	NotifyMemInfo(MemBlockFlags::WRITE, bufferPtr, videoImageSize, "VideoDecodeRange");

	return videoImageSize;
#endif // USE_FFMPEG
	return 0;
}

u8 *MediaEngine::getFrameImage() {
#ifdef USE_FFMPEG
	return m_pFrameRGB->data[0];
#else
	return nullptr;
#endif
}

int MediaEngine::getRemainSize() {
	if (!m_pdata)
		return 0;
	return std::max(m_pdata->getRemainSize() - m_decodingsize - 2048, 0);
}

int MediaEngine::getAudioRemainSize() {
	if (!m_demux) {
		// No audio, so it can't be full, return video instead.
		return getRemainSize();
	}

	return m_demux->getRemainSize();
}

int MediaEngine::getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2) {
	// When getting a frame, increment pts
	m_audiopts += 4180;

	// Demux now (rather than on add data) so that we select the right stream.
	m_demux->demux(m_audioStream);

	s64 pts = 0;
	int result = m_demux->getNextAudioFrame(buf, headerCode1, headerCode2, &pts);
	if (pts != 0) {
		// m_audiopts is supposed to be after the returned frame.
		m_audiopts = pts - m_firstTimeStamp + 4180;
	}
	return result;
}

int MediaEngine::getAudioSamples(u32 bufferPtr) {
	int16_t *buffer = (int16_t *)Memory::GetPointerWriteRange(bufferPtr, 8192);
	if (buffer == nullptr) {
		ERROR_LOG_REPORT(Log::ME, "Ignoring bad audio decode address %08x during video playback", bufferPtr);
	}
	if (!m_demux) {
		return 0;
	}

	u8 *audioFrame = nullptr;
	int headerCode1, headerCode2;
	int frameSize = getNextAudioFrame(&audioFrame, &headerCode1, &headerCode2);
	if (frameSize == 0) {
		return 0;
	}
	int outSamples = 0;

	if (m_audioContext != nullptr) {
		if (headerCode1 == 0x24) {
			// This means mono audio - tell the decoder to expect it before the first frame.
			// Note that it will always send us back stereo audio.
			m_audioContext->SetChannels(1);
		}

		int inbytesConsumed = 0;
		if (!m_audioContext->Decode(audioFrame, frameSize, &inbytesConsumed, 2, buffer, &outSamples)) {
			ERROR_LOG(Log::ME, "Audio (%s) decode failed during video playback", GetCodecName(m_audioType));
		}
		int outBytes = outSamples * sizeof(int16_t) * 2;

		NotifyMemInfo(MemBlockFlags::WRITE, bufferPtr, outBytes, "VideoDecodeAudio");
	}

	return 0x2000;
}

bool MediaEngine::IsNoAudioData() {
	if (!m_demux) {
		return true;
	}

	// Let's double check.  Here should be a safe enough place to demux.
	m_demux->demux(m_audioStream);
	return !m_demux->hasNextAudioFrame(NULL, NULL, NULL, NULL);
}

bool MediaEngine::IsActuallyPlayingAudio() {
	return getAudioTimeStamp() >= 0;
}

s64 MediaEngine::getVideoTimeStamp() {
	return m_videopts;
}

s64 MediaEngine::getAudioTimeStamp() {
	return m_demux ? m_audiopts - 4180 : -1;
}

s64 MediaEngine::getLastTimeStamp() {
	if (!m_pdata)
		return 0;
	return m_lastTimeStamp - m_firstTimeStamp;
}
