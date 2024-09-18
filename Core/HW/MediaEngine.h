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
class AudioDecoder;

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

class MediaEngine {
public:
	MediaEngine();
	~MediaEngine();

	void closeMedia();
	bool loadStream(const u8 *buffer, int readSize, int RingbufferSize);
	bool reloadStream();
	bool addVideoStream(int streamNum, int streamId = -1);
	// open the mpeg context
	bool openContext(bool keepReadPos = false);
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

	s64 getVideoTimeStamp();
	s64 getAudioTimeStamp();
	s64 getLastTimeStamp();

	bool IsVideoEnd() { return m_isVideoEnd; }
	bool IsNoAudioData();
	bool IsActuallyPlayingAudio();
	int VideoWidth() { return m_desWidth; }
	int VideoHeight() { return m_desHeight; }

	void DoState(PointerWrap &p);

private:
	bool SetupStreams();
	bool setVideoDim(int width = 0, int height = 0);
	void updateSwsFormat(int videoPixelMode);
	int getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2);

	static int MpegReadbuffer(void *opaque, uint8_t *buf, int buf_size);

public:  // TODO: Very little of this below should be public.

#ifdef USE_FFMPEG
	std::map<int, AVCodecContext *> m_pCodecCtxs;
	AVFrame *m_pFrame = nullptr;
	AVFrame *m_pFrameRGB = nullptr;
#endif

	u8 *m_buffer = nullptr;

	int m_desWidth = 0;
	int m_desHeight = 0;
	int m_bufSize;
	s64 m_videopts = 0;

	s64 m_firstTimeStamp = 0;
	s64 m_lastTimeStamp = 0;

	bool m_isVideoEnd = false;

private:
#ifdef USE_FFMPEG
	// Video ffmpeg context - not used for audio
	AVFormatContext *m_pFormatCtx = nullptr;
	std::vector<AVCodecContext *> m_codecsToClose;
	AVIOContext *m_pIOContext = nullptr;
	SwsContext *m_sws_ctx = nullptr;
#endif

	int m_sws_fmt = 0;
	int m_videoStream = -1;
	int m_expectedVideoStreams = 0;

	// Used by the demuxer.
	int m_audioStream = -1;

	int m_decodingsize = 0;
	BufferQueue *m_pdata = nullptr;

	s64 m_lastPts = -1;

	MpegDemux *m_demux = nullptr;
	AudioDecoder *m_audioContext = nullptr;
	s64 m_audiopts = 0;

	// used for audio type 
	int m_audioType;

	int m_ringbuffersize;

	u8 m_mpegheader[0x10000];  // TODO: Allocate separately
	int m_mpegheaderReadPos = 0;
	int m_mpegheaderSize = 0;
};
