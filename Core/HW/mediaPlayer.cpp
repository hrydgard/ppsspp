#ifdef _WIN32

#include "mediaPlayer.h"
#include "OMAConvert.h"
#include "MpegDemux.h"
#include "audioPlayer.h"

#include "../../GPU/GLES/Framebuffer.h"
#include "../Core/System.h"

#include <Windows.h>
#include <process.h>

extern "C" {

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

}

// use the ffmpeg lib
//#pragma comment(lib, "avcodec.lib")
//#pragma comment(lib, "avformat.lib")
//#pragma comment(lib, "swscale.lib")
//#pragma comment(lib, "avutil.lib")

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

mediaPlayer::mediaPlayer(void)
{
	m_pFormatCtx = 0;
	m_pCodecCtx = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
	m_videoStream = -1;
	m_buffer = 0;
	if (!g_FramebufferMoviePlayingbuf)
		g_FramebufferMoviePlayingbuf = new u8[g_FramebufferMoviePlayinglinesize*280*4];
}


mediaPlayer::~mediaPlayer(void)
{
	closeMedia();
	if (g_FramebufferMoviePlayingbuf)
		delete [] g_FramebufferMoviePlayingbuf;
	g_FramebufferMoviePlayingbuf = 0;
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

	m_sws_ctx = (void*)
    sws_getContext
    (
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        PIX_FMT_RGB24,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

	// Allocate video frame for RGB24
	m_pFrameRGB = avcodec_alloc_frame();
	int numBytes = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);  
    m_buffer = (u8 *)av_malloc(numBytes*sizeof(uint8_t));
  
    // Assign appropriate parts of buffer to image planes in pFrameRGB   
    avpicture_fill((AVPicture *)m_pFrameRGB, m_buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);  

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
	if (m_pCodecCtx)
		avcodec_close((AVCodecContext*)m_pCodecCtx);
	if (m_pFormatCtx)
		avformat_close_input((AVFormatContext**)&m_pFormatCtx);
	m_buffer = 0;
	m_pFrame = 0;
	m_pFrameRGB = 0;
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
				int height = pCodecCtx->height;
				int width = pCodecCtx->width;
				u8* imgbuf = buffer;
				u8* data = pFrameRGB->data[0];
				if (videoPixelMode == 3)
				{
					// RGBA8888
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
							*(imgbuf++) = 0xFF;
							*(imgbuf++) = 0xFF;
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
static audioPlayer *g_pmfaudioPlayer = 0;

bool loadPMFaudioStream(u8* audioStream, int audioSize)
{
	u8 *oma = 0;
	int omasize = OMAConvert::convertStreamtoOMA(audioStream, audioSize, &oma);
	if (omasize <= 0)
		return false;

	FILE *wfp = fopen("tmp\\movie.oma", "wb");
	fwrite(oma, 1, omasize, wfp);
	fclose(wfp);

	OMAConvert::releaseStream(&oma);

	g_pmfaudioPlayer = new audioPlayer;
	bool bResult = g_pmfaudioPlayer->load("tmp\\movie.oma");
	if (!bResult)
		g_pmfaudioPlayer->closeMedia();
	return bResult;
}

bool loadPMFStream(u8* pmf, int pmfsize)
{
	if (bFirst)
	{
		CreateDirectory("tmp", NULL);
		bFirst = false;
	}
	FILE *wfp = fopen("tmp\\movie.pmf", "wb");
	fwrite(pmf, 1, pmfsize, wfp);
	fclose(wfp);

	MpegDemux *demux = new MpegDemux(pmf, pmfsize, 0);
	demux->demux(-1);
	u8 *audioStream;
	int audioSize = demux->getaudioStream(&audioStream);
	if (audioSize > 0)
	{
		loadPMFaudioStream(audioStream, audioSize);
	}
	delete demux;

	bool bResult = g_pmfPlayer.load("tmp\\movie.pmf");
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
	delete [] buf;

	return bResult;
}

bool deletePMFStream()
{
	if (!bPlaying)
		return true;
	bStop = true;
	bPlaying = false;
	g_FramebufferMoviePlaying = false;

	if (g_pmfaudioPlayer) delete g_pmfaudioPlayer;
	g_pmfaudioPlayer = 0;
	g_pmfPlayer.closeMedia();

	DeleteFileA("tmp\\movie.pmf");
	DeleteFileA("tmp\\movie.oma");
	return true;
}

mediaPlayer* getPMFPlayer()
{
	return &g_pmfPlayer;
}

UINT WINAPI loopPlaying(LPVOID lpvoid)
{
	if (g_pmfaudioPlayer) 
		g_pmfaudioPlayer->play();

	while (bPlaying)
	{
		clock_t starttime = clock();
		if (!g_pmfPlayer.writeVideoImage(g_FramebufferMoviePlayingbuf, 
			g_FramebufferMoviePlayinglinesize, 3))
		{
			bStop = true;
			return 0;
		}
		clock_t endtime = clock();
		// keep the movie frames 30FPS
		long idletime = 33 - (endtime - starttime) * 1000 / CLOCKS_PER_SEC;
		if (idletime > 0)
			Sleep(idletime);
	}

	return 0;
}

bool playPMFVideo()
{
	if (bStop)
		return false;
	if (!bPlaying)
	{
		bPlaying = true;
		UINT uiThread;
		HANDLE hThread=(HANDLE)::_beginthreadex(NULL, 0, loopPlaying,
			                                   0, 0, &uiThread);
		CloseHandle(hThread);
	}
	return true;
}

#endif // _WIN32