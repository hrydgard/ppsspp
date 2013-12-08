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
#include "Core/HW/MediaEngine.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "GPU/GPUInterface.h"
#include "Core/HW/SimpleAT3Dec.h"

#include <algorithm>

#ifdef USE_FFMPEG

extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

}
#endif // USE_FFMPEG

static const int TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650 = 0x00;
static const int TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR5551 = 0x01;
static const int TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR4444 = 0x02;
static const int TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888 = 0x03;

int g_iNumVideos = 0;

#ifdef USE_FFMPEG
static AVPixelFormat getSwsFormat(int pspFormat)
{
	switch (pspFormat)
	{
	case TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650:
		return AV_PIX_FMT_BGR565LE;
	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR5551:
		return AV_PIX_FMT_BGR555LE;
	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR4444:
		return AV_PIX_FMT_BGR444LE;
	case TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888:
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
	case TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650:
	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR5551:
	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR4444:
		return 2;
	case TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888:
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
	m_pCodecCtx = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
#endif
	m_videoStream = -1;
	m_audioStream = -1;
	m_buffer = 0;
	m_demux = 0;
	m_audioContext = 0;
	m_isVideoEnd = false;
	m_noAudioData = false;
	m_bufSize = 0x2000;
	m_mpegheaderReadPos = 0;
	g_iNumVideos++;
}

MediaEngine::~MediaEngine() {
	closeMedia();
	g_iNumVideos--;
}

void MediaEngine::closeMedia() {
#ifdef USE_FFMPEG
	if (m_buffer)
		av_free(m_buffer);
	if (m_pFrameRGB)
		av_free(m_pFrameRGB);
	if (m_pFrame)
		av_free(m_pFrame);
	if (m_pIOContext && m_pIOContext->buffer)
		av_free(m_pIOContext->buffer);
	if (m_pIOContext)
		av_free(m_pIOContext);
	if (m_pCodecCtx)
		avcodec_close(m_pCodecCtx);
	if (m_pFormatCtx)
		avformat_close_input(&m_pFormatCtx);
#endif // USE_FFMPEG
	if (m_pdata)
		delete m_pdata;
	if (m_demux)
		delete m_demux;
	m_buffer = 0;
#ifdef USE_FFMPEG
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
	m_pCodecCtx = 0;
	m_pFormatCtx = 0;
#endif
	m_pdata = 0;
	m_demux = 0;
	AT3Close(&m_audioContext);
	m_isVideoEnd = false;
	m_noAudioData = false;
}

void MediaEngine::DoState(PointerWrap &p){
	auto s = p.Section("MediaEngine", 1);
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

	p.Do(m_isVideoEnd);
	p.Do(m_noAudioData);
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

	if (avformat_find_stream_info(m_pFormatCtx, NULL) < 0)
		return false;

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

	// Get a pointer to the codec context for the video stream
	m_pCodecCtx = m_pFormatCtx->streams[m_videoStream]->codec;

	// Find the decoder for the video stream
	AVCodec *pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
	if(pCodec == NULL)
		return false;

	// Open codec
	AVDictionary *optionsDict = 0;
	if(avcodec_open2(m_pCodecCtx, pCodec, &optionsDict)<0)
		return false; // Could not open codec

	setVideoDim();
	m_audioContext = AT3Create();
	m_isVideoEnd = false;
	m_noAudioData = false;
	m_mpegheaderReadPos++;
	av_seek_frame(m_pFormatCtx, m_videoStream, 0, 0);
#endif // USE_FFMPEG
	return true;
}

bool MediaEngine::loadStream(u8* buffer, int readSize, int RingbufferSize)
{
	closeMedia();

	m_videopts = 0;
	m_audiopts = 0;
	m_ringbuffersize = RingbufferSize;
	m_pdata = new BufferQueue(RingbufferSize + 2048);
	if (!m_pdata)
		return false;
	m_pdata->push(buffer, readSize);
	m_firstTimeStamp = getMpegTimeStamp(buffer + PSMF_FIRST_TIMESTAMP_OFFSET);
	m_lastTimeStamp = getMpegTimeStamp(buffer + PSMF_LAST_TIMESTAMP_OFFSET);
	int mpegoffset = (int)(*(s32_be*)(buffer + 8));
	m_demux = new MpegDemux(RingbufferSize + 2048, mpegoffset);
	m_demux->addStreamData(buffer, readSize);
	return true;
}

int MediaEngine::addStreamData(u8* buffer, int addSize) {
	int size = addSize;
	if (size > 0 && m_pdata) {
		if (!m_pdata->push(buffer, size)) 
			size  = 0;
		if (m_demux) {
			m_demux->addStreamData(buffer, addSize);
			m_demux->demux(m_audioStream);
		}
#ifdef USE_FFMPEG
		if (!m_pFormatCtx) {
			m_pdata->get_front(m_mpegheader, sizeof(m_mpegheader));
			int mpegoffset = (int)(*(s32_be*)(m_mpegheader + 8));
			m_pdata->pop_front(0, mpegoffset);
			openContext();
		}
#endif // USE_FFMPEG
	}
	return size;
}

bool MediaEngine::setVideoDim(int width, int height)
{
#ifdef USE_FFMPEG
	if (!m_pCodecCtx)
		return false;
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
	m_pFrame = avcodec_alloc_frame();
	
	m_sws_ctx = NULL;
	m_sws_fmt = -1;
	updateSwsFormat(TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888);

	// Allocate video frame for RGB24
	m_pFrameRGB = avcodec_alloc_frame();
	int numBytes = avpicture_get_size((AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight);
    m_buffer = (u8*)av_malloc(numBytes * sizeof(uint8_t));
  
    // Assign appropriate parts of buffer to image planes in pFrameRGB   
    avpicture_fill((AVPicture *)m_pFrameRGB, m_buffer, (AVPixelFormat)m_sws_fmt, m_desWidth, m_desHeight);
#endif // USE_FFMPEG
	return true;
}

void MediaEngine::updateSwsFormat(int videoPixelMode) {
#ifdef USE_FFMPEG
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

bool MediaEngine::stepVideo(int videoPixelMode) {
	// if video engine is broken, force to add timestamp
	m_videopts += 3003;
#ifdef USE_FFMPEG
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
				sws_scale(m_sws_ctx, m_pFrame->data, m_pFrame->linesize, 0,
					m_pCodecCtx->height, m_pFrameRGB->data, m_pFrameRGB->linesize);

				m_videopts = m_pFrame->pkt_dts + av_frame_get_pkt_duration(m_pFrame) - m_firstTimeStamp;
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

int MediaEngine::writeVideoImage(u8* buffer, int frameWidth, int videoPixelMode) {
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
	case TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888:
		for (int y = 0; y < height; y++) {
			writeVideoLineRGBA(imgbuf, data, width);
			data += width * sizeof(u32);
			imgbuf += frameWidth * sizeof(u32);
		}
		videoImageSize = frameWidth * sizeof(u32) * height;
		break;

	case TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf, data, width);
			data += width * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * height;
		break;

	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR5551:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf, data, width);
			data += width * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * height;
		break;

	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR4444:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf, data, width);
			data += width * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * height;
		break;

	default:
		ERROR_LOG(ME, "Unsupported video pixel format %d", videoPixelMode);
		break;
	}
	return videoImageSize;
#endif // USE_FFMPEG
	return 0;
}

int MediaEngine::writeVideoImageWithRange(u8* buffer, int frameWidth, int videoPixelMode, 
	                             int xpos, int ypos, int width, int height) {
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
	case TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888:
		data += (ypos * m_desWidth + xpos) * sizeof(u32);
		for (int y = 0; y < height; y++) {
			writeVideoLineRGBA(imgbuf, data, width);
			data += m_desWidth * sizeof(u32);
			imgbuf += frameWidth * sizeof(u32);
		}
		videoImageSize = frameWidth * sizeof(u32) * m_desHeight;
		break;

	case TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * m_desHeight;
		break;

	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR5551:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * m_desHeight;
		break;

	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR4444:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += frameWidth * sizeof(u16);
		}
		videoImageSize = frameWidth * sizeof(u16) * m_desHeight;
		break;

	default:
		ERROR_LOG(ME, "Unsupported video pixel format %d", videoPixelMode);
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

int MediaEngine::getAudioSamples(u8* buffer) {
	if (!m_demux) {
		return 0;
	}
	u8 *audioFrame = 0;
	int headerCode1, headerCode2;
	int frameSize = m_demux->getNextaudioFrame(&audioFrame, &headerCode1, &headerCode2);
	if (frameSize == 0) {
		m_noAudioData = true;
		return 0;
	}
	int outbytes = 0;

	if (m_audioContext != NULL) {
		if (!AT3Decode(m_audioContext, audioFrame, frameSize, &outbytes, buffer)) {
			ERROR_LOG(ME, "AT3 decode failed during video playback");
		}
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
	m_audiopts += 4180;
	m_noAudioData = false;
	return 0x2000;
}

s64 MediaEngine::getVideoTimeStamp() {
	return m_videopts;
}

s64 MediaEngine::getAudioTimeStamp() {
	if (m_demux)
		return std::max(m_audiopts - 4180, (s64)0);
	return m_videopts;
}

s64 MediaEngine::getLastTimeStamp() {
	if (!m_pdata)
		return 0;
	return m_lastTimeStamp - m_firstTimeStamp;
}
