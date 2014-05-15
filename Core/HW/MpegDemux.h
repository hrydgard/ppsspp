// This is a simple version MpegDemux that can get media's audio stream.
// Thanks to JPCSP project.

#pragma once

#include "Common/CommonTypes.h"
#include "Core/HW/BufferQueue.h"

class PointerWrap;

class MpegDemux
{
public:
	MpegDemux(int size, int offset);
	~MpegDemux();

	bool addStreamData(const u8 *buf, int addSize);
	void demux(int audioChannel);

	// return its framesize
	int getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2, s64 *pts = NULL);
	bool hasNextAudioFrame(int *gotsizeOut, int *frameSizeOut, int *headerCode1, int *headerCode2);

	inline int getRemainSize() {
		return m_len - m_readSize;
	}

	void DoState(PointerWrap &p);

private:
	struct PesHeader {
		s64 pts;
		s64 dts;
		int channel;

		PesHeader(int chan) {
			pts = 0;
			dts = 0;
			channel = chan;
		}
	};

	int read8() {
		return m_buf[m_index++];
	}
	int read16() {
		return (read8() << 8) | read8();
	}
	s64 readPts() {
		return readPts(read8());
	}
	s64 readPts(int c) {
		return (((s64) (c & 0x0E)) << 29) | ((read16() >> 1) << 15) | (read16() >> 1);
	}
	bool isEOF() {
		return m_index >= m_len;
	}
	void skip(int n) {
		if (n > 0) {
			m_index += n;
		}
	}
	int readPesHeader(PesHeader &pesHeader, int length, int startCode);
	int demuxStream(bool bdemux, int startCode, int channel);

	int m_index;
	int m_len;
	u8* m_buf;
	BufferQueue m_audioStream;
	u8  m_audioFrame[0x2000];
	int m_audioChannel;
	int m_readSize;
};

