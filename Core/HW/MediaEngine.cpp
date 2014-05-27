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

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HW/MediaEngine.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "GPU/GPUInterface.h"
#include "Core/HW/SimpleAudioDec.h"

#include <algorithm>

#ifdef USE_FFMPEG

extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

}
#endif // USE_FFMPEG

int g_iNumVideos = 0;

#ifdef USE_FFMPEG
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
		ERROR_LOG(ME, "Unknown pixel format");
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
		ERROR_LOG(ME, "FF: %s", tmp);
	} else if (level >= AV_LOG_VERBOSE) {
		DEBUG_LOG(ME, "FF: %s", tmp);
	} else {
		INFO_LOG(ME, "FF: %s", tmp);
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
		ERROR_LOG(ME, "Unknown pixel format");
		return 4;
	}
}

void __AdjustBGMVolume(s16 *samples, u32 count) {
	if (g_Config.iBGMVolume < 0 || g_Config.iBGMVolume >= MAX_CONFIG_VOLUME) {
		return;
	}

	int volumeShift = MAX_CONFIG_VOLUME - g_Config.iBGMVolume;
	for (u32 i = 0; i < count; ++i) {
		samples[i] >>= volumeShift;
	}
}

MediaEngine::MediaEngine(): m_pdata(0) {
#ifdef USE_FFMPEG
	m_pFormatCtx = 0;
	m_pCodecCtxs.clear();
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
	m_sws_ctx = 0;
#endif
	m_sws_fmt = 0;
	m_buffer = 0;

	m_videoStream = -1;
	m_audioStream = -1;

	m_desWidth = 0;
	m_desHeight = 0;
	m_decodingsize = 0;
	m_bufSize = 0x2000;
	m_videopts = 0;
	m_pdata = 0;
	m_demux = 0;
	m_audioContext = 0;
	m_audiopts = 0;

	m_firstTimeStamp = 0;
	m_lastTimeStamp = 0;
	m_isVideoEnd = false;

	m_ringbuffersize = 0;
	m_mpegheaderReadPos = 0;
	m_audioType = PSP_CODEC_AT3PLUS; // in movie, we use only AT3+ audio
	g_iNumVideos++;
}

MediaEngine::~MediaEngine() {
	closeMedia();
	g_iNumVideos--;
}

void MediaEngine::closeMedia() {
	closeContext();
	if (m_pdata)
		delete m_pdata;
	if (m_demux)
		delete m_demux;
	m_pdata = 0;
	m_demux = 0;
	AudioClose(&m_audioContext);
	m_isVideoEnd = false;
}

void MediaEngine::DoState(PointerWrap &p){
	auto s = p.Section("MediaEngine", 1, 3);
	if (!s)
		return;

	p.Do(m_videoStream);
	p.Do(m_audioStream);

	p.DoArray(m_mpegheader, sizeof(m_mpegheader));

	p.Do(m_ringbuffersize);

	u32 hasloadStream = m_pdata != NULL;
	p.Do(hasloadStream);
	if (hasloadStream && p.mode == p.MODE_READ)
		loadStream(m_mpegheader, 2048, m_ringbuffersize);
#ifdef USE_FFMPEG
	u32 hasopencontext = m_pFormatCtx != NULL;
#else
	u32 hasopencontext = false;
#endif
	p.Do(hasopencontext);
	if (hasopencontext && p.mode == p.MODE_READ)
		openContext();
	if (m_pdata)
		m_pdata->DoState(p);
	if (m_demux)
		m_demux->DoState(p);

	p.Do(m_videopts);
	p.Do(m_audiopts);

	if (s >= 2) {
		p.Do(m_firstTimeStamp);
		p.Do(m_lastTimeStamp);
	}

	p.Do(m_isVideoEnd);
	bool noAudioDataRemoved;
	p.Do(noAudioDataRemoved);
	if (s >= 3) {
		p.Do(m_audioType);
	} else {
		m_audioType = PSP_CODEC_AT3PLUS;
	}
}

int _MpegReadbuffer(void *opaque, uint8_t *buf, int buf_size)
{
	MediaEngine *mpeg = (MediaEngine *)opaque;

	int size = buf_size;
	const int mpegheaderSize = sizeof(mpeg->m_mpegheader);
	if (mpeg->m_mpegheaderReadPos < mpegheaderSize) {
		size = std::min(buf_size, mpegheaderSize - mpeg->m_mpegheaderReadPos);
		memcpy(buf, mpeg->m_mpegheader + mpeg->m_mpegheaderReadPos, size);
		mpeg->m_mpegheaderReadPos += size;
	} else if (mpeg->m_mpegheaderReadPos == mpegheaderSize) {
		return 0;
	} else {
		size = mpeg->m_pdata->pop_front(buf, buf_size);
		if (size > 0)
			mpeg->m_decodingsize = size;
	}
	return size;
}

bool MediaEngine::openContext() {
#ifdef USE_FFMPEG
	InitFFmpeg();

	if (m_pFormatCtx || !m_pdata)
		return false;
	m_mpegheaderReadPos = 0;
	m_decodingsize = 0;

	u8* tempbuf = (u8*)av_malloc(m_bufSize);

	m_pFormatCtx = avformat_alloc_context();
	m_pIOContext = avio_alloc_context(tempbuf, m_bufSize, 0, (void*)this, _MpegReadbuffer, NULL, 0);
	m_pFormatCtx->pb = m_pIOContext;

	// Open video file
	if (avformat_open_input((AVFormatContext**)&m_pFormatCtx, NULL, NULL, NULL) != 0)
		return false;

	if (avformat_find_stream_info(m_pFormatCtx, NULL) < 0) {
		closeContext();
		return false;
	}

	if (m_videoStream >= (int)m_pFormatCtx->nb_streams) {
		WARN_LOG_REPORT(ME, "Bad video stream %d", m_videoStream);
		m_videoStream = -1;
	}

	if (m_videoStream == -1) {
		// Find the first video stream
		for(int i = 0; i < (int)m_pFormatCtx->nb_streams; i++) {
			if(m_pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
				m_videoStream = i;
				break;
			}
		}
		if(m_videoStream == -1)
			return false;
	}

	if (!setVideoStream(m_videoStream, true))
		return false;

	setVideoDim();
	m_audioContext = new SimpleAudio(m_audioType);
	m_isVideoEnd = false;
	m_mpegheaderReadPos++;
	av_seek_frame(m_pFormatCtx, m_videoStream, 0, 0);
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
	for (auto it = m_pCodecCtxs.begin(), end = m_pCodecCtxs.end(); it != end; ++it)
		avcodec_close(it->second);
	m_pCodecCtxs.clear();
	if (m_pFormatCtx)
		avformat_close_input(&m_pFormatCtx);
	sws_freeContext(m_sws_ctx);
	m_sws_ctx = NULL;
	m_pIOContext = 0;
#endif
	m_buffer = 0;
}

bool MediaEngine::loadStream(const u8 *buffer, int readSize, int RingbufferSize)
{
	closeMedia();

	m_videopts = 0;
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
			m_pdata->get_front(m_mpegheader, sizeof(m_mpegheader));
			int mpegoffset = (int)(*(s32_be*)(m_mpegheader + 8));
			m_pdata->pop_front(0, mpegoffset);
			openContext();
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
		AVCodecContext *m_pCodecCtx = m_pFormatCtx->streams[streamNum]->codec;

		// Find the decoder for the video stream
		AVCodec *pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
		if (pCodec == NULL) {
			return false;
		}

		// Open codec
		AVDictionary *optionsDict = 0;
		if (avcodec_open2(m_pCodecCtx, pCodec, &optionsDict) < 0) {
			return false; // Could not open codec
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
	m_pFrame = av_frame_alloc();

	sws_freeContext(m_sws_ctx);
	m_sws_ctx = NULL;
	m_sws_fmt = -1;
	updateSwsFormat(GE_CMODE_32BIT_ABGR8888);

	// Allocate video frame for RGB24
	m_pFrameRGB = av_frame_alloc();
	int numBytes = avpicture_get_size((AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight);
	m_buffer = (u8*)av_malloc(numBytes * sizeof(uint8_t));

	// Assign appropriate parts of buffer to image planes in m_pFrameRGB
	avpicture_fill((AVPicture *)m_pFrameRGB, m_buffer, (AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight);
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
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;

	updateSwsFormat(videoPixelMode);
	// TODO: Technically we could set this to frameWidth instead of m_desWidth for better perf.
	// Update the linesize for the new format too.  We started with the largest size, so it should fit.
	m_pFrameRGB->linesize[0] = getPixelFormatBytes(videoPixelMode) * m_desWidth;

	AVPacket packet;
	av_init_packet(&packet);
	int frameFinished;
	bool bGetFrame = false;
	while (!bGetFrame) {
		bool dataEnd = av_read_frame(m_pFormatCtx, &packet) < 0;
		// Even if we've read all frames, some may have been re-ordered frames at the end.
		// Still need to decode those, so keep calling avcodec_decode_video2().
		if (dataEnd || packet.stream_index == m_videoStream) {
			// avcodec_decode_video2() gives us the re-ordered frames with a NULL packet.
			if (dataEnd)
				av_free_packet(&packet);

			int result = avcodec_decode_video2(m_pCodecCtx, m_pFrame, &frameFinished, &packet);
			if (frameFinished) {
				if (!skipFrame) {
					sws_scale(m_sws_ctx, m_pFrame->data, m_pFrame->linesize, 0,
						m_pCodecCtx->height, m_pFrameRGB->data, m_pFrameRGB->linesize);
				}

				if (av_frame_get_best_effort_timestamp(m_pFrame) != AV_NOPTS_VALUE)
					m_videopts = av_frame_get_best_effort_timestamp(m_pFrame) + av_frame_get_pkt_duration(m_pFrame) - m_firstTimeStamp;
				else
					m_videopts += av_frame_get_pkt_duration(m_pFrame);
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
		av_free_packet(&packet);
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

	u32 mask = 0x00FFFFFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

inline void writeVideoLineABGR5650(void *destp, const void *srcp, int width) {
	memcpy(destp, srcp, width * sizeof(u16));
}

inline void writeVideoLineABGR5551(void *destp, const void *srcp, int width) {
	// TODO: Use SSE/NEON.
	u16_le *dest = (u16_le *)destp;
	const u16_le *src = (u16_le *)srcp;

	u16 mask = 0x7FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

inline void writeVideoLineABGR4444(void *destp, const void *srcp, int width) {
	// TODO: Use SSE/NEON.
	u16_le *dest = (u16_le *)destp;
	const u16_le *src = (u16_le *)srcp;

	u16 mask = 0x0FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

int MediaEngine::writeVideoImage(u32 bufferPtr, int frameWidth, int videoPixelMode) {
	if (!Memory::IsValidAddress(bufferPtr) || frameWidth > 2048) {
		// Clearly invalid values.  Let's just not.
		ERROR_LOG_REPORT(ME, "Ignoring invalid video decode address %08x/%x", bufferPtr, frameWidth);
		return false;
	}

	u8 *buffer = Memory::GetPointer(bufferPtr);

#ifdef USE_FFMPEG
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;
	int videoImageSize = 0;
	// lock the image size
	int height = m_desHeight;
	int width = m_desWidth;
	u8 *imgbuf = buffer;
	const u8 *data = m_pFrameRGB->data[0];

	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		for (int y = 0; y < height; y++) {
			writeVideoLineRGBA(imgbuf, data, width);
			data += width * sizeof(u32);
			imgbuf += frameWidth * sizeof(u32);
		}
		videoImageSize = frameWidth * sizeof(u32) * height;
		break;

	case GE_CMODE_16BIT_BGR5650:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf, data, width);
			data += width * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * height;
		break;

	case GE_CMODE_16BIT_ABGR5551:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf, data, width);
			data += width * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * height;
		break;

	case GE_CMODE_16BIT_ABGR4444:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf, data, width);
			data += width * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * height;
		break;

	default:
		ERROR_LOG_REPORT(ME, "Unsupported video pixel format %d", videoPixelMode);
		break;
	}

#ifndef MOBILE_DEVICE
	CBreakPoints::ExecMemCheck(bufferPtr, true, videoImageSize, currentMIPS->pc);
#endif
	return videoImageSize;
#endif // USE_FFMPEG
	return 0;
}

int MediaEngine::writeVideoImageWithRange(u32 bufferPtr, int frameWidth, int videoPixelMode,
	                             int xpos, int ypos, int width, int height) {
	if (!Memory::IsValidAddress(bufferPtr) || frameWidth > 2048) {
		// Clearly invalid values.  Let's just not.
		ERROR_LOG_REPORT(ME, "Ignoring invalid video decode address %08x/%x", bufferPtr, frameWidth);
		return false;
	}

	u8 *buffer = Memory::GetPointer(bufferPtr);

#ifdef USE_FFMPEG
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;
	int videoImageSize = 0;
	// lock the image size
	u8 *imgbuf = buffer;
	const u8 *data = m_pFrameRGB->data[0];

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
			imgbuf += frameWidth * sizeof(u32);
#ifndef MOBILE_DEVICE
			CBreakPoints::ExecMemCheck(bufferPtr + y * frameWidth * sizeof(u32), true, width * sizeof(u32), currentMIPS->pc);
#endif
		}
		videoImageSize = frameWidth * sizeof(u32) * m_desHeight;
		break;

	case GE_CMODE_16BIT_BGR5650:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
#ifndef MOBILE_DEVICE
			CBreakPoints::ExecMemCheck(bufferPtr + y * frameWidth * sizeof(u16), true, width * sizeof(u16), currentMIPS->pc);
#endif
		}
		videoImageSize = frameWidth * sizeof(u16) * m_desHeight;
		break;

	case GE_CMODE_16BIT_ABGR5551:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
#ifndef MOBILE_DEVICE
			CBreakPoints::ExecMemCheck(bufferPtr + y * frameWidth * sizeof(u16), true, width * sizeof(u16), currentMIPS->pc);
#endif
		}
		videoImageSize = frameWidth * sizeof(u16) * m_desHeight;
		break;

	case GE_CMODE_16BIT_ABGR4444:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
#ifndef MOBILE_DEVICE
			CBreakPoints::ExecMemCheck(bufferPtr + y * frameWidth * sizeof(u16), true, width * sizeof(u16), currentMIPS->pc);
#endif
		}
		videoImageSize = frameWidth * sizeof(u16) * m_desHeight;
		break;

	default:
		ERROR_LOG_REPORT(ME, "Unsupported video pixel format %d", videoPixelMode);
		break;
	}
	return videoImageSize;
#endif // USE_FFMPEG
	return 0;
}

u8 *MediaEngine::getFrameImage() {
#ifdef USE_FFMPEG
	return m_pFrameRGB->data[0];
#else
	return NULL;
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
	if (!Memory::IsValidAddress(bufferPtr)) {
		ERROR_LOG_REPORT(ME, "Ignoring bad audio decode address %08x during video playback", bufferPtr);
	}

	u8 *buffer = Memory::GetPointer(bufferPtr);
	if (!m_demux) {
		return 0;
	}

	u8 *audioFrame = 0;
	int headerCode1, headerCode2;
	int frameSize = getNextAudioFrame(&audioFrame, &headerCode1, &headerCode2);
	if (frameSize == 0) {
		return 0;
	}
	int outbytes = 0;

	if (m_audioContext != NULL) {
		if (!m_audioContext->Decode(audioFrame, frameSize, buffer, &outbytes)) {
			ERROR_LOG(ME, "Audio (%s) decode failed during video playback", GetCodecName(m_audioType));
		}
#ifndef MOBILE_DEVICE
		CBreakPoints::ExecMemCheck(bufferPtr, true, outbytes, currentMIPS->pc);
#endif
	}

	if (headerCode1 == 0x24) {
		// it a mono atrac3plus, convert it to stereo
		s16 *outbuf = (s16*)buffer;
		s16 *inbuf = (s16*)buffer;
		for (int i = 0x800 - 1; i >= 0; i--) {
			s16 sample = inbuf[i];
			outbuf[i * 2] = sample;
			outbuf[i * 2 + 1] = sample;
		}
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
