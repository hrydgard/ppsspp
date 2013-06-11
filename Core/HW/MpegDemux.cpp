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

MpegDemux::MpegDemux(u8* buffer, int size, int offset)
{
	m_buf = buffer;
	m_len = size;
	m_index = offset;
	m_audioStream = 0;
	m_audiopos = 0;
	m_audioChannel = -1;
	m_readSize = 0;
}


MpegDemux::~MpegDemux(void)
{
	if (m_audioStream)
		delete [] m_audioStream;
}

void MpegDemux::setReadSize(int readSize)
{
	m_readSize = readSize;
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
			memcpy(m_audioStream + m_audiopos, m_buf + m_index, length);
			m_audiopos += length;
		}
		skip(length);
	} else {
		skip(length);
	}
	return channel;
}

void MpegDemux::demux(int audioChannel)
{
	if (!m_audioStream)
		m_audioStream = new u8[m_len - m_index];
	if (audioChannel >= 0)
		m_audioChannel = audioChannel;
	while (m_index < m_len)
	{
		if (m_readSize != m_len && m_index + 2048 > m_readSize)
			return;
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
}

int MpegDemux::getaudioStream(u8** audioStream)
{
	*audioStream = m_audioStream;
	return m_audiopos;
}
