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
// High level emulation: the PSMF demuxed with libretro-common's rpsmf, the video decoded by
// AvcDecoder (rh264), the audio by MpegDemux and the audio decoders.

#pragma once

// An approximation of what the interface will look like. Similar to JPCSP's.

#include <vector>
#include "Common/CommonTypes.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HW/MpegDemux.h"
#include "Core/HW/SimpleAudioDec.h"

class PointerWrap;
class AudioDecoder;
class AvcDecoder;
struct rpsmf;

inline s64 getMpegTimeStamp(const u8 *buf) {
	return (s64)buf[5] | ((s64)buf[4] << 8) | ((s64)buf[3] << 16) | ((s64)buf[2] << 24) 
		| ((s64)buf[1] << 32) | ((s64)buf[0] << 36);
}

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
	bool setAudioStream(int streamNum);

	u8 *getFrameImage();
	int getRemainSize();
	int getAudioRemainSize();

	bool stepVideo(int videoPixelMode, bool skipFrame = false);
	int writeVideoImage(u32 bufferPtr, int frameWidth = 512, int videoPixelMode = 3);
	int writeVideoImageWithRange(u32 bufferPtr, int frameWidth, int videoPixelMode,
	                             int xpos, int ypos, int width, int height);
	int getAudioSamples(u32 bufferPtr);

	// PMP (homebrew) video: one access unit decoded, and the frame it gave, if any, converted
	// into the image writeVideoImage hands out. True when there is a new frame.
	bool decodeAccessUnit(const u8 *data, int size, int videoPixelMode);

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
	int getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2);

	// The program stream, fed from the ring a pack at a time; false when the ring has nothing.
	bool demuxMore();
	// The next whole access unit of the selected video stream, into unit/pts.
	bool takeAccessUnit(bool streamEnded, std::vector<u8> &unit, s64 *pts);
	// A decoded frame: the time it stands for, and its pixels unless skipped.
	void frameDecoded(int videoPixelMode, bool skipFrame);
	// Whether the whole program stream the header promised has gone to the demuxer.
	bool streamFed() const;
	void clearUnits();
	void dropOldestPts();

public:  // TODO: Very little of this below should be public.
	// The current frame, in the pixel mode it was last converted to, m_desWidth wide.
	u8 *m_buffer = nullptr;

	int m_desWidth = 0;
	int m_desHeight = 0;
	s64 m_videopts = 0;

	s64 m_firstTimeStamp = 0;
	s64 m_lastTimeStamp = 0;

	bool m_isVideoEnd = false;

private:
	// The video side: the demuxer over the program stream, the decoder of the selected stream,
	// and that stream's bytes as they come, until they make an access unit. Where in them each
	// packet with a time stamp began, and the stamps of units decoded but not yet shown.
	bool m_contextOpen = false;
	struct rpsmf *m_psmf = nullptr;
	AvcDecoder *m_avc = nullptr;
	std::vector<u8> m_au;
	size_t m_auBase = 0;  // video bytes taken before m_au[0]
	size_t m_auScan = 0;
	bool m_auHasSlice = false;
	std::vector<std::pair<size_t, s64>> m_auPts;
	std::vector<s64> m_pendingPts;
	std::vector<size_t> m_packEnds;  // packs read but not used up: where their video ends
	bool m_packPending = false;
	s64 m_psFed = 0;     // program stream bytes given to the demuxer
	s64 m_psSize = 0;    // and how many the header says there are; 0 unknown
	u8 m_videoStreamIds[16]{};

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

