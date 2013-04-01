#ifdef _USE_FFMPEG_

#include "mediaPlayer.h"
#include "OMAConvert.h"

#ifdef _USE_DSHOW_
#include "MpegDemux.h"
#include "audioPlayer.h"
#endif // _USE_DSHOW_

#include "../../GPU/GLES/Framebuffer.h"
#include "../Core/System.h"

#include "../../Common/StdThread.h"
#include "../../Common/Thread.h"

extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

}

// use the ffmpeg lib
//#pragma comment(lib, "avcodec.lib")
//#pragma comment(lib, "avformat.lib")
//#pragma comment(lib, "swscale.lib")
//#pragma comment(lib, "avutil.lib")

struct StreamBuffer{
	unsigned char* buf;
	int pos;
	int streamsize;
	int bufsize;
};

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

u8* g_MoviePlayingbuf = 0;

mediaPlayer::mediaPlayer(void)
{
	m_pFormatCtx = 0;
	m_pCodecCtx = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
	m_videoStream = -1;
	m_buffer = 0;
	m_videobuf = (void *)new StreamBuffer;
	((StreamBuffer *)m_videobuf)->buf = 0;
	((StreamBuffer *)m_videobuf)->bufsize = 0x2000;

	if (!g_MoviePlayingbuf)
		g_MoviePlayingbuf = new u8[g_FramebufferMoviePlayinglinesize*280*4];
}


mediaPlayer::~mediaPlayer(void)
{
	closeMedia();
	if (m_videobuf)
		delete m_videobuf;
	if (g_MoviePlayingbuf)
		delete [] g_MoviePlayingbuf;
	g_MoviePlayingbuf = 0;
}

bool mediaPlayer::load(const char* filename)
{
	// Register all formats and codecs
	av_register_all();

	// Open video file
	if(avformat_open_input((AVFormatContext**)&m_pFormatCtx, filename, NULL, NULL) != 0)
		return false;

	AVFormatContext *pFormatCtx = (AVFormatContext*)m_pFormatCtx;
	if(avformat_find_stream_info(pFormatCtx, NULL) < 0)
		return false;

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
  
	// Allocate video frame
	m_pFrame = avcodec_alloc_frame();

	// make the image size the same as PSP window
	int desWidth = 480;
	int desHeight = 272;
	m_sws_ctx = (void*)
    sws_getContext
    (
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        desWidth,
        desHeight,
        PIX_FMT_RGB24,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

	// Allocate video frame for RGB24
	m_pFrameRGB = avcodec_alloc_frame();
	int numBytes = avpicture_get_size(PIX_FMT_RGB24, desWidth, desHeight);  
    m_buffer = (u8 *)av_malloc(numBytes*sizeof(uint8_t));
  
    // Assign appropriate parts of buffer to image planes in pFrameRGB   
    avpicture_fill((AVPicture *)m_pFrameRGB, m_buffer, PIX_FMT_RGB24, desWidth, desHeight);  

	return true;
}

int read_buffer(void *opaque, uint8_t *buf, int buf_size)
{
	StreamBuffer *vstream = (StreamBuffer*)opaque;
	int size = std::min(buf_size, vstream->bufsize);
	size = std::min(vstream->streamsize - vstream->pos, size);
	memcpy(buf, vstream->buf + vstream->pos, size);
	vstream->pos += size;
    return size;
}

bool mediaPlayer::loadStream(u8* buffer, int size, bool bAutofreebuffer)
{
	// Register all formats and codecs
	av_register_all();

	StreamBuffer *vstream = (StreamBuffer*)m_videobuf;
	vstream->streamsize = size;
	vstream->pos = 0;
	if (bAutofreebuffer)
		vstream->buf = buffer;
	else
	{
		vstream->buf = new u8[size];
		memcpy(vstream->buf, buffer, size);
	}
	u8* tempbuf = (u8*)av_malloc(vstream->bufsize);

	AVFormatContext *pFormatCtx = avformat_alloc_context();
	m_pFormatCtx = (void*)pFormatCtx;
	m_pIOContext = (void*)avio_alloc_context(tempbuf, vstream->bufsize, 0, (void*)vstream, read_buffer, NULL, NULL);
	pFormatCtx->pb = (AVIOContext*)m_pIOContext;
  
	// Open video file
	if(avformat_open_input((AVFormatContext**)&m_pFormatCtx, NULL, NULL, NULL) != 0)
		return false;

	if(avformat_find_stream_info(pFormatCtx, NULL) < 0)
		return false;

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

	return true;
}

bool mediaPlayer::setVideoSize(int width, int height)
{
	if (!m_pCodecCtx)
		return false;

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
    m_buffer = (u8 *)av_malloc(numBytes*sizeof(uint8_t));
  
    // Assign appropriate parts of buffer to image planes in pFrameRGB   
    avpicture_fill((AVPicture *)m_pFrameRGB, m_buffer, PIX_FMT_RGB24, m_desWidth, m_desHeight);  

	return true;
}

bool mediaPlayer::closeMedia()
{
	if (m_buffer)
		av_free(m_buffer);
	if (m_pFrameRGB)
		av_free(m_pFrameRGB);
	if (m_pFrame)
		av_free(m_pFrame);
	if (m_pIOContext)
		av_free(m_pIOContext);
	if (m_pCodecCtx)
		avcodec_close((AVCodecContext*)m_pCodecCtx);
	if (m_pFormatCtx)
		avformat_close_input((AVFormatContext**)&m_pFormatCtx);
	if (((StreamBuffer *)m_videobuf)->buf)
	{
		delete [] (((StreamBuffer *)m_videobuf)->buf);
		((StreamBuffer *)m_videobuf)->buf = 0;
	}
	m_buffer = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_pIOContext = 0;
	m_pCodecCtx = 0;
	m_pFormatCtx = 0;
	m_videoStream = -1;
	return true;
}

bool mediaPlayer::writeVideoImage(u8* buffer, int frameWidth, int videoPixelMode)
{
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
      
			if(frameFinished) {
				sws_scale((SwsContext*)m_sws_ctx, pFrame->data, pFrame->linesize, 0, 
					pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
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
						imgbuf += (frameWidth - width)*4;
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
						imgbuf += (frameWidth - width)*2;
					}
				} 
				bGetFrame = true;
			}
		}
		av_free_packet(&packet);
		if (bGetFrame) break;
	}
	return bGetFrame;
}

static bool bFirst = true;
static volatile bool bPlaying = false;
static volatile bool bStop = true;
static mediaPlayer g_pmfPlayer;
static struct MovieInfo{
	u8* movieBuf;
	int frameWidth;
	int videoPixelMode;
	int iPlayVideo;
	int iPlayAudio;
} movieInfo;

static const int modeBpp[4] = { 2, 2, 2, 4 };

#ifdef _USE_DSHOW_
static audioPlayer *g_pmfaudioPlayer = 0;
static char audioname[260];

bool loadPMFaudioStream(u8* audioStream, int audioSize)
{
	if (bFirst)
	{
		CreateDirectory("tmp", NULL);
		bFirst = false;
	}
	u8 *oma = 0;
	int omasize = OMAConvert::convertStreamtoOMA(audioStream, audioSize, &oma);
	if (omasize <= 0)
		return false;

	sprintf(audioname, "tmp\\movie_%08x.oma", omasize);
	FILE *wfp = fopen(audioname, "wb");
	fwrite(oma, 1, omasize, wfp);
	fclose(wfp);

	OMAConvert::releaseStream(&oma);

	g_pmfaudioPlayer = new audioPlayer;
	bool bResult = g_pmfaudioPlayer->load(audioname);
	if (!bResult)
		g_pmfaudioPlayer->closeMedia();
	return bResult;
}
#endif // _USE_DSHOW_

bool loadPMFStream(u8* pmf, int pmfsize)
{
#ifdef _USE_DSHOW_
	MpegDemux *demux = new MpegDemux(pmf, pmfsize, 0);
	demux->demux(-1);
	u8 *audioStream;
	int audioSize = demux->getaudioStream(&audioStream);
	if (audioSize > 0)
	{
		loadPMFaudioStream(audioStream, audioSize);
	}
	delete demux;
#endif // _USE_DSHOW_

	bool bResult = g_pmfPlayer.loadStream(pmf, pmfsize, true);
	if (!bResult)
		g_pmfPlayer.closeMedia();
	else
		g_FramebufferMoviePlaying = true;
	bStop = false;
	return bResult;
}

bool loadPMFPSFFile(const char *filename, int mpegsize)
{
	PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
	s64 infosize = info.size;
	if ((mpegsize >= 0) && (abs(((int)infosize) - mpegsize) > 0x2000))
		return false;
	u8* buf = new u8[infosize];
	u32 h = pspFileSystem.OpenFile(filename, (FileAccess) FILEACCESS_READ);
	pspFileSystem.ReadFile(h, buf, infosize);
	pspFileSystem.CloseFile(h);

	bool bResult = loadPMFStream(buf, infosize);

	return bResult;
}

bool loadPMFPackageFile(const char* package, u32 startpos, int mpegsize, u8* buffer)
{
	u32 h = pspFileSystem.OpenFile(package, (FileAccess) FILEACCESS_READ);
	if (h == 0) 
		return false;
	int filesize = mpegsize;
	u8* buf = new u8[filesize];
	pspFileSystem.SeekFile(h, startpos, FILEMOVE_BEGIN);
	if (strlen(package) >= 10)
		filesize = pspFileSystem.ReadFile(h, buf, filesize);
	else
		pspFileSystem.ReadFile(h, buf, filesize / 2048);
	pspFileSystem.CloseFile(h);

	bool bResult;
	if (memcmp(buffer, buf , 0x20) == 0)
		bResult = loadPMFStream(buf, filesize);
	else
		bResult = false;

	return bResult;
}

bool deletePMFStream()
{
	if (!bPlaying)
		return true;
	bStop = true;
	bPlaying = false;
	g_FramebufferMoviePlaying = false;

	g_pmfPlayer.closeMedia();

#ifdef _USE_DSHOW_
	if (g_pmfaudioPlayer) delete g_pmfaudioPlayer;
	g_pmfaudioPlayer = 0;
	DeleteFileA(audioname);
#endif // _USE_DSHOW_

	return true;
}

mediaPlayer* getPMFPlayer()
{
	return &g_pmfPlayer;
}

u32 loopPlaying(void * lpvoid)
{
	int count = 0;
	long counttime = clock();
	while (bPlaying)
	{
		clock_t starttime = clock();
#ifdef _USE_DSHOW_
		if (movieInfo.iPlayAudio > 0)
		{
			if (g_pmfaudioPlayer) 
				g_pmfaudioPlayer->play();
			movieInfo.iPlayAudio--;
		}
		else
		{
			if (g_pmfaudioPlayer) 
				g_pmfaudioPlayer->pause();
		}
#endif // _USE_DSHOW_
		if (movieInfo.iPlayVideo > 0)
		{
			if (!g_pmfPlayer.writeVideoImage(movieInfo.movieBuf, 
				movieInfo.frameWidth, movieInfo.videoPixelMode))
			{
				g_FramebufferMoviePlaying = false;
				bStop = true;
				return 0;
			}
			movieInfo.iPlayVideo--;
			g_FramebufferMoviePlaying = true;
		}
		else
			g_FramebufferMoviePlaying = false;
		clock_t endtime = clock();
		// keep the movie frames 30FPS
		count = (count + 1) % 30;
		if (count == 0)
		{
			long curtime = clock();
			long reftime = 1000 - (curtime - counttime) * 1000 / CLOCKS_PER_SEC;
			if (reftime > 0) {
				Common::SleepCurrentThread(reftime);
			}
			counttime += CLOCKS_PER_SEC;
		}
		else {
			long idletime = 33 - (endtime - starttime) * 1000 / CLOCKS_PER_SEC;
			if (idletime > 0) {
				Common::SleepCurrentThread(idletime);
			}
		}
	}

	return 0;
}

bool playPMFVideo(u8* buffer, int frameWidth, int videoPixelMode)
{
	if (bStop)
		return false;
	movieInfo.iPlayVideo = 5;
	movieInfo.iPlayAudio = 5;
	if (!bPlaying)
	{
		// force to clear the useless FBO
		gpu->Resized();

		bPlaying = true;
		if (!buffer) {
			movieInfo.movieBuf = g_MoviePlayingbuf;
			g_FramebufferMoviePlayingbuf = g_MoviePlayingbuf;
			// use the same size as PSP window
			g_pmfPlayer.setVideoSize(480, 272);
		}
		else {
			movieInfo.movieBuf = g_MoviePlayingbuf;
			g_FramebufferMoviePlayingbuf = 0;
			// use the orginal video size
			g_pmfPlayer.setVideoSize(0, 0);
		}
		movieInfo.frameWidth = frameWidth;
		movieInfo.videoPixelMode = videoPixelMode;

#ifdef _USE_DSHOW_
		stopAllAtrac3Audio();
#endif // _USE_DSHOW

		std::thread thread(loopPlaying, (void*)0);
	}
	return true;
}

bool writePMFVideoImage(u8* buffer, int frameWidth, int videoPixelMode)
{
	if (!playPMFVideo(buffer, frameWidth, videoPixelMode))
	{
		return false;
	}
	if (buffer)
	{
		memcpy(buffer, g_MoviePlayingbuf, frameWidth * 272 * modeBpp[videoPixelMode]);
	}
	return true;
}

bool writePMFVideoImageWithRange(u8* buffer, int frameWidth, int videoPixelMode, 
	                             int xpos, int ypos, int width, int height)
{
	if (!playPMFVideo(buffer, frameWidth, videoPixelMode))
		return false;
	if (buffer)
	{
		int bpp = modeBpp[videoPixelMode];
		u8* data = g_MoviePlayingbuf + (ypos * frameWidth + xpos) * bpp;
		u8* imgbuf = buffer;
		for (int y = 0; y < height; y++){
			memcpy(imgbuf, data, width * bpp);
			imgbuf += frameWidth * bpp;
			data += frameWidth * bpp;
		}
	}
	return true;
}

bool isPMFVideoEnd()
{
	return bStop;
}

#endif // _USE_FFMPEG_