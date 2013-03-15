#pragma once

#ifdef _WIN32
#include "../../Globals.h"

class mediaPlayer
{
public:
	mediaPlayer(void);
	~mediaPlayer(void);

	bool load(const char* filename);
	bool closeMedia();
	bool writeVideoImage(u8* buffer, int frameWidth, int videoPixelMode);
private:
	void *m_pFormatCtx;
	void *m_pCodecCtx;
	void *m_pFrame;
    void *m_pFrameRGB;
	int  m_videoStream;
    void *m_sws_ctx;
    u8* m_buffer;
};

bool loadPMFStream(u8* pmf, int pmfsize);
bool loadPMFPSFFile(const char *filename);
bool deletePMFStream();
mediaPlayer* getPMFPlayer();
bool playPMFVideo();

#endif