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

struct SwsContext;
struct AVFrame;
struct AVIOContext;
struct AVFormatContext;
struct AVCodecContext;

inline s64 getMpegTimeStamp(u8* buf) {
	return (s64)buf[5] | ((s64)buf[4] << 8) | ((s64)buf[3] << 16) | ((s64)buf[2] << 24) 
		| ((s64)buf[1] << 32) | ((s64)buf[0] << 36);
}

class MediaEngine
{
public:
	MediaEngine();
	~MediaEngine();

	void closeMedia();
	bool loadStream(u8* buffer, int readSize, int StreamSize);
	bool loadFile(const char* filename);
	// open the mpeg context
	bool openContext();
	// Returns number of packets actually added.
	int addStreamData(u8* buffer, int addSize);

	void setVideoStream(int streamNum) { m_videoStream = streamNum; }
	void setAudioStream(int streamNum) { m_audioStream = streamNum; }

	int getRemainSize() { return m_streamSize - m_readSize;}
	int getBufferedSize();

	bool stepVideo(int videoPixelMode);
	int writeVideoImage(u8* buffer, int frameWidth = 512, int videoPixelMode = 3);
	int writeVideoImageWithRange(u8* buffer, int frameWidth, int videoPixelMode, 
	                             int xpos, int ypos, int width, int height);
	int getAudioSamples(u8* buffer);

	bool setVideoDim(int width = 0, int height = 0);
	s64 getVideoTimeStamp();
	s64 getAudioTimeStamp();
	s64 getLastTimeStamp();

	bool IsVideoEnd() { return m_isVideoEnd; }
	bool IsAudioEnd() { return m_isAudioEnd; }

	void DoState(PointerWrap &p) {
		p.Do(m_streamSize);
		p.Do(m_readSize);
		p.DoMarker("MediaEngine");
	}

private:
	void updateSwsFormat(int videoPixelMode);

public:

	AVFormatContext *m_pFormatCtx;
	AVCodecContext *m_pCodecCtx;
	AVFrame *m_pFrame;
	AVFrame *m_pFrameRGB;
	AVIOContext *m_pIOContext;
	SwsContext *m_sws_ctx;
	int m_sws_fmt;
	u8 *m_buffer;
	int m_videoStream;
	int m_audioStream;

	int  m_desWidth;
	int  m_desHeight;
	int m_streamSize;
	int m_readSize;
	int m_decodeNextPos;
	s64 m_decodedPos;
	int m_bufSize;
	s64 m_videopts;
	u8* m_pdata;
	
	MpegDemux *m_demux;
	int m_audioPos;
	void* m_audioContext;
	s64 m_audiopts;

	bool m_isVideoEnd;
	bool m_isAudioEnd;
};
