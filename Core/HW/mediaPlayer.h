#pragma once

#ifdef _USE_FFMPEG_
#include "../../Globals.h"

class mediaPlayer
{
public:
	mediaPlayer(void);
	~mediaPlayer(void);

	bool load(const char* filename);
	bool loadStream(u8* buffer, int size, bool bAutofreebuffer = true);
	bool closeMedia();
	bool writeVideoImage(u8* buffer, int frameWidth, int videoPixelMode);
private:
	void *m_pFormatCtx;
	void *m_pCodecCtx;
	void *m_pFrame;
    void *m_pFrameRGB;
	void *m_pIOContext;
	int  m_videoStream;
    void *m_sws_ctx;
    u8* m_buffer;
	void *m_videobuf;
	u8* m_tempbuf;
};

bool loadPMFStream(u8* pmf, int pmfsize);
bool loadPMFPSFFile(const char *filename, int mpegsize = -1);
bool loadPMFPackageFile(const char* package, u32 startpos, int mpegsize, u8* buffer);
bool deletePMFStream();
mediaPlayer* getPMFPlayer();
bool playPMFVideo();

#endif // _USE_FFMPEG_
