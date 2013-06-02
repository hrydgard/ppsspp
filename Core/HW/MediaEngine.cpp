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

inline void YUV444toRGB888(u8 ypos, u8 upos, u8 vpos, u8 &r, u8 &g, u8 &b)
{
	u8 u = upos - 128;
	u8 v = vpos -128;
	int rdif = v + ((v * 103) >> 8);
	int invgdif = ((u * 88) >> 8) + ((v * 183) >> 8);
	int bdif = u + ((u * 198) >> 8);

	r = (u8)(ypos + rdif);
	g = (u8)(ypos - invgdif);
	b = (u8)(ypos + bdif);
}

void getPixelColor(u8 r, u8 g, u8 b, u8 a, int pixelMode, u16* color)
{
	switch (pixelMode)
	{
	case TPSM_PIXEL_STORAGE_MODE_16BIT_BGR5650: 
		{
			*color = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
		}
		break;
	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR5551:
		{
			*color = ((a >> 7) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
		}
		break;
	case TPSM_PIXEL_STORAGE_MODE_16BIT_ABGR4444:
		{
			*color = ((a >> 4) << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4);
		}
		break;
	default:
		// do nothing yet
		break;
	}
}

MediaEngine::MediaEngine(): m_pdata(0), m_streamSize(0), m_readSize(0){
	m_pFormatCtx = 0;
	m_pCodecCtx = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
	m_videoStream = -1;
	m_buffer = 0;
	m_demux = 0;
	m_audioContext = 0;
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
	if (m_pIOContext && ((AVIOContext*)m_pIOContext)->buffer)
		av_free(((AVIOContext*)m_pIOContext)->buffer);
	if (m_pIOContext)
		av_free(m_pIOContext);
	if (m_pCodecCtx)
		avcodec_close((AVCodecContext*)m_pCodecCtx);
	if (m_pFormatCtx)
		avformat_close_input((AVFormatContext**)&m_pFormatCtx);
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
	m_videoStream = -1;
	m_pdata = 0;
	m_demux = 0;
	Atrac3plus_Decoder::CloseContext(&m_audioContext);
}

int _MpegReadbuffer(void *opaque, uint8_t *buf, int buf_size)
{
	MediaEngine *mpeg = (MediaEngine*)opaque;
	int size = std::min(mpeg->m_bufSize, buf_size);
	size = std::max(std::min((mpeg->m_readSize - mpeg->m_decodePos), size), 0);
	if (size > 0)
		memcpy(buf, mpeg->m_pdata + mpeg->m_decodePos, size);
	mpeg->m_decodePos += size;
	return size;
}

int64_t _MpegSeekbuffer(void *opaque, int64_t offset, int whence)
{
	MediaEngine *mpeg = (MediaEngine*)opaque;
	switch (whence) {
	case SEEK_SET:
		mpeg->m_decodePos = offset;
		break;
	case SEEK_CUR:
		mpeg->m_decodePos += offset;
		break;
	case SEEK_END:
		mpeg->m_decodePos = mpeg->m_streamSize - (u32)offset;
		break;
	}
	return offset;
}

bool MediaEngine::openContext() {
#ifdef USE_FFMPEG
	u8* tempbuf = (u8*)av_malloc(m_bufSize);

	AVFormatContext *pFormatCtx = avformat_alloc_context();
	m_pFormatCtx = (void*)pFormatCtx;
	m_pIOContext = (void*)avio_alloc_context(tempbuf, m_bufSize, 0, (void*)this, _MpegReadbuffer, NULL, _MpegSeekbuffer);
	pFormatCtx->pb = (AVIOContext*)m_pIOContext;
  
	// Open video file
	if(avformat_open_input((AVFormatContext**)&m_pFormatCtx, NULL, NULL, NULL) != 0)
		return false;

	if(avformat_find_stream_info(pFormatCtx, NULL) < 0)
		return false;

	 // Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, NULL, 0);

	// Find the first video stream
	for(int i = 0; i < pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			m_videoStream = i;
			break;
		}
	}
	if(m_videoStream == -1)
		return false;

	// Get a pointer to the codec context for the video stream
	m_pCodecCtx = (void*)pFormatCtx->streams[m_videoStream]->codec;
	AVCodecContext *pCodecCtx = (AVCodecContext*)m_pCodecCtx;
  
	// Find the decoder for the video stream
	AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec == NULL)
		return false;
  
	// Open codec
	AVDictionary *optionsDict = 0;
	if(avcodec_open2(pCodecCtx, pCodec, &optionsDict)<0)
		return false; // Could not open codec

	setVideoDim();
	int mpegoffset = bswap32(*(int*)(m_pdata + 8));
	m_demux = new MpegDemux(m_pdata, m_streamSize, mpegoffset);
	m_demux->setReadSize(m_readSize);
	m_demux->demux();
	m_audioPos = 0;
	m_audioContext = Atrac3plus_Decoder::OpenContext();
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
	m_decodePos = 0;
	m_readSize = readSize;
	m_streamSize = StreamSize;
	m_pdata = new u8[StreamSize];
	memcpy(m_pdata, buffer, m_readSize);
	
	if (readSize > 0x2000)
		openContext();
	return true;
}

bool MediaEngine::loadFile(const char* filename)
{
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	s64 infosize = info.size;
	u8* buf = new u8[infosize];
	u32 h = pspFileSystem.OpenFile(filename, (FileAccess) FILEACCESS_READ);
	pspFileSystem.ReadFile(h, buf, infosize);
	pspFileSystem.CloseFile(h);

	closeMedia();
	// force to clear the useless FBO
	gpu->Resized();

	m_videopts = 0;
	m_audiopts = 0;
	m_bufSize = 0x2000;
	m_decodePos = 0;
	m_readSize = infosize;
	m_streamSize = infosize;
	m_pdata = buf;
	
	if (m_readSize > 0x2000)
		openContext();

	return true;
}

void MediaEngine::addStreamData(u8* buffer, int addSize) {
	int size = std::min(addSize, m_streamSize - m_readSize);
	if (size > 0) {
		memcpy(m_pdata + m_readSize, buffer, size);
		m_readSize += size;
		if (!m_pFormatCtx && m_readSize > 0x2000)
			openContext();
		if (m_demux) {
			m_demux->setReadSize(m_readSize);
			m_demux->demux();
		}
	}
}

bool MediaEngine::setVideoDim(int width, int height)
{
	if (!m_pCodecCtx)
		return false;
#ifdef USE_FFMPEG
	AVCodecContext *pCodecCtx = (AVCodecContext*)m_pCodecCtx;
	if (width == 0 && height == 0)
	{
		// use the orignal video size
		m_desWidth = pCodecCtx->width;
		m_desHeight = pCodecCtx->height;
	}
	else
	{
		m_desWidth = width;
		m_desHeight = height;
	}

	// Allocate video frame
	m_pFrame = avcodec_alloc_frame();
	
	m_sws_ctx = (void*)
    sws_getContext
    (
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        m_desWidth,
        m_desHeight,
        PIX_FMT_RGB24,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

	// Allocate video frame for RGB24
	m_pFrameRGB = avcodec_alloc_frame();
	int numBytes = avpicture_get_size(PIX_FMT_RGB24, m_desWidth, m_desHeight);  
    m_buffer = (u8*)av_malloc(numBytes * sizeof(uint8_t));
  
    // Assign appropriate parts of buffer to image planes in pFrameRGB   
    avpicture_fill((AVPicture *)m_pFrameRGB, m_buffer, PIX_FMT_RGB24, m_desWidth, m_desHeight);  
#endif // USE_FFMPEG
	return true;
}

bool MediaEngine::stepVideo() {

	// if video engine is broken, force to add timestamp
	m_videopts += 3003;
#ifdef USE_FFMPEG
	AVFormatContext *pFormatCtx = (AVFormatContext*)m_pFormatCtx;
	AVCodecContext *pCodecCtx = (AVCodecContext*)m_pCodecCtx;
	AVFrame *pFrame = (AVFrame*)m_pFrame;
	AVFrame *pFrameRGB = (AVFrame*)m_pFrameRGB;
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;
	AVPacket packet;
	int frameFinished;
	bool bGetFrame = false;
	while(av_read_frame(pFormatCtx, &packet)>=0) {
		if(packet.stream_index == m_videoStream) {
			// Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
			sws_scale((SwsContext*)m_sws_ctx, pFrame->data, pFrame->linesize, 0, 
					pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
      
			if(frameFinished) {
				if (m_videopts == 3003) {
					m_audiopts = packet.pts;
				}
				m_videopts = packet.pts + packet.duration;
				bGetFrame = true;
			}
		}
		av_free_packet(&packet);
		if (bGetFrame) break;
	}
	if (m_audiopts > 0) {
		if (m_audiopts - m_videopts > 5000)
			return stepVideo();
	}
	return bGetFrame;
#else
	return true;
#endif // USE_FFMPEG
}

bool MediaEngine::writeVideoImage(u8* buffer, int frameWidth, int videoPixelMode) {
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;
#ifdef USE_FFMPEG
	AVFrame *pFrameRGB = (AVFrame*)m_pFrameRGB;
	// lock the image size
	int height = m_desHeight;
	int width = m_desWidth;
	u8* imgbuf = buffer;
	u8* data = pFrameRGB->data[0];
	if (videoPixelMode == TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888)
	{
		// ABGR8888
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				u8 r = *(data++);
				u8 g = *(data++);
				u8 b = *(data++);
				*(imgbuf++) = r;
				*(imgbuf++) = g;
				*(imgbuf++) = b;
				*(imgbuf++) = 0xFF;
			}
			imgbuf += (frameWidth - width) * 4;
		}
	}
	else
	{
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				u8 r = *(data++);
				u8 g = *(data++);
				u8 b = *(data++);
				getPixelColor(r, g, b, 0xFF, videoPixelMode, (u16*)imgbuf);
				imgbuf += 2;
			}
			imgbuf += (frameWidth - width) * 2;
		}
	} 
#endif // USE_FFMPEG
	return true;
}

bool MediaEngine::writeVideoImageWithRange(u8* buffer, int frameWidth, int videoPixelMode, 
	                             int xpos, int ypos, int width, int height) {
	if ((!m_pFrame)||(!m_pFrameRGB))
		return false;
#ifdef USE_FFMPEG
	AVFrame *pFrameRGB = (AVFrame*)m_pFrameRGB;
	// lock the image size
	u8* imgbuf = buffer;
	u8* data = pFrameRGB->data[0];
	if (videoPixelMode == TPSM_PIXEL_STORAGE_MODE_32BIT_ABGR8888)
	{
		// ABGR8888
		data +=  (ypos *  m_desWidth + xpos) * 3;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				u8 r = *(data++);
				u8 g = *(data++);
				u8 b = *(data++);
				*(imgbuf++) = r;
				*(imgbuf++) = g;
				*(imgbuf++) = b;
				*(imgbuf++) = 0xFF;
			}
			imgbuf += (frameWidth - width) * 4;
			data += (m_desWidth - width) * 3;
		}
	}
	else
	{
		data +=  (ypos *  m_desWidth + xpos) * 3;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++)
			{
				u8 r = *(data++);
				u8 g = *(data++);
				u8 b = *(data++);
				getPixelColor(r, g, b, 0xFF, videoPixelMode, (u16*)imgbuf);
				imgbuf += 2;
			}
			imgbuf += (frameWidth - width) * 2;
			data += (m_desWidth - width) * 3;
		}
	} 
#endif // USE_FFMPEG
	return true;
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

int MediaEngine::getAudioSamples(u8* buffer) {
	if (!m_demux) {
		return 0;
	}
	u8* audioStream = 0;
	int audioSize = m_demux->getaudioStream(&audioStream);
	if (m_audioPos >= audioSize || !isHeader(audioStream, m_audioPos))
	{
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
	if (nextHeader >= 0) {
		m_audioPos = nextHeader;
	} else
		m_audioPos = audioSize;
	m_audiopts += 4180;
	return outbytes;
}

s64 MediaEngine::getVideoTimeStamp() {
	return m_videopts;
}

s64 MediaEngine::getAudioTimeStamp() {
	if (m_audiopts > 0)
		return m_audiopts;
	return m_videopts;
}

s64 MediaEngine::getLastTimeStamp() {
	if (!m_pdata)
		return 0;
	int lastTimeStamp = bswap32(*(int*)(m_pdata + 92));
	return lastTimeStamp;
}