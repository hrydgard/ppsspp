// This is a simple version MpegDemux that can get media's audio stream.
// Thanks to JPCSP project.

#pragma once

#include "../../Globals.h"

class MpegDemux
{
public:
	MpegDemux(u8* buffer, int size, int offset);
	~MpegDemux(void);

	void setReadSize(int readSize);

	void demux(int audioChannel);

	// return its size
	int getaudioStream(u8 **audioStream);
	int getFilePosition() { return m_index; }
private:
	struct PesHeader {
		long pts;
		long dts;
		int channel;

		PesHeader(int chan) {
			pts = 0;
			dts = 0;
			channel = chan;
		}
	};
	int read8() {
		return m_buf[m_index++] & 0xFF;
	}
	int read16() {
		return (read8() << 8) | read8();
	}
	long readPts() {
		return readPts(read8());
	}
	long readPts(int c) {
		return (((long) (c & 0x0E)) << 29) | ((read16() >> 1) << 15) | (read16() >> 1);
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
private:
	int m_index;
	int m_len;
	u8* m_buf;
	u8* m_audioStream;
	int m_audiopos;
	int m_audioChannel;
	int m_readSize;
};

