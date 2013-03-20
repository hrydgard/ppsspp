#ifdef _USE_FFMPEG_

#include "mediaPlayer.h"
#include "OMAConvert.h"

#ifdef _USE_DSHOW_
#include "MpegDemux.h"
#include "audioPlayer.h"
#endif // _USE_DSHOW_

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

struct StreamBuffer{
	unsigned char* buf;
	int pos;
	int size;
};

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
	m_pIOContext = 0;
	m_videoStream = -1;
	m_buffer = 0;
	m_videobuf = new StreamBuffer;
	((StreamBuffer *)m_videobuf)->buf = 0;
	m_tempbuf = new u8[8192];

	if (!g_FramebufferMoviePlayingbuf)
		g_FramebufferMoviePlayingbuf = new u8[g_FramebufferMoviePlayinglinesize*280*4];
}


mediaPlayer::~mediaPlayer(void)
{
	closeMedia();
	if (m_tempbuf)
		delete [] m_tempbuf;
	if (m_videobuf)
		delete m_videobuf;
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

static int read_buffer(void *opaque, uint8_t *buf, int buf_size)
{
	StreamBuffer *vstream = (StreamBuffer*)opaque;
	int size = (vstream->size - vstream->pos > buf_size? buf_size: vstream->size - vstream->pos);
	memcpy(buf, vstream->buf + vstream->pos, size);
	vstream->pos += size;
    return size;
}

bool mediaPlayer::loadStream(u8* buffer, int size, bool bAutofreebuffer)
{
	// Register all formats and codecs
	av_register_all();

	StreamBuffer *vstream = (StreamBuffer*)m_videobuf;
	vstream->size = size;
	vstream->pos = 0;
	if (bAutofreebuffer)
		vstream->buf = buffer;
	else
	{
		vstream->buf = new u8[size];
		memcpy(vstream->buf, buffer, size);
	}

	AVFormatContext *pFormatCtx = avformat_alloc_context();
	m_pFormatCtx = (void*)pFormatCtx;
	m_pIOContext = (void*)avio_alloc_context(m_tempbuf, 8192, 0, (void*)vstream, read_buffer, NULL, NULL);
	pFormatCtx->pb = (AVIOContext*)m_pIOContext;
  
	// Open video file
	if(avformat_open_input((AVFormatContext**)&m_pFormatCtx, "stream", NULL, NULL) != 0)
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
				int height = 272;
				int width = 480;
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

#ifdef _USE_DSHOW_
static audioPlayer *g_pmfaudioPlayer = 0;

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

	bool bResult = g_pmfPlayer.loadStream(pmf, pmfsize);
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
	if (strlen(package) < 10)
		return false;
	u32 h = pspFileSystem.OpenFile(package, (FileAccess) FILEACCESS_READ);
	if (h == 0) 
		return false;
	int filesize = mpegsize + 0x2000;
	u8* buf = new u8[filesize];
	pspFileSystem.SeekFile(h, startpos, FILEMOVE_BEGIN);
	filesize = pspFileSystem.ReadFile(h, buf, filesize);
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
	DeleteFileA("tmp\\movie.oma");
#endif // _USE_DSHOW_

	return true;
}

mediaPlayer* getPMFPlayer()
{
	return &g_pmfPlayer;
}

UINT WINAPI loopPlaying(LPVOID lpvoid)
{
#ifdef _USE_DSHOW_
	if (g_pmfaudioPlayer) 
		g_pmfaudioPlayer->play();
#endif // _USE_DSHOW_

	while (bPlaying)
	{
		clock_t starttime = clock();
		if (!g_pmfPlayer.writeVideoImage(g_FramebufferMoviePlayingbuf, 
			g_FramebufferMoviePlayinglinesize, 3))
		{
			g_FramebufferMoviePlaying = false;
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

#endif // _USE_FFMPEG_