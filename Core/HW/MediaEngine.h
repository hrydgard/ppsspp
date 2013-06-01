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



// Simulation of the hardware video/audio decoders.
// The idea is high level emulation where we simply use FFMPEG.
// TODO: Actually hook up to ffmpeg.

#pragma once

// An approximation of what the interface will look like. Similar to JPCSP's.

#include "../../Globals.h"
#include "../HLE/sceMpeg.h"
#include "ChunkFile.h"
#include "Core/HW/MpegDemux.h"

class MediaEngine
{
public:
	MediaEngine();
	~MediaEngine();

	void closeMedia();
	bool loadStream(u8* buffer, int readSize, int StreamSize);
	bool loadFile(const char* filename);
	void addStreamData(u8* buffer, int addSize);
	int getRemainSize() { return m_streamSize - m_readSize;}

	bool stepVideo();
	bool writeVideoImage(u8* buffer, int frameWidth = 512, int videoPixelMode = 3);
	bool writeVideoImageWithRange(u8* buffer, int frameWidth, int videoPixelMode, 
	                             int xpos, int ypos, int width, int height);
	int getAudioSamples(u8* buffer);

	bool setVideoDim(int width = 0, int height = 0);
	s64 getVideoTimeStamp();
	s64 getAudioTimeStamp();
	s64 getLastTimeStamp();

	void DoState(PointerWrap &p) {
		p.Do(m_streamSize);
		p.Do(m_readSize);
		p.DoMarker("MediaEngine");
	}

private:
    bool openContext();

public:

	void *m_pFormatCtx;
	void *m_pCodecCtx;
	void *m_pFrame;
    void *m_pFrameRGB;
	void *m_pIOContext;
	int  m_videoStream;
    void *m_sws_ctx;
	u8* m_buffer;

	int  m_desWidth;
	int  m_desHeight;
	int m_streamSize;
	int m_readSize;
	int m_decodePos;
	int m_bufSize;
	s64 m_videopts;
	u8* m_pdata;
	
	MpegDemux *m_demux;
	int m_audioPos;
	void* m_audioContext;
	s64 m_audiopts;
};
