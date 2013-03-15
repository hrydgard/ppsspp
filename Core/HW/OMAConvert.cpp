#include "OMAConvert.h"

namespace OMAConvert {

const u32 OMA_EA3_MAGIC = 0x45413301;
const u8 OMA_CODECID_ATRAC3P = 1;
const int OMAHeaderSize = 96;
const int FMT_CHUNK_MAGIC = 0x20746D66;
const int DATA_CHUNK_MAGIC = 0x61746164;

template <typename T> void BigEndianWriteBuf(u8* buf, T x, int &pos)
{
	int k = sizeof(T);
	for (int i = k - 1; i >= 0; i--)
	{
		buf[pos + i] = (u8)(x & 0xFF);
		x >>= 8;
	}
	pos += k;
}

template <typename T> inline T getBufValue(T* buf, int offsetbytes)
{
	return *(T*)(((u8*)buf) + offsetbytes);
}

inline void WriteBuf(u8* dst, int &pos, u8* src, int size)
{
	memcpy(dst + pos, src, size);
	pos += size;
}

bool isHeader(u8* audioStream, int offset)
{
	const u8 header1 = (u8)0x0F;
	const u8 header2 = (u8)0xD0;
	return (audioStream[offset] == header1) && (audioStream[offset+1] == header2);
}

// header set to the headerbuf, and return it's size
int getOmaHeader(u8 codecId, u8 headerCode1, u8 headerCode2, u8* headerbuf)
{
	int pos = 0;
	BigEndianWriteBuf(headerbuf, (u32)OMA_EA3_MAGIC, pos);
	BigEndianWriteBuf(headerbuf, (u16)OMAHeaderSize, pos);
	BigEndianWriteBuf(headerbuf, (u16)-1, pos);

	// Unknown 24 bytes...
	BigEndianWriteBuf(headerbuf, (u32)0x00000000, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x010f5000, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x00040000, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x0000f5ce, pos);
	BigEndianWriteBuf(headerbuf, (u32)0xd2929132, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x2480451c, pos);

	BigEndianWriteBuf(headerbuf, (u8)codecId, pos);
	BigEndianWriteBuf(headerbuf, (u8)0, pos);
	BigEndianWriteBuf(headerbuf, (u8)headerCode1, pos);
	BigEndianWriteBuf(headerbuf, (u8)headerCode2, pos);

	while (pos < OMAHeaderSize) BigEndianWriteBuf(headerbuf, (u8)0, pos);

	return pos;
}

int getNextHeaderPosition(u8* audioStream, int curpos, int limit, int frameSize)
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

void releaseStream(u8** stream)
{
	if (*stream) delete [] (*stream);
	*stream = 0;
}

int convertStreamtoOMA(u8* audioStream, int audioSize, u8** outputStream)
{
	if (!isHeader(audioStream, 0))
	{
		*outputStream = 0;
		return 0;
	}
	u8 headerCode1 = audioStream[2];
	u8 headerCode2 = audioStream[3];

	int frameSize = ((headerCode1 & 0x03) << 8) | (headerCode2 & 0xFF) * 8 + 0x10;
	int numCompleteFrames = audioSize / (frameSize + 8);
	int lastFrameSize = audioSize - (numCompleteFrames * (frameSize + 8));

	int omaStreamSize = OMAHeaderSize + numCompleteFrames * frameSize + lastFrameSize;

	// Allocate an OMA stream size large enough (better too large than too short)
	if (audioSize > omaStreamSize) omaStreamSize = audioSize;
	u8* oma = new u8[omaStreamSize];
	int omapos = 0;
	int audiopos = 0;

	omapos += getOmaHeader(OMA_CODECID_ATRAC3P, headerCode1, headerCode2, oma);
	while (audioSize - audiopos > 8) {
		// Skip 8 bytes frame header
		audiopos += 8;
		int nextHeader = getNextHeaderPosition(audioStream, audiopos, audioSize, frameSize);
		u8* frame = audioStream + audiopos;
		int framelimit = audioSize - audiopos;
		if (nextHeader >= 0) {
			framelimit = nextHeader - audiopos;
			audiopos = nextHeader;
		} else
			audiopos = audioSize;
		WriteBuf(oma, omapos, frame, framelimit);
	}

	*outputStream = oma;
	return omapos;
}

int getChunkOffset(u8* riff, int limit, int chunkMagic, int offset) {
	for (int i = offset; i <= limit - 4;) {
		if (getBufValue((int*)riff, i) == chunkMagic)
			return i;
		// Move to next chunk
		int chunkSize = getBufValue((int*)riff, i + 4);
		i += chunkSize + 8;
	}

	return -1;
}

int convertRIFFtoOMA(u8* riff, int riffSize, u8** outputStream)
{
	const int firstChunkOffset = 12;
	int fmtChunkOffset = getChunkOffset(riff, riffSize, FMT_CHUNK_MAGIC, firstChunkOffset);
	if (fmtChunkOffset < 0) {
		*outputStream = 0;
		return 0;
	}
	u8 codecId = getBufValue(riff, fmtChunkOffset + 0x30);
	u8 headerCode1 = getBufValue(riff, fmtChunkOffset + 0x32);
	u8 headerCode2 = getBufValue(riff, fmtChunkOffset + 0x33);

	int dataChunkOffset = getChunkOffset(riff, riffSize, DATA_CHUNK_MAGIC, firstChunkOffset);
	if (dataChunkOffset < 0) {
		*outputStream = 0;
		return 0;
	}
	int dataSize = getBufValue((int*)riff, dataChunkOffset + 4);
	int datapos = dataChunkOffset + 8;

	u8* oma = new u8[OMAHeaderSize + dataSize];
	int omapos = 0;

	omapos += getOmaHeader(codecId, headerCode1, headerCode2, oma);
	WriteBuf(oma, omapos, riff + datapos, dataSize);

	*outputStream = oma;
	return omapos;
}

int getOMANumberAudioChannels(u8* oma)
{
	int headerParameters = getBufValue((int*)oma, 0x20);
	int channels = (headerParameters >> 18) & 0x7;

	return channels;
}

} // namespace OMAConvert