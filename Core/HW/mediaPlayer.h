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
	bool setVideoSize(int width = 0, int height = 0);
	int getDesHeight() { return m_desHeight;}
	int getDesWidth()  { return m_desWidth; }
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
	int  m_desWidth;
	int  m_desHeight;
};

bool loadPMFStream(u8* pmf, int pmfsize);
bool loadPMFPSFFile(const char *filename, int mpegsize = -1);
bool loadPMFPackageFile(const char* package, u32 startpos, int mpegsize, u8* buffer, void* pgd_info = 0);
bool deletePMFStream();
mediaPlayer* getPMFPlayer();
bool playPMFVideo(u8* buffer = 0, int frameWidth = 512, int videoPixelMode = 3);
bool writePMFVideoImage(u8* buffer = 0, int frameWidth = 512, int videoPixelMode = 3);
bool writePMFVideoImageWithRange(u8* buffer, int frameWidth, int videoPixelMode, 
	                             int xpos, int ypos, int width, int height);
bool isPMFVideoEnd();
#endif // _USE_FFMPEG_
