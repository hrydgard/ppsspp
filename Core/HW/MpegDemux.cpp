#include "MpegDemux.h"

const int PACKET_START_CODE_MASK   = 0xffffff00;
const int PACKET_START_CODE_PREFIX = 0x00000100;

// http://dvd.sourceforge.net/dvdinfo/mpeghdrs.html

const int USER_DATA_START_CODE     = 0x000001b2;
const int SEQUENCE_START_CODE      = 0x000001b3;
const int EXT_START_CODE           = 0x000001b5;
const int SEQUENCE_END_CODE        = 0x000001b7;
const int GOP_START_CODE           = 0x000001b8;
const int ISO_11172_END_CODE       = 0x000001b9;
const int PACK_START_CODE          = 0x000001ba;
const int SYSTEM_HEADER_START_CODE = 0x000001bb;
const int PROGRAM_STREAM_MAP       = 0x000001bc;
const int PRIVATE_STREAM_1         = 0x000001bd;
const int PADDING_STREAM           = 0x000001be;
const int PRIVATE_STREAM_2         = 0x000001bf;

MpegDemux::MpegDemux(int size, int offset) : m_audioStream(size) {
	m_buf = new u8[size];

	m_len = size;
	m_index = offset;
	m_audioChannel = -1;
	m_readSize = 0;
}

MpegDemux::~MpegDemux() {
	delete [] m_buf;
}

void MpegDemux::DoState(PointerWrap &p) {
	auto s = p.Section("MpegDemux", 1);
	if (!s)
		return;

	p.Do(m_index);
	p.Do(m_len);
	p.Do(m_audioChannel);
	p.Do(m_readSize);
	if (m_buf)
		p.DoArray(m_buf, m_len);
	p.DoClass(m_audioStream);
}

bool MpegDemux::addStreamData(const u8 *buf, int addSize) {
	if (m_readSize + addSize > m_len)
		return false;
	memcpy(m_buf + m_readSize, buf, addSize);
	m_readSize += addSize;
	return true;
}

int MpegDemux::readPesHeader(PesHeader &pesHeader, int length, int startCode) {
	int c = 0;
	while (length > 0) {
		c = read8();
		length--;
		if (c != 0xFF) {
			break;
		}
	}
	if ((c & 0xC0) == 0x40) {
		read8();
		c = read8();
		length -= 2;
	}
	pesHeader.pts = 0;
	pesHeader.dts = 0;
	if ((c & 0xE0) == 0x20) {
		pesHeader.dts = pesHeader.pts = readPts(c);
		length -= 4;
		if ((c & 0x10) != 0) {
			pesHeader.dts = readPts();
			length -= 5;
		}
	} else if ((c & 0xC0) == 0x80) {
		int flags = read8();
		int headerLength = read8();
		length -= 2;
		length -= headerLength;
		if ((flags & 0x80) != 0) {
			pesHeader.dts = pesHeader.pts = readPts();
			headerLength -= 5;
			if ((flags & 0x40) != 0) {
				pesHeader.dts = readPts();
				headerLength -= 5;
			}
		}
		if ((flags & 0x3F) != 0 && headerLength == 0) {
			flags &= 0xC0;
		}
		if ((flags & 0x01) != 0) {
			int pesExt = read8();
			headerLength--;
			int skip = (pesExt >> 4) & 0x0B;
			skip += skip & 0x09;
			if ((pesExt & 0x40) != 0 || skip > headerLength) {
				pesExt = skip = 0;
			}
			this->skip(skip);
			headerLength -= skip;
			if ((pesExt & 0x01) != 0) {
				int ext2Length = read8();
				headerLength--;
				 if ((ext2Length & 0x7F) != 0) {
					 int idExt = read8();
					 headerLength--;
					 if ((idExt & 0x80) == 0) {
						 startCode = ((startCode & 0xFF) << 8) | idExt;
					 }
				 }
			}
		}
		skip(headerLength);
	}
	if (startCode == PRIVATE_STREAM_1) {
		int channel = read8();
		pesHeader.channel = channel;
		length--;
		if (channel >= 0x80 && channel <= 0xCF) {
			// Skip audio header
			skip(3);
			length -= 3;
			if (channel >= 0xB0 && channel <= 0xBF) {
				skip(1);
				length--;
			}
		} else {
			// PSP audio has additional 3 bytes in header
			skip(3);
			length -= 3;
		}
	}
	return length;
}

int MpegDemux::demuxStream(bool bdemux, int startCode, int channel)
{
	int length = read16();
	if (bdemux) {
		PesHeader pesHeader(channel);
		length = readPesHeader(pesHeader, length, startCode);
		if (pesHeader.channel == channel || channel < 0) {
			channel = pesHeader.channel;
			m_audioStream.push(m_buf + m_index, length, pesHeader.pts);
		}
		skip(length);
	} else {
		skip(length);
	}
	return channel;
}

void MpegDemux::demux(int audioChannel)
{
	if (audioChannel >= 0)
		m_audioChannel = audioChannel;
	while (m_index < m_len)
	{
		if (m_index + 2048 > m_readSize)
			break;
		// Search for start code
		int startCode = 0xFF;
		while ((startCode & PACKET_START_CODE_MASK) != PACKET_START_CODE_PREFIX && !isEOF()) {
			startCode = (startCode << 8) | read8();
		}
		switch (startCode) {
		case PACK_START_CODE:
			skip(10);
			break;
		case SYSTEM_HEADER_START_CODE:
			skip(14);
			break;
		case PADDING_STREAM:
		case PRIVATE_STREAM_2:
			{
				int length = read16();
				skip(length);
				break;
			}
		case PRIVATE_STREAM_1: {
			// Audio stream
			m_audioChannel = demuxStream(true, startCode, m_audioChannel);
			break;
		}
		case 0x1E0: case 0x1E1: case 0x1E2: case 0x1E3:
		case 0x1E4: case 0x1E5: case 0x1E6: case 0x1E7:
		case 0x1E8: case 0x1E9: case 0x1EA: case 0x1EB:
		case 0x1EC: case 0x1ED: case 0x1EE: case 0x1EF:
			// Video Stream
			demuxStream(false, startCode, -1);
			break;
		case USER_DATA_START_CODE:
			// User data, probably same as queried by sceMpegGetUserdataAu.
			// Not sure what exactly to do or how much to read.
			// TODO: implement properly.
			break;
		}
	}
	if (m_index < m_readSize) {
		int size = m_readSize - m_index;
		memcpy(m_buf, m_buf + m_index, size);
		m_index = 0;
		m_readSize = size;
	} else {
		m_index = 0;
		m_readSize = 0;
	}
}

static bool isHeader(u8* audioStream, int offset)
{
	const u8 header1 = (u8)0x0F;
	const u8 header2 = (u8)0xD0;
	return (audioStream[offset] == header1) && (audioStream[offset+1] == header2);
}

static int getNextHeaderPosition(u8* audioStream, int curpos, int limit, int frameSize)
{
	int endScan = limit - 1;

	// Most common case: the header can be found at each frameSize
	int offset = curpos + frameSize - 8;
	if (offset < endScan && isHeader(audioStream, offset))
		return offset;
	for (int scan = curpos; scan < endScan; scan++) {
		if (isHeader(audioStream, scan))
			return scan;
	}

	return -1;
}

int MpegDemux::getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2, s64 *pts)
{
	int gotsize;
	int frameSize;
	if (!hasNextAudioFrame(&gotsize, &frameSize, headerCode1, headerCode2))
		return 0;
	int audioPos = 8;
	int nextHeader = getNextHeaderPosition(m_audioFrame, audioPos, gotsize, frameSize);
	if (nextHeader >= 0) {
		audioPos = nextHeader;
	} else {
		audioPos = gotsize;
	}
	m_audioStream.pop_front(0, audioPos, pts);
	if (buf) {
		*buf = m_audioFrame + 8;
	}
	return frameSize - 8;
}

bool MpegDemux::hasNextAudioFrame(int *gotsizeOut, int *frameSizeOut, int *headerCode1, int *headerCode2)
{
	int gotsize = m_audioStream.get_front(m_audioFrame, 0x2000);
	if (gotsize == 0 || !isHeader(m_audioFrame, 0))
		return false;
	u8 code1 = m_audioFrame[2];
	u8 code2 = m_audioFrame[3];
	int frameSize = (((code1 & 0x03) << 8) | ((code2 & 0xFF) * 8)) + 0x10;
	if (frameSize > gotsize)
		return false;

	if (gotsizeOut)
		*gotsizeOut = gotsize;
	if (frameSizeOut)
		*frameSizeOut = frameSize;
	if (headerCode1)
		*headerCode1 = code1;
	if (headerCode2)
		*headerCode2 = code2;

	return true;
}
