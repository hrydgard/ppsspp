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

#include "MediaEngine.h"
#include "../MemMap.h"
#include "GPU/GPUInterface.h"
#include "Core/HW/atrac3plus.h"

#ifdef USE_FFMPEG

// Urgh! Why is this needed?
#ifdef ANDROID
#ifndef UINT64_C
#define UINT64_C(c) (c ## ULL)
#endif
#endif

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

MediaEngine::MediaEngine(): m_streamSize(0), m_readSize(0), m_decodedPos(0), m_pdata(0) {
	m_pFormatCtx = 0;
	m_pCodecCtx = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
	m_videoStream = -1;
	m_audioStream = -1;
	m_buffer = 0;
	m_demux = 0;
	m_audioContext = 0;
	m_isVideoEnd = false;
	m_isAudioEnd = false;
}

MediaEngine::~MediaEngine() {
	closeMedia();
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
		delete [] m_pdata;
	if (m_demux)
		delete m_demux;
	m_buffer = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
	m_pCodecCtx = 0;
	m_pFormatCtx = 0;
	m_pdata = 0;
	m_demux = 0;
	Atrac3plus_Decoder::CloseContext(&m_audioContext);
	m_isVideoEnd = false;
	m_isAudioEnd = false;
}

int _MpegReadbuffer(void *opaque, uint8_t *buf, int buf_size)
{
	MediaEngine *mpeg = (MediaEngine *)opaque;
	if ((u32)mpeg->m_decodeNextPos > (u32)mpeg->m_streamSize)
		return -1;

	int size = std::min(mpeg->m_bufSize, buf_size);
	int available = mpeg->m_readSize - mpeg->m_decodeNextPos;
	int remaining = mpeg->m_streamSize - mpeg->m_decodeNextPos;

	// There's more in the file, and there's not as much as requested available.
	// Return nothing.  Partial packets will cause artifacts or green frames.
	if (available < remaining && size > available)
		return 0;

	size = std::min(size, remaining);
	if (size > 0)
		memcpy(buf, mpeg->m_pdata + mpeg->m_decodeNextPos, size);
	mpeg->m_decodeNextPos += size;
	return size;
}

int64_t _MpegSeekbuffer(void *opaque, int64_t offset, int whence)
{
	MediaEngine *mpeg = (MediaEngine*)opaque;
	switch (whence) {
	case SEEK_SET:
		mpeg->m_decodeNextPos = offset;
		break;
	case SEEK_CUR:
		mpeg->m_decodeNextPos += offset;
		break;
	case SEEK_END:
		mpeg->m_decodeNextPos = mpeg->m_streamSize - (u32)offset;
		break;

#ifdef USE_FFMPEG
	// Don't seek, just return the full size.
	// Returning this means FFmpeg won't think frames are truncated if we don't have them yet.
	case AVSEEK_SIZE:
		return mpeg->m_streamSize;
#endif
	}
	return mpeg->m_decodeNextPos;
}

#ifdef _DEBUG
void ffmpeg_logger(void *, int, const char *format, va_list va_args) {
	char tmp[1024];
	vsprintf(tmp, format, va_args);
	INFO_LOG(HLE, tmp);
}
#endif

bool MediaEngine::openContext() {
#ifdef USE_FFMPEG

#ifdef _DEBUG
	av_log_set_level(AV_LOG_VERBOSE);
	av_log_set_callback(&ffmpeg_logger);
#endif 
	if (m_readSize <= 0x2000 || m_pFormatCtx || !m_pdata)
		return false;

	u8* tempbuf = (u8*)av_malloc(m_bufSize);

	m_pFormatCtx = avformat_alloc_context();
	m_pIOContext = avio_alloc_context(tempbuf, m_bufSize, 0, (void*)this, _MpegReadbuffer, NULL, _MpegSeekbuffer);
	m_pFormatCtx->pb = m_pIOContext;
  
	// Open video file
	if(avformat_open_input((AVFormatContext**)&m_pFormatCtx, NULL, NULL, NULL) != 0)
		return false;

	if(avformat_find_stream_info(m_pFormatCtx, NULL) < 0)
		return false;

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
	int mpegoffset = bswap32(*(int*)(m_pdata + 8));
	m_demux = new MpegDemux(m_pdata, m_streamSize, mpegoffset);
	m_demux->setReadSize(m_readSize);
	m_demux->demux(m_audioStream);
	m_audioPos = 0;
	m_audioContext = Atrac3plus_Decoder::OpenContext();
	m_isVideoEnd = false;
	m_isAudioEnd = false;
	m_decodedPos = mpegoffset;
#endif // USE_FFMPEG
	return true;
}

bool MediaEngine::loadStream(u8* buffer, int readSize, int StreamSize)
{
	closeMedia();
	// force to clear the useless FBO
	gpu->Resized();

	m_videopts = 0;
	m_audiopts = 0;
	m_bufSize = 0x2000;
	m_decodeNextPos = 0;
	m_readSize = readSize;
	m_streamSize = StreamSize;
	m_pdata = new u8[StreamSize];
	if (!m_pdata)
		return false;
	memcpy(m_pdata, buffer, m_readSize);
	
	return true;
}

bool MediaEngine::loadFile(const char* filename)
{
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	s64 infosize = info.size;
	u8* buf = new u8[infosize];
	if (!buf)
		return false;
	u32 h = pspFileSystem.OpenFile(filename, (FileAccess) FILEACCESS_READ);
	pspFileSystem.ReadFile(h, buf, infosize);
	pspFileSystem.CloseFile(h);

	closeMedia();
	// force to clear the useless FBO
	gpu->Resized();

	m_videopts = 0;
	m_audiopts = 0;
	m_bufSize = 0x2000;
	m_decodeNextPos = 0;
	m_readSize = infosize;
	m_streamSize = infosize;
	m_pdata = buf;

	return true;
}

int MediaEngine::addStreamData(u8* buffer, int addSize) {
	int size = std::min(addSize, m_streamSize - m_readSize);
	if (size > 0 && m_pdata) {
		memcpy(m_pdata + m_readSize, buffer, size);
		m_readSize += size;
		if (!m_pFormatCtx && (m_readSize > 0x20000 || m_readSize >= m_streamSize))
			openContext();
		if (m_demux) {
			m_demux->setReadSize(m_readSize);
			m_demux->demux(m_audioStream);
		}
	}
	return size;
}

bool MediaEngine::setVideoDim(int width, int height)
{
	if (!m_pCodecCtx)
		return false;
#ifdef USE_FFMPEG
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
		if (!dataEnd) {
			if (packet.pos != -1) {
				m_decodedPos = packet.pos;
			} else {
				// Packet doesn't know where it is in the file, let's try to approximate.
				m_decodedPos += packet.size;
			}
		}

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

				s64 firstTimeStamp = getMpegTimeStamp(m_pdata + PSMF_FIRST_TIMESTAMP_OFFSET);
				m_videopts = m_pFrame->pkt_dts + av_frame_get_pkt_duration(m_pFrame) - firstTimeStamp;
				bGetFrame = true;
			}
			if (result <= 0 && dataEnd) {
				// Sometimes, m_readSize is less than m_streamSize at the end, but not by much.
				// This is kinda a hack, but the ringbuffer would have to be prematurely empty too.
				m_isVideoEnd = !bGetFrame && m_readSize >= (m_streamSize - 4096);
				if (m_isVideoEnd)
					m_decodedPos = m_readSize;
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
	u32 *dest = (u32 *)destp;
	const u32 *src = (u32 *)srcp;

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
	u16 *dest = (u16 *)destp;
	const u16 *src = (u16 *)srcp;

	u16 mask = 0x7FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

inline void writeVideoLineABGR4444(void *destp, const void *srcp, int width) {
	// TODO: Use SSE/NEON.
	u16 *dest = (u16 *)destp;
	const u16 *src = (u16 *)srcp;

	u16 mask = 0x0FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

int MediaEngine::writeVideoImage(u8* buffer, int frameWidth, int videoPixelMode) {
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;
#ifdef USE_FFMPEG
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
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;
#ifdef USE_FFMPEG
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

static bool isHeader(u8* audioStream, int offset)
{
	const u8 header1 = (u8)0x0F;
	const u8 header2 = (u8)0xD0;
	return (audioStream[offset] == header1) && (audioStream[offset+1] == header2);
}

static int getNextHeaderPosition(u8* audioStream, int curpos, int limit, int frameSize)
{
	int endScan = limit - 1;

	// Most common case: the header can be found at each frameSize
	int offset = curpos + frameSize - 8;
	if (offset < endScan && isHeader(audioStream, offset))
		return offset;
	for (int scan = curpos; scan < endScan; scan++) {
		if (isHeader(audioStream, scan))
			return scan;
	}

	return -1;
}

int MediaEngine::getBufferedSize() {
    return std::max(0, m_readSize - (int)m_decodedPos);
}

int MediaEngine::getAudioSamples(u8* buffer) {
	if (!m_demux) {
		return 0;
	}
	u8* audioStream = 0;
	int audioSize = m_demux->getaudioStream(&audioStream);
	if (m_audioPos >= audioSize || !isHeader(audioStream, m_audioPos))
	{
		m_isAudioEnd = m_demux->getFilePosition() >= m_streamSize;
		return 0;
	}
	u8 headerCode1 = audioStream[2];
	u8 headerCode2 = audioStream[3];
	int frameSize = ((headerCode1 & 0x03) << 8) | (headerCode2 & 0xFF) * 8 + 0x10;
	if (m_audioPos + frameSize > audioSize)
		return 0;
	m_audioPos += 8;
	int nextHeader = getNextHeaderPosition(audioStream, m_audioPos, audioSize, frameSize);
	u8* frame = audioStream + m_audioPos;
	int outbytes = 0;
	Atrac3plus_Decoder::Decode(m_audioContext, frame, frameSize - 8, &outbytes, buffer);
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
	if (nextHeader >= 0) {
		m_audioPos = nextHeader;
	} else
		m_audioPos = audioSize;
	m_audiopts += 4180;
	m_decodedPos += frameSize;
	return outbytes;
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
	s64 firstTimeStamp = getMpegTimeStamp(m_pdata + PSMF_FIRST_TIMESTAMP_OFFSET);
	s64 lastTimeStamp = getMpegTimeStamp(m_pdata + PSMF_LAST_TIMESTAMP_OFFSET);
	return lastTimeStamp - firstTimeStamp;
}
