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

#pragma once

// An approximation of what the interface will look like. Similar to JPCSP's.

#include <map>
#include "Common/CommonTypes.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HW/MpegDemux.h"
#include "Core/HW/SimpleAudioDec.h"

class PointerWrap;
class SimpleAudio;

#ifdef USE_FFMPEG
struct SwsContext;
struct AVFrame;
struct AVIOContext;
struct AVFormatContext;
struct AVCodecContext;
#endif

inline s64 getMpegTimeStamp(const u8 *buf) {
	return (s64)buf[5] | ((s64)buf[4] << 8) | ((s64)buf[3] << 16) | ((s64)buf[2] << 24) 
		| ((s64)buf[1] << 32) | ((s64)buf[0] << 36);
}

#ifdef USE_FFMPEG
bool InitFFmpeg();
#endif

void __AdjustBGMVolume(s16 *samples, u32 count);

class MediaEngine
{
public:
	MediaEngine();
	~MediaEngine();

	void closeMedia();
	bool loadStream(const u8 *buffer, int readSize, int RingbufferSize);
	// open the mpeg context
	bool openContext();
	void closeContext();

	// Returns number of packets actually added. I guess the buffer might be full.
	int addStreamData(const u8 *buffer, int addSize);
	bool seekTo(s64 timestamp, int videoPixelMode);

	bool setVideoStream(int streamNum, bool force = false);
	// TODO: Return false if the stream doesn't exist.
	bool setAudioStream(int streamNum) { m_audioStream = streamNum; return true; }

	u8 *getFrameImage();
	int getRemainSize();
	int getAudioRemainSize();

	bool stepVideo(int videoPixelMode, bool skipFrame = false);
	int writeVideoImage(u32 bufferPtr, int frameWidth = 512, int videoPixelMode = 3);
	int writeVideoImageWithRange(u32 bufferPtr, int frameWidth, int videoPixelMode,
	                             int xpos, int ypos, int width, int height);
	int getAudioSamples(u32 bufferPtr);

	bool setVideoDim(int width = 0, int height = 0);
	s64 getVideoTimeStamp();
	s64 getAudioTimeStamp();
	s64 getLastTimeStamp();

	bool IsVideoEnd() { return m_isVideoEnd; }
	bool IsNoAudioData();
	int VideoWidth() { return m_desWidth; }
	int VideoHeight() { return m_desHeight; }

	void DoState(PointerWrap &p);

private:
	void updateSwsFormat(int videoPixelMode);
	int getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2);

public:  // TODO: Very little of this below should be public.

	// Video ffmpeg context - not used for audio
#ifdef USE_FFMPEG
	AVFormatContext *m_pFormatCtx;
	std::map<int, AVCodecContext *> m_pCodecCtxs;
	AVFrame *m_pFrame;
	AVFrame *m_pFrameRGB;
	AVIOContext *m_pIOContext;
	SwsContext *m_sws_ctx;
#endif

	int m_sws_fmt;
	u8 *m_buffer;
	int m_videoStream;

	// Used by the demuxer.
	int m_audioStream;

	int m_desWidth;
	int m_desHeight;
	int m_decodingsize;
	int m_bufSize;
	s64 m_videopts;
	BufferQueue *m_pdata;

	MpegDemux *m_demux;
	SimpleAudio *m_audioContext;
	s64 m_audiopts;

	s64 m_firstTimeStamp;
	s64 m_lastTimeStamp;

	bool m_isVideoEnd;

	int m_ringbuffersize;
	u8 m_mpegheader[0x10000];  // TODO: Allocate separately
	int m_mpegheaderReadPos;

	// used for audio type 
	int m_audioType;
};
